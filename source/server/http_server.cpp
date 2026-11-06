#include <string>
#include <map>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <json-c/json.h>
#include "json_object_ref.h"
#include "http/httplib.h"
#include "server/http_server.h"
#include "actions.h"
#include "clients/smbclient.h"
#include "clients/sftpclient.h"
#include "clients/ftpclient.h"
#include "clients/nfsclient.h"
#include "clients/webdav.h"
#include "clients/apache.h"
#include "clients/archiveorg.h"
#include "clients/iis.h"
#include "clients/github.h"
#include "clients/myrient.h"
#include "clients/nginx.h"
#include "clients/npxserve.h"
#include "clients/rclone.h"
#include "filehost/filehost.h"
#include "config.h"
#include "fs.h"
#include "windows.h"
#include "lang.h"
#include "zip_util.h"
#include "sfo.h"
#include "util.h"
#include "installer.h"
#include "activity.h"
#include "remote_pool.h"

extern bool done;
// #include "dbglogger.h"

#define SUCCESS_MSG "{ \"result\": { \"success\": true, \"error\": null } }"
#define FAILURE_MSG "{ \"result\": { \"success\": false, \"error\": \"%s\" } }"
#define SUCCESS_MSG_LEN 48
#define PKG_INITIAL_REQUEST_SIZE 8388608ul

std::shared_mutex mutex_;

using namespace httplib;

Server *svr;
int http_server_port = 9090;
int http_int_server_port = 6701;
char compressed_file_path[1024];
bool web_server_enabled = false;

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
        char *response_msg = (char*) malloc(msg.length() + strlen(FAILURE_MSG) + 10);
        snprintf(response_msg, msg.length() + strlen(FAILURE_MSG), "{ \"result\": { \"success\": false, \"error\": \"%s\" } }", msg.c_str());
        res.set_content(response_msg, strlen(response_msg), "application/json");
        free(response_msg);
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

    // Copies src's contents into dest (which becomes the folder's final path).
    static bool CopyDirInto(const std::string &src, const std::string &dest)
    {
        FS::MkDirs(dest.c_str());

        int err = 0;
        std::vector<DirEntry> entries = FS::ListDir(src, &err);
        if (err)
            return false;

        for (size_t i = 0; i < entries.size(); i++)
        {
            if (!entries[i].selectable)
                continue;

            std::string target = dest + "/" + entries[i].name;
            if (entries[i].isDir)
            {
                if (!CopyDirInto(entries[i].path, target))
                    return false;
            }
            else if (FS::Copy(entries[i].path, target.c_str()) <= 0)
            {
                return false;
            }
        }

        return true;
    }

    int CopyOrMove(const DirEntry &src, const char *dest, bool isCopy)
    {
        int ret;
        if (src.isDir)
        {
            int err;
            std::vector<DirEntry> entries = FS::ListDir(src.path, &err);
            FS::MkDirs(dest);
            for (int i = 0; i < entries.size(); i++)
            {
                int path_length = strlen(dest) + strlen(entries[i].name) + 2;
                char *new_path = (char *)malloc(path_length);
                snprintf(new_path, path_length, "%s%s%s", dest, FS::hasEndSlash(dest) ? "" : "/", entries[i].name);

                if (entries[i].isDir)
                {
                    if (strcmp(entries[i].name, "..") == 0)
                        continue;

                    FS::MkDirs(new_path);
                    ret = CopyOrMove(entries[i], new_path, isCopy);
                    if (ret <= 0)
                    {
                        free(new_path);
                        return ret;
                    }
                }
                else
                {
                    if (isCopy)
                    {
                        ret = FS::Copy(entries[i].path, new_path);
                    }
                    else
                    {
                        ret = FS::Move(entries[i].path, new_path);
                    }
                    if (ret <= 0)
                    {
                        free(new_path);
                        return ret;
                    }
                }
                free(new_path);
            }
        }
        else
        {
            int path_length = strlen(dest) + strlen(src.name) + 2;
            char *new_path = (char *)malloc(path_length);
            snprintf(new_path, path_length, "%s%s%s", dest, FS::hasEndSlash(dest) ? "" : "/", src.name);
            if (isCopy)
            {
                ret = FS::Copy(src.path, new_path);
            }
            else
            {
                ret = FS::Move(src.path, new_path);
            }
            if (ret <= 0)
            {
                free(new_path);
                return 0;
            }
            free(new_path);
        }
        return 1;
    }

    static void DeleteRemoteClient(RemoteClient *tmp_client)
    {
        tmp_client->Quit();
        delete tmp_client;
    }
    
    // The web UI keeps its own connection per site. The native UI still owns the
    // global remoteclient, so sharing it would let one disconnect the other.
    static std::mutex remote_session_mutex;
    static std::map<std::string, RemoteClient *> remote_sessions;

    // The web UI keeps its own active site so selecting one here never moves the
    // native UI's remote_settings/remote_directory.
    static std::mutex web_site_mutex;

    static std::string GetWebSite()
    {
        std::lock_guard<std::mutex> lock(web_site_mutex);
        return std::string(web_last_site);
    }

    static void SetWebSite(const std::string &site)
    {
        std::lock_guard<std::mutex> lock(web_site_mutex);
        snprintf(web_last_site, sizeof(web_last_site), "%s", site.c_str());
    }

    static std::string ResolveSite(const char *site)
    {
        return (site != nullptr && strlen(site) > 0) ? std::string(site) : GetWebSite();
    }

    // Caller must hold remote_session_mutex.
    static void CloseRemoteSession(const std::string &site)
    {
        std::map<std::string, RemoteClient *>::iterator it = remote_sessions.find(site);
        if (it == remote_sessions.end())
            return;

        if (it->second != nullptr)
        {
            it->second->Quit();
            delete it->second;
        }
        remote_sessions.erase(it);
    }

    // Caller must hold remote_session_mutex. Connects lazily and reports why a
    // connection could not be established through ERROR.
    static RemoteClient *GetRemoteSession(const std::string &site, std::string &error)
    {
        std::map<std::string, RemoteSettings>::iterator setting = site_settings.find(site);
        if (setting == site_settings.end())
        {
            error = "Unknown site";
            return nullptr;
        }
        if (strlen(setting->second.server) == 0)
        {
            error = "Site has no server configured";
            return nullptr;
        }

        std::map<std::string, RemoteClient *>::iterator session = remote_sessions.find(site);
        if (session != remote_sessions.end() && session->second != nullptr && session->second->IsConnected())
            return session->second;

        CloseRemoteSession(site);

        RemoteClient *client = Actions::CreateRemoteClient(&setting->second);
        if (client == nullptr)
        {
            error = "Unsupported protocol";
            return nullptr;
        }

        if (!client->Connect(setting->second.server, setting->second.username, setting->second.password))
        {
            error = strlen(client->LastResponse()) > 0 ? client->LastResponse() : "Connection failed";
            client->Quit();
            delete client;
            return nullptr;
        }

        remote_sessions[site] = client;
        return client;
    }

    // Progress for cross-pane copies. The copy runs on a worker thread so the
    // HTTP request can return immediately and the UI can poll for bytes.
    struct WebTransfer
    {
        std::string name;
        uint64_t loaded = 0;
        uint64_t total = 0;
        int state = 0; // 0 running, 1 done, 2 failed
        std::string error;
        uint64_t activity_id = 0;
        // While an upload is in flight, progress is read from the connection's
        // own counters instead of the shared globals.
        std::shared_ptr<RemoteClient> held;
        RemoteClient *client = nullptr;
        uint64_t base = 0;
        bool live = false;
    };

    static std::mutex web_transfer_mutex;
    static std::map<std::string, WebTransfer> web_transfers;
    static unsigned long web_transfer_seq = 0;

    static std::string NewTransferId()
    {
        std::lock_guard<std::mutex> lock(web_transfer_mutex);
        web_transfer_seq++;
        std::string id = std::to_string((unsigned long)time(nullptr)) + "-" + std::to_string(web_transfer_seq);
        web_transfers[id] = WebTransfer();

        // Keep the map from growing without bound.
        if (web_transfers.size() > 32)
        {
            for (std::map<std::string, WebTransfer>::iterator it = web_transfers.begin(); it != web_transfers.end();)
            {
                if (it->second.state != 0 && it->first != id)
                    it = web_transfers.erase(it);
                else
                    ++it;
            }
        }
        return id;
    }

    static void UpdateTransfer(const std::string &id, uint64_t loaded, uint64_t total, const std::string &name)
    {
        std::lock_guard<std::mutex> lock(web_transfer_mutex);
        std::map<std::string, WebTransfer>::iterator it = web_transfers.find(id);
        if (it == web_transfers.end())
            return;
        it->second.loaded = loaded;
        it->second.total = total;
        if (!name.empty())
            it->second.name = name;
        if (it->second.activity_id != 0)
            Activities::Progress(it->second.activity_id, loaded, total);
    }

    static void SetTransferLive(const std::string &id, RemoteClient *client, uint64_t base, bool live)
    {
        std::lock_guard<std::mutex> lock(web_transfer_mutex);
        std::map<std::string, WebTransfer>::iterator it = web_transfers.find(id);
        if (it == web_transfers.end())
            return;
        it->second.client = live ? client : nullptr;
        it->second.base = base;
        it->second.live = live;
    }

    static void FinishTransfer(const std::string &id, int state, const std::string &error)
    {
        std::lock_guard<std::mutex> lock(web_transfer_mutex);
        std::map<std::string, WebTransfer>::iterator it = web_transfers.find(id);
        if (it == web_transfers.end())
            return;
        it->second.state = state;
        it->second.error = error;
    }

    struct CopyItem
    {
        std::string path;
        bool is_dir = false;
    };

    struct CopyJob
    {
        std::string id;
        std::string site;
        std::string dest;
        std::vector<CopyItem> items;
        std::set<std::string> skip;
        bool to_remote = false;
        uint64_t total = 0;
    };

    static std::string CopyItemName(const std::string &path)
    {
        size_t slash = path.find_last_of('/');
        return (slash == std::string::npos) ? path : path.substr(slash + 1);
    }

    // Items may be plain paths (legacy) or {path, is_dir} objects.
    static void ParseCopyItems(json_object *items, std::vector<CopyItem> *out)
    {
        size_t count = json_object_array_length(items);
        for (size_t i = 0; i < count; i++)
        {
            json_object *entry = json_object_array_get_idx(items, i);
            if (entry == nullptr)
                continue;

            if (json_object_get_type(entry) == json_type_object)
            {
                const char *path = json_object_get_string(json_object_object_get(entry, "path"));
                if (path == nullptr)
                    continue;

                CopyItem item;
                item.path = path;
                json_object *dir = json_object_object_get(entry, "is_dir");
                item.is_dir = dir != nullptr && json_object_get_boolean(dir) != 0;
                out->push_back(item);
            }
            else
            {
                const char *path = json_object_get_string(entry);
                if (path != nullptr)
                {
                    CopyItem item;
                    item.path = path;
                    out->push_back(item);
                }
            }
        }
    }

    static void ProbeLocalConflicts(RemoteClient *client, const std::string &src, const std::string &dest, json_object *conflicts)
    {
        if (FS::IsFolder(src) == 1)
        {
            int err = 0;
            std::vector<DirEntry> entries = FS::ListDir(src, &err);
            if (err)
                return;

            for (size_t i = 0; i < entries.size(); i++)
            {
                if (!entries[i].selectable)
                    continue;
                std::string target = dest + "/" + entries[i].name;
                if (entries[i].isDir)
                    ProbeLocalConflicts(client, entries[i].path, target, conflicts);
                else if (client->FileExists(target))
                    json_object_array_add(conflicts, json_object_new_string(target.c_str()));
            }
            return;
        }

        if (client->FileExists(dest))
            json_object_array_add(conflicts, json_object_new_string(dest.c_str()));
    }

    static void ProbeRemoteConflicts(RemoteClient *client, const std::string &src, const std::string &dest, bool is_dir, json_object *conflicts)
    {
        if (is_dir)
        {
            std::vector<DirEntry> entries = client->ListDir(src);
            for (size_t i = 0; i < entries.size(); i++)
            {
                if (strcmp(entries[i].name, "..") == 0)
                    continue;
                std::string target = dest + "/" + entries[i].name;
                ProbeRemoteConflicts(client, entries[i].path, target, entries[i].isDir, conflicts);
            }
            return;
        }

        if (FS::FileExists(dest))
            json_object_array_add(conflicts, json_object_new_string(dest.c_str()));
    }

    static uint64_t LocalTreeSize(const std::string &path, const std::set<std::string> &skip)
    {
        int err = 0;
        std::vector<DirEntry> entries = FS::ListDir(path, &err);
        if (err)
            return 0;

        uint64_t total = 0;
        for (size_t i = 0; i < entries.size(); i++)
        {
            if (!entries[i].selectable)
                continue;
            if (entries[i].isDir)
                total += LocalTreeSize(entries[i].path, skip);
            else if (skip.find(entries[i].path) == skip.end())
                total += (uint64_t) entries[i].file_size;
        }
        return total;
    }

    static uint64_t RemoteTreeSize(RemoteClient *client, const std::string &path, const std::set<std::string> &skip)
    {
        std::vector<DirEntry> entries = client->ListDir(path);
        uint64_t total = 0;
        for (size_t i = 0; i < entries.size(); i++)
        {
            if (strcmp(entries[i].name, "..") == 0)
                continue;
            if (entries[i].isDir)
                total += RemoteTreeSize(client, entries[i].path, skip);
            else if (skip.find(entries[i].path) == skip.end())
                total += entries[i].file_size;
        }
        return total;
    }

    static bool CopyLocalToRemote(RemoteClient *client, const CopyJob *job, const std::string &src,
                                  const std::string &dest, uint64_t *loaded, std::string *failed)
    {
        if (job->skip.find(dest) != job->skip.end())
            return true;

        if (FS::IsFolder(src) == 1)
        {
            client->Mkdir(dest);

            int err = 0;
            std::vector<DirEntry> entries = FS::ListDir(src, &err);
            if (err)
            {
                *failed += src + ",";
                return false;
            }

            bool ok = true;
            for (size_t i = 0; i < entries.size(); i++)
            {
                if (!entries[i].selectable)
                    continue;
                std::string target = dest + "/" + entries[i].name;
                if (!CopyLocalToRemote(client, job, entries[i].path, target, loaded, failed))
                    ok = false;
            }
            return ok;
        }

        UpdateTransfer(job->id, *loaded, job->total, CopyItemName(src));
        SetTransferLive(job->id, client, *loaded, true);
        int put_result = client->Put(src, dest);
        SetTransferLive(job->id, nullptr, *loaded, false);

        if (put_result == 0)
        {
            *failed += src + ",";
            return false;
        }

        int64_t size = FS::GetSize(src);
        if (size > 0)
            *loaded += (uint64_t) size;
        UpdateTransfer(job->id, *loaded, job->total, CopyItemName(src));
        return true;
    }

    // Hands a remote file to the ezRemote server's background downloader, the
    // same way Actions::BackgroundDownload does, using this site's settings.
    static bool QueueBackgroundDownload(RemoteClient *client, const CopyJob *job, const std::string &src,
                                        const std::string &dest, uint64_t size)
    {
        std::map<std::string, RemoteSettings>::iterator it = site_settings.find(job->site);
        if (it == site_settings.end())
            return false;

        RemoteSettings &settings = it->second;
        std::string source = src;

        json_object *params = json_object_new_object();
        JsonObjectRef params_ref(params);
        json_object_object_add(params, "type", json_object_new_int(settings.type));
        json_object_object_add(params, "username", json_object_new_string(settings.username));
        json_object_object_add(params, "dest_path", json_object_new_string(dest.c_str()));
        json_object_object_add(params, "size", json_object_new_uint64(size));
        json_object_object_add(params, "id", json_object_new_uint64((uint64_t) Util::GetTick()));

        if (settings.type == CLIENT_TYPE_HTTP_SERVER && strcmp(settings.http_server_type, HTTP_SERVER_GITHUB) == 0)
        {
            GithubClient *github = static_cast<GithubClient *>(client);
            source = github->GetRedirectUrl(src);
            json_object_object_add(params, "url", json_object_new_string("https://release-assets.githubusercontent.com"));
        }
        else
        {
            json_object_object_add(params, "url", json_object_new_string(settings.server));
        }

        json_object_object_add(params, "src_path", json_object_new_string(source.c_str()));
        json_object_object_add(params, "password", json_object_new_string(settings.password));
        if (settings.type == CLIENT_TYPE_HTTP_SERVER)
            json_object_object_add(params, "http_server_type", json_object_new_string(settings.http_server_type));

        std::string body = json_object_to_json_string(params);

        CHTTPClient::HttpResponse res;
        CHTTPClient::HeadersMap headers;
        CHTTPClient tmp_client([](const std::string &log) {});
        tmp_client.InitSession(true, CHTTPClient::SettingsFlag::NO_FLAGS);
        tmp_client.SetCertificateFile(CACERT_FILE);
        headers["Content-Type"] = "application/json";

        std::string url = "http://localhost:" + std::to_string(http_int_server_port) + "/download_url";
        return tmp_client.Post(url, headers, body, res) && HTTP_SUCCESS(res.iCode);
    }

    static bool CopyRemoteToLocal(RemoteClient *client, const CopyJob *job, const std::string &src,
                                  const std::string &dest, bool is_dir, uint64_t *loaded, std::string *failed)
    {
        if (job->skip.find(dest) != job->skip.end())
            return true;

        if (is_dir)
        {
            FS::MkDirs(dest);

            std::vector<DirEntry> entries = client->ListDir(src);
            bool ok = true;
            for (size_t i = 0; i < entries.size(); i++)
            {
                if (strcmp(entries[i].name, "..") == 0)
                    continue;
                std::string target = dest + "/" + entries[i].name;
                if (!CopyRemoteToLocal(client, job, entries[i].path, target, entries[i].isDir, loaded, failed))
                    ok = false;
            }
            return ok;
        }

        uint64_t size = 0;
        client->Size(src, &size);

        if (enable_background_download && size > minimum_backgrond_file_size &&
            QueueBackgroundDownload(client, job, src, dest, size))
        {
            *loaded += size;
            UpdateTransfer(job->id, *loaded, job->total, CopyItemName(src));
            return true;
        }

        UpdateTransfer(job->id, *loaded, job->total, CopyItemName(src));
        FILE *out = FS::Create(dest);
        if (out == nullptr)
        {
            *failed += src + ",";
            return false;
        }

        DataSink sink;
        sink.write = [out, job, loaded](const char *data, size_t len) {
            if (FS::Write(out, data, (uint32_t) len) != (int) len)
                return false;
            *loaded += len;
            UpdateTransfer(job->id, *loaded, job->total, "");
            return true;
        };

        int ret = client->GetRange(src, sink, size, 0);
        FS::Close(out);

        if (ret != 1)
        {
            FS::Rm(dest);
            *failed += src + ",";
            return false;
        }

        UpdateTransfer(job->id, *loaded, job->total, CopyItemName(src));
        return true;
    }

    static void *CopyJobThread(void *argp)
    {
        CopyJob *job = static_cast<CopyJob *>(argp);
        std::string failed_items;
        uint64_t loaded = 0;

        Activities::Guard activity(ACTIVITY_COPY, job->to_remote ? "Uploading to remote" : "Downloading to local", ACTIVITY_WEB, ACTIVITY_REMOTE);

        {
            std::lock_guard<std::mutex> lock(web_transfer_mutex);
            web_transfers[job->id].activity_id = activity.id;
        }

        std::shared_ptr<RemoteClient> held;
        RemoteClient *client = nullptr;

        {
            // Own connection for the copy, so it neither blocks nor is blocked
            // by the shared site session.
            std::map<std::string, RemoteSettings>::iterator setting = site_settings.find(job->site);
            if (setting == site_settings.end() || strlen(setting->second.server) == 0)
            {
                FinishTransfer(job->id, 2, "Unknown site");
                delete job;
                return nullptr;
            }

            held = RemotePool::AcquireShared(setting->second);
            if (held == nullptr)
            {
                FinishTransfer(job->id, 2, "Could not connect");
                delete job;
                return nullptr;
            }
            client = held.get();

            for (size_t i = 0; i < job->items.size(); i++)
            {
                const CopyItem &item = job->items[i];
                Activities::Item(activity.id, item.path);
                if (job->to_remote)
                {
                    if (item.is_dir)
                    {
                        job->total += LocalTreeSize(item.path, job->skip);
                    }
                    else
                    {
                        int64_t size = FS::GetSize(item.path);
                        if (size > 0)
                            job->total += (uint64_t) size;
                    }
                }
                else if (item.is_dir)
                {
                    job->total += RemoteTreeSize(client, item.path, job->skip);
                }
                else
                {
                    uint64_t size = 0;
                    if (client->Size(item.path, &size))
                        job->total += size;
                }
            }
            UpdateTransfer(job->id, 0, job->total, "");

            for (size_t i = 0; i < job->items.size(); i++)
            {
                const CopyItem &item = job->items[i];
                std::string target = job->dest;
                if (target.empty() || target[target.size() - 1] != '/')
                    target += "/";
                target += CopyItemName(item.path);

                if (job->to_remote)
                    CopyLocalToRemote(client, job, item.path, target, &loaded, &failed_items);
                else
                    CopyRemoteToLocal(client, job, item.path, target, item.is_dir, &loaded, &failed_items);
            }
        }

        if (!failed_items.empty())
            FinishTransfer(job->id, 2, "Failed: " + failed_items);
        else
            FinishTransfer(job->id, 1, "");

        delete job;
        return nullptr;
    }

    static void RespondConflicts(Response &res, json_object *conflicts)
    {
        json_object *result = json_object_new_object();
        JsonObjectRef result_ref(result);
        json_object_object_add(result, "id", json_object_new_string(""));
        json_object_object_add(result, "conflicts", conflicts);
        const char *result_str = json_object_to_json_string(result);
        res.status = 200;
        res.set_content(result_str, strlen(result_str), "application/json");
    }

    static void *RestartEzRemoteServerThread(void *argp)
    {
        Actions::RestartServer();
        return nullptr;
    }

    static std::mutex web_auth_mutex;
    static std::map<std::string, uint64_t> web_sessions;

    static bool WebAuthRequired()
    {
        return strlen(web_password) > 0;
    }

    static std::string WebSessionToken(const Request &req)
    {
        std::string cookies = req.get_header_value("Cookie");
        const std::string key = "ezremote_session=";
        size_t pos = cookies.find(key);
        if (pos == std::string::npos)
            return "";

        size_t start = pos + key.size();
        size_t end = cookies.find(';', start);
        return cookies.substr(start, end == std::string::npos ? std::string::npos : end - start);
    }

    // Sessions expire after `web_session_timeout` minutes without a request.
    // Util::GetTick() is microseconds, not milliseconds.
    static uint64_t WebSessionTimeoutUs()
    {
        int minutes = web_session_timeout > 0 ? web_session_timeout : 60;
        return (uint64_t) minutes * 60ull * 1000000ull;
    }

    static bool WebSessionValid(const Request &req)
    {
        if (!WebAuthRequired())
            return true;

        std::string token = WebSessionToken(req);
        if (token.empty())
            return false;

        const uint64_t now = Util::GetTick();

        std::lock_guard<std::mutex> lock(web_auth_mutex);
        std::map<std::string, uint64_t>::iterator session = web_sessions.find(token);
        if (session == web_sessions.end())
            return false;

        if (now - session->second > WebSessionTimeoutUs())
        {
            web_sessions.erase(session);
            return false;
        }

        // Sliding window: activity extends the session.
        session->second = now;
        return true;
    }

    static void WebClearSessions()
    {
        std::lock_guard<std::mutex> lock(web_auth_mutex);
        web_sessions.clear();
    }

    // Converts "rwxr-xr-x" (optionally prefixed with the file type) to mode bits.
    static int ParseRights(const char *rights)
    {
        std::string text(rights);
        if (text.size() == 10 && (text[0] == 'd' || text[0] == '-'))
            text = text.substr(1);
        if (text.size() != 9)
            return -1;

        int mode = 0;
        for (int i = 0; i < 9; i++)
        {
            char c = text[i];
            if (c == 'r' || c == 'w' || c == 'x' || c == 's' || c == 't')
                mode |= 1 << (8 - i);
            else if (c != '-' && c != ' ')
                return -1;
        }
        return mode;
    }

    void *ServerThread(void *argp)
    {
        svr->Get("/", [&](const Request &req, Response &res)
                 { res.set_redirect("/index.html"); });

        svr->Get("/index.html", [&](const Request &req, Response &res)
                 {
            FILE *in = FS::OpenRead("/data/homebrew/ezremote-client/assets/index.html");
            size_t size = FS::GetSize("/data/homebrew/ezremote-client/assets/index.html");
            res.set_content_provider(
                size, "text/html",
                [in](size_t offset, size_t length, DataSink &sink) {
                    size_t size_to_read = std::min(static_cast<size_t>(length), (size_t)1048576);
                    char buff[size_to_read];
                    size_t read_len;
                    FS::Seek(in, offset);
                    read_len = FS::Read(in, buff, size_to_read);
                    sink.write(buff, read_len);
                    return read_len == size_to_read;
                },
                [in](bool success) {
                    FS::Close(in);
                }); });

        svr->Get("/favicon.ico", [&](const Request &req, Response &res)
                 {
            FILE *in = FS::OpenRead("/data/homebrew/ezremote-client/assets/favicon.ico");
            size_t size = FS::GetSize("/data/homebrew/ezremote-client/assets/favicon.ico");
            res.set_content_provider(
                size, "image/vnd.microsoft.icon",
                [in](size_t offset, size_t length, DataSink &sink) {
                    size_t size_to_read = std::min(static_cast<size_t>(length), (size_t)1048576);
                    char buff[size_to_read];
                    size_t read_len;
                    FS::Seek(in, offset);
                    read_len = FS::Read(in, buff, size_to_read);
                    sink.write(buff, read_len);
                    return read_len == size_to_read;
                },
                [in](bool success) {
                    FS::Close(in);
                }); });

        svr->Post("/__local__/list", [&](const Request &req, Response &res)
        {
            const char *path;
            bool onlyFolders = false;
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);
            if (jobj != nullptr)
            {
                path = json_object_get_string(json_object_object_get(jobj, "path"));
                const char *onlyFolders_text = json_object_get_string(json_object_object_get(jobj, "onlyFolders"));
                if (onlyFolders_text != nullptr && strcasecmp(onlyFolders_text, "true")==0)
                    onlyFolders = true;
                if (path == nullptr)
                {
                    bad_request(res, "Required path parameter missing");
                    return;
                }
            }
            else
            {
                bad_request(res, "Invalid payload");
                return;
            }

            int err;
            std::vector<DirEntry> files = FS::ListDir(path, &err);
            DirEntry::Sort(files);
            json_object *json_files = json_object_new_array();
            for (std::vector<DirEntry>::iterator it = files.begin(); it != files.end();)
            {
                if (((onlyFolders && it->isDir) || !onlyFolders) && strcmp(it->name, "..") != 0)
                {
                    json_object *new_file = json_object_new_object();
                    char display_date[32];
                    sprintf(display_date, "%04d-%02d-%02d %02d:%02d:%02d", it->modified.year, it->modified.month, it->modified.day, it->modified.hours, it->modified.minutes, it->modified.seconds);
                    json_object_object_add(new_file, "name", json_object_new_string(it->name));
                    json_object_object_add(new_file, "rights", json_object_new_string(it->rights));
                    json_object_object_add(new_file, "date", json_object_new_string(display_date));
                    json_object_object_add(new_file, "size", json_object_new_string(it->isDir ? "" : std::to_string(it->file_size).c_str()));
                    json_object_object_add(new_file, "type", json_object_new_string(it->isDir ? "dir" : "file"));
                    json_object_array_add(json_files, new_file);
                }
                it++;
            }
            json_object *results = json_object_new_object();
            JsonObjectRef results_ref(results);
            json_object_object_add(results, "result", json_files);
            const char *results_str = json_object_to_json_string(results);

            res.status = 200;
            res.set_content(results_str, strlen(results_str), "application/json"); });

        svr->Post("/__local__/rename", [&](const Request &req, Response &res)
        {
            const char *item;
            const char *newItemPath;
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);
            if (jobj != nullptr)
            {
                item = json_object_get_string(json_object_object_get(jobj, "item"));
                newItemPath = json_object_get_string(json_object_object_get(jobj, "newItemPath"));
                if (item == nullptr || newItemPath == nullptr)
                {
                    bad_request(res, "Required item or newItemPath parameter missing");
                    return;
                }
            }
            else
            {
                bad_request(res, "Invalid payload");
                return;
            }

            FS::Rename(item, newItemPath);
            success(res);
            return; });

        svr->Post("/__local__/move", [&](const Request &req, Response &res)
        {
            Activities::Guard activity(ACTIVITY_MOVE, "Moving local files", ACTIVITY_WEB);

            const json_object *items;
            const char *newPath;
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);
            if (jobj != nullptr)
            {
                items = json_object_object_get(jobj, "items");
                newPath = json_object_get_string(json_object_object_get(jobj, "newPath"));
                if (items == nullptr || json_object_get_type(items) != json_type_array || newPath == nullptr)
                {
                    bad_request(res, "Required items or newPath parameter missing");
                    return;
                }
            }
            else
            {
                bad_request(res, "Invalid payload");
                return;
            }

            std::string failed_items;
            size_t len = json_object_array_length(items);
            for (size_t i=0; i < len; i++)
            {
                const char *item = json_object_get_string(json_object_array_get_idx(items, i));
                if (item == nullptr)
                    continue;
                Activities::Item(activity.id, item);
                DirEntry entry;
                std::string temp = std::string(item);
                size_t slash_pos = temp.find_last_of("/");
                sprintf(entry.name, "%s", temp.substr(slash_pos+1).c_str());
                sprintf(entry.path, "%s", item);
                entry.isDir = FS::IsFolder(item);
                std::string new_path = std::string(newPath);
                if (entry.isDir)
                    new_path =  new_path + "/" + entry.name;
                bool ret = CopyOrMove(entry, new_path.c_str(), false);
                if (!ret)
                {
                    failed_items += std::string(item) + ",";
                }
                if (entry.isDir && ret)
                {
                    FS::RmRecursive(item);
                }
            }

            if (failed_items.length() > 0)
            {
                std::string error_msg = std::string("One or more file(s) failed to move. ") + failed_items;
                failed(res, 200, error_msg);
            }
            else
                success(res); });

        svr->Post("/__local__/copy", [&](const Request &req, Response &res)
        {
            Activities::Guard activity(ACTIVITY_COPY, "Copying local files", ACTIVITY_WEB);

            const json_object *items;
            const char *newPath;
            const char *singleFilename;

            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);
            if (jobj != nullptr)
            {
                items = json_object_object_get(jobj, "items");
                newPath = json_object_get_string(json_object_object_get(jobj, "newPath"));
                singleFilename = json_object_get_string(json_object_object_get(jobj, "singleFilename"));

                if (items == nullptr || json_object_get_type(items) != json_type_array || newPath == nullptr)
                {
                    bad_request(res, "Required items or newPath or singleFilename parameter missing");
                    return;
                }
            }
            else
            {
                bad_request(res, "Invalid payload");
                return;
            }

            std::string failed_items;
            if (singleFilename != nullptr)
            {
                const char *src = json_object_get_string(json_object_array_get_idx(items, 0));
                if (src == nullptr)
                {
                    bad_request(res, "Required items parameter missing");
                    return;
                }
                std::string dest = std::string(newPath) + "/" + singleFilename;

                std::string temp = std::string(src);
                size_t slash_pos = temp.find_last_of("/");
                DirEntry entry;
                sprintf(entry.name, "%s", temp.substr(slash_pos+1).c_str());
                sprintf(entry.path, "%s", src);
                entry.isDir = FS::IsFolder(src);
                // The caller already chose the target name; honour it directly.
                if (dest.compare(src) != 0)
                {
                    if (!entry.isDir)
                    {
                        if (FS::Copy(src, dest.c_str()) <= 0)
                            failed_items += src;
                    }
                    else if (!CopyDirInto(src, dest))
                    {
                        failed_items += src;
                    }
                }
            }
            else
            {
                size_t len = json_object_array_length(items);
                for (size_t i=0; i < len; i++)
                {
                    const char *item = json_object_get_string(json_object_array_get_idx(items, i));
                    if (item == nullptr)
                        continue;
                    Activities::Item(activity.id, item);
                    DirEntry entry;
                    std::string temp = std::string(item);
                    size_t slash_pos = temp.find_last_of("/");
                    sprintf(entry.name, "%s", temp.substr(slash_pos+1).c_str());
                    sprintf(entry.path, "%s", item);
                    entry.isDir = FS::IsFolder(item);
                    std::string new_path = std::string(newPath);
                    if (entry.isDir)
                        new_path =  new_path + "/" + entry.name;
                    bool ret = CopyOrMove(entry, new_path.c_str(), true);
                    if (!ret)
                    {
                        failed_items += std::string(item) + ",";
                    }
                }
            }

            if (failed_items.length() > 0)
            {
                std::string error_msg = std::string("One or more file(s) failed to copy. ") + failed_items;
                failed(res, 200, error_msg);
            }
            else
                success(res); });

        svr->Post("/__local__/remove", [&](const Request &req, Response &res)
        {
            Activities::Guard activity(ACTIVITY_DELETE, "Deleting local files", ACTIVITY_WEB);

            json_object *items;
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);
            if (jobj != nullptr)
            {
                items = json_object_object_get(jobj, "items");
                if (items == nullptr || json_object_get_type(items) != json_type_array)
                {
                    bad_request(res, "Required items parameter missing");
                    return;
                }
            }
            else
            {
                bad_request(res, "Invalid payload");
                return;
            }

            std::string failed_items;
            size_t len = json_object_array_length(items);
            for (size_t i=0; i < len; i++)
            {
                const char *item = json_object_get_string(json_object_array_get_idx(items, i));
                if (item == nullptr)
                    continue;
                Activities::Item(activity.id, item);
                bool ret = FS::RmRecursive(item);
                if (!ret)
                {
                    failed_items += std::string(item) + ",";
                }
            }

            if (failed_items.length() > 0)
            {
                std::string error_msg = std::string("One or more file(s) failed to delete. ") + failed_items;
                failed(res, 200, error_msg);
            }
            else
                success(res); });

        svr->Post("/__local__/install", [&](const Request &req, Response &res)
        {
            Activities::Guard activity(ACTIVITY_INSTALL, "Installing local packages", ACTIVITY_WEB);
            json_object *items;
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);
            if (jobj != nullptr)
            {
                items = json_object_object_get(jobj, "items");
                if (items == nullptr || json_object_get_type(items) != json_type_array)
                {
                    bad_request(res, "Required items parameter missing");
                    return;
                }
            }
            else
            {
                bad_request(res, "Invalid payload");
                return;
            }

            std::string failed_items;
            size_t len = json_object_array_length(items);
            for (size_t i=0; i < len; i++)
            {
                const char *item = json_object_get_string(json_object_array_get_idx(items, i));
                if (item == nullptr)
                    continue;
                Activities::Item(activity.id, item);
                if (!INSTALLER::InstallLocalPkg(item))
                    failed_items += (std::string(item) + ",");
            }

            if (failed_items.length() > 0)
            {
                std::string error_msg = std::string("One or more file(s) failed to install. ") + failed_items;
                failed(res, 200, error_msg);
            }
            else
                success(res);
        });

        svr->Post("/__local__/edit", [&](const Request &req, Response &res)
        {
            const char *item;
            const char *content;
            size_t content_len;
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);
            if (jobj != nullptr)
            {
                item = json_object_get_string(json_object_object_get(jobj, "item"));
                json_object *content_obj = json_object_object_get(jobj, "content");
                content = json_object_get_string(content_obj);
                content_len = json_object_get_string_len(content_obj);
                if (item == nullptr || content == nullptr)
                {
                    bad_request(res, "Required item or content parameter missing");
                    return;
                }
            }
            else
            {
                bad_request(res, "Invalid payload");
                return;
            }

            bool ret = FS::Save(item, content, content_len);
            if (!ret)
            {
                failed(res, 200, "Failed to save content to file.");
                return;
            }

            success(res); });

        svr->Post("/__local__/getContent", [&](const Request &req, Response &res)
        {
            const char *item;
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);
            if (jobj != nullptr)
            {
                item = json_object_get_string(json_object_object_get(jobj, "item"));
                if (item == nullptr)
                {
                    bad_request(res, "Required item parameter missing");
                    return;
                }
            }
            else
            {
                bad_request(res, "Invalid payload");
                return;
            }

            std::vector<char> content = FS::Load(item);
            json_object *result = json_object_new_object();
            JsonObjectRef result_ref(result);
            const char *content_str = content.empty() ? "" : content.data();
            json_object_object_add(result, "result", json_object_new_string(content_str));
            const char *result_str = json_object_to_json_string(result);

            res.status = 200;
            res.set_content(result_str, strlen(result_str), "application/json");
        });

        svr->Post("/__local__/createFolder", [&](const Request &req, Response &res)
        {
            const char *newPath;
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);
            if (jobj != nullptr)
            {
                newPath = json_object_get_string(json_object_object_get(jobj, "newPath"));
                if (newPath == nullptr)
                {
                    bad_request(res, "Required newPath parameter missing");
                    return;
                }
            }
            else
            {
                bad_request(res, "Invalid payload");
                return;
            }

            FS::MkDirs(newPath);
            success(res); });

        svr->Post("/__local__/permission", [&](const Request &req, Response &res)
                  { failed(res, 200, "Operation not supported"); });

        // Server profiles, mirroring what the connection panel edits.
        svr->Get("/__local__/servers", [&](const Request &req, Response &res)
                 {
            json_object *list = json_object_new_array();

            for (size_t i = 0; i < sites.size(); i++)
            {
                RemoteSettings &setting = site_settings[sites[i]];
                json_object *item = json_object_new_object();

                json_object_object_add(item, "name", json_object_new_string(sites[i].c_str()));
                json_object_object_add(item, "server", json_object_new_string(setting.server));
                json_object_object_add(item, "username", json_object_new_string(setting.username));
                json_object_object_add(item, "hasPassword", json_object_new_boolean(strlen(setting.password) > 0));
                json_object_object_add(item, "type", json_object_new_int((int)setting.type));
                json_object_object_add(item, "httpServerType", json_object_new_string(setting.http_server_type));
                json_object_object_add(item, "defaultDirectory", json_object_new_string(setting.default_directory));
                json_object_object_add(item, "enableRpi", json_object_new_boolean(setting.enable_rpi));
                json_object_object_add(item, "enableDiskCache", json_object_new_boolean(setting.enable_disk_cache));

                uint32_t capabilities = 0;
                if (strlen(setting.server) > 0)
                {
                    RemoteClient *probe = Actions::CreateRemoteClient(&setting);
                    if (probe != nullptr)
                    {
                        capabilities = probe->SupportedActions();
                        delete probe;
                    }
                }
                json_object_object_add(item, "supportedActions", json_object_new_int64((int64_t)capabilities));
                json_object_array_add(list, item);
            }

            json_object *types = json_object_new_array();
            json_object_array_add(types, json_object_new_string("FTP"));
            json_object_array_add(types, json_object_new_string("SFTP"));
            json_object_array_add(types, json_object_new_string("SMB"));
            json_object_array_add(types, json_object_new_string("WebDAV"));
            json_object_array_add(types, json_object_new_string("HTTP server"));
            json_object_array_add(types, json_object_new_string("NFS"));
            json_object_array_add(types, json_object_new_string("File host"));

            json_object *http_types = json_object_new_array();
            for (size_t i = 0; i < http_servers.size(); i++)
            {
                json_object_array_add(http_types, json_object_new_string(http_servers[i].c_str()));
            }

            json_object *result = json_object_new_object();
            JsonObjectRef result_ref(result);
            json_object_object_add(result, "active", json_object_new_string(GetWebSite().c_str()));
            json_object_object_add(result, "sites", list);
            json_object_object_add(result, "clientTypes", types);
            json_object_object_add(result, "httpServerTypes", http_types);

            const char *result_str = json_object_to_json_string(result);
            res.status = 200;
            res.set_content(result_str, strlen(result_str), "application/json"); });

        svr->Post("/__local__/server", [&](const Request &req, Response &res)
        {
            const char *name = nullptr;
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);

            if (jobj == nullptr)
            {
                bad_request(res, "Invalid payload");
                return;
            }

            name = json_object_get_string(json_object_object_get(jobj, "name"));
            if (name == nullptr || site_settings.find(name) == site_settings.end())
            {
                bad_request(res, "Unknown site");
                return;
            }

            RemoteSettings &setting = site_settings[name];

            const char *server = json_object_get_string(json_object_object_get(jobj, "server"));
            if (server != nullptr)
                snprintf(setting.server, sizeof(setting.server), "%s", server);

            const char *username = json_object_get_string(json_object_object_get(jobj, "username"));
            if (username != nullptr)
                snprintf(setting.username, sizeof(setting.username), "%s", username);

            // An absent password field keeps the stored one; an empty string clears it.
            json_object *password_obj = json_object_object_get(jobj, "password");
            if (password_obj != nullptr)
            {
                const char *password = json_object_get_string(password_obj);
                snprintf(setting.password, sizeof(setting.password), "%s", password == nullptr ? "" : password);
            }

            const char *http_server_type = json_object_get_string(json_object_object_get(jobj, "httpServerType"));
            if (http_server_type != nullptr)
                snprintf(setting.http_server_type, sizeof(setting.http_server_type), "%s", http_server_type);

            const char *default_directory = json_object_get_string(json_object_object_get(jobj, "defaultDirectory"));
            if (default_directory != nullptr && default_directory[0] == '/')
                snprintf(setting.default_directory, sizeof(setting.default_directory), "%s", default_directory);

            json_object *rpi_obj = json_object_object_get(jobj, "enableRpi");
            if (rpi_obj != nullptr)
                setting.enable_rpi = json_object_get_boolean(rpi_obj) != 0;

            json_object *cache_obj = json_object_object_get(jobj, "enableDiskCache");
            if (cache_obj != nullptr)
                setting.enable_disk_cache = json_object_get_boolean(cache_obj) != 0;

            CONFIG::SetClientType(&setting);
            CONFIG::SaveSiteConfig(name);

            success(res); });

        svr->Post("/__local__/server/select", [&](const Request &req, Response &res)
        {
            const char *name = nullptr;
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);

            if (jobj == nullptr)
            {
                bad_request(res, "Invalid payload");
                return;
            }

            name = json_object_get_string(json_object_object_get(jobj, "name"));
            if (name == nullptr || site_settings.find(name) == site_settings.end())
            {
                bad_request(res, "Unknown site");
                return;
            }

            // Web-only selection: last_site / remote_settings / remote_directory
            // belong to the native UI and must not move from here.
            SetWebSite(name);
            CONFIG::SaveWebSite(name);

            success(res); });

        svr->Post("/__local__/install_bg", [&](const Request &req, Response &res)
        {
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);

            const char *path = nullptr;
            uint64_t size = 0;
            std::string site = ResolveSite(nullptr);

            if (jobj != nullptr)
            {
                path = json_object_get_string(json_object_object_get(jobj, "path"));
                size = json_object_get_uint64(json_object_object_get(jobj, "size"));
                site = ResolveSite(json_object_get_string(json_object_object_get(jobj, "site")));
            }

            if (path == nullptr)
            {
                bad_request(res, "Required path parameter missing");
                return;
            }

            std::map<std::string, RemoteSettings>::iterator it = site_settings.find(site);
            if (it == site_settings.end())
            {
                failed(res, 200, "Unknown site");
                return;
            }

            RemoteSettings &settings = it->second;

            // One connection for the whole handler: redirect resolution and the
            // header/title read share it instead of taking the session twice.
            std::shared_ptr<RemoteClient> held = RemotePool::AcquireShared(settings);
            if (held == nullptr)
            {
                failed(res, 200, "Could not connect");
                return;
            }

            bool anonymous = strlen(settings.username) == 0 && strlen(settings.password) == 0 &&
                (settings.type == CLIENT_TYPE_WEBDAV ||
                 (settings.type == CLIENT_TYPE_HTTP_SERVER &&
                  strcmp(settings.http_server_type, HTTP_SERVER_ARCHIVEORG) != 0 &&
                  strcmp(settings.http_server_type, HTTP_SERVER_GITHUB) != 0));

            std::string url;
            if (anonymous)
            {
                // No credentials: stream straight from the server, as
                // INSTALLER::getRemoteUrl does for this case.
                std::string full_url = WebDAVClient::GetHttpUrl(std::string(settings.server) + path);
                size_t scheme_pos = full_url.find("://");
                size_t root_pos = (scheme_pos == std::string::npos) ? std::string::npos : full_url.find("/", scheme_pos + 3);
                if (root_pos == std::string::npos)
                {
                    failed(res, 200, "Invalid server URL");
                    return;
                }
                url = full_url.substr(0, root_pos) + httplib::detail::encode_url(full_url.substr(root_pos));
            }
            else
            {
                std::string target_path(path);
                if (settings.type == CLIENT_TYPE_HTTP_SERVER &&
                    strcmp(settings.http_server_type, HTTP_SERVER_GITHUB) == 0)
                {
                    target_path = static_cast<GithubClient *>(held.get())->GetRedirectUrl(target_path);
                }

                std::string hash = INSTALLER::StoreBgInstallHostData(&settings, target_path, size);
                if (hash.empty())
                {
                    failed(res, 200, "Could not queue the install");
                    return;
                }
                url = "http://localhost:" + std::to_string(http_int_server_port) + "/bg_install/" + hash;
            }

            pkg_header header;
            memset(&header, 0, sizeof(header));
            std::string title;
            title = INSTALLER::GetRemotePkgTitle(held.get(), path, &header);

            if (INSTALLER::InstallRemotePkg(url, &header, title) == 0)
            {
                failed(res, 200, "Install failed");
                return;
            }

            success(res);
        });

        // Identifies the client; the server's launcher polls it to know when the
        // web UI is ready.
        svr->Get("/version", [&](const Request &req, Response &res)
        {
            std::string version = "unknown";
            FILE *file = fopen(DATA_PATH "/version.txt", "r");
            if (file != nullptr)
            {
                char buffer[64] = {0};
                if (fgets(buffer, sizeof(buffer), file) != nullptr)
                    version = buffer;
                fclose(file);

                while (!version.empty() && (version[version.size() - 1] == '\n' || version[version.size() - 1] == '\r'))
                    version.erase(version.size() - 1);
            }

            res.status = 200;
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_content(version, "text/plain");
        });

        svr->Get("/__local__/ezremote_server/version", [&](const Request &req, Response &res)
        {
            CHTTPClient::HttpResponse http_res;
            CHTTPClient::HeadersMap headers;
            CHTTPClient tmp_client([](const std::string &log) {});
            tmp_client.InitSession(true, CHTTPClient::SettingsFlag::NO_FLAGS);
            tmp_client.SetCertificateFile(CACERT_FILE);
            tmp_client.SetTimeout(1);

            std::string version;
            std::string url = "http://localhost:" + std::to_string(http_int_server_port) + "/version";
            if (tmp_client.Get(url, headers, http_res) && HTTP_SUCCESS(http_res.iCode))
                version.assign(http_res.strBody.begin(), http_res.strBody.end());

            json_object *result = json_object_new_object();
            JsonObjectRef result_ref(result);
            json_object_object_add(result, "version", json_object_new_string(version.c_str()));
            const char *result_str = json_object_to_json_string(result);
            res.status = 200;
            res.set_content(result_str, strlen(result_str), "application/json");
        });

        // Restarting sleeps a few seconds; run it off the request thread.
        svr->Post("/__local__/ezremote_server/stop", [&](const Request &req, Response &res)
        {
            Actions::StopServer();
            success(res);
        });

        svr->Post("/__local__/ezremote_server/restart", [&](const Request &req, Response &res)
        {
            pthread_t tid;
            if (pthread_create(&tid, NULL, RestartEzRemoteServerThread, NULL) != 0)
            {
                failed(res, 200, "Could not restart the ezRemote server");
                return;
            }
            pthread_detach(tid);
            success(res);
        });

        // Proxies the ezRemote server's background download state.
        svr->Get("/__local__/activities", [&](const Request &req, Response &res)
        {
            std::vector<Activity> list = Activities::Snapshot();
            const uint64_t now = Util::GetTick();

            json_object *result = json_object_new_array();
            JsonObjectRef result_ref(result);
            for (size_t i = 0; i < list.size(); i++)
            {
                json_object *item = json_object_new_object();
                json_object_object_add(item, "id", json_object_new_uint64(list[i].id));
                json_object_object_add(item, "kind", json_object_new_string(Activities::KindName(list[i].kind)));
                json_object_object_add(item, "state", json_object_new_string(Activities::StateName(list[i].state)));
                json_object_object_add(item, "label", json_object_new_string(list[i].label.c_str()));
                json_object_object_add(item, "item", json_object_new_string(list[i].item.c_str()));
                json_object_object_add(item, "done", json_object_new_uint64(list[i].done));
                json_object_object_add(item, "total", json_object_new_uint64(list[i].total));
                json_object_object_add(item, "elapsed", json_object_new_uint64(((list[i].finished > 0 ? list[i].finished : now) - list[i].started) / 1000000));
                json_object_object_add(item, "origin", json_object_new_string(Activities::OriginName(list[i].origin)));
                json_object_array_add(result, item);
            }

            const char *result_str = json_object_to_json_string(result);
            res.status = 200;
            res.set_content(result_str, strlen(result_str), "application/json");
        });

        svr->Post("/__local__/activities/clear", [&](const Request &req, Response &res)
        {
            int removed = Activities::ClearFinished();

            json_object *result = json_object_new_object();
            JsonObjectRef result_ref(result);
            json_object_object_add(result, "removed", json_object_new_int(removed));
            const char *result_str = json_object_to_json_string(result);
            res.status = 200;
            res.set_content(result_str, strlen(result_str), "application/json");
        });

        svr->Get("/__local__/bg_downloads", [&](const Request &req, Response &res)
        {
            json_object *list = json_object_new_array();

            CHTTPClient::HttpResponse http_res;
            CHTTPClient::HeadersMap headers;
            CHTTPClient tmp_client([](const std::string &log) {});
            tmp_client.InitSession(true, CHTTPClient::SettingsFlag::NO_FLAGS);
            tmp_client.SetCertificateFile(CACERT_FILE);
            tmp_client.SetTimeout(1);

            std::string url = "http://localhost:" + std::to_string(http_int_server_port) + "/get_download_state";
            if (tmp_client.Get(url, headers, http_res) && HTTP_SUCCESS(http_res.iCode))
            {
                std::string body(http_res.strBody.begin(), http_res.strBody.end());
                json_object *jobj = json_tokener_parse(body.c_str());
                JsonObjectRef jobj_ref(jobj);
                if (jobj != nullptr && json_object_get_type(jobj) == json_type_array)
                {
                    size_t count = json_object_array_length(jobj);
                    for (size_t i = 0; i < count; i++)
                    {
                        json_object *entry = json_object_array_get_idx(jobj, i);
                        if (entry == nullptr)
                            continue;

                        json_object *item = json_object_new_object();
                        json_object_object_add(item, "path", json_object_new_string(JsonGetString(entry, "path").c_str()));
                        json_object_object_add(item, "loaded", json_object_new_int64((int64_t) json_object_get_uint64(json_object_object_get(entry, "bytes_transfered"))));
                        json_object_object_add(item, "size", json_object_new_int64((int64_t) json_object_get_uint64(json_object_object_get(entry, "file_size"))));
                        json_object_object_add(item, "state", json_object_new_int(json_object_get_int(json_object_object_get(entry, "state"))));
                        json_object_array_add(list, item);
                    }
                }
            }

            json_object *result = json_object_new_object();
            JsonObjectRef result_ref(result);
            json_object_object_add(result, "result", list);
            const char *result_str = json_object_to_json_string(result);
            res.status = 200;
            res.set_content(result_str, strlen(result_str), "application/json");
        });

        svr->Post("/__local__/bg_downloads/clear", [&](const Request &req, Response &res)
        {
            CHTTPClient::HttpResponse http_res;
            CHTTPClient::HeadersMap headers;
            CHTTPClient tmp_client([](const std::string &log) {});
            tmp_client.InitSession(true, CHTTPClient::SettingsFlag::NO_FLAGS);
            tmp_client.SetCertificateFile(CACERT_FILE);
            tmp_client.SetTimeout(5);

            headers["Content-Type"] = "application/json";

            std::string url = "http://localhost:" + std::to_string(http_int_server_port) + "/clear_download_history";
            if (tmp_client.Post(url, headers, "{}", http_res) && HTTP_SUCCESS(http_res.iCode))
            {
                success(res);
                return;
            }

            // Surface the status so a stale ezRemote server (no such route) is
            // distinguishable from a real failure.
            std::string message = "Could not clear the download history (HTTP " + std::to_string(http_res.iCode) + ")";
            failed(res, 200, message);
        });

        // Extracts the SFO/param.json and icon from a local package, the same way
        // the native package info dialog does.
        svr->Post("/__local__/pkg_info", [&](const Request &req, Response &res)
        {
            const char *path = nullptr;
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);

            if (jobj != nullptr)
                path = json_object_get_string(json_object_object_get(jobj, "item"));

            if (path == nullptr)
            {
                bad_request(res, "Required item parameter missing");
                return;
            }

            if (!INSTALLER::ExtractLocalPkg(path, TMP_SFO_PATH, TMP_PARAM_JSON_PATH, TMP_ICON_PATH))
            {
                failed(res, 200, "Cannot read this package");
                return;
            }

            std::map<std::string, std::string> params;
            if (FS::FileExists(TMP_SFO_PATH))
            {
                std::vector<char> sfo = FS::Load(TMP_SFO_PATH);
                if (!sfo.empty())
                    params = SFO::GetParams(sfo.data(), sfo.size());
            }
            else if (FS::FileExists(TMP_PARAM_JSON_PATH))
            {
                std::vector<char> sfo = FS::Load(TMP_PARAM_JSON_PATH);
                if (!sfo.empty())
                    params = SFO::GetParamsFromParamJson(sfo.data(), sfo.size());
            }

            json_object *attributes = json_object_new_object();
            for (std::map<std::string, std::string>::iterator it = params.begin(); it != params.end(); ++it)
            {
                json_object_object_add(attributes, it->first.c_str(), json_object_new_string(it->second.c_str()));
            }

            json_object *result = json_object_new_object();
            JsonObjectRef result_ref(result);
            json_object_object_add(result, "params", attributes);
            json_object_object_add(result, "hasIcon", json_object_new_boolean(FS::FileExists(TMP_ICON_PATH)));

            const char *result_str = json_object_to_json_string(result);
            res.status = 200;
            res.set_content(result_str, strlen(result_str), "application/json");
        });

        svr->Post("/__remote__/pkg_info", [&](const Request &req, Response &res)
        {
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);

            const char *path = nullptr;
            std::string site = ResolveSite(nullptr);
            if (jobj != nullptr)
            {
                path = json_object_get_string(json_object_object_get(jobj, "path"));
                site = ResolveSite(json_object_get_string(json_object_object_get(jobj, "site")));
            }

            if (path == nullptr)
            {
                bad_request(res, "Required path parameter missing");
                return;
            }

            {
                std::map<std::string, RemoteSettings>::iterator setting = site_settings.find(site);
                if (setting == site_settings.end())
                {
                    failed(res, 200, "Unknown site");
                    return;
                }

                // Extraction reads the whole package header tree; own connection.
                std::shared_ptr<RemoteClient> held = RemotePool::AcquireShared(setting->second);
                RemoteClient *client = held.get();
                if (client == nullptr)
                {
                    failed(res, 200, "Could not connect");
                    return;
                }

                bool extracted = INSTALLER::ExtractRemotePkg(client, path, TMP_SFO_PATH, TMP_PARAM_JSON_PATH, TMP_ICON_PATH);

                if (!extracted)
                {
                    failed(res, 200, "Cannot read this package");
                    return;
                }
            }

            std::map<std::string, std::string> params;
            if (FS::FileExists(TMP_SFO_PATH))
            {
                std::vector<char> sfo = FS::Load(TMP_SFO_PATH);
                if (!sfo.empty())
                    params = SFO::GetParams(sfo.data(), sfo.size());
            }
            else if (FS::FileExists(TMP_PARAM_JSON_PATH))
            {
                std::vector<char> sfo = FS::Load(TMP_PARAM_JSON_PATH);
                if (!sfo.empty())
                    params = SFO::GetParamsFromParamJson(sfo.data(), sfo.size());
            }

            json_object *attributes = json_object_new_object();
            for (std::map<std::string, std::string>::iterator it = params.begin(); it != params.end(); ++it)
                json_object_object_add(attributes, it->first.c_str(), json_object_new_string(it->second.c_str()));

            json_object *result = json_object_new_object();
            JsonObjectRef result_ref(result);
            json_object_object_add(result, "params", attributes);
            json_object_object_add(result, "hasIcon", json_object_new_boolean(FS::FileExists(TMP_ICON_PATH)));

            const char *result_str = json_object_to_json_string(result);
            res.status = 200;
            res.set_content(result_str, strlen(result_str), "application/json");
        });

        svr->Get("/__remote__/pkg_icon", [&](const Request &req, Response &res)
        {
            std::vector<char> icon = FS::Load(TMP_ICON_PATH);
            if (icon.empty())
            {
                res.status = 404;
                return;
            }
            res.set_content(icon.data(), icon.size(), "image/png");
        });

        svr->Get("/__local__/pkg_icon", [&](const Request &req, Response &res)
        {
            std::vector<char> icon = FS::Load(TMP_ICON_PATH);
            if (icon.empty())
            {
                res.status = 404;
                return;
            }
            res.set_content(icon.data(), icon.size(), "image/png");
        });

        // Cross-pane transfers: the commander layout copies files between the
        // local and remote panes without a browser round trip.
        // Starts an async local -> remote copy; the UI polls /__local__/transfer.
        svr->Post("/__remote__/copy_from_local", [&](const Request &req, Response &res)
        {
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);
            json_object *items = nullptr;
            const char *dest = nullptr;
            std::string site = ResolveSite(nullptr);

            if (jobj != nullptr)
            {
                items = json_object_object_get(jobj, "items");
                dest = json_object_get_string(json_object_object_get(jobj, "dest"));
                site = ResolveSite(json_object_get_string(json_object_object_get(jobj, "site")));
            }

            if (items == nullptr || json_object_get_type(items) != json_type_array || dest == nullptr)
            {
                bad_request(res, "Required items and dest parameters missing");
                return;
            }

            bool overwrite = false;
            json_object *overwrite_obj = json_object_object_get(jobj, "overwrite");
            if (overwrite_obj != nullptr)
                overwrite = json_object_get_boolean(overwrite_obj) != 0;

            CopyJob *job = new CopyJob();
            job->site = site;
            job->dest = dest;
            job->to_remote = true;

            ParseCopyItems(items, &job->items);

            json_object *skip_obj = json_object_object_get(jobj, "skip");
            if (skip_obj != nullptr && json_object_get_type(skip_obj) == json_type_array)
            {
                size_t skip_count = json_object_array_length(skip_obj);
                for (size_t i = 0; i < skip_count; i++)
                {
                    const char *skipped = json_object_get_string(json_object_array_get_idx(skip_obj, i));
                    if (skipped != nullptr)
                        job->skip.insert(skipped);
                }
            }

            if (!overwrite)
            {
                json_object *conflicts = json_object_new_array();
                {
                    std::map<std::string, RemoteSettings>::iterator setting = site_settings.find(site);
                    if (setting == site_settings.end())
                    {
                        json_object_put(conflicts);
                        delete job;
                        failed(res, 200, "Unknown site");
                        return;
                    }

                    std::shared_ptr<RemoteClient> held = RemotePool::AcquireShared(setting->second);
                    if (held == nullptr)
                    {
                        json_object_put(conflicts);
                        delete job;
                        failed(res, 200, "Could not connect");
                        return;
                    }

                    for (size_t i = 0; i < job->items.size(); i++)
                    {
                        std::string target = job->dest;
                        if (target.empty() || target[target.size() - 1] != '/')
                            target += "/";
                        target += CopyItemName(job->items[i].path);

                        ProbeLocalConflicts(held.get(), job->items[i].path, target, conflicts);
                    }
                }

                if (json_object_array_length(conflicts) > 0)
                {
                    delete job;
                    RespondConflicts(res, conflicts);
                    return;
                }
                json_object_put(conflicts);
            }

            job->id = NewTransferId();

            std::string transfer_id = job->id;
            pthread_t tid;
            if (job->items.empty() || pthread_create(&tid, NULL, CopyJobThread, job) != 0)
            {
                FinishTransfer(transfer_id, 2, "Could not start the copy");
                delete job;
                failed(res, 200, "Could not start the copy");
                return;
            }
            pthread_detach(tid);

            json_object *result = json_object_new_object();
            JsonObjectRef result_ref(result);
            json_object_object_add(result, "id", json_object_new_string(transfer_id.c_str()));
            json_object_object_add(result, "conflicts", json_object_new_array());
            const char *result_str = json_object_to_json_string(result);
            res.status = 200;
            res.set_content(result_str, strlen(result_str), "application/json");
        });

        // Starts an async remote -> local copy.
        svr->Post("/__local__/copy_from_remote", [&](const Request &req, Response &res)
        {
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);
            json_object *items = nullptr;
            const char *dest = nullptr;
            std::string site = ResolveSite(nullptr);

            if (jobj != nullptr)
            {
                items = json_object_object_get(jobj, "items");
                dest = json_object_get_string(json_object_object_get(jobj, "dest"));
                site = ResolveSite(json_object_get_string(json_object_object_get(jobj, "site")));
            }

            if (items == nullptr || json_object_get_type(items) != json_type_array || dest == nullptr)
            {
                bad_request(res, "Required items and dest parameters missing");
                return;
            }

            bool overwrite = false;
            json_object *overwrite_obj = json_object_object_get(jobj, "overwrite");
            if (overwrite_obj != nullptr)
                overwrite = json_object_get_boolean(overwrite_obj) != 0;

            CopyJob *job = new CopyJob();
            job->site = site;
            job->dest = dest;
            job->to_remote = false;

            ParseCopyItems(items, &job->items);

            json_object *skip_obj = json_object_object_get(jobj, "skip");
            if (skip_obj != nullptr && json_object_get_type(skip_obj) == json_type_array)
            {
                size_t skip_count = json_object_array_length(skip_obj);
                for (size_t i = 0; i < skip_count; i++)
                {
                    const char *skipped = json_object_get_string(json_object_array_get_idx(skip_obj, i));
                    if (skipped != nullptr)
                        job->skip.insert(skipped);
                }
            }

            if (!overwrite)
            {
                json_object *conflicts = json_object_new_array();
                {
                    std::map<std::string, RemoteSettings>::iterator setting = site_settings.find(site);
                    if (setting == site_settings.end())
                    {
                        json_object_put(conflicts);
                        delete job;
                        failed(res, 200, "Unknown site");
                        return;
                    }

                    std::shared_ptr<RemoteClient> held = RemotePool::AcquireShared(setting->second);
                    if (held == nullptr)
                    {
                        json_object_put(conflicts);
                        delete job;
                        failed(res, 200, "Could not connect");
                        return;
                    }

                    for (size_t i = 0; i < job->items.size(); i++)
                    {
                        std::string target = job->dest;
                        if (target.empty() || target[target.size() - 1] != '/')
                            target += "/";
                        target += CopyItemName(job->items[i].path);

                        ProbeRemoteConflicts(held.get(), job->items[i].path, target, job->items[i].is_dir, conflicts);
                    }
                }

                if (json_object_array_length(conflicts) > 0)
                {
                    delete job;
                    RespondConflicts(res, conflicts);
                    return;
                }
                json_object_put(conflicts);
            }

            job->id = NewTransferId();

            std::string transfer_id = job->id;
            pthread_t tid;
            if (job->items.empty() || pthread_create(&tid, NULL, CopyJobThread, job) != 0)
            {
                FinishTransfer(transfer_id, 2, "Could not start the copy");
                delete job;
                failed(res, 200, "Could not start the copy");
                return;
            }
            pthread_detach(tid);

            json_object *result = json_object_new_object();
            JsonObjectRef result_ref(result);
            json_object_object_add(result, "id", json_object_new_string(transfer_id.c_str()));
            json_object_object_add(result, "conflicts", json_object_new_array());
            const char *result_str = json_object_to_json_string(result);
            res.status = 200;
            res.set_content(result_str, strlen(result_str), "application/json");
        });

        // Remote actions, gated in the UI by RemoteClient::SupportedActions().
        svr->Post("/__remote__/copy", [&](const Request &req, Response &res)
        {
            Activities::Guard activity(ACTIVITY_COPY, "Copying remote files", ACTIVITY_WEB, ACTIVITY_REMOTE);
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);
            json_object *items = nullptr;
            const char *dest = nullptr;
            std::string site = ResolveSite(nullptr);

            if (jobj != nullptr)
            {
                items = json_object_object_get(jobj, "items");
                dest = json_object_get_string(json_object_object_get(jobj, "dest"));
                site = ResolveSite(json_object_get_string(json_object_object_get(jobj, "site")));
            }

            if (items == nullptr || json_object_get_type(items) != json_type_array || dest == nullptr)
            {
                bad_request(res, "Required items and dest parameters missing");
                return;
            }

            std::map<std::string, RemoteSettings>::iterator setting = site_settings.find(site);
            if (setting == site_settings.end())
            {
                failed(res, 200, "Unknown site");
                return;
            }

            std::shared_ptr<RemoteClient> held = RemotePool::AcquireShared(setting->second);
            RemoteClient *client = held.get();
            if (client == nullptr)
            {
                failed(res, 200, "Could not connect");
                return;
            }

            std::string failed_items;
            size_t count = json_object_array_length(items);
            for (size_t i = 0; i < count; i++)
            {
                const char *item = json_object_get_string(json_object_array_get_idx(items, i));
                if (item == nullptr)
                    continue;
                Activities::Item(activity.id, item);

                std::string source(item);
                std::string target = std::string(dest);
                if (target.empty() || target[target.size() - 1] != '/')
                    target += "/";
                target += CopyItemName(source);

                if (client->Copy(source, target) == 0)
                    failed_items += source + ",";
            }


            if (!failed_items.empty())
            {
                failed(res, 200, "Failed to copy: " + failed_items);
                return;
            }
            success(res);
        });

        svr->Post("/__remote__/move", [&](const Request &req, Response &res)
        {
            Activities::Guard activity(ACTIVITY_MOVE, "Moving remote files", ACTIVITY_WEB, ACTIVITY_REMOTE);
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);
            json_object *items = nullptr;
            const char *dest = nullptr;
            std::string site = ResolveSite(nullptr);

            if (jobj != nullptr)
            {
                items = json_object_object_get(jobj, "items");
                dest = json_object_get_string(json_object_object_get(jobj, "dest"));
                site = ResolveSite(json_object_get_string(json_object_object_get(jobj, "site")));
            }

            if (items == nullptr || json_object_get_type(items) != json_type_array || dest == nullptr)
            {
                bad_request(res, "Required items and dest parameters missing");
                return;
            }

            std::map<std::string, RemoteSettings>::iterator setting = site_settings.find(site);
            if (setting == site_settings.end())
            {
                failed(res, 200, "Unknown site");
                return;
            }

            std::shared_ptr<RemoteClient> held = RemotePool::AcquireShared(setting->second);
            RemoteClient *client = held.get();
            if (client == nullptr)
            {
                failed(res, 200, "Could not connect");
                return;
            }

            std::string failed_items;
            size_t count = json_object_array_length(items);
            for (size_t i = 0; i < count; i++)
            {
                const char *item = json_object_get_string(json_object_array_get_idx(items, i));
                if (item == nullptr)
                    continue;
                Activities::Item(activity.id, item);

                std::string source(item);
                std::string target = std::string(dest);
                if (target.empty() || target[target.size() - 1] != '/')
                    target += "/";
                target += CopyItemName(source);

                if (client->Move(source, target) == 0)
                    failed_items += source + ",";
            }


            if (!failed_items.empty())
            {
                failed(res, 200, "Failed to move: " + failed_items);
                return;
            }
            success(res);
        });

        svr->Post("/__remote__/newfile", [&](const Request &req, Response &res)
        {
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);
            const char *path = nullptr;
            std::string site = ResolveSite(nullptr);

            if (jobj != nullptr)
            {
                path = json_object_get_string(json_object_object_get(jobj, "path"));
                site = ResolveSite(json_object_get_string(json_object_object_get(jobj, "site")));
            }

            if (path == nullptr || path[0] == '\0')
            {
                bad_request(res, "Missing path");
                return;
            }

            std::string temp_path = std::string(temp_folder) + "/remote_newfile";
            FILE *fp = FS::Create(temp_path);
            if (fp == nullptr)
            {
                failed(res, 200, "Cannot create a temporary file");
                return;
            }
            FS::Close(fp);

            bool ok = false;
            std::string error;
            {
                std::map<std::string, RemoteSettings>::iterator setting = site_settings.find(site);
                if (setting == site_settings.end())
                {
                    error = "Unknown site";
                }
                else
                {
                    // Self-releasing: no path can forget to return the slot.
                    std::shared_ptr<RemoteClient> held = RemotePool::AcquireShared(setting->second);
                    if (held == nullptr)
                        error = "Could not connect";
                    else
                        ok = held->Put(temp_path, path) != 0;
                }
            }
            FS::Rm(temp_path);

            if (!ok)
            {
                failed(res, 200, error.empty() ? "Cannot create this file" : error);
                return;
            }
            success(res);
        });

        svr->Post("/__remote__/getContent", [&](const Request &req, Response &res)
        {
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);
            const char *item = nullptr;
            std::string site = ResolveSite(nullptr);

            if (jobj != nullptr)
            {
                item = json_object_get_string(json_object_object_get(jobj, "item"));
                site = ResolveSite(json_object_get_string(json_object_object_get(jobj, "site")));
            }

            if (item == nullptr)
            {
                bad_request(res, "Required item parameter missing");
                return;
            }

            std::string temp_path = std::string(temp_folder) + "/remote_edit";
            bool ok = false;
            std::string error;
            {
                std::map<std::string, RemoteSettings>::iterator setting = site_settings.find(site);
                if (setting == site_settings.end())
                {
                    error = "Unknown site";
                }
                else
                {
                    // Self-releasing: the editor works even mid-transfer.
                    std::shared_ptr<RemoteClient> held = RemotePool::AcquireShared(setting->second);
                    if (held == nullptr)
                        error = "Could not connect";
                    else
                        ok = held->Get(temp_path, item) != 0;
                }
            }

            std::vector<char> content;
            if (ok)
                content = FS::Load(temp_path);
            FS::Rm(temp_path);

            if (!ok)
            {
                failed(res, 200, error.empty() ? "Cannot read this file" : error);
                return;
            }

            json_object *result = json_object_new_object();
            JsonObjectRef result_ref(result);
            std::string text = content.empty() ? "" : std::string(content.data(), content.size());
            json_object_object_add(result, "result", json_object_new_string(text.c_str()));
            const char *result_str = json_object_to_json_string(result);
            res.status = 200;
            res.set_content(result_str, strlen(result_str), "application/json");
        });

        svr->Post("/__remote__/edit", [&](const Request &req, Response &res)
        {
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);
            const char *item = nullptr;
            const char *content = nullptr;
            std::string site = ResolveSite(nullptr);

            if (jobj != nullptr)
            {
                item = json_object_get_string(json_object_object_get(jobj, "item"));
                content = json_object_get_string(json_object_object_get(jobj, "content"));
                site = ResolveSite(json_object_get_string(json_object_object_get(jobj, "site")));
            }

            if (item == nullptr || content == nullptr)
            {
                bad_request(res, "Required item or content parameter missing");
                return;
            }

            std::string temp_path = std::string(temp_folder) + "/remote_edit";
            if (!FS::Save(temp_path, content, strlen(content)))
            {
                failed(res, 200, "Cannot stage the edited file");
                return;
            }

            bool ok = false;
            std::string error;
            {
                std::map<std::string, RemoteSettings>::iterator setting = site_settings.find(site);
                if (setting == site_settings.end())
                {
                    error = "Unknown site";
                }
                else
                {
                    std::shared_ptr<RemoteClient> held = RemotePool::AcquireShared(setting->second);
                    if (held == nullptr)
                        error = "Could not connect";
                    else
                        ok = held->Put(temp_path, item) != 0;
                }
            }
            FS::Rm(temp_path);

            if (!ok)
            {
                failed(res, 200, error.empty() ? "Cannot save this file" : error);
                return;
            }
            success(res);
        });

        svr->Get("/__local__/transfer", [&](const Request &req, Response &res)
        {
            std::string id = req.get_param_value("id");
            std::lock_guard<std::mutex> lock(web_transfer_mutex);
            std::map<std::string, WebTransfer>::iterator it = web_transfers.find(id);

            if (it == web_transfers.end())
            {
                failed(res, 200, "Unknown transfer");
                return;
            }

            json_object *result = json_object_new_object();
            JsonObjectRef result_ref(result);
            json_object_object_add(result, "id", json_object_new_string(id.c_str()));
            json_object_object_add(result, "name", json_object_new_string(it->second.name.c_str()));
            uint64_t loaded = it->second.loaded;
            uint64_t total = it->second.total;
            if (it->second.live && it->second.client != nullptr)
            {
                loaded = it->second.base + it->second.client->TransferredBytes();
                if (total == 0)
                    total = it->second.client->TotalBytes();
            }

            json_object_object_add(result, "loaded", json_object_new_int64((int64_t)loaded));
            json_object_object_add(result, "total", json_object_new_int64((int64_t)total));
            json_object_object_add(result, "state", json_object_new_int(it->second.state));
            json_object_object_add(result, "error", json_object_new_string(it->second.error.c_str()));

            const char *result_str = json_object_to_json_string(result);
            res.status = 200;
            res.set_content(result_str, strlen(result_str), "application/json");
        });

        svr->Get("/__local__/auth", [&](const Request &req, Response &res)
        {
            json_object *result = json_object_new_object();
            JsonObjectRef result_ref(result);
            json_object_object_add(result, "required", json_object_new_boolean(WebAuthRequired()));
            json_object_object_add(result, "authenticated", json_object_new_boolean(WebSessionValid(req)));
            const char *result_str = json_object_to_json_string(result);
            res.status = 200;
            res.set_content(result_str, strlen(result_str), "application/json");
        });

        svr->Post("/__local__/login", [&](const Request &req, Response &res)
        {
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);

            const char *password = nullptr;
            if (jobj != nullptr)
                password = json_object_get_string(json_object_object_get(jobj, "password"));

            if (password == nullptr || strcmp(password, web_password) != 0)
            {
                failed(res, 200, "Invalid password");
                return;
            }

            char token[64];
            snprintf(token, sizeof(token), "%016llx%08x", (unsigned long long) Util::GetTick(), (unsigned) rand());
            {
                std::lock_guard<std::mutex> lock(web_auth_mutex);
                web_sessions[token] = Util::GetTick();
            }

            // Session cookie: no Max-Age, so the browser drops it on close.
            res.set_header("Set-Cookie", std::string("ezremote_session=") + token + "; Path=/; HttpOnly; SameSite=Strict");
            success(res);
        });

        svr->Post("/__local__/logout", [&](const Request &req, Response &res)
        {
            std::string token = WebSessionToken(req);
            if (!token.empty())
            {
                std::lock_guard<std::mutex> lock(web_auth_mutex);
                web_sessions.erase(token);
            }
            res.set_header("Set-Cookie", "ezremote_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
            success(res);
        });

        svr->Get("/__local__/settings", [&](const Request &req, Response &res)
                 {
            json_object *result = json_object_new_object();
            JsonObjectRef result_ref(result);

            json_object *languages = json_object_new_array();
            for (size_t i = 0; i < langs.size(); i++)
                json_object_array_add(languages, json_object_new_string(langs[i].c_str()));

            json_object_object_add(result, "language", json_object_new_string(language));
            json_object_object_add(result, "languages", languages);
            json_object_object_add(result, "autoDeleteTmpPkg", json_object_new_boolean(auto_delete_tmp_pkg));
            json_object_object_add(result, "showHiddenFiles", json_object_new_boolean(show_hidden_files));
            json_object_object_add(result, "tempFolder", json_object_new_string(temp_folder));
            json_object_object_add(result, "enableBackgroundDownload", json_object_new_boolean(enable_background_download));
            json_object_object_add(result, "showActivityDialog", json_object_new_boolean(show_activity_dialog));
            json_object_object_add(result, "hasWebPassword", json_object_new_boolean(strlen(web_password) > 0));
            json_object_object_add(result, "webSessionTimeout", json_object_new_int(web_session_timeout));
            json_object_object_add(result, "minimumBackgroundFileSize", json_object_new_int64((int64_t)minimum_backgrond_file_size));
            json_object_object_add(result, "webServerEnabled", json_object_new_boolean(web_server_enabled));
            json_object_object_add(result, "httpServerPort", json_object_new_int(http_server_port));
            json_object_object_add(result, "compressedFilePath", json_object_new_string(compressed_file_path));
            json_object_object_add(result, "hasAllDebridKey", json_object_new_boolean(strlen(alldebrid_api_key) > 0));
            json_object_object_add(result, "hasRealDebridKey", json_object_new_boolean(strlen(realdebrid_api_key) > 0));

            const char *result_str = json_object_to_json_string(result);
            res.status = 200;
            res.set_content(result_str, strlen(result_str), "application/json"); });

        svr->Post("/__local__/settings", [&](const Request &req, Response &res)
        {
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);

            if (jobj == nullptr)
            {
                bad_request(res, "Invalid payload");
                return;
            }

            const char *text = nullptr;

            text = json_object_get_string(json_object_object_get(jobj, "language"));
            if (text != nullptr)
                snprintf(language, sizeof(language), "%s", text);

            text = json_object_get_string(json_object_object_get(jobj, "tempFolder"));
            if (text != nullptr && text[0] == '/')
                snprintf(temp_folder, sizeof(temp_folder), "%s", text);

            text = json_object_get_string(json_object_object_get(jobj, "compressedFilePath"));
            if (text != nullptr && text[0] == '/')
                snprintf(compressed_file_path, 512, "%s", text);

            // Absent keys keep the stored secret; an empty string clears it.
            json_object *key_obj = json_object_object_get(jobj, "alldebridApiKey");
            if (key_obj != nullptr)
                snprintf(alldebrid_api_key, sizeof(alldebrid_api_key), "%s", json_object_get_string(key_obj) == nullptr ? "" : json_object_get_string(key_obj));

            key_obj = json_object_object_get(jobj, "realdebridApiKey");
            if (key_obj != nullptr)
                snprintf(realdebrid_api_key, sizeof(realdebrid_api_key), "%s", json_object_get_string(key_obj) == nullptr ? "" : json_object_get_string(key_obj));

            key_obj = json_object_object_get(jobj, "webPassword");
            if (key_obj != nullptr)
            {
                const char *plain = json_object_get_string(key_obj);
                // Plaintext lives in memory; SaveGlobalConfig encrypts it.
                snprintf(web_password, sizeof(web_password), "%s", plain == nullptr ? "" : plain);
                // Changing the password invalidates every open session.
                WebClearSessions();
            }

            json_object *timeout_obj = json_object_object_get(jobj, "webSessionTimeout");
            if (timeout_obj != nullptr)
            {
                int minutes = json_object_get_int(timeout_obj);
                web_session_timeout = minutes < 1 ? 60 : (minutes > 1440 ? 1440 : minutes);
            }

            json_object *bool_obj = json_object_object_get(jobj, "autoDeleteTmpPkg");
            if (bool_obj != nullptr)
                auto_delete_tmp_pkg = json_object_get_boolean(bool_obj) != 0;

            bool_obj = json_object_object_get(jobj, "showHiddenFiles");
            if (bool_obj != nullptr)
                show_hidden_files = json_object_get_boolean(bool_obj) != 0;

            bool_obj = json_object_object_get(jobj, "enableBackgroundDownload");
            if (bool_obj != nullptr)
                enable_background_download = json_object_get_boolean(bool_obj) != 0;

            bool_obj = json_object_object_get(jobj, "showActivityDialog");
            if (bool_obj != nullptr)
                show_activity_dialog = json_object_get_boolean(bool_obj) != 0;

            bool_obj = json_object_object_get(jobj, "webServerEnabled");
            if (bool_obj != nullptr)
                web_server_enabled = json_object_get_boolean(bool_obj) != 0;

            json_object *num_obj = json_object_object_get(jobj, "minimumBackgroundFileSize");
            if (num_obj != nullptr)
                minimum_backgrond_file_size = (uint64_t)json_object_get_int64(num_obj);

            num_obj = json_object_object_get(jobj, "httpServerPort");
            if (num_obj != nullptr)
            {
                int port = json_object_get_int(num_obj);
                if (port > 0 && port < 65536)
                    http_server_port = port;
            }

            CONFIG::SaveGlobalConfig();
            success(res); });

        // Saved package URLs, shared with the native favourites dialog.
        svr->Get("/__local__/favorites", [&](const Request &req, Response &res)
                 {
            json_object *list = json_object_new_array();

            for (int i = 0; i < MAX_FAVORITE_URLS; i++)
            {
                if (strlen(favorite_urls[i]) == 0)
                    continue;

                json_object *item = json_object_new_object();
                json_object_object_add(item, "index", json_object_new_int(i));
                json_object_object_add(item, "url", json_object_new_string(favorite_urls[i]));
                json_object_array_add(list, item);
            }

            json_object *result = json_object_new_object();
            JsonObjectRef result_ref(result);
            json_object_object_add(result, "favorites", list);
            json_object_object_add(result, "capacity", json_object_new_int(MAX_FAVORITE_URLS));

            const char *result_str = json_object_to_json_string(result);
            res.status = 200;
            res.set_content(result_str, strlen(result_str), "application/json"); });

        svr->Post("/__local__/favorites/save", [&](const Request &req, Response &res)
        {
            const char *url = nullptr;
            int index = -1;
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);

            if (jobj == nullptr)
            {
                bad_request(res, "Invalid payload");
                return;
            }

            url = json_object_get_string(json_object_object_get(jobj, "url"));
            json_object *index_obj = json_object_object_get(jobj, "index");
            if (index_obj != nullptr)
                index = json_object_get_int(index_obj);

            if (url == nullptr || url[0] == '\0')
            {
                bad_request(res, "Required url parameter missing");
                return;
            }

            if (index < 0 || index >= MAX_FAVORITE_URLS)
            {
                index = -1;
                for (int i = 0; i < MAX_FAVORITE_URLS; i++)
                {
                    if (strlen(favorite_urls[i]) == 0)
                    {
                        index = i;
                        break;
                    }
                }
            }

            if (index < 0)
            {
                failed(res, 200, "All favourite slots are in use");
                return;
            }

            snprintf(favorite_urls[index], sizeof(favorite_urls[index]), "%s", url);
            CONFIG::SaveFavoriteUrl(index, favorite_urls[index]);

            json_object *result = json_object_new_object();
            JsonObjectRef result_ref(result);
            json_object_object_add(result, "index", json_object_new_int(index));
            const char *result_str = json_object_to_json_string(result);
            res.status = 200;
            res.set_content(result_str, strlen(result_str), "application/json"); });

        svr->Post("/__local__/favorites/remove", [&](const Request &req, Response &res)
        {
            int index = -1;
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);

            if (jobj != nullptr)
            {
                json_object *index_obj = json_object_object_get(jobj, "index");
                if (index_obj != nullptr)
                    index = json_object_get_int(index_obj);
            }

            if (index < 0 || index >= MAX_FAVORITE_URLS)
            {
                bad_request(res, "Unknown favourite");
                return;
            }

            favorite_urls[index][0] = '\0';
            CONFIG::SaveFavoriteUrl(index, favorite_urls[index]);
            success(res); });

        // Remote browsing, backed by a per-site connection held by the server.
        svr->Post("/__remote__/list", [&](const Request &req, Response &res)
        {
            const char *site = nullptr;
            const char *path = nullptr;
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);

            if (jobj != nullptr)
            {
                site = json_object_get_string(json_object_object_get(jobj, "site"));
                path = json_object_get_string(json_object_object_get(jobj, "path"));
            }

            std::string site_name = (site != nullptr && strlen(site) > 0) ? std::string(site) : std::string(last_site);
            std::string target;
            json_object *entries = json_object_new_array();

            {
                std::lock_guard<std::mutex> lock(remote_session_mutex);

                std::map<std::string, RemoteSettings>::iterator setting = site_settings.find(site_name);
                if (setting == site_settings.end())
                {
                    failed(res, 200, "Unknown site");
                    return;
                }

                target = (path != nullptr && path[0] != '\0') ? std::string(path) : std::string(setting->second.default_directory);

                std::string error;
                RemoteClient *client = GetRemoteSession(site_name, error);
                if (client == nullptr)
                {
                    failed(res, 200, error);
                    return;
                }

                std::vector<DirEntry> files = client->ListDir(target);
                for (size_t i = 0; i < files.size(); i++)
                {
                    DirEntry &entry = files[i];
                    char display_date[32] = "";

                    if (strcmp(entry.name, "..") == 0)
                        continue;

                    if (entry.modified.year != 0)
                    {
                        sprintf(display_date, "%04d-%02d-%02d %02d:%02d:%02d", entry.modified.year,
                                entry.modified.month, entry.modified.day, entry.modified.hours,
                                entry.modified.minutes, entry.modified.seconds);
                    }

                    json_object *file = json_object_new_object();
                    json_object_object_add(file, "name", json_object_new_string(entry.name));
                    json_object_object_add(file, "type", json_object_new_string(entry.isDir ? "dir" : "file"));
                    json_object_object_add(file, "size", json_object_new_string(entry.isDir ? "" : std::to_string(entry.file_size).c_str()));
                    json_object_object_add(file, "date", json_object_new_string(display_date));
                    json_object_object_add(file, "rights", json_object_new_string(entry.rights));
                    json_object_array_add(entries, file);
                }
            }

            json_object *result = json_object_new_object();
            JsonObjectRef result_ref(result);
            json_object_object_add(result, "site", json_object_new_string(site_name.c_str()));
            json_object_object_add(result, "path", json_object_new_string(target.c_str()));
            json_object_object_add(result, "entries", entries);

            const char *result_str = json_object_to_json_string(result);
            res.status = 200;
            res.set_content(result_str, strlen(result_str), "application/json");
        });

        svr->Post("/__remote__/disconnect", [&](const Request &req, Response &res)
        {
            const char *site = nullptr;
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);

            if (jobj != nullptr)
                site = json_object_get_string(json_object_object_get(jobj, "site"));

            std::lock_guard<std::mutex> lock(remote_session_mutex);
            std::string closing_site = (site != nullptr && strlen(site) > 0) ? std::string(site) : std::string(last_site);
            CloseRemoteSession(closing_site);

            // Pooled connections belong to the session being closed.
            std::map<std::string, RemoteSettings>::iterator setting = site_settings.find(closing_site);
            if (setting != site_settings.end())
                RemotePool::ClearFor(setting->second);

            success(res);
        });

        // Streams a remote file to the browser. Client::GetRange opens its own
        // data connection per call and seeks to the requested offset, so it is
        // safe to drive from httplib's content provider.
        svr->Get("/__remote__/download", [&](const Request &req, Response &res)
        {
            std::string site = ResolveSite(req.get_param_value("site").c_str());
            std::string path = req.get_param_value("path");
            uint64_t size = 0;

            if (path.empty())
            {
                bad_request(res, "Missing path");
                return;
            }

            {
                std::map<std::string, RemoteSettings>::iterator setting = site_settings.find(site);
                if (setting == site_settings.end())
                {
                    failed(res, 200, "Unknown site");
                    return;
                }

                // Hold the connection through the whole stream: the shared_ptr
                // captured by the provider returns it to the pool once httplib
                // destroys the response.
                std::shared_ptr<RemoteClient> held = RemotePool::AcquireShared(setting->second);
                if (held == nullptr)
                {
                    failed(res, 200, "Could not connect");
                    return;
                }
                if (!held->Size(path, &size))
                {
                    failed(res, 200, "Cannot read this file");
                    return;
                }

                size_t slash = path.find_last_of('/');
                std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
                res.set_header("Content-Disposition", "attachment; filename=\"" + name + "\"");
                res.set_content_provider(size, "application/octet-stream",
                    [held, path](size_t offset, size_t length, DataSink &sink) {
                        return held->GetRange(path, sink, length, offset) == 1;
                    });
            }
        });

        // Receives a file (optionally in chunks) and pushes it to the remote
        // server. Chunks are staged in temp_folder and Put() on the last one,
        // so a failed browser upload never leaves a partial remote file.
        svr->Post("/__remote__/upload", [&](const Request &req, Response &res, const ContentReader &content_reader)
        {
            MultipartFormDataItems items;
            std::string site = last_site;
            std::string remote_dir;
            std::string filename;
            std::string upload_id;
            size_t chunk_size = 0;
            size_t chunk_number = 0;
            size_t total_size = 0;
            std::string temp_path;
            FILE *out = nullptr;

            content_reader(
                [&](const MultipartFormData &item)
                {
                    items.push_back(item);
                    if (item.name == "file")
                    {
                        size_t slash = item.filename.find_last_of("/\\");
                        filename = (slash == std::string::npos) ? item.filename : item.filename.substr(slash + 1);

                        if (upload_id.empty())
                            upload_id = std::to_string((unsigned long)time(nullptr));

                        // The id is client supplied; keep it to a safe character set
                        // so it cannot escape temp_folder.
                        for (size_t i = 0; i < upload_id.size(); i++)
                        {
                            char c = upload_id[i];
                            if (!isalnum((unsigned char)c) && c != '-' && c != '_')
                                upload_id = std::to_string((unsigned long)time(nullptr));
                        }

                        temp_path = std::string(temp_folder) + "/remote_upload_" + upload_id;

                        if (out != nullptr)
                            FS::Close(out);

                        if (chunk_number == 0)
                            out = FS::Create(temp_path.c_str());
                        else
                            out = FS::Append(temp_path.c_str());
                    }
                    return true;
                },
                [&](const char *data, size_t data_length)
                {
                    items.back().content.append(data, data_length);
                    if (items.back().name == "site")
                    {
                        site = items.back().content;
                    }
                    else if (items.back().name == "path")
                    {
                        remote_dir = items.back().content;
                    }
                    else if (items.back().name == "_uploadId")
                    {
                        upload_id = items.back().content;
                    }
                    else if (items.back().name == "_chunkSize")
                    {
                        std::stringstream ss(items.back().content);
                        ss >> chunk_size;
                    }
                    else if (items.back().name == "_chunkNumber")
                    {
                        std::stringstream ss(items.back().content);
                        ss >> chunk_number;
                    }
                    else if (items.back().name == "_totalSize")
                    {
                        std::stringstream ss(items.back().content);
                        ss >> total_size;
                    }
                    else if (out != nullptr)
                    {
                        FS::Write(out, data, data_length);
                    }
                    return true;
                });

            if (out != nullptr)
            {
                FS::Close(out);
                out = nullptr;
            }

            if (temp_path.empty() || filename.empty() || remote_dir.empty())
            {
                if (!temp_path.empty())
                    FS::Rm(temp_path.c_str());
                failed(res, 200, "Missing upload data");
                return;
            }

            bool is_last_chunk = true;
            if (chunk_size > 0 && total_size > 0)
                is_last_chunk = (chunk_number + 1) >= ((total_size + chunk_size - 1) / chunk_size);

            if (!is_last_chunk)
            {
                success(res);
                return;
            }

            if (!FS::FolderExists(temp_folder))
                FS::MkDirs(temp_folder);

            std::string remote_path = remote_dir;
            if (remote_path.empty() || remote_path[remote_path.size() - 1] != '/')
                remote_path += "/";
            remote_path += filename;

            bool uploaded = false;
            std::string error;
            {
                std::map<std::string, RemoteSettings>::iterator setting = site_settings.find(site);
                if (setting == site_settings.end())
                {
                    error = "Unknown site";
                }
                else
                {
                    std::shared_ptr<RemoteClient> held = RemotePool::AcquireShared(setting->second);
            RemoteClient *client = held.get();
                    if (client == nullptr)
                    {
                        error = "Could not connect";
                    }
                    else
                    {
                        uploaded = client->Put(temp_path, remote_path) != 0;
                    }
                }
            }

            FS::Rm(temp_path.c_str());

            if (!uploaded)
            {
                failed(res, 200, error.empty() ? "Upload failed" : error);
                return;
            }
            success(res);
        });

        svr->Post("/__remote__/mkdir", [&](const Request &req, Response &res)
        {
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);
            const char *path = nullptr;
            std::string site = last_site;

            if (jobj != nullptr)
            {
                path = json_object_get_string(json_object_object_get(jobj, "path"));
                site = ResolveSite(json_object_get_string(json_object_object_get(jobj, "site")));
            }
            if (path == nullptr || path[0] == '\0')
            {
                bad_request(res, "Missing path");
                return;
            }

            std::map<std::string, RemoteSettings>::iterator setting = site_settings.find(site);
            if (setting == site_settings.end())
            {
                failed(res, 200, "Unknown site");
                return;
            }

            std::shared_ptr<RemoteClient> held = RemotePool::AcquireShared(setting->second);
            if (held == nullptr)
            {
                failed(res, 200, "Could not connect");
                return;
            }
            if (!held->Mkdir(path))
            {
                failed(res, 200, "Cannot create this folder");
                return;
            }
            success(res);
        });

        svr->Post("/__remote__/rename", [&](const Request &req, Response &res)
        {
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);
            const char *item = nullptr;
            const char *new_item = nullptr;
            std::string site = last_site;

            if (jobj != nullptr)
            {
                item = json_object_get_string(json_object_object_get(jobj, "item"));
                new_item = json_object_get_string(json_object_object_get(jobj, "newItemPath"));
                site = ResolveSite(json_object_get_string(json_object_object_get(jobj, "site")));
            }
            if (item == nullptr || new_item == nullptr)
            {
                bad_request(res, "Missing item or newItemPath");
                return;
            }

            std::map<std::string, RemoteSettings>::iterator setting = site_settings.find(site);
            if (setting == site_settings.end())
            {
                failed(res, 200, "Unknown site");
                return;
            }

            std::shared_ptr<RemoteClient> held = RemotePool::AcquireShared(setting->second);
            if (held == nullptr)
            {
                failed(res, 200, "Could not connect");
                return;
            }
            if (!held->Rename(item, new_item))
            {
                failed(res, 200, "Cannot rename this item");
                return;
            }
            success(res);
        });

        svr->Post("/__remote__/remove", [&](const Request &req, Response &res)
        {
            Activities::Guard activity(ACTIVITY_DELETE, "Deleting remote files", ACTIVITY_WEB, ACTIVITY_REMOTE);
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);
            json_object *files = nullptr;
            json_object *dirs = nullptr;
            std::string site = last_site;

            if (jobj != nullptr)
            {
                files = json_object_object_get(jobj, "files");
                dirs = json_object_object_get(jobj, "dirs");
                site = ResolveSite(json_object_get_string(json_object_object_get(jobj, "site")));
            }

            // Own connection: a delete of a large tree must not block other
            // requests, nor be blocked by them.
            std::map<std::string, RemoteSettings>::iterator setting = site_settings.find(site);
            if (setting == site_settings.end())
            {
                failed(res, 200, "Unknown site");
                return;
            }

            std::shared_ptr<RemoteClient> held = RemotePool::AcquireShared(setting->second);
            RemoteClient *client = held.get();
            if (client == nullptr)
            {
                failed(res, 200, "Could not connect");
                return;
            }

            std::string failed_items;
            size_t count = 0;

            if (files != nullptr && json_object_get_type(files) == json_type_array)
            {
                size_t len = json_object_array_length(files);
                for (size_t i = 0; i < len; i++)
                {
                    const char *item = json_object_get_string(json_object_array_get_idx(files, i));
                    if (item == nullptr)
                        continue;
                    Activities::Item(activity.id, item);
                    count++;
                    if (!client->Delete(item))
                        failed_items += std::string(item) + ",";
                }
            }

            if (dirs != nullptr && json_object_get_type(dirs) == json_type_array)
            {
                size_t len = json_object_array_length(dirs);
                for (size_t i = 0; i < len; i++)
                {
                    const char *item = json_object_get_string(json_object_array_get_idx(dirs, i));
                    if (item == nullptr)
                        continue;
                    count++;
                    if (!client->Rmdir(item, true))
                        failed_items += std::string(item) + ",";
                }
            }


            if (count == 0)
            {
                bad_request(res, "Nothing to remove");
                return;
            }
            if (!failed_items.empty())
            {
                failed(res, 200, "Failed to remove: " + failed_items);
                return;
            }
            success(res);
        });

        svr->Post("/__local__/chmod", [&](const Request &req, Response &res)
        {
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);

            const char *path = nullptr;
            const char *rights = nullptr;
            if (jobj != nullptr)
            {
                path = json_object_get_string(json_object_object_get(jobj, "path"));
                rights = json_object_get_string(json_object_object_get(jobj, "rights"));
            }

            if (path == nullptr || rights == nullptr)
            {
                bad_request(res, "Required path and rights parameters missing");
                return;
            }

            int mode = ParseRights(rights);
            if (mode < 0)
            {
                failed(res, 200, "Expected rights like rwxr-xr-x");
                return;
            }

            if (chmod(path, mode) != 0)
            {
                failed(res, 200, "Could not change permissions");
                return;
            }

            success(res);
        });

        svr->Post("/__local__/newfile", [&](const Request &req, Response &res)
        {
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);

            const char *path = nullptr;
            if (jobj != nullptr)
                path = json_object_get_string(json_object_object_get(jobj, "path"));

            if (path == nullptr)
            {
                bad_request(res, "Required path parameter missing");
                return;
            }

            FILE *file = FS::Create(path);
            if (file == nullptr)
            {
                failed(res, 200, "Cannot create this file");
                return;
            }
            FS::Close(file);

            success(res);
        });

        svr->Post("/__local__/compress", [&](const Request &req, Response &res)
        {

            json_object *items;
            const char* destination;
            const char* compressedFilename;
            
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);
            if (jobj != nullptr)
            {
                items = json_object_object_get(jobj, "items");
                destination = json_object_get_string(json_object_object_get(jobj, "destination"));
                compressedFilename = json_object_get_string(json_object_object_get(jobj, "compressedFilename"));

                if (items == nullptr || json_object_get_type(items) != json_type_array || destination == nullptr || compressedFilename == nullptr)
                {
                    bad_request(res, "Required items,destination,compressedFilename parameter missing");
                    return;
                }
            }
            else
            {
                bad_request(res, "Invalid payload");
                return;
            }

            // The client picks the folder; fall back to the configured one.
            std::string destination_dir = (strlen(destination) > 0) ? std::string(destination) : std::string(compressed_file_path);
            if (!FS::FolderExists(destination_dir))
                FS::MkDirs(destination_dir);
            std::string zip_file = destination_dir +
                (FS::hasEndSlash(destination_dir.c_str()) ? "" : "/") + compressedFilename;

            Activities::Guard activity(ACTIVITY_COMPRESS, std::string("Creating ") + compressedFilename, ACTIVITY_WEB);
            Activities::Item(activity.id, zip_file);

            zipFile zf = zipOpen64(zip_file.c_str(), APPEND_STATUS_CREATE);
            if (zf != NULL)
            {
                size_t len = json_object_array_length(items);
                for (size_t i=0; i < len; i++)
                {
                    const char *item = json_object_get_string(json_object_array_get_idx(items, i));
                    if (item == nullptr)
                        continue;
                    std::string src = std::string(item);
                    size_t slash_pos = src.find_last_of("/");
                    int ret = ZipUtil::ZipAddPath(zf, src, (slash_pos != std::string::npos ? slash_pos + 1 : 1), Z_DEFAULT_COMPRESSION);
                    if (ret != 1)
                    {
                        zipClose(zf, NULL);
                        FS::Rm(zip_file);
                        failed(res, 200, "Failed to create zip");
                    }
                }
                zipClose(zf, NULL);
                success(res);
            }
            else
            {
                failed(res, 200, "Failed to create zip");
            } });

        svr->Post("/__local__/extract", [&](const Request &req, Response &res)
        {

            const char* item;
            const char* destination;
            const char* folderName;
            
            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);
            if (jobj != nullptr)
            {
                item = json_object_get_string(json_object_object_get(jobj, "item"));
                destination = json_object_get_string(json_object_object_get(jobj, "destination"));
                folderName = json_object_get_string(json_object_object_get(jobj, "folderName"));

                if (item == nullptr || destination == nullptr || folderName == nullptr)
                {
                    bad_request(res, "Required item,destination,folderName parameter missing");
                    return;
                }
            }
            else
            {
                bad_request(res, "Invalid payload");
                return;
            }

            std::string extract_zip_folder = std::string(destination) + "/" + folderName;
            DirEntry entry;
            sprintf(entry.name, "%s", "");
            sprintf(entry.path, "%s", item);
            entry.isDir = false;

            int ret;
            {
                // Runs inline in this request, so register it for the trackers.
                Activities::Guard activity(ACTIVITY_EXTRACT, std::string("Extracting ") + item, ACTIVITY_WEB);
                Activities::Item(activity.id, extract_zip_folder);
                FS::MkDirs(extract_zip_folder);
                ret = ZipUtil::Extract(entry, extract_zip_folder);
            }
            if (ret == 0)
                failed(res, 200, "Failed to extract file");
            else if (ret == -1)
                failed(res, 200, "Unsupported compressed file format");
            else
                success(res); });

        svr->Get("/__local__/uploadResumeSize", [&](const Request &req, Response &res)
        {
            std::string destination = req.get_param_value("destination");
            std::string filename = req.get_param_value("filename");
            std::string file_path = destination + "/" + filename;
            int64_t size = 0;
            if (FS::FileExists(file_path))
                size = FS::GetSize(file_path);
            std::string result_str = "{\"size\":" + std::to_string(size) + "}";
            res.status = 200;
            res.set_content(result_str.c_str(), result_str.length(), "application/json"); });

        svr->Post("/__local__/upload", [&](const Request &req, Response &res, const ContentReader &content_reader)
        {
            MultipartFormDataItems items;
            std::string destination;
            size_t chunk_size = 0;
            size_t chunk_number = -1;
            size_t total_size = 0;
            size_t currentChunkSize = 0;
            FILE *out = nullptr;
            std::string new_file;
            content_reader(
                [&](const MultipartFormData &item)
                {
                    items.push_back(item);
                    if (item.name == "file")
                    {
                        new_file = destination + "/" + item.filename;
                        if (out != nullptr)
                        {
                            FS::Close(out);
                        }

                        if (chunk_number == 0)
                            out = FS::Create(new_file);
                        else if (chunk_number > 0)
                            out = FS::Append(new_file);
                    }
                    return true;
                },
                [&](const char *data, size_t data_length)
                {
                    items.back().content.append(data, data_length);
                    if (items.back().name == "destination")
                    {
                        destination = items.back().content;
                    }
                    else if (items.back().name == "_chunkSize")
                    {
                        std::stringstream ss(items.back().content);
                        ss >> chunk_size;
                    }
                    else if (items.back().name == "_chunkNumber")
                    {
                        std::stringstream ss(items.back().content);
                        ss >> chunk_number;
                    }
                    else if (items.back().name == "_totalSize")
                    {
                        std::stringstream ss(items.back().content);
                        ss >> total_size;
                    }
                    else if (items.back().name == "_currentChunkSize")
                    {
                        std::stringstream ss(items.back().content);
                        ss >> currentChunkSize;
                    }
                    else
                    {
                        if (out != nullptr)
                            FS::Write(out, data, data_length);
                    }
                    return true;
                });
            if (out != nullptr)
            {
                FS::Close(out);
            }
            success(res); });

        // Download multiple files as ZIP
        svr->Get("/__local__/downloadMultiple", [&](const Request &req, Response &res)
        {
            if (req.get_param_value_count("items") == 0 || req.get_param_value_count("toFilename") == 0)
            {
                failed(res, 200, "Required items and toFilename parameter missing");
                return;
            }

            if (!FS::FolderExists(compressed_file_path))
                FS::MkDirs(compressed_file_path);

            std::string toFilename = req.get_param_value("toFilename");
            std::string zip_file = std::string(compressed_file_path) + "/" + toFilename;
            zipFile zf = zipOpen64(zip_file.c_str(), APPEND_STATUS_CREATE);
            if (zf != NULL)
            {
                int items_count = req.get_param_value_count("items");
                for (size_t i=0; i < items_count; i++)
                {
                    std::string src = req.get_param_value("items", i);
                    size_t slash_pos = src.find_last_of("/");
                    int ret = ZipUtil::ZipAddPath(zf, src, (slash_pos != std::string::npos ? slash_pos + 1 : 1), Z_DEFAULT_COMPRESSION);
                    if (ret != 1)
                    {
                        zipClose(zf, NULL);
                        FS::Rm(zip_file);
                        failed(res, 200, "Failed to create zip file");
                        return;
                    }
                }
                zipClose(zf, NULL);

                // start stream the zip
                FILE *in = FS::OpenRead(zip_file);
                uint64_t size = FS::GetSize(zip_file);
                res.set_header("Content-Disposition", "attachment; filename=\"" + std::string(toFilename) + "\"");
                res.set_content_provider(
                    size, "application/octet-stream",
                    [in](size_t offset, size_t length, DataSink &sink) {
                        size_t size_to_read = std::min(static_cast<size_t>(length), (size_t)1048576);
                        char buff[size_to_read];
                        size_t read_len;
                        FS::Seek(in, offset);
                        read_len = FS::Read(in, buff, size_to_read);
                        sink.write(buff, read_len);
                        return read_len == size_to_read;
                    },
                    [in, zip_file](bool success) {
                        FS::Close(in);
                        FS::Rm(zip_file);
                    });
            }
            else
            {
                failed(res, 200, "Failed to create zip");
            } });

        // Download single file
        svr->Get("/__local__/downloadFile", [&](const Request &req, Response &res)
        {
            std::string path = req.get_param_value("path", 0);
            if (path.empty())
            {
                bad_request(res, "Failed to download");
                return;
            }

            int64_t size = FS::GetSize(path);
            FILE *in = FS::OpenRead(path);

            size_t slash_pos = path.find_last_of("/");
            std::string name = path;
            if (slash_pos != std::string::npos)
                name = path.substr(slash_pos+1);

            res.set_header("Content-Disposition", "attachment; filename=\"" + name + "\"");
            res.set_content_provider(
                size, "application/octet-stream",
                [in](size_t offset, size_t length, DataSink &sink) {
                    // One reusable heap buffer instead of a 1 MB stack VLA per
                    // call, and read sequentially rather than seeking each time.
                    static thread_local std::vector<char> buffer(4 * 1024 * 1024);
                    size_t size_to_read = std::min(static_cast<size_t>(length), buffer.size());

                    size_t read_len = FS::Read(in, buffer.data(), size_to_read);
                    if (read_len == 0)
                        return false;

                    return sink.write(buffer.data(), read_len);
                },
                [in](bool success) {
                    FS::Close(in);
                });
        });

        svr->Get("/google_auth", [](const Request &req, Response &res)
        {
            /* std::string auth_code = req.get_param_value("code");
            Client client(GOOGLE_OAUTH_HOST);
            client.set_follow_location(true);
            client.enable_server_certificate_verification(false);
            
            std::string url = std::string("/token");
            std::string post_data = std::string("code=") + auth_code +
                                                "&client_id=" + gg_app.client_id +
                                                "&client_secret=" + gg_app.client_secret +
                                                "&redirect_uri=http%3A//localhost%3A" + std::to_string(http_server_port) + "/google_auth"
                                                "&grant_type=authorization_code";
                            
            if (auto result = client.Post(url, post_data.c_str(), post_data.length(),  "application/x-www-form-urlencoded"))
            {
                if (HTTP_SUCCESS(result->status))
                {
                    json_object *jobj = json_tokener_parse(result.value().body.c_str());
                    enum json_type type;
                    json_object_object_foreach(jobj, key, val)
                        {
                            if (strcmp(key, "access_token")==0)
                                snprintf(remote_settings->gg_account.access_token, 255, "%s", json_object_get_string(val));
                            else if (strcmp(key, "refresh_token")==0)
                                snprintf(remote_settings->gg_account.refresh_token, 255, "%s", json_object_get_string(val));
                            else if (strcmp(key, "expires_in")==0)
                            {
                                OrbisTick tick;
                                sceRtcGetCurrentTick(&tick);
                                remote_settings->gg_account.token_expiry = tick.mytick + (json_object_get_uint64(val)*1000000);
                            }
                        }
                    CONFIG::SaveConfig();
                    login_state = 1;
                    res.set_content(lang_strings[STR_GET_TOKEN_SUCCESS_MSG], "text/plain");
                    return;
                }
                else
                {
                    login_state = -1;
                    std::string str = std::string(lang_strings[STR_FAIL_GET_TOKEN_MSG]) + " Google";
                    res.set_content(str.c_str(), "text/plain");
                }
            }
            login_state = -1;
            std::string str = std::string(lang_strings[STR_FAIL_GET_TOKEN_MSG]) + " Google";
            res.set_content(str.c_str(), "text/plain"); */
        });

        svr->Get("/rmt_inst/Site (\\d+)(/)(.*)", [&](const Request &req, Response &res)
        {
            RemoteClient *tmp_client = nullptr;
            auto site_idx = std::stoi(req.matches[1])-1;
            std::string path;
            if (site_idx != 98)
            {
                path = std::string("/") + std::string(req.matches[3]);
                tmp_client = INSTALLER::GetRemoteClient(site_idx);
            }
            else
            {
                std::string hash = std::string(req.matches[3]);
                std::string url = FileHost::GetCachedDownloadUrl(hash);
                size_t scheme_pos = url.find("://");
                size_t root_pos = url.find("/", scheme_pos + 3);
                std::string host = url.substr(0, root_pos);
                path = url.substr(root_pos);

                tmp_client = new BaseClient();
                tmp_client->Connect(host, "", "", false);
            }

            res.status = 206;
            uint64_t file_size;
            tmp_client->Size(path, &file_size);
                
            std::pair<ssize_t, ssize_t> range = req.ranges[0];
            res.set_content_provider(
                file_size, "application/octet-stream",
                [tmp_client, path](size_t offset, size_t length, DataSink &sink) {
                    int ret;
                    ret = tmp_client->GetRange(path, sink, length, offset);
                    return (ret==1);
                },
                [tmp_client](bool success) {
                    DeleteRemoteClient(tmp_client);
                });
        });

        svr->Get("/archive_inst/(.*)", [&](const Request &req, Response &res)
        {
            std::string hash = req.matches[1];
            ArchivePkgInstallData *pkg_data = INSTALLER::GetArchivePkgInstallData(hash);

            if (pkg_data == nullptr)
            {
                failed(res, 200, "Cannot resume archive_inst");
                return;
            }

            res.status = 206;
            res.set_content_provider(
                pkg_data->archive_entry->filesize, "application/octet-stream",
                [pkg_data](size_t offset, size_t length, DataSink &sink) {
                    char *buf = (char*) malloc(length);
                    ssize_t bytes_read = pkg_data->split_file->Read(buf, length, offset);
                    if (bytes_read < 0)
                    {
                        free(buf);
                        return false;
                    }
                    sink.write(buf, bytes_read);
                    free(buf);
                    return true;
                },
                [](bool success) {
                    return true;
                });
        });

        svr->Get("/split_inst/(.*)", [&](const Request &req, Response &res)
        {
            std::string hash = req.matches[1];

            SplitPkgInstallData *pkg_data = INSTALLER::GetSplitPkgInstallData(hash);

            if (pkg_data == nullptr)
            {
                failed(res, 500, "Cannot resume split_inst");
                return;
            }

            res.status = 206;
            res.set_content_provider(
                pkg_data->size, "application/octet-stream",
                [pkg_data](size_t offset, size_t length, DataSink &sink) {
                    char *buf = (char*) malloc(length);
                    ssize_t bytes_read = pkg_data->split_file->Read(buf, length, offset);
                    if (bytes_read < 0)
                    {
                        free(buf);
                        return false;
                    }
                    sink.write(buf, bytes_read);
                    free(buf);
                    return true;
                },
                [](bool success) {
                    return true;
                });
        });

        svr->Post("/__local__/install_url", [&](const Request &req, Response &res)
        {
            std::string url;
            const char *url_param;
            bool use_alldebrid = false;
            bool use_realdebrid = false;
            bool use_disk_cache = false;
            bool enable_rpi = false;

            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);
            if (jobj != nullptr)
            {
                url_param = json_object_get_string(json_object_object_get(jobj, "url"));
                use_alldebrid  = json_object_get_boolean(json_object_object_get(jobj, "use_alldebrid"));
                use_realdebrid = json_object_get_boolean(json_object_object_get(jobj, "use_realdebrid"));
                use_disk_cache = json_object_get_boolean(json_object_object_get(jobj, "use_disk_cache"));
                enable_rpi = json_object_get_boolean(json_object_object_get(jobj, "enable_rpi"));

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

            if (!INSTALLER::IsDirectPackageInstallerEnabled())
            {
                failed(res, 200, lang_strings[STR_DPI_NOT_STARTED_ERROR_MSG]);
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
            snprintf(activity_message, 1023, "%s %s", lang_strings[STR_INSTALLING], filehost->GetUrl().c_str());
            prev_tick = Util::GetTick();


            // Covers the URL fetch and setup; the split/extract threads carry
            // the rest of the work under their own activities.
            Activities::Guard activity(ACTIVITY_INSTALL, std::string("Installing from URL: ") + filehost->GetUrl(), ACTIVITY_WEB);
            Activities::Item(activity.id, filehost->GetUrl());

            std::string download_url = filehost->GetDownloadUrl();
            if (download_url.empty())
            {
                failed(res, 200, lang_strings[STR_CANT_EXTRACT_URL_MSG]);
                return;
            }
            delete(filehost);

			size_t scheme_pos = download_url.find("://");
			size_t root_pos = download_url.find("/", scheme_pos + 3);
			std::string host = download_url.substr(0, root_pos);
			std::string path = download_url.substr(root_pos);
            pkg_header header;
            uint64_t file_size;

            BaseClient *baseclient = new BaseClient();
            baseclient->Connect(host, "", "");
            
            if (!baseclient->FileExists(path))
            {
                failed(res, 200, baseclient->LastResponse());
                return;
            }
            baseclient->Head(path, &header, sizeof(pkg_header));
            baseclient->Size(path, &file_size);

            FileHost::AddCacheDownloadUrl(hash, download_url);
            std::string title = INSTALLER::GetRemotePkgTitle(baseclient, path, &header);

            if (BE32(header.pkg_magic) == PKG_CNT_MAGIC || BE32(header.pkg_magic) == PKG_FIH_MAGIC)
            {
                if (enable_rpi && !use_disk_cache)
                {
                    json_object *history_item_obj = json_object_new_object();
                    JsonObjectRef history_item_ref(history_item_obj);
                    json_object_object_add(history_item_obj, "hash", json_object_new_string(hash.c_str()));
                    json_object_object_add(history_item_obj, "url", json_object_new_string(host.c_str()));
                    json_object_object_add(history_item_obj, "path", json_object_new_string(path.c_str()));
                    json_object_object_add(history_item_obj, "username", json_object_new_string(""));
                    json_object_object_add(history_item_obj, "password", json_object_new_string(""));
                    json_object_object_add(history_item_obj, "type", json_object_new_int(CLIENT_TYPE_FILEHOST));
                    json_object_object_add(history_item_obj, "file_size", json_object_new_uint64(file_size));

                    const char *params_str = json_object_to_json_string(history_item_obj);

                    CHTTPClient::HttpResponse resp;
                    CHTTPClient::HeadersMap headers;
                    CHTTPClient tmp_client([](const std::string& log){});
                    tmp_client.InitSession(true, CHTTPClient::SettingsFlag::NO_FLAGS);
                    tmp_client.SetCertificateFile(CACERT_FILE);
                    headers["Content-Type"] = "application/json";

                    std::string store_bg_install_data_url = std::string("http://localhost:") + std::to_string(http_int_server_port) + "/store_bg_install_data";
                    if (tmp_client.Post(store_bg_install_data_url, headers, params_str, resp))
                    {
                        if (HTTP_SUCCESS(resp.iCode))
                        {
                        }
                    }
                    else
                    {
                        failed(res, 200, "Could not save host data for background install");
                    }
                    sleep(2);

                    std::string remote_install_url = std::string("http://localhost:") + std::to_string(http_int_server_port) + "/bg_install/" + hash;
                    int rc = INSTALLER::InstallRemotePkg(remote_install_url, &header, title);
                }
                else if (enable_rpi && use_disk_cache && BE32(header.pkg_magic) == PKG_CNT_MAGIC)
                {
                    SplitPkgInstallData *install_data = (SplitPkgInstallData*) malloc(sizeof(SplitPkgInstallData));
                    memset(install_data, 0, sizeof(SplitPkgInstallData));

                    std::string install_pkg_path = std::string(temp_folder) + "/" + std::to_string(Util::GetTick()) + ".pkg";
                    FileStream *sp = CreateFileStream(install_pkg_path, INSTALL_ARCHIVE_PKG_SPLIT_SIZE/2);

                    install_data->split_file = sp;
                    install_data->remote_client = baseclient;
                    install_data->path = path;
                    baseclient->Size(path, &install_data->size);
                    install_data->stop_write_thread = false;
                    install_data->delete_client = true;

                    int ret = pthread_create(&install_data->thread, NULL, Actions::DownloadSplitPkg, install_data);

                    ret = INSTALLER::InstallSplitPkg(download_url, install_data, true);

                    if (ret == 0)
                    {
                        failed(res, 200, lang_strings[STR_FAIL_INSTALL_FROM_URL_MSG]);
                        return;
                    }
                }
            }
            else
            {
                ArchiveEntry *entry = ZipUtil::GetPackageEntry(path, baseclient);
                if (entry != nullptr)
                {
                    ArchivePkgInstallData *install_data = (ArchivePkgInstallData*) malloc(sizeof(ArchivePkgInstallData));
                    memset(install_data, 0, sizeof(ArchivePkgInstallData));

                    std::string install_pkg_path = std::string(temp_folder) + "/" + entry->filename;
                    FileStream *sp = CreateFileStream(install_pkg_path, INSTALL_ARCHIVE_PKG_SPLIT_SIZE);
                    
                    install_data->archive_entry = entry;
                    install_data->split_file = sp;
                    install_data->stop_write_thread = false;

                    int ret = pthread_create(&install_data->thread, NULL, Actions::ExtractArchivePkg, install_data);

                    ret = INSTALLER::InstallArchivePkg(entry->filename, install_data, true);

                    if (ret == 0)
                    {
                        failed(res, 200, lang_strings[STR_FAIL_INSTALL_FROM_URL_MSG]);
                        free(install_data);
                        return;
                    }
                }
                else
                {
                    failed(res, 200, lang_strings[STR_FAIL_INSTALL_FROM_URL_MSG]);
                    return;
                }
            }
            success(res);
    
        });

        svr->Post("/__local__/download_url", [&](const Request &req, Response &res)
        {
            std::string url;
            std::string dest;
            const char *url_param;
            const char *dest_param;
            bool use_alldebrid = false;
            bool use_realdebrid = false;

            json_object *jobj = json_tokener_parse(req.body.c_str());
            JsonObjectRef jobj_ref(jobj);
            if (jobj != nullptr)
            {
                url_param = json_object_get_string(json_object_object_get(jobj, "url"));
                dest_param = json_object_get_string(json_object_object_get(jobj, "dest"));
                use_alldebrid  = json_object_get_boolean(json_object_object_get(jobj, "use_alldebrid"));
                use_realdebrid = json_object_get_boolean(json_object_object_get(jobj, "use_realdebrid"));

                if (url_param == nullptr || dest_param == nullptr)
                {
                    bad_request(res, "Required url, dest parameter missing");
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

            std::string download_url = filehost->GetDownloadUrl();
            if (download_url.empty())
            {
                failed(res, 200, lang_strings[STR_CANT_EXTRACT_URL_MSG]);
                return;
            }
            delete(filehost);

			size_t scheme_pos = download_url.find("://");
			size_t root_pos = download_url.find("/", scheme_pos + 3);
			std::string host = download_url.substr(0, root_pos);
			std::string path = download_url.substr(root_pos);
            uint64_t file_size;

            RemoteClient *baseclient = new BaseClient();
            baseclient->Connect(host, "", "");
            baseclient->Size(path, &file_size);
            delete baseclient;

            uint64_t id = Util::GetTick();
            json_object *params = json_object_new_object();
            JsonObjectRef params_ref(params);
            json_object_object_add(params, "type", json_object_new_int(CLIENT_TYPE_FILEHOST));
            json_object_object_add(params, "url", json_object_new_string(host.c_str()));
            json_object_object_add(params, "username", json_object_new_string(""));
            json_object_object_add(params, "password", json_object_new_string(""));
            json_object_object_add(params, "src_path", json_object_new_string(path.c_str()));
            json_object_object_add(params, "dest_path", json_object_new_string(dest_param));
            json_object_object_add(params, "size", json_object_new_uint64(file_size));
            json_object_object_add(params, "id", json_object_new_uint64(id));

            const char *params_str = json_object_to_json_string(params);

            CHTTPClient::HttpResponse resp;
            CHTTPClient::HeadersMap headers;
            CHTTPClient tmp_client([](const std::string &log) {});
            tmp_client.InitSession(true, CHTTPClient::SettingsFlag::NO_FLAGS);
            tmp_client.SetCertificateFile(CACERT_FILE);
            headers["Content-Type"] = "application/json";

            std::string download_req_url = std::string("http://localhost:") + std::to_string(http_int_server_port) + "/download_url";
            if (tmp_client.Post(download_req_url, headers, params_str, resp))
            {
                if (HTTP_SUCCESS(resp.iCode))
                {
                    Util::RichNotify(id, "%s queued for download", path.c_str());
                    success(res);
                    return;
                }
                else
                {
                    Util::RichNotify(id, "Failed to queue %s for download in background", path.c_str());
                    failed(res, 200, "Failed to download");
                    return;
                }
            }

            failed(res, 200, "Failed to download");
        });

        svr->Get("/stop", [&](const Request & /*req*/, Response &res)
        {
            res.status = 200;
            res.set_content("Stopping", "text/plain");
            svr->stop();
            // Ends the render loop in gui.cpp so the client terminates.
            done = true;
        });

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

        // Everything under the API needs a session once a password is set; the
        // static UI and the auth endpoints stay open so the form can render.
        svr->set_pre_routing_handler([](const Request &req, Response &res) {
            if (!WebAuthRequired())
                return Server::HandlerResponse::Unhandled;

            const std::string path = req.path;
            if (path == "/__local__/auth" || path == "/__local__/login")
                return Server::HandlerResponse::Unhandled;

            // /stop shuts the client down, so it needs a session as well.
            if (path.rfind("/__local__/", 0) != 0 && path.rfind("/__remote__", 0) != 0 && path != "/stop")
                return Server::HandlerResponse::Unhandled;

            if (WebSessionValid(req))
                return Server::HandlerResponse::Unhandled;

            res.status = 401;
            res.set_content("{\"success\":false,\"error\":\"Authentication required\"}", "application/json");
            return Server::HandlerResponse::Handled;
        });
        svr->set_mount_point("/", "/");

        if (web_server_enabled)
            svr->listen("0.0.0.0", http_server_port);
        else
            svr->listen("127.0.0.1", http_server_port);

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

    bool AnyTransferRunning()
    {
        std::lock_guard<std::mutex> lock(web_transfer_mutex);
        for (std::map<std::string, WebTransfer>::iterator it = web_transfers.begin(); it != web_transfers.end(); ++it)
        {
            if (it->second.state == 0)
                return true;
        }
        return false;
    }

    void Stop()
    {
        static std::atomic<bool> stopped{false};
        if (stopped.exchange(true))
            return;

        if (svr != nullptr)
        {
            svr->stop();
        }
    }
}
