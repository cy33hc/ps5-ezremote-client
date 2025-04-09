#include "httpclient/HTTPClient.h"
#include <json-c/json.h>

#include "config.h"
#include "common.h"
#include "realdebrid.h"

RealDebridHost::RealDebridHost(const std::string &url) : FileHost(url)
{
}

bool RealDebridHost::IsValidUrl()
{
    CHTTPClient::HttpResponse res;
    CHTTPClient::HeadersMap headers;
    CHTTPClient tmp_client([](const std::string& log){});
    tmp_client.InitSession(true, CHTTPClient::SettingsFlag::NO_FLAGS);
    tmp_client.SetCertificateFile(CACERT_FILE);

    headers["Authorization"] = std::string("Bearer ") + realdebrid_api_key;
    headers["Content-Type"] = "application/x-www-form-urlencoded";
    std::string path = std::string("https://api.real-debrid.com/rest/1.0/unrestrict/check");
    std::string post_data = std::string("link=") + CHTTPClient::EncodeUrl(this->url) + "&password=";

    if (tmp_client.Post(path, headers, post_data, res))
    {
        if (HTTP_SUCCESS(res.iCode))
        {
            json_object *jobj = json_tokener_parse(res.strBody.data());
            uint64_t supported = json_object_get_uint64(json_object_object_get(jobj, "supported"));

            if (supported == 1)
                return true;
        }
    }

    return false;
}

std::string RealDebridHost::GetDownloadUrl()
{
    CHTTPClient::HttpResponse res;
    CHTTPClient::HeadersMap headers;
    CHTTPClient tmp_client([](const std::string& log){});
    tmp_client.InitSession(true, CHTTPClient::SettingsFlag::NO_FLAGS);
    tmp_client.SetCertificateFile(CACERT_FILE);

    headers["Authorization"] = std::string("Bearer ") + realdebrid_api_key;
    headers["Content-Type"] = "application/x-www-form-urlencoded";
    std::string path = std::string("https://api.real-debrid.com/rest/1.0/unrestrict/link");
    std::string post_data = std::string("link=") + CHTTPClient::EncodeUrl(this->url) + "&password=&remote=0";

    if (tmp_client.Post(path, headers, post_data, res))
    {
        if (HTTP_SUCCESS(res.iCode))
        {
            json_object *jobj = json_tokener_parse(res.strBody.data());
            const char *download = json_object_get_string(json_object_object_get(jobj, "download"));

            if (download != nullptr)
            {
                return std::string(download);
            }
            else
            {
                return "";
            }
        }
    }
    
    return "";
}
