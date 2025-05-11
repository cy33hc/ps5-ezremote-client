#include <fstream>
#include <curl/curl.h>
#include <sys/time.h>
#include "clients/remote_client.h"
#include "clients/baseclient.h"
#include "config.h"
#include "lang.h"
#include "split_file.h"
#include "util.h"
#ifndef NO_GUI
#include "windows.h"
#endif
#include "common.h"

using httplib::DataSink;

BaseClient::BaseClient(){};

BaseClient::~BaseClient()
{
    if (client != nullptr)
        delete client;
};

#ifndef NO_GUI
int BaseClient::NothingCallback(void* ptr, double dTotalToDownload, double dNowDownloaded, double dTotalToUpload, double dNowUploaded)
{
    return 0;
}

int BaseClient::DownloadProgressCallback(void* ptr, double dTotalToDownload, double dNowDownloaded, double dTotalToUpload, double dNowUploaded)
{
    CHTTPClient::ProgressFnStruct *progress_data = (CHTTPClient::ProgressFnStruct*) ptr;
    int64_t *bytes_transfered = (int64_t *) progress_data->pOwner;
	*bytes_transfered = dNowDownloaded;
    return 0;
}

int BaseClient::UploadProgressCallback(void* ptr, double dTotalToDownload, double dNowDownloaded, double dTotalToUpload, double dNowUploaded)
{
    CHTTPClient::ProgressFnStruct *progress_data = (CHTTPClient::ProgressFnStruct*) ptr;
    int64_t *bytes_transfered = (int64_t *) progress_data->pOwner;
    *bytes_transfered = dNowUploaded;
    return 0;
}
#endif

size_t BaseClient::WriteToSplitFileCallback(void *buff, size_t size, size_t nmemb, void *data)
{
    if ((size == 0) || (nmemb == 0) || ((size * nmemb) < 1) || (data == nullptr))
        return 0;

    SplitFile *split_file = reinterpret_cast<SplitFile *>(data);
    if (!split_file->IsClosed())
    {
        if (split_file->Write(reinterpret_cast<char *>(buff), size * nmemb) < 0)
            return 0;
    }
    else
    {
        return 0;
    }

    return size * nmemb;
}

int BaseClient::Connect(const std::string &url, const std::string &username, const std::string &password, bool send_ping)
{
    this->host_url = url;
    size_t scheme_pos = url.find("://");
    size_t root_pos = url.find("/", scheme_pos + 3);
    if (root_pos != std::string::npos)
    {
        this->host_url = url.substr(0, root_pos);
        this->base_path = url.substr(root_pos);
    }

    client = new CHTTPClient([](const std::string& log){});
    if (!username.empty())
    {
        client->SetBasicAuth(username, password);
    }
    client->InitSession(true, CHTTPClient::SettingsFlag::NO_FLAGS);
    client->SetCertificateFile(CACERT_FILE);

    if (!send_ping)
        this->connected = true;
    else if (Ping())
        this->connected = true;

    return 1;
}

int BaseClient::Mkdir(const std::string &path)
{
    sprintf(this->response, "%s", lang_strings[STR_UNSUPPORTED_OPERATION_MSG]);
    return 0;
}

int BaseClient::Rmdir(const std::string &path, bool recursive)
{
    sprintf(this->response, "%s", lang_strings[STR_UNSUPPORTED_OPERATION_MSG]);
    return 0;
}

int BaseClient::Size(const std::string &path, uint64_t *size)
{
    CHTTPClient::HeadersMap headers;
    CHTTPClient::HttpResponse res;

    std::string encoded_url = this->host_url + CHTTPClient::EncodeUrl(GetFullPath(path));

#ifndef NO_GUI
    client->SetProgressFnCallback(nullptr, NothingCallback);
#endif

    if (client->Head(encoded_url, headers, res))
    {
        if (HTTP_SUCCESS(res.iCode))
        {
            std::string content_length = res.mapHeadersLowercase["content-length"];
            if (content_length.length() > 0)
                *size = atoll(content_length.c_str());
            return 1;
        }
        else // Server doesn't support HEAD request. Try get range with 0 bytes and grab size from the response header 
             // example: Content-Range: bytes 0-10/4372785
        {
            CHTTPClient::HttpResponse range_res;
            CHTTPClient::HeadersMap range_headers;
            range_headers["Range"] = "bytes=0-1";

            if (client->Get(encoded_url, range_headers, range_res))
            {
                if (HTTP_SUCCESS(range_res.iCode))
                {
                    std::string content_range = range_res.mapHeaders["Content-Range"];
                    std::vector<std::string> range_parts = Util::Split(content_range, "/");
                    if (range_parts.size() == 2)
                    {
                        *size = atoll(range_parts[1].c_str());
                        return 1;
                    }
                }
            }
        }
    }
    else
    {
        sprintf(this->response, "%s", res.errMessage.c_str());
    }
    return 0;
}

int BaseClient::Get(const std::string &outputfile, const std::string &path, uint64_t offset)
{
    long status;
    u_int64_t file_size;
    CHTTPClient::HeadersMap headers;

    if (!Size(path, &file_size))
    {
        sprintf(this->response, "%s", lang_strings[STR_FAIL_DOWNLOAD_MSG]);
        return 0;
    }

#ifndef NO_GUI
    prev_tick = Util::GetTick();
    bytes_transfered = 0;
    bytes_to_download = file_size;
    client->SetProgressFnCallback(&bytes_transfered, DownloadProgressCallback);
#endif

    std::string encoded_url = this->host_url + CHTTPClient::EncodeUrl(GetFullPath(path));
    if (client->DownloadFile(outputfile, encoded_url, status))
    {
        return 1;
    }
    else
    {
        sprintf(this->response, "%ld - %s", status, lang_strings[STR_FAIL_DOWNLOAD_MSG]);
    }
    return 0;
}

int BaseClient::Get(SplitFile *split_file, const std::string &path, uint64_t offset)
{
    long status;
    CHTTPClient::HeadersMap headers;

    std::string encoded_url = this->host_url + CHTTPClient::EncodeUrl(GetFullPath(path));
    
#ifndef NO_GUI
    prev_tick = Util::GetTick();
    client->SetProgressFnCallback(nullptr, NothingCallback);
#endif

    if (client->DownloadFile((void*)split_file, encoded_url, (void*)WriteToSplitFileCallback, status))
    {
        return 1;
    }
    else
    {
        sprintf(this->response, "%ld - %s", status, lang_strings[STR_FAIL_DOWNLOAD_MSG]);
    }
    return 0;
}

int BaseClient::GetRange(const std::string &path, DataSink &sink, uint64_t size, uint64_t offset)
{
    CHTTPClient::HttpResponse res;
    CHTTPClient::HeadersMap headers;

    char range_header[128];
    sprintf(range_header, "bytes=%lu-%lu", offset, offset + size - 1);
    headers["Range"] = range_header;

    std::string encoded_url = this->host_url + CHTTPClient::EncodeUrl(GetFullPath(path));

#ifndef NO_GUI
    client->SetProgressFnCallback(nullptr, NothingCallback);
#endif

    if (client->Get(encoded_url, headers, res))
    {
        uint64_t len = MIN(size, res.strBody.size());
        sink.write(res.strBody.data(), len);
        return 1;
    }
    else
    {
        sprintf(this->response, "%s", res.errMessage.c_str());
    }
    return 0;
}


int BaseClient::GetRange(const std::string &path, void *buffer, uint64_t size, uint64_t offset)
{
    CHTTPClient::HttpResponse res;
    CHTTPClient::HeadersMap headers;

    char range_header[128];
    sprintf(range_header, "bytes=%lu-%lu", offset, offset + size - 1);
    headers["Range"] = range_header;

    std::string encoded_url = this->host_url + CHTTPClient::EncodeUrl(GetFullPath(path));

#ifndef NO_GUI
    client->SetProgressFnCallback(nullptr, NothingCallback);
#endif

    if (client->Get(encoded_url, headers, res))
    {
        uint64_t len = MIN(size, res.strBody.size());
        memcpy(buffer, res.strBody.data(), len);
        return 1;
    }
    else
    {
        sprintf(this->response, "%s", res.errMessage.c_str());
    }
    return 0;
}

int BaseClient::Put(const std::string &inputfile, const std::string &path, uint64_t offset)
{
    sprintf(this->response, "%s", lang_strings[STR_UNSUPPORTED_OPERATION_MSG]);
    return 0;
}

int BaseClient::Rename(const std::string &src, const std::string &dst)
{
    sprintf(this->response, "%s", lang_strings[STR_UNSUPPORTED_OPERATION_MSG]);
    return 0;
}

int BaseClient::Delete(const std::string &path)
{
    sprintf(this->response, "%s", lang_strings[STR_UNSUPPORTED_OPERATION_MSG]);
    return 0;
}

int BaseClient::Copy(const std::string &from, const std::string &to)
{
    sprintf(this->response, "%s", lang_strings[STR_UNSUPPORTED_OPERATION_MSG]);
    return 0;
}

int BaseClient::Move(const std::string &from, const std::string &to)
{
    sprintf(this->response, "%s", lang_strings[STR_UNSUPPORTED_OPERATION_MSG]);
    return 0;
}

int BaseClient::Head(const std::string &path, void *buffer, uint64_t size)
{
    CHTTPClient::HttpResponse res;
    CHTTPClient::HeadersMap headers;

    char range_header[128];
    sprintf(range_header, "bytes=%lu-%lu", 0L, size - 1);
    headers["Range"] = range_header;

    std::string encoded_url = this->host_url + CHTTPClient::EncodeUrl(GetFullPath(path));

#ifndef NO_GUI
    client->SetProgressFnCallback(nullptr, NothingCallback);
#endif

    if (client->Get(encoded_url, headers, res))
    {
        uint64_t len = MIN(size, res.strBody.size());
        memcpy(buffer, res.strBody.data(), len);
        return 1;
    }
    else
    {
        sprintf(this->response, "%s", res.errMessage.c_str());
    }
    return 0;
}

bool BaseClient::FileExists(const std::string &path)
{
    uint64_t file_size;
    return Size(path, &file_size);
}

std::vector<DirEntry> BaseClient::ListDir(const std::string &path)
{
    std::vector<DirEntry> out;
    DirEntry entry;
    Util::SetupPreviousFolder(path, &entry);
    out.push_back(entry);

    return out;
}

std::string BaseClient::GetPath(std::string ppath1, std::string ppath2)
{
    std::string path1 = ppath1;
    std::string path2 = ppath2;
    path1 = Util::Trim(Util::Trim(path1, " "), "/");
    path2 = Util::Trim(Util::Trim(path2, " "), "/");
    path1 = this->base_path + ((this->base_path.length() > 0) ? "/" : "") + path1 + "/" + path2;
    if (path1[0] != '/')
        path1 = "/" + path1;
    return path1;
}

std::string BaseClient::GetFullPath(std::string ppath1)
{
    std::string path1 = ppath1;
    path1 = Util::Trim(Util::Trim(path1, " "), "/");
    path1 = this->base_path + "/" + path1;
    Util::ReplaceAll(path1, "//", "/");
    return path1;
}

bool BaseClient::IsConnected()
{
    return this->connected;
}

bool BaseClient::Ping()
{
    CHTTPClient::HttpResponse res;
    CHTTPClient::HeadersMap headers;

    std::string encoded_url = this->host_url + CHTTPClient::EncodeUrl(GetFullPath("/"));
    if (client->Head(encoded_url, headers, res))
    {
        return true;
    }
    else
    {
        sprintf(this->response, "%s", res.errMessage.c_str());
    }
    return false;
}

const char *BaseClient::LastResponse()
{
    return this->response;
}

int BaseClient::Quit()
{
    if (client != nullptr)
    {
        client->CleanupSession();
        delete client;
        client = nullptr;
    }
    return 1;
}

ClientType BaseClient::clientType()
{
    return CLIENT_TYPE_HTTP_SERVER;
}

uint32_t BaseClient::SupportedActions()
{
    return REMOTE_ACTION_DOWNLOAD | REMOTE_ACTION_INSTALL | REMOTE_ACTION_EXTRACT;
}

std::string BaseClient::Escape(const std::string &url)
{
    CURL *curl = curl_easy_init();
    if (curl)
    {
        char *output = curl_easy_escape(curl, url.c_str(), url.length());
        if (output)
        {
            std::string encoded_url = std::string(output);
            curl_free(output);
            return encoded_url;
        }
        curl_easy_cleanup(curl);
    }
    return "";
}

std::string BaseClient::UnEscape(const std::string &url)
{
    CURL *curl = curl_easy_init();
    if (curl)
    {
        int decode_len;
        char *output = curl_easy_unescape(curl, url.c_str(), url.length(), &decode_len);
        if (output)
        {
            std::string decoded_url = std::string(output, decode_len);
            curl_free(output);
            return decoded_url;
        }
        curl_easy_cleanup(curl);
    }
    return "";
}

void *BaseClient::Open(const std::string &path, int flags)
{
    sprintf(this->response, "%s", lang_strings[STR_UNSUPPORTED_OPERATION_MSG]);
    return nullptr;
}

void BaseClient::Close(void *fp)
{
    sprintf(this->response, "%s", lang_strings[STR_UNSUPPORTED_OPERATION_MSG]);
}

int BaseClient::GetRange(void *fp, DataSink &sink, uint64_t size, uint64_t offset)
{
    sprintf(this->response, "%s", lang_strings[STR_UNSUPPORTED_OPERATION_MSG]);
    return -1;
}

int BaseClient::GetRange(void *fp, void *buffer, uint64_t size, uint64_t offset)
{
    sprintf(this->response, "%s", lang_strings[STR_UNSUPPORTED_OPERATION_MSG]);
    return -1;
}
