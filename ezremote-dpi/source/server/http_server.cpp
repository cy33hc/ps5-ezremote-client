#include <string>
#include <json-c/json.h>
#include <server/range_parser.h>
#include "http/httplib.h"
#include "server/http_server.h"
#include "clients/baseclient.h"
#include "clients/smbclient.h"
#include "filehost/filehost.h"
#include "actions.h"
#include "config.h"
#include "fs.h"
#include "installer.h"
#include "lang.h"
#include "zip_util.h"
#include "util.h"
#include "sceAppInstUtil.h"
#include "dbglogger.h"

#define SUCCESS_MSG "{ \"result\": { \"success\": true, \"error\": null } }"
#define FAILURE_MSG "{ \"result\": { \"success\": false, \"error\": \"%s\" } }"
#define SUCCESS_MSG_LEN 48

using namespace httplib;

Server *svr;
int http_server_port = 9040;

struct PackageInstallInfo
{
    std::string host;
    std::string username;
    std::string password;
    std::string path;
    std::string parameters;
    ClientType type;
    bool enable_rpi;
    bool enable_alldebrid;
    bool enable_realdebrid;
    RemoteClient *client;
};

static std::map<std::string, PackageInstallInfo> url_package_info_map;

namespace HttpServer
{
    std::string dump_headers(const Headers &headers)
    {
        std::string s;
        char buf[BUFSIZ];

        for (auto it = headers.begin(); it != headers.end(); ++it)
        {
            const auto &x = *it;
            snprintf(buf, sizeof(buf), "%s: %s\n", x.first.c_str(), x.second.c_str());
            s += buf;
        }

        return s;
    }

    std::string log(const Request &req, const Response &res)
    {
        std::string s;
        char buf[BUFSIZ];

        s += "================================\n";

        snprintf(buf, sizeof(buf), "%s %s %s", req.method.c_str(),
                 req.version.c_str(), req.path.c_str());
        s += buf;

        std::string query;
        for (auto it = req.params.begin(); it != req.params.end(); ++it)
        {
            const auto &x = *it;
            snprintf(buf, sizeof(buf), "%c%s=%s",
                     (it == req.params.begin()) ? '?' : '&', x.first.c_str(),
                     x.second.c_str());
            query += buf;
        }
        snprintf(buf, sizeof(buf), "%s\n", query.c_str());
        s += buf;

        s += dump_headers(req.headers);

        s += "--------------------------------\n";

        snprintf(buf, sizeof(buf), "%d %s\n", res.status, res.version.c_str());
        s += buf;
        s += dump_headers(res.headers);
        s += "\n";

        if (!res.body.empty())
        {
            s += res.body;
        }

        s += "\n";

        return s;
    }

    void failed(Response &res, int status, const std::string &msg)
    {
        res.status = status;
        char response_msg[4096];
        snprintf(response_msg, sizeof(response_msg), "{ \"result\": { \"success\": false, \"error\": \"%s\" } }", msg.c_str());
        res.set_content(response_msg, strlen(response_msg), "application/json");
        return;
    }

    void bad_request(Response &res, const std::string &msg)
    {
        failed(res, 200, msg);
        return;
    }

    void success(Response &res)
    {
        res.status = 200;
        res.set_content(SUCCESS_MSG, SUCCESS_MSG_LEN, "application/json");
        return;
    }

    static PackageInstallInfo GetCachedPakageInfo(const std::string &hash)
    {
        return url_package_info_map[hash];
    }

    void AddCachedPakageInfo(const std::string &hash, PackageInstallInfo pkg_info)
    {
        std::pair<std::string, PackageInstallInfo> pair = std::make_pair(hash, pkg_info);
        url_package_info_map.erase(hash);
        url_package_info_map.insert(pair);
    }

    static RemoteClient *GetRemoteClient(const std::string &hash)
    {
        RemoteClient *tmp_client = nullptr;
        PackageInstallInfo info = GetCachedPakageInfo(hash);

        if (info.type == CLIENT_TYPE_HTTP_SERVER)
        {
            tmp_client = info.client;
            tmp_client = new BaseClient();
        }
        else if (info.type == CLIENT_TYPE_SMB)
        {
            tmp_client = new SmbClient();
        }

        tmp_client->Connect(info.host, info.username, info.password, false);

        return tmp_client;
    }

    static std::string GetPath(const std::string &hash)
    {
        return url_package_info_map[hash].path;
    }

    static void DeleteRemoteClient(RemoteClient *tmp_client)
    {
        tmp_client->Quit();
        delete tmp_client;
    }
    
    static PackageInstallInfo GetPackageInstallInfo(const std::string &url)
    {
        PackageInstallInfo pkg_info;
        int scheme_pos = url.find("://");
        int root_pos = url.substr(scheme_pos+3).find("/");
        std::string scheme = url.substr(0, scheme_pos);
        std::string user_and_host = url.substr(scheme_pos+3, root_pos);
        std::string path_and_parameters = url.substr(scheme_pos+3+root_pos);
        int param_pos = path_and_parameters.find("?");
        int at_pos = user_and_host.find("@");

        if (param_pos != std::string::npos)
        {
            pkg_info.path = path_and_parameters.substr(0, param_pos);
            pkg_info.parameters = path_and_parameters.substr(param_pos+1);;
        }
        else
        {
            pkg_info.path = path_and_parameters;
            pkg_info.parameters = "";
        }
        
        if (at_pos != std::string::npos)
        {
            pkg_info.host = url.substr(0, scheme_pos+3) + user_and_host.substr(at_pos+1);
            std::string username_and_password = user_and_host.substr(0, at_pos);
            int colon_pos = username_and_password.find(":");
            if (colon_pos == std::string::npos)
            {
                pkg_info.username = username_and_password;
                pkg_info.password = "";
            }
            else
            {
                pkg_info.username = username_and_password.substr(0, colon_pos);
                pkg_info.password = username_and_password.substr(colon_pos+1);
            }
        }
        else
        {
            pkg_info.username = "";
            pkg_info.password = "";
            pkg_info.host = url.substr(0, scheme_pos+3) + user_and_host;
        }

        if (scheme.compare("http") == 0 || scheme.compare("https") == 0)
        {
            pkg_info.type = CLIENT_TYPE_HTTP_SERVER;
        }
        else if (scheme.compare("smb") == 0)
        {
            pkg_info.type = CLIENT_TYPE_SMB;
            int share_pos = pkg_info.path.find("/", 1);
            std::string share = pkg_info.path.substr(0, share_pos);
            pkg_info.host = pkg_info.host + share;
            pkg_info.path = pkg_info.path.substr(share_pos);
        }
        dbglogger_log("host=%s, username=%s, password=%s, path=%s, type=%d", pkg_info.host.c_str(), pkg_info.username.c_str(), pkg_info.password.c_str(), pkg_info.path.c_str(), pkg_info.type);

        return pkg_info;
    }

    void *ServerThread(void *argp)
    {
        svr->Get("/", [&](const Request &req, Response &res)
                 { res.set_redirect("/index.html"); });

        svr->Get("/install_pkg/(.*)", [&](const Request &req, Response &res)
        {
            std::string hash = req.matches[1];

            RemoteClient *tmp_client = nullptr;
            tmp_client = GetRemoteClient(hash);
            std::string path = GetPath(hash);

            dbglogger_log("tmp_client =%d, path=%s", tmp_client, path.c_str());

            res.status = 206;
            size_t range_len = (req.ranges[0].second - req.ranges[0].first) + 1;
                
            std::pair<ssize_t, ssize_t> range = req.ranges[0];
            res.set_content_provider(
                range_len, "application/octet-stream",
                [tmp_client, path, range, range_len](size_t offset, size_t length, DataSink &sink) {
                    int ret;
                    dbglogger_log("before GetRange");
                    ret = tmp_client->GetRange(path, sink, range_len, range.first);
                    dbglogger_log("after GetRange");
                    return (ret==1);
                },
                [tmp_client](bool success) {
                    DeleteRemoteClient(tmp_client);
                });
        });

        svr->Post("/install_url", [&](const Request &req, Response &res)
        {
            std::string url;
            const char *url_param;
            bool use_alldebrid = false;
            bool use_realdebrid = false;

            json_object *jobj = json_tokener_parse(req.body.c_str());
            if (jobj != nullptr)
            {
                url_param = json_object_get_string(json_object_object_get(jobj, "url"));
                use_alldebrid  = json_object_get_boolean(json_object_object_get(jobj, "use_alldebrid"));
                use_realdebrid = json_object_get_boolean(json_object_object_get(jobj, "use_realdebrid"));

                if (url_param == nullptr)
                {
                    bad_request(res, "Required url_param parameter missing");
                    return;
                }
            }
            else
            {
                bad_request(res, "Invalid payload");
                return;
            }

            if ((use_alldebrid && strlen(alldebrid_api_key) == 0) || (use_realdebrid && strlen(realdebrid_api_key) == 0))
            {
                failed(res, 200, lang_strings[STR_ALLDEBRID_API_KEY_MISSING_MSG]);
                return;
            }

            url = std::string(url_param);
            FileHost *filehost = FileHost::getFileHost(url, use_alldebrid, use_realdebrid);

            if (!filehost->IsValidUrl())
            {
                failed(res, 200, lang_strings[STR_INVALID_URL]);
                return;
            }

            std::string hash = Util::UrlHash(filehost->GetUrl());

            std::string download_url = filehost->GetDownloadUrl();
            if (download_url.empty())
            {
                failed(res, 200, lang_strings[STR_CANT_EXTRACT_URL_MSG]);
                return;
            }
            delete(filehost);

            AddCachedPakageInfo(hash, GetPackageInstallInfo(download_url));

            std::string remote_install_url = std::string("http://localhost:") + std::to_string(http_server_port) + "/install_pkg/" + hash;

            PlayGoInfo playgo_info = {0};
            SceAppInstallPkgInfo pkg_info = {0};
            MetaInfo metainfo;
            metainfo.uri = remote_install_url.c_str();
            metainfo.ex_uri = "";
            metainfo.playgo_scenario_id = "";
            metainfo.content_id = "";
            metainfo.content_name = download_url.c_str();
            metainfo.icon_url = "";

            int rc = sceAppInstUtilInstallByPackage(&metainfo, &pkg_info, &playgo_info);
            if (rc != 0)
            {
                failed(res, 200, lang_strings[STR_FAIL_INSTALL_FROM_URL_MSG]);
                return;
            }
            success(res);
        });

        svr->Get("/stop", [&](const Request & /*req*/, Response & /*res*/)
                 { svr->stop(); });

        svr->set_error_handler([](const Request & /*req*/, Response &res)
        {
            const char *fmt = "<p>Error Status: <span style='color:red;'>%d</span></p>";
            char buf[BUFSIZ];
            snprintf(buf, sizeof(buf), fmt, res.status);
            res.set_content(buf, "text/html");
        });

        /*
        svr->set_logger([](const Request &req, const Response &res)
        {
            dbglogger_log("%s", log(req, res).c_str());
        });
        */
       
        svr->set_payload_max_length(1024 * 1024 * 12);
        svr->set_tcp_nodelay(true);
        svr->set_mount_point("/", "/");

        svr->listen("0.0.0.0", http_server_port);

        return NULL;
    }

    void Start()
    {
        if (svr == nullptr)
            svr = new Server();
        if (!svr->is_valid())
        {
            return;
        }
        int ret = pthread_create(&http_server_thid, NULL, ServerThread, NULL);
    }

    void Stop()
    {
        if (svr != nullptr)
            svr->stop();
    }
}
