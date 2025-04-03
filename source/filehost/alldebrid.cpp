#include "httpclient/HTTPClient.h"
#include <json-c/json.h>

#include "config.h"
#include "common.h"
#include "alldebrid.h"

AllDebridHost::AllDebridHost(const std::string &url) : FileHost(url)
{
}

bool AllDebridHost::IsValidUrl()
{
    CHTTPClient::HttpResponse res;
    CHTTPClient::HeadersMap headers;
    CHTTPClient tmp_client([](const std::string& log){});
    tmp_client.InitSession(true, CHTTPClient::SettingsFlag::NO_FLAGS);
    tmp_client.SetCertificateFile(CACERT_FILE);

    std::string path = std::string("https://api.alldebrid.com/v4/link/unlock?agent=ezRemoteClient&apikey=") + alldebrid_api_key +  "&link=" + httplib::detail::encode_url(url);
    if (tmp_client.Get(path, headers, res))
    {
        if (HTTP_SUCCESS(res.iCode))
        {
            json_object *jobj = json_tokener_parse(res.strBody.data());
            const char *status = json_object_get_string(json_object_object_get(jobj, "status"));

            if (strcmp(status, "success") == 0)
                return true;
        }
    }
    
    return false;
}

std::string AllDebridHost::GetDownloadUrl()
{
    CHTTPClient::HttpResponse res;
    CHTTPClient::HeadersMap headers;
    CHTTPClient tmp_client([](const std::string& log){});
    tmp_client.InitSession(true, CHTTPClient::SettingsFlag::NO_FLAGS);
    tmp_client.SetCertificateFile(CACERT_FILE);

    std::string path = std::string("https://api.alldebrid.com/v4/link/unlock?agent=ezRemoteClient&apikey=") + alldebrid_api_key +  "&link=" + CHTTPClient::EncodeUrl(url);
    if (tmp_client.Get(path, headers, res))
    {
        if (HTTP_SUCCESS(res.iCode))
        {
            json_object *jobj = json_tokener_parse(res.strBody.data());
            const char *status = json_object_get_string(json_object_object_get(jobj, "status"));

            if (status != nullptr && strcmp(status, "success") == 0)
            {
                json_object *data = json_object_object_get(jobj, "data");
                const char *link = json_object_get_string(json_object_object_get(data, "link"));
                return std::string(link);
            }
            else
            {
                return "";
            }
        }
    }

    return "";
}
