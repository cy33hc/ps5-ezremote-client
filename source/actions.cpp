#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <json-c/json.h>
#include "json_object_ref.h"
#include "activity.h"
#include <atomic>
#include <mutex>

#include "remote_pool.h"
#include <lexbor/html/parser.h>
#include <lexbor/dom/interfaces/element.h>
#include <minizip/unzip.h>
#include "clients/smbclient.h"
#include "clients/webdav.h"
#include "clients/apache.h"
#include "clients/github.h"
#include "clients/archiveorg.h"
#include "clients/ftpclient.h"
#include "clients/sftpclient.h"
#include "clients/myrient.h"
#include "clients/nginx.h"
#include "clients/npxserve.h"
#include "clients/nfsclient.h"
#include "clients/iis.h"
#include "clients/rclone.h"
#include "clients/remote_client.h"
#include "filehost/filehost.h"
#include "server/http_server.h"
#include "common.h"
#include "fs.h"
#include "config.h"
#include "windows.h"
#include "util.h"
#include "lang.h"
#include "actions.h"
#include "installer.h"
#include "sfo.h"
#include "zip_util.h"
#include "sceSystemService.h"

namespace Actions
{
    // Per-operation inputs handed to worker threads, so a second operation
    // cannot change what a running one works on.
    struct TransferJob
    {
        std::vector<DirEntry> files;
        std::string target; // destination folder for copy/move
    };

    // Per-operation inputs, so two archive jobs cannot overwrite each other.
    struct ArchiveJob
    {
        std::vector<DirEntry> files;
        std::string target; // zip path or extract folder
    };

    // Remote transfers block inside Get, so a watcher feeds the activity's
    // progress from the connection's own counters.
    struct ProgressWatch
    {
        RemoteClient *client;
        uint64_t activity_id;
        std::atomic<bool> done;
    };

    static void *ProgressWatchThread(void *argp)
    {
        ProgressWatch *watch = (ProgressWatch *)argp;
        while (!watch->done)
        {
            Activities::Progress(watch->activity_id, watch->client->TransferredBytes(), watch->client->TotalBytes());
            usleep(200000);
        }
        delete watch;
        return NULL;
    }

    static std::vector<DirEntry> TakeSelection(const std::set<DirEntry> &multi, const DirEntry &single)
    {
        std::vector<DirEntry> files;
        if (multi.size() > 0)
            std::copy(multi.begin(), multi.end(), std::back_inserter(files));
        else
            files.push_back(single);
        return files;
    }

    static int FtpCallback(int64_t xfered, void *arg)
    {
        // The owning client is passed as the callback argument so progress stays
        // per-connection instead of going through the shared global.
        if (arg != nullptr)
            static_cast<FtpClient *>(arg)->SetTransferredBytes((uint64_t) xfered);
        return 1;
    }

    void RefreshLocalFiles(bool apply_filter)
    {
        multi_selected_local_files.clear();
        local_files.clear();
        int err;
        if (strlen(local_filter) > 0 && apply_filter)
        {
            std::vector<DirEntry> temp_files = FS::ListDir(local_directory, &err);
            std::string lower_filter = Util::ToLower(local_filter);
            for (std::vector<DirEntry>::iterator it = temp_files.begin(); it != temp_files.end();)
            {
                std::string lower_name = Util::ToLower(it->name);
                if (lower_name.find(lower_filter) != std::string::npos || strcmp(it->name, "..") == 0)
                {
                    local_files.push_back(*it);
                }
                ++it;
            }
            temp_files.clear();
        }
        else
        {
            local_files = FS::ListDir(local_directory, &err);
        }
        DirEntry::Sort(local_files);
        if (err != 0)
            sprintf(status_message, "%s", lang_strings[STR_FAIL_READ_LOCAL_DIR_MSG]);
    }

    void RefreshRemoteFiles(bool apply_filter)
    {
        if (!remoteclient->Ping())
        {
            remoteclient->Quit();
            sprintf(status_message, "%s", lang_strings[STR_CONNECTION_CLOSE_ERR_MSG]);
            return;
        }

        std::string strList;
        multi_selected_remote_files.clear();
        remote_files.clear();
        if (strlen(remote_filter) > 0 && apply_filter)
        {
            std::vector<DirEntry> temp_files = remoteclient->ListDir(remote_directory);
            std::string lower_filter = Util::ToLower(remote_filter);
            for (std::vector<DirEntry>::iterator it = temp_files.begin(); it != temp_files.end();)
            {
                std::string lower_name = Util::ToLower(it->name);
                if (lower_name.find(lower_filter) != std::string::npos || strcmp(it->name, "..") == 0)
                {
                    remote_files.push_back(*it);
                }
                ++it;
            }
            temp_files.clear();
        }
        else
        {
            remote_files = remoteclient->ListDir(remote_directory);
        }
        DirEntry::Sort(remote_files);
    }

    void HandleChangeLocalDirectory(const DirEntry entry)
    {
        if (!entry.isDir)
            return;

        if (strcmp(entry.name, "..") == 0)
        {
            std::string temp_path = std::string(entry.directory);
            if (temp_path.size() > 1)
            {
                if (temp_path.find_last_of("/") == 0)
                {
                    sprintf(local_directory, "%s", "/");
                }
                else
                {
                    sprintf(local_directory, "%s", temp_path.substr(0, temp_path.find_last_of("/")).c_str());
                }
            }
            sprintf(local_file_to_select, "%s", temp_path.substr(temp_path.find_last_of("/") + 1).c_str());
        }
        else
        {
            sprintf(local_directory, "%s", entry.path);
        }
        RefreshLocalFiles(false);
        if (strcmp(entry.name, "..") != 0)
        {
            sprintf(local_file_to_select, "%s", local_files[0].name);
        }
        selected_action = ACTION_NONE;
    }

    void HandleChangeRemoteDirectory(const DirEntry entry)
    {
        if (!entry.isDir)
            return;

        if (!remoteclient->Ping())
        {
            remoteclient->Quit();
            sprintf(status_message, "%s", lang_strings[STR_CONNECTION_CLOSE_ERR_MSG]);
            return;
        }

        if (strcmp(entry.name, "..") == 0)
        {
            std::string temp_path = std::string(entry.directory);
            if (temp_path.size() > 1)
            {
                if (temp_path.find_last_of("/") == 0)
                {
                    sprintf(remote_directory, "%s", "/");
                }
                else
                {
                    sprintf(remote_directory, "%s", temp_path.substr(0, temp_path.find_last_of("/")).c_str());
                }
            }
            sprintf(remote_file_to_select, "%s", temp_path.substr(temp_path.find_last_of("/") + 1).c_str());
        }
        else
        {
            sprintf(remote_directory, "%s", entry.path);
        }
        RefreshRemoteFiles(false);
        if (strcmp(entry.name, "..") != 0)
        {
            sprintf(remote_file_to_select, "%s", remote_files[0].name);
        }
        selected_action = ACTION_NONE;
    }

    void HandleRefreshLocalFiles()
    {
        int prev_count = local_files.size();
        RefreshLocalFiles(false);
        int new_count = local_files.size();
        if (prev_count != new_count)
        {
            sprintf(local_file_to_select, "%s", local_files[0].name);
        }
        selected_action = ACTION_NONE;
    }

    void HandleRefreshRemoteFiles()
    {
        if (remoteclient != nullptr)
        {
            int prev_count = remote_files.size();
            RefreshRemoteFiles(false);
            int new_count = remote_files.size();
            if (prev_count != new_count)
            {
                sprintf(remote_file_to_select, "%s", remote_files[0].name);
            }
        }
        selected_action = ACTION_NONE;
    }

    void CreateNewLocalFolder(char *new_folder)
    {
        sprintf(status_message, "%s", "");
        std::string folder = std::string(new_folder);
        folder = Util::Rtrim(Util::Trim(folder, " "), "/");
        std::string path = FS::GetPath(local_directory, folder);
        FS::MkDirs(path);
        RefreshLocalFiles(false);
        sprintf(local_file_to_select, "%s", folder.c_str());
    }

    void CreateNewRemoteFolder(char *new_folder)
    {
        sprintf(status_message, "%s", "");
        std::string folder = std::string(new_folder);
        folder = Util::Rtrim(Util::Trim(folder, " "), "/");
        std::string path = remoteclient->GetPath(remote_directory, folder.c_str());
        if (remoteclient->Mkdir(path.c_str()))
        {
            RefreshRemoteFiles(false);
            sprintf(remote_file_to_select, "%s", folder.c_str());
        }
        else
        {
            sprintf(status_message, "%s - %s", lang_strings[STR_FAILED], remoteclient->LastResponse());
        }
    }

    void RenameLocalFolder(const char *old_path, const char *new_path)
    {
        sprintf(status_message, "%s", "");
        std::string new_name = std::string(new_path);
        new_name = Util::Rtrim(Util::Trim(new_name, " "), "/");
        std::string path = FS::GetPath(local_directory, new_name);
        FS::Rename(old_path, path);
        RefreshLocalFiles(false);
        sprintf(local_file_to_select, "%s", new_name.c_str());
    }

    void RenameRemoteFolder(const char *old_path, const char *new_path)
    {
        sprintf(status_message, "%s", "");
        std::string new_name = std::string(new_path);
        new_name = Util::Rtrim(Util::Trim(new_name, " "), "/");
        std::string path = FS::GetPath(remote_directory, new_name);
        if (remoteclient->Rename(old_path, path.c_str()))
        {
            RefreshRemoteFiles(false);
            sprintf(remote_file_to_select, "%s", new_name.c_str());
        }
        else
        {
            sprintf(status_message, "%s - %s", lang_strings[STR_FAILED], remoteclient->LastResponse());
        }
    }

    // Only one overwrite question is shown at a time; the answer belongs to the
    // operation that asked it.
    static std::mutex confirm_mutex;

    static void PromptOverwrite(const char *dest)
    {
        std::lock_guard<std::mutex> lock(confirm_mutex);
        sprintf(confirm_message, "%s %s?", lang_strings[STR_OVERWRITE], dest);
        confirm_state = CONFIRM_WAIT;
        action_to_take = selected_action;
        while (confirm_state == CONFIRM_WAIT)
        {
            usleep(100000);
        }
        selected_action = action_to_take;
    }

    void *DeleteSelectedLocalFilesThread(void *argp)
    {
        TransferJob *job = static_cast<TransferJob *>(argp);
        Activities::Guard activity(ACTIVITY_DELETE, STR_DELETING);
        std::vector<DirEntry> files = job->files;
        delete job;
        int failed = 0;
        int total = 0;


        for (std::vector<DirEntry>::iterator it = files.begin(); it != files.end(); ++it)
        {
            sprintf(activity_message, "%s %s", lang_strings[STR_DELETING], it->path);
            Activities::Item(activity.id, it->path);
            total++;
            if (FS::RmRecursive(it->path) <= 0)
                failed++;
        }
        if (failed > 0)
        {
            if (failed < total)
                activity.partial(status_message);
            else
                activity.fail(status_message);
        }

        selected_action = ACTION_REFRESH_LOCAL_FILES;
        return NULL;
    }

    void DeleteSelectedLocalFiles()
    {
        TransferJob *job = new TransferJob();
        job->files = TakeSelection(multi_selected_local_files, selected_local_file);

        int thread_res = pthread_create(&bk_activity_thid, NULL, DeleteSelectedLocalFilesThread, job);
        if (thread_res != 0)
        {
            selected_action = ACTION_REFRESH_LOCAL_FILES;
        }
    }

    void *DeleteSelectedRemotesFilesThread(void *argp)
    {
        TransferJob *job = static_cast<TransferJob *>(argp);
        Activities::Guard activity(ACTIVITY_DELETE, STR_DELETING, ACTIVITY_NATIVE, ACTIVITY_REMOTE);
        std::vector<DirEntry> files = job->files;
        delete job;
        int failed = 0;
        int total = 0;


        // Own connection for the duration of this operation.
        RemoteClient *client = RemotePool::Acquire(*remote_settings);
        bool healthy = true;

        if (client != nullptr && client->Ping())
        {
            for (std::vector<DirEntry>::iterator it = files.begin(); it != files.end(); ++it)
            {
                Activities::Item(activity.id, it->path);
                if (it->isDir)
                {
                    total++;
                    if (!client->Rmdir(it->path, true))
                    {
                        failed++;
                        healthy = false;
                    }
                }
                else
                {
                    sprintf(activity_message, "%s %s", lang_strings[STR_DELETING], it->path);
                    total++;
                    if (!client->Delete(it->path))
                    {
                        failed++;
                        healthy = false;
                        sprintf(status_message, "%s - %s", lang_strings[STR_FAILED], client->LastResponse());
                    }
                }
            }
            selected_action = ACTION_REFRESH_REMOTE_FILES;
        }
        else
        {
            sprintf(status_message, "%s", lang_strings[STR_CONNECTION_CLOSE_ERR_MSG]);
            healthy = false;
            Disconnect();
        }

        if (failed > 0)
        {
            if (failed < total)
                activity.partial(status_message);
            else
                activity.fail(status_message);
        }

        RemotePool::Release(client, healthy);
        return NULL;
    }

    void DeleteSelectedRemotesFiles()
    {
        TransferJob *job = new TransferJob();
        job->files = TakeSelection(multi_selected_remote_files, selected_remote_file);

        int thread_res = pthread_create(&bk_activity_thid, NULL, DeleteSelectedRemotesFilesThread, job);
        if (thread_res != 0)
        {
        }
    }

    int UploadFile(const char *src, const char *dest, RemoteClient *client)
    {
        int ret;
        if (!client->Ping())
        {
            client->Quit();
            sprintf(status_message, "%s", lang_strings[STR_CONNECTION_CLOSE_ERR_MSG]);
            return 0;
        }

        if (Activities::OverwriteType() == OVERWRITE_PROMPT && client->FileExists(dest))
        {
            PromptOverwrite(dest);
        }
        else if (Activities::OverwriteType() == OVERWRITE_NONE && client->FileExists(dest))
        {
            confirm_state = CONFIRM_NO;
        }
        else
        {
            confirm_state = CONFIRM_YES;
        }

        if (confirm_state == CONFIRM_YES)
        {
            prev_tick = Util::GetTick();

            ProgressWatch *watch = new ProgressWatch{client, activity_current_id, false};
            pthread_t watch_thid;
            bool watching = pthread_create(&watch_thid, NULL, ProgressWatchThread, watch) == 0;
            if (!watching)
                delete watch;

            int ret = client->Put(src, dest);

            if (watching)
            {
                watch->done = true;
                pthread_join(watch_thid, NULL);
            }
            return ret;
        }

        sceSystemServicePowerTick();
        return 1;
    }

    int Upload(const DirEntry &src, const char *dest, RemoteClient *client)
    {
        if (Activities::Stopped())
            return 1;

        int ret;
        if (src.isDir)
        {
            int err;
            std::vector<DirEntry> entries = FS::ListDir(src.path, &err);
            client->Mkdir(dest);
            for (int i = 0; i < entries.size(); i++)
            {
                if (Activities::Stopped())
                    return 1;

                int path_length = strlen(dest) + strlen(entries[i].name) + 2;
                char *new_path = (char *)malloc(path_length);
                snprintf(new_path, path_length, "%s%s%s", dest, FS::hasEndSlash(dest) ? "" : "/", entries[i].name);

                if (entries[i].isDir)
                {
                    if (strcmp(entries[i].name, "..") == 0)
                    {
                        free(new_path);
                        continue;
                    }

                    client->Mkdir(new_path);
                    ret = Upload(entries[i], new_path, client);
                    if (ret <= 0)
                    {
                        free(new_path);
                        return ret;
                    }
                }
                else
                {
                    snprintf(activity_message, 1024, "%s %s", lang_strings[STR_UPLOADING], entries[i].path);
                    prev_tick = Util::GetTick();
                    ret = UploadFile(entries[i].path, new_path, client);
                    if (ret <= 0)
                    {
                        sprintf(status_message, "%s %s", lang_strings[STR_FAIL_UPLOAD_MSG], entries[i].path);
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
            snprintf(activity_message, 1024, "%s %s", lang_strings[STR_UPLOADING], src.name);
            ret = UploadFile(src.path, new_path, client);
            if (ret <= 0)
            {
                free(new_path);
                sprintf(status_message, "%s %s", lang_strings[STR_FAIL_UPLOAD_MSG], src.name);
                return 0;
            }
            free(new_path);
        }
        return 1;
    }

    void *UploadFilesThread(void *argp)
    {
        TransferJob *job = static_cast<TransferJob *>(argp);
        Activities::Guard activity(ACTIVITY_UPLOAD, STR_UPLOADING, ACTIVITY_NATIVE, ACTIVITY_REMOTE);
        std::vector<DirEntry> files = job->files;
        delete job;
        int failed = 0;
        int total = 0;


        RemoteClient *client = RemotePool::Acquire(*remote_settings);

        if (client != nullptr)
        {
            for (std::vector<DirEntry>::iterator it = files.begin(); it != files.end(); ++it)
            {
                Activities::Item(activity.id, it->path);
                if (it->isDir)
                {
                    char new_dir[512];
                    sprintf(new_dir, "%s%s%s", remote_directory, FS::hasEndSlash(remote_directory) ? "" : "/", it->name);
                    total++;
                    if (Upload(*it, new_dir, client) == 0)
                        failed++;
                }
                else
                {
                    total++;
                    if (Upload(*it, remote_directory, client) == 0)
                        failed++;
                }
            }
        }
        else
        {
            sprintf(status_message, "%s", lang_strings[STR_CONNECTION_CLOSE_ERR_MSG]);
        }

        if (failed > 0)
        {
            if (failed < total)
                activity.partial(status_message);
            else
                activity.fail(status_message);
        }

        RemotePool::Release(client, true);
        multi_selected_local_files.clear();
        selected_action = ACTION_REFRESH_REMOTE_FILES;
        return NULL;
    }

    void UploadFiles()
    {
        sprintf(status_message, "%s", "");
        TransferJob *job = new TransferJob();
        job->files = TakeSelection(multi_selected_local_files, selected_local_file);

        int thread_res = pthread_create(&bk_activity_thid, NULL, UploadFilesThread, job);
        if (thread_res != 0)
        {
            multi_selected_local_files.clear();
            selected_action = ACTION_REFRESH_REMOTE_FILES;
        }
    }

    int BackgroundDownload(const char* src, const char *dest, uint64_t file_size)
    {
        uint64_t id = Util::GetTick();
        json_object *params = json_object_new_object();
        JsonObjectRef params_ref(params);
        std::string _src = std::string(src);

        json_object_object_add(params, "type", json_object_new_int(remote_settings->type));
        json_object_object_add(params, "username", json_object_new_string(remote_settings->username));
        json_object_object_add(params, "dest_path", json_object_new_string(dest));
        json_object_object_add(params, "size", json_object_new_uint64(file_size));
        json_object_object_add(params, "id", json_object_new_uint64(id));

        if (remote_settings->type == CLIENT_TYPE_HTTP_SERVER && strcmp(remote_settings->http_server_type, HTTP_SERVER_GITHUB) == 0)
        {
            GithubClient *tmp_client = (GithubClient*) remoteclient;
            _src = tmp_client->GetRedirectUrl(src);
        }
        json_object_object_add(params, "src_path", json_object_new_string(_src.c_str()));

        if (remote_settings->type == CLIENT_TYPE_HTTP_SERVER && strcmp(remote_settings->http_server_type, HTTP_SERVER_GITHUB) == 0)
        {
            json_object_object_add(params, "url", json_object_new_string("https://release-assets.githubusercontent.com"));
        }
        else
        {
            json_object_object_add(params, "url", json_object_new_string(remote_settings->server));
        }

        json_object_object_add(params, "password", json_object_new_string(remote_settings->password));

        if (remote_settings->type == CLIENT_TYPE_HTTP_SERVER)
        {
            json_object_object_add(params, "http_server_type", json_object_new_string(remote_settings->http_server_type));
        }

        const char *params_str = json_object_to_json_string(params);

		CHTTPClient::HttpResponse res;
		CHTTPClient::HeadersMap headers;
		CHTTPClient tmp_client([](const std::string& log){});
		tmp_client.InitSession(true, CHTTPClient::SettingsFlag::NO_FLAGS);
		tmp_client.SetCertificateFile(CACERT_FILE);
		headers["Content-Type"] = "application/json";

		std::string download_url = std::string("http://localhost:") + std::to_string(http_int_server_port) + "/download_url";
		if (tmp_client.Post(download_url, headers, params_str, res))
		{
			if (HTTP_SUCCESS(res.iCode))
			{
                Util::RichNotify(id, "%s queued for download", src);
                return 1;
	  		}
			else
			{
                Util::RichNotify(id, "Failed to queue %s for download in background", src);
				return 0;
			}
		}

        return 0;
    }

    int DownloadFile(const char *src, const char *dest, RemoteClient *client)
    {
        prev_tick = Util::GetTick();
        uint64_t file_size = 0;
        if (!client->Size(src, &file_size))
        {
            client->Quit();
            sprintf(status_message, "%s", lang_strings[STR_CONNECTION_CLOSE_ERR_MSG]);
            return 0;
        }

        if (Activities::OverwriteType() == OVERWRITE_PROMPT && FS::FileExists(dest))
        {
            PromptOverwrite(dest);
        }
        else if (Activities::OverwriteType() == OVERWRITE_NONE && FS::FileExists(dest))
        {
            confirm_state = CONFIRM_NO;
        }
        else
        {
            confirm_state = CONFIRM_YES;
        }

        if (confirm_state == CONFIRM_YES)
        {
            prev_tick = Util::GetTick();

            if (enable_background_download && file_size > minimum_backgrond_file_size)
            {
                return BackgroundDownload(src, dest, file_size);
            }

            ProgressWatch *watch = new ProgressWatch{client, activity_current_id, false};
            pthread_t watch_thid;
            bool watching = pthread_create(&watch_thid, NULL, ProgressWatchThread, watch) == 0;
            if (!watching)
                delete watch;

            int ret = client->Get(dest, src);

            if (watching)
            {
                watch->done = true;
                pthread_join(watch_thid, NULL);
            }
            return ret;
        }

        sceSystemServicePowerTick();
        return 1;
    }

    int Download(const DirEntry &src, const char *dest, RemoteClient *client)
    {
        if (Activities::Stopped())
            return 1;

        int ret;
        if (src.isDir)
        {
            int err;
            std::vector<DirEntry> entries = client->ListDir(src.path);
            FS::MkDirs(dest);
            for (int i = 0; i < entries.size(); i++)
            {
                if (Activities::Stopped())
                    return 1;

                int path_length = strlen(dest) + strlen(entries[i].name) + 2;
                char *new_path = (char *)malloc(path_length);
                snprintf(new_path, path_length, "%s%s%s", dest, FS::hasEndSlash(dest) ? "" : "/", entries[i].name);

                if (entries[i].isDir)
                {
                    if (strcmp(entries[i].name, "..") == 0)
                    {
                        free(new_path);
                        continue;
                    }

                    FS::MkDirs(new_path);
                    ret = Download(entries[i], new_path, client);
                    if (ret <= 0)
                    {
                        free(new_path);
                        return ret;
                    }
                }
                else
                {
                    snprintf(activity_message, 1024, "%s %s", lang_strings[STR_DOWNLOADING], entries[i].path);
                    ret = DownloadFile(entries[i].path, new_path, client);
                    if (ret <= 0)
                    {
                        sprintf(status_message, "%s %s", lang_strings[STR_FAIL_DOWNLOAD_MSG], entries[i].path);
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
            snprintf(activity_message, 1024, "%s %s", lang_strings[STR_DOWNLOADING], src.path);
            ret = DownloadFile(src.path, new_path, client);
            if (ret <= 0)
            {
                free(new_path);
                sprintf(status_message, "%s %s", lang_strings[STR_FAIL_DOWNLOAD_MSG], src.path);
                return 0;
            }
            free(new_path);
        }
        return 1;
    }

    void *DownloadFilesThread(void *argp)
    {
        TransferJob *job = static_cast<TransferJob *>(argp);
        Activities::Guard activity(ACTIVITY_DOWNLOAD, STR_DOWNLOADING, ACTIVITY_NATIVE, ACTIVITY_REMOTE);
        std::vector<DirEntry> files = job->files;
        delete job;
        int failed = 0;
        int total = 0;


        RemoteClient *client = RemotePool::Acquire(*remote_settings);

        if (client != nullptr)
        {
            for (std::vector<DirEntry>::iterator it = files.begin(); it != files.end(); ++it)
            {
                Activities::Item(activity.id, it->path);
                if (it->isDir)
                {
                    char new_dir[512];
                    sprintf(new_dir, "%s%s%s", local_directory, FS::hasEndSlash(local_directory) ? "" : "/", it->name);
                    total++;
                    if (Download(*it, new_dir, client) == 0)
                        failed++;
                }
                else
                {
                    total++;
                    if (Download(*it, local_directory, client) == 0)
                        failed++;
                }
            }
        }
        else
        {
            sprintf(status_message, "%s", lang_strings[STR_CONNECTION_CLOSE_ERR_MSG]);
        }

        if (failed > 0)
        {
            if (failed < total)
                activity.partial(status_message);
            else
                activity.fail(status_message);
        }

        RemotePool::Release(client, true);

        multi_selected_remote_files.clear();
        selected_action = ACTION_REFRESH_LOCAL_FILES;
        return NULL;
    }

    void DownloadFiles()
    {
        sprintf(status_message, "%s", "");
        TransferJob *job = new TransferJob();
        job->files = TakeSelection(multi_selected_remote_files, selected_remote_file);

        int thread_res = pthread_create(&bk_activity_thid, NULL, DownloadFilesThread, job);
        if (thread_res != 0)
        {
            multi_selected_remote_files.clear();
            selected_action = ACTION_REFRESH_LOCAL_FILES;
        }
    }

    void *InstallRemotePkgsThread(void *argp)
    {
        TransferJob *job = static_cast<TransferJob *>(argp);
        Activities::Guard activity(ACTIVITY_INSTALL, STR_INSTALLING, ACTIVITY_NATIVE, ACTIVITY_REMOTE);
        int failed = 0;
        int success = 0;
        int skipped = 0;

        std::vector<DirEntry> files = job->files;
        delete job;

        RemoteClient *client = RemotePool::Acquire(*remote_settings);

        if (client == nullptr)
        {
            sprintf(status_message, "%s", lang_strings[STR_CONNECTION_CLOSE_ERR_MSG]);
            RemotePool::Release(client, true);
            return NULL;
        }

        for (std::vector<DirEntry>::iterator it = files.begin(); it != files.end(); ++it)
        {
            if (Activities::Stopped())
                break;
            sprintf(activity_message, "%s %s", lang_strings[STR_INSTALLING], it->name);
            Activities::Item(activity.id, it->name);

            if (!it->isDir)
            {
                std::string path = std::string(it->path);
                path = Util::ToLower(path);
                if (path.size() > 4 && path.substr(path.size() - 4) == ".pkg")
                {
                    pkg_header header;
                    memset(&header, 0, sizeof(header));

                    if (client->Head(it->path, (void *)&header, sizeof(header)) == 0)
                        failed++;
                    else
                    {
                        if (BE32(header.pkg_magic) == PKG_CNT_MAGIC || BE32(header.pkg_magic) == PKG_FIH_MAGIC)
                        {
                            uint64_t file_size;
                            client->Size(it->path, &file_size);
                            if (!remote_settings->enable_rpi)
                            {
                                if (DownloadAndInstallPkg(it->path, &header, client) == 0)
                                    failed++;
                                else
                                    success++;
                            }
                            else
                            {
                                if (remote_settings->enable_disk_cache)
                                {
                                    SplitPkgInstallData *install_data = (SplitPkgInstallData*) malloc(sizeof(SplitPkgInstallData));
                                    memset(install_data, 0, sizeof(SplitPkgInstallData));

                                    uint64_t tick = Util::GetTick();
                                    std::string install_pkg_path = std::string(temp_folder) + "/" + std::to_string(tick) + ".pkg";
                                    FileStream *sp = CreateFileStream(install_pkg_path, INSTALL_ARCHIVE_PKG_SPLIT_SIZE/2);

                                    install_data->split_file = sp;
                                    install_data->remote_client = INSTALLER::GetRemoteClient(remote_settings);
                                    install_data->path = it->path;
                                    client->Size(it->path, &install_data->size);
                                    install_data->stop_write_thread = false;
                                    install_data->delete_client = true;

                                    int ret = pthread_create(&install_data->thread, NULL, DownloadSplitPkg, install_data);

                                    if (INSTALLER::InstallSplitPkg(it->path, install_data, false) == 0)
                                        failed++;
                                    else
                                        success++;
                                }
                                else
                                {
                                    std::string url = INSTALLER::getRemoteUrl(it->path, file_size, true);
                                    std::string title = INSTALLER::GetRemotePkgTitle(client, it->path, &header);
                                    if (INSTALLER::InstallRemotePkg(url, &header, title) == 0)
                                        failed++;
                                    else
                                        success++;

                                    if (it != files.end())
                                    {
                                        sleep(1);
                                    }
                                }
                            }
                        }
                        else
                            skipped++;
                    }
                }
                else if (Util::EndsWith(path,".zip") || Util::EndsWith(path,".rar") || Util::EndsWith(path,".7z") ||
                        Util::EndsWith(path,".tar.xz") || Util::EndsWith(path,".tar.gz"))
                {
                    ArchiveEntry *entry = ZipUtil::GetPackageEntry(it->path, remoteclient);
                    if (entry != nullptr)
                    {
                        while (entry != nullptr)
                        {
                            snprintf(activity_message, 1023, "%s %s", lang_strings[STR_INSTALLING], entry->filename.c_str());

                            ArchivePkgInstallData *install_data = (ArchivePkgInstallData*) malloc(sizeof(ArchivePkgInstallData));
                            memset(install_data, 0, sizeof(ArchivePkgInstallData));

                            std::string install_pkg_path = std::string(temp_folder) + "/" + entry->filename;
                            FileStream *sp = CreateFileStream(install_pkg_path, INSTALL_ARCHIVE_PKG_SPLIT_SIZE/2);
                            
                            install_data->archive_entry = entry;
                            install_data->activity_id = activity.id;
                            install_data->split_file = sp;
                            install_data->stop_write_thread = false;

                            int res = pthread_create(&install_data->thread, NULL, ExtractArchivePkg, install_data);

                            if (INSTALLER::InstallArchivePkg(entry->filename, install_data) != 0)
                                success++;
                            else
                                failed++;

                            ArchiveEntry *previos = entry;
                            entry = ZipUtil::GetNextPackageEntry(entry);
                            free(previos);
                        }
                    }
                    else
                        skipped++;
                }
                else
                    skipped++;
            }
            else
                skipped++;

            sprintf(status_message, "%s %s = %d, %s = %d, %s = %d", lang_strings[STR_INSTALL],
                    lang_strings[STR_INSTALL_SUCCESS], success, lang_strings[STR_INSTALL_FAILED], failed,
                    lang_strings[STR_INSTALL_SKIPPED], skipped);

            if (failed > 0)
            {
                if (success > 0)
                    activity.partial(status_message);
                else
                    activity.fail(status_message);
            }
        }
    finish:
        RemotePool::Release(client, true);
        multi_selected_remote_files.clear();
        return NULL;
    }

    void InstallRemotePkgs()
    {
        sprintf(status_message, "%s", "");
        TransferJob *job = new TransferJob();
        job->files = TakeSelection(multi_selected_remote_files, selected_remote_file);

        int thread_res = pthread_create(&bk_activity_thid, NULL, InstallRemotePkgsThread, job);
        if (thread_res != 0)
        {
            multi_selected_remote_files.clear();
        }
    }

    void *ExtractArchivePkg(void *argp)
    {
        ssize_t len;
        char *buffer = (char*) malloc(ARCHIVE_TRANSFER_SIZE);

        ArchivePkgInstallData *install_data = (ArchivePkgInstallData*) argp;
        FileStream *sp = install_data->split_file;

        // Runs inside the install that owns it, so progress belongs there.
        const uint64_t activity_id = install_data->activity_id;
        uint64_t written = 0;
        uint64_t total = 0;
        if (install_data->archive_entry != nullptr)
        {
            Activities::Item(activity_id, install_data->archive_entry->filename);
            total = (uint64_t) archive_entry_size(install_data->archive_entry->entry);
        }

        /* loop over file contents and write to fd */
        sp->Open();
        while (!install_data->stop_write_thread)
        {
            len = archive_read_data(install_data->archive_entry->archive, buffer, ARCHIVE_TRANSFER_SIZE);

            if (len == 0)
                break;

            if (len < 0)
            {
                sprintf(status_message, "error archive_read_data('%s')", install_data->archive_entry->filename.c_str());
                break;
            }

            if (sp->Write(buffer, len) != len)
            {
                sprintf(status_message, "error write('%s')", install_data->archive_entry->filename.c_str());
                break;;
            }

            written += len;
            if (total > 0)
                Activities::Progress(activity_id, written, total);
        }

        sp->Close();
        free(buffer);
        return NULL;
    }

    void *DownloadSplitPkg(void *argp)
    {
        SplitPkgInstallData *install_data = (SplitPkgInstallData*) argp;
        FileStream *sp = install_data->split_file;

        Activities::Guard activity(ACTIVITY_INSTALL, STR_DOWNLOADING, ACTIVITY_NATIVE, ACTIVITY_REMOTE);
        Activities::Item(activity.id, install_data->path);

        /* loop over file contents and write to fd */
        sp->Open();
        install_data->remote_client->Get(sp, install_data->path);
        sp->Close();
        return NULL;
    }

    void *InstallLocalPkgsThread(void *argp)
    {
        TransferJob *job = static_cast<TransferJob *>(argp);
        Activities::Guard activity(ACTIVITY_INSTALL, STR_INSTALLING);
        int failed = 0;
        int success = 0;
        int skipped = 0;
        int ret;

        std::vector<DirEntry> files = job->files;
        delete job;

        for (std::vector<DirEntry>::iterator it = files.begin(); it != files.end(); ++it)
        {
            if (Activities::Stopped())
                break;
            sprintf(activity_message, "%s %s", lang_strings[STR_INSTALLING], it->name);
            Activities::Item(activity.id, it->name);

            if (!it->isDir)
            {
                std::string path = std::string(it->path);
                path = Util::ToLower(path);
                if (Util::EndsWith(path,".pkg"))
                {
                    pkg_header header;
                    memset(&header, 0, sizeof(header));
                    if (FS::Head(it->path, (void *)&header, sizeof(header)) == 0)
                        failed++;
                    else
                    {
                        if (BE32(header.pkg_magic) == PKG_CNT_MAGIC || BE32(header.pkg_magic) == PKG_FIH_MAGIC)
                        {
                            if ((ret = INSTALLER::InstallLocalPkg(it->path, &header)) <= 0)
                            {
                                if (ret == -1)
                                {
                                    sprintf(activity_message, "%s - %s", it->name, lang_strings[STR_INSTALL_FROM_DATA_MSG]);
                                    usleep(3000000);
                                }
                                else if (ret == -2)
                                {
                                    sprintf(activity_message, "%s - %s", it->name, lang_strings[STR_ALREADY_INSTALLED_MSG]);
                                    usleep(3000000);
                                }
                                failed++;
                            }
                            else
                                success++;
                        }
                        else
                            skipped++;
                    }
                }
                else if (Util::EndsWith(path,".zip") || Util::EndsWith(path,".rar") || Util::EndsWith(path,".7z") ||
                         Util::EndsWith(path,".tar.xz") || Util::EndsWith(path,".tar.gz") || Util::EndsWith(path,".tar.bz2") )
                {
                    ArchiveEntry *entry = ZipUtil::GetPackageEntry(it->path);
                    if (entry != nullptr)
                    {
                        while (entry != nullptr)
                        {
                            ArchivePkgInstallData *install_data = (ArchivePkgInstallData*) malloc(sizeof(ArchivePkgInstallData));
                            memset(install_data, 0, sizeof(ArchivePkgInstallData));

                            std::string install_pkg_path = std::string(temp_folder) + "/" + entry->filename;
                            FileStream *sp = CreateFileStream(install_pkg_path, INSTALL_ARCHIVE_PKG_SPLIT_SIZE);
                            
                            install_data->archive_entry = entry;
                            install_data->activity_id = activity.id;
                            install_data->split_file = sp;
                            install_data->stop_write_thread = false;

                            int res = pthread_create(&install_data->thread, NULL, ExtractArchivePkg, install_data);

                            if (INSTALLER::InstallArchivePkg(entry->filename, install_data) != 0)
                                success++;
                            else
                                failed++;

                            ArchiveEntry *previous = entry;
                            entry = ZipUtil::GetNextPackageEntry(entry);
                            free(previous);
                        }
                    }
                    else
                        skipped++;
                }
                else
                    skipped++;
            }
            else
                skipped++;

            sprintf(status_message, "%s %s = %d, %s = %d, %s = %d", lang_strings[STR_INSTALL],
                    lang_strings[STR_INSTALL_SUCCESS], success, lang_strings[STR_INSTALL_FAILED], failed,
                    lang_strings[STR_INSTALL_SKIPPED], skipped);

            if (failed > 0)
            {
                if (success > 0)
                    activity.partial(status_message);
                else
                    activity.fail(status_message);
            }
        }
        multi_selected_local_files.clear();
        return NULL;
    }

    void InstallLocalPkgs()
    {
        sprintf(status_message, "%s", "");
        TransferJob *job = new TransferJob();
        job->files = TakeSelection(multi_selected_local_files, selected_local_file);

        int thread_res = pthread_create(&bk_activity_thid, NULL, InstallLocalPkgsThread, job);
        if (thread_res != 0)
        {
            multi_selected_local_files.clear();
        }
    }

    void *ExtractZipThread(void *argp)
    {
        ArchiveJob *job = static_cast<ArchiveJob *>(argp);
        Activities::Guard activity(ACTIVITY_EXTRACT, STR_EXTRACTING);
        std::string target = job->target;
        FS::MkDirs(target);
        std::vector<DirEntry> files = job->files;
        delete job;

        for (std::vector<DirEntry>::iterator it = files.begin(); it != files.end(); ++it)
        {
            if (Activities::Stopped())
                break;

            Activities::Item(activity.id, it->name);
            if (!it->isDir)
            {
                int ret = ZipUtil::Extract(*it, target.c_str());
                if (ret == 0)
                {
                    sprintf(status_message, "%s %s", lang_strings[STR_FAILED_TO_EXTRACT], it->name);
                    usleep(100000);
                }
            }
        }
        multi_selected_local_files.clear();
        selected_action = ACTION_REFRESH_LOCAL_FILES;
        return NULL;
    }

    void ExtractLocalZips()
    {
        sprintf(status_message, "%s", "");
        ArchiveJob *job = new ArchiveJob();
        job->files = TakeSelection(multi_selected_local_files, selected_local_file);
        job->target = std::string(extract_zip_folder);

        int res = pthread_create(&bk_activity_thid, NULL, ExtractZipThread, job);
        if (res != 0)
        {
            delete job;
            multi_selected_local_files.clear();
        }
    }

    void *ExtractRemoteZipThread(void *argp)
    {
        ArchiveJob *job = static_cast<ArchiveJob *>(argp);
        Activities::Guard activity(ACTIVITY_EXTRACT, STR_EXTRACTING, ACTIVITY_NATIVE, ACTIVITY_REMOTE);
        FS::MkDirs(job->target);
        std::vector<DirEntry> files = job->files;
        std::string target = job->target;
        delete job;

        RemoteClient *client = RemotePool::Acquire(*remote_settings);

        if (client != nullptr)
        {
            for (std::vector<DirEntry>::iterator it = files.begin(); it != files.end(); ++it)
            {
                if (Activities::Stopped())
                    break;
                
                if (!it->isDir)
                {
                    Activities::Item(activity.id, it->name);
                    int ret = ZipUtil::Extract(*it, target.c_str(), client);
                    if (ret == 0)
                    {
                        sprintf(status_message, "%s %s", lang_strings[STR_FAILED_TO_EXTRACT], it->name);
                        usleep(100000);
                    }
                }
            }
        }
        else
        {
            sprintf(status_message, "%s", lang_strings[STR_CONNECTION_CLOSE_ERR_MSG]);
        }

        RemotePool::Release(client, true);
        multi_selected_remote_files.clear();
        selected_action = ACTION_REFRESH_LOCAL_FILES;
        return NULL;
    }

    void ExtractRemoteZips()
    {
        sprintf(status_message, "%s", "");
        ArchiveJob *job = new ArchiveJob();
        job->files = TakeSelection(multi_selected_remote_files, selected_remote_file);
        job->target = std::string(extract_zip_folder);

        int res = pthread_create(&bk_activity_thid, NULL, ExtractRemoteZipThread, job);
        if (res != 0)
        {
            delete job;
            multi_selected_remote_files.clear();
        }
    }

    void *MakeZipThread(void *argp)
    {
        ArchiveJob *job = static_cast<ArchiveJob *>(argp);
        Activities::Guard activity(ACTIVITY_COMPRESS, STR_COMPRESSING);

        zipFile zf = zipOpen64(job->target.c_str(), APPEND_STATUS_CREATE);
        if (zf != NULL)
        {
            std::vector<DirEntry> files = job->files;

            for (std::vector<DirEntry>::iterator it = files.begin(); it != files.end(); ++it)
            {
                if (Activities::Stopped())
                    break;
                
                Activities::Item(activity.id, it->path);
                int res = ZipUtil::ZipAddPath(zf, it->path, strlen(local_directory) + 1, Z_DEFAULT_COMPRESSION);
                if (res <= 0)
                {
                    sprintf(status_message, "%s", lang_strings[STR_ERROR_CREATE_ZIP]);
                    usleep(1000000);
                }
            }
            zipClose(zf, NULL);
        }
        else
        {
            sprintf(status_message, "%s", lang_strings[STR_ERROR_CREATE_ZIP]);
        }
        multi_selected_local_files.clear();
        selected_action = ACTION_REFRESH_LOCAL_FILES;
        return NULL;
    }

    void MakeLocalZip()
    {
        sprintf(status_message, "%s", "");
        ArchiveJob *job = new ArchiveJob();
        job->files = TakeSelection(multi_selected_local_files, selected_local_file);
        job->target = std::string(zip_file_path);

        int res = pthread_create(&bk_activity_thid, NULL, MakeZipThread, job);
        if (res != 0)
        {
            delete job;
            multi_selected_local_files.clear();
        }
    }

    void *InstallLocalUrlPkgThread(void *argp)
    {
        prev_tick = Util::GetTick();
        sprintf(status_message, "%s", "");
        pkg_header header;
        char filename[2000];
        sprintf(filename, "%s/%lu.pkg", temp_folder, prev_tick);

        std::string full_url = std::string(install_pkg_url.url);
        FileHost *filehost = FileHost::getFileHost(full_url, install_pkg_url.enable_alldebrid, install_pkg_url.enable_realdebrid);
        if (!filehost->IsValidUrl())
        {
            sprintf(status_message, "%s", lang_strings[STR_FAIL_TO_OBTAIN_GG_DL_MSG]);
            return NULL;
        }
        full_url = filehost->GetDownloadUrl();
        delete(filehost);

        if (full_url.empty())
        {
            sprintf(status_message, "%s", lang_strings[STR_FAIL_TO_OBTAIN_GG_DL_MSG]);
            return NULL;
        }
        size_t scheme_pos = full_url.find_first_of("://");
        size_t path_pos = full_url.find_first_of("/", scheme_pos + 3);
        std::string host = full_url.substr(0, path_pos);
        std::string path = full_url.substr(path_pos);

        BaseClient tmp_client;
        tmp_client.Connect(host.c_str(), install_pkg_url.username, install_pkg_url.password);

        sprintf(activity_message, "%s URL to %s", lang_strings[STR_DOWNLOADING], filename);
        int s = sizeof(pkg_header);
        memset(&header, 0, s);

        uint64_t url_file_size = 0;
        int ret = tmp_client.Size(path, &url_file_size);
        if (ret == 0)
        {
            sprintf(status_message, "%s", tmp_client.LastResponse());
            tmp_client.Quit();
            return NULL;
        }

        int is_performed = tmp_client.Get(filename, path);

        if (is_performed == 0)
        {
            sprintf(status_message, "%s - %s", lang_strings[STR_FAILED], tmp_client.LastResponse());
            tmp_client.Quit();
            return NULL;
        }
        tmp_client.Quit();

        FILE *in = FS::OpenRead(filename);
        if (in == NULL)
        {
            sprintf(status_message, "%s - Error opening temp pkg file - %s", lang_strings[STR_FAILED], filename);
            return NULL;
        }

        FS::Read(in, (void *)&header, s);
        FS::Close(in);
        if (BE32(header.pkg_magic) == PKG_CNT_MAGIC || BE32(header.pkg_magic) == PKG_FIH_MAGIC)
        {
            int ret;
            if ((ret = INSTALLER::InstallLocalPkg(filename, &header, true)) != 1)
            {
                if (ret == -1)
                {
                    sprintf(activity_message, "%s", lang_strings[STR_INSTALL_FROM_DATA_MSG]);
                    usleep(3000000);
                }
                else if (ret == -2)
                {
                    sprintf(activity_message, "%s", lang_strings[STR_ALREADY_INSTALLED_MSG]);
                    usleep(3000000);
                }
                else if (ret == -3)
                {
                    sprintf(activity_message, "%s", lang_strings[STR_FAIL_INSTALL_TMP_PKG_MSG]);
                    usleep(5000000);
                }
                if (ret != -3 && auto_delete_tmp_pkg)
                    FS::Rm(filename);
            }
        }

        return NULL;
    }

    void *InstallRpiUrlPkgThread(void *argp)
    {
        json_object *params = json_object_new_object();
        JsonObjectRef params_ref(params);
        json_object_object_add(params, "url", json_object_new_string(install_pkg_url.url));
        json_object_object_add(params, "use_alldebrid", json_object_new_boolean(install_pkg_url.enable_alldebrid));
        json_object_object_add(params, "use_realdebrid", json_object_new_boolean(install_pkg_url.enable_realdebrid));
        json_object_object_add(params, "use_disk_cache", json_object_new_boolean(install_pkg_url.enable_disk_cache));

        const char *params_str = json_object_to_json_string(params);

        char host[128];
        sprintf(host, "http://127.0.0.1:%d", http_server_port);

        CHTTPClient::HttpResponse res;
        CHTTPClient::HeadersMap headers;
        CHTTPClient tmp_client([](const std::string& log){});
        tmp_client.InitSession(true, CHTTPClient::SettingsFlag::NO_FLAGS);
        tmp_client.SetCertificateFile(CACERT_FILE);
        headers["Content-Type"] = "application/json";

        if (tmp_client.Post("/__local__/install_url", headers, params_str, res))
        {
            if (HTTP_SUCCESS(res.iCode))
            {
                json_object *jobj = json_tokener_parse(res.strBody.data());
                JsonObjectRef jobj_ref(jobj);
                if (jobj != nullptr)
                {
                    json_object *result = json_object_object_get(jobj, "result");
                    if (result != nullptr)
                    {
                        bool success = json_object_get_boolean(json_object_object_get(result, "success"));
                        if (!success)
                        {
                            std::string error_message = JsonGetString(result, "error");
                            snprintf(status_message, 1024, "%s", error_message.c_str());
                        }
                    }
                }
                else
                {
                }
            }
            else
            {
            }
        }

        return NULL;
    }

    void InstallUrlPkg()
    {
        int res;
        sprintf(status_message, "%s", "");

        if (!install_pkg_url.enable_rpi)
            res = pthread_create(&bk_activity_thid, NULL, InstallLocalUrlPkgThread, NULL);
        else
            res = pthread_create(&bk_activity_thid, NULL, InstallRpiUrlPkgThread, NULL);

        if (res != 0)
        {
        }
    }

    RemoteClient *CreateRemoteClient(RemoteSettings *settings)
    {
        if (strncmp(settings->server, "https://", 8) == 0 || strncmp(settings->server, "http://", 7) == 0)
        {
            if (strcmp(settings->http_server_type, HTTP_SERVER_APACHE) == 0)
                return new ApacheClient();
            if (strcmp(settings->http_server_type, HTTP_SERVER_MS_IIS) == 0)
                return new IISClient();
            if (strcmp(settings->http_server_type, HTTP_SERVER_NGINX) == 0)
                return new NginxClient();
            if (strcmp(settings->http_server_type, HTTP_SERVER_NPX_SERVE) == 0)
                return new NpxServeClient();
            if (strcmp(settings->http_server_type, HTTP_SERVER_RCLONE) == 0)
                return new RCloneClient();
            if (strcmp(settings->http_server_type, HTTP_SERVER_ARCHIVEORG) == 0)
                return new ArchiveOrgClient();
            if (strcmp(settings->http_server_type, HTTP_SERVER_MYRIENT) == 0)
                return new MyrientClient();
            if (strcmp(settings->http_server_type, HTTP_SERVER_GITHUB) == 0)
                return new GithubClient();
            return nullptr;
        }

        if (strncmp(settings->server, "webdavs://", 10) == 0 || strncmp(settings->server, "webdav://", 9) == 0)
            return new WebDAVClient();

        if (strncmp(settings->server, "smb://", 6) == 0)
            return new SmbClient();

        if (strncmp(settings->server, "ftp://", 6) == 0)
        {
            FtpClient *client = new FtpClient();
            client->SetConnmode(FtpClient::pasv);
            client->SetCallbackBytes(1);
            client->SetCallbackXferFunction(FtpCallback);
            client->SetCallbackArg(client);
            return client;
        }

        if (strncmp(settings->server, "sftp://", 7) == 0)
            return new SFTPClient();

        if (strncmp(settings->server, "nfs://", 6) == 0)
            return new NfsClient();

        return nullptr;
    }

    void Connect()
    {
        CONFIG::SaveConfig();

        remoteclient = CreateRemoteClient(remote_settings);
        if (remoteclient == nullptr)
        {
            sprintf(status_message, "%s", lang_strings[STR_PROTOCOL_NOT_SUPPORTED]);
            selected_action = ACTION_NONE;
            return;
        }

        if (remoteclient->Connect(remote_settings->server, remote_settings->username, remote_settings->password))
        {
            RefreshRemoteFiles(false);

            if (remoteclient->clientType() == CLIENT_TYPE_FTP)
            {
                int res = pthread_create(&ftp_keep_alive_thid, NULL, KeepAliveThread, NULL);
            }
        }
        else
        {
            sprintf(status_message, "%s - %s", lang_strings[STR_FAIL_TIMEOUT_MSG], remoteclient->LastResponse());
            if (remoteclient != nullptr)
            {
                remoteclient->Quit();
                delete remoteclient;
                remoteclient = nullptr;
            }
        }
        selected_action = ACTION_NONE;
    }

    void Disconnect()
    {
        // Pooled connections belong to the session being closed.
        RemotePool::Clear();

        if (remoteclient != nullptr)
        {
            if (remoteclient->IsConnected())
                remoteclient->Quit();
            multi_selected_remote_files.clear();
            remote_files.clear();
            sprintf(status_message, "%s", "");
            delete remoteclient;
            remoteclient = nullptr;
        }
    }

    void SelectAllLocalFiles()
    {
        for (int i = 0; i < local_files.size(); i++)
        {
            if (strcmp(local_files[i].name, "..") != 0)
                multi_selected_local_files.insert(local_files[i]);
        }
    }

    void SelectAllRemoteFiles()
    {
        for (int i = 0; i < remote_files.size(); i++)
        {
            if (strcmp(remote_files[i].name, "..") != 0)
                multi_selected_remote_files.insert(remote_files[i]);
        }
    }

    void *KeepAliveThread(void *argp)
    {
        long idle;
        while (remoteclient != nullptr && remoteclient->clientType() == CLIENT_TYPE_FTP && remoteclient->IsConnected())
        {
            FtpClient *client = (FtpClient *)remoteclient;
            idle = client->GetIdleTime();
            if (idle > 60000000)
            {
                if (!client->Ping())
                {
                    client->Quit();
                    sprintf(status_message, "%s", lang_strings[STR_REMOTE_TERM_CONN_MSG]);
                    return NULL;
                }
            }
            usleep(500000);
        }
        return NULL;
    }

    int CopyOrMoveLocalFile(const char *src, const char *dest, bool isCopy)
    {
        int ret;
        if (Activities::OverwriteType() == OVERWRITE_PROMPT && FS::FileExists(dest))
        {
            PromptOverwrite(dest);
        }
        else if (Activities::OverwriteType() == OVERWRITE_NONE && FS::FileExists(dest))
        {
            confirm_state = CONFIRM_NO;
        }
        else
        {
            confirm_state = CONFIRM_YES;
        }

        if (confirm_state == CONFIRM_YES)
        {
            prev_tick = Util::GetTick();
            if (isCopy)
                return FS::Copy(src, dest);
            else
                return FS::Move(src, dest);
        }

        return 1;
    }

    int CopyOrMove(const DirEntry &src, const char *dest, bool isCopy)
    {
        if (Activities::Stopped())
            return 1;

        int ret;
        if (src.isDir)
        {
            int err;
            if (!isCopy && !FS::FolderExists(dest))
            {
                errno = 0;
                int ret = rename(src.path, dest);
                if (ret != 0 && errno != EXDEV && errno != EEXIST)
                {
                    return 0;
                }
            }

            std::vector<DirEntry> entries = FS::ListDir(src.path, &err);
            FS::MkDirs(dest);
            for (int i = 0; i < entries.size(); i++)
            {
                if (Activities::Stopped())
                    return 1;

                int path_length = strlen(dest) + strlen(entries[i].name) + 2;
                char *new_path = (char *)malloc(path_length);
                snprintf(new_path, path_length, "%s%s%s", dest, FS::hasEndSlash(dest) ? "" : "/", entries[i].name);

                if (entries[i].isDir)
                {
                    if (strcmp(entries[i].name, "..") == 0)
                    {
                        free(new_path);
                        continue;
                    }

                    ret = CopyOrMove(entries[i], new_path, isCopy);
                    if (ret <= 0)
                    {
                        free(new_path);
                        return ret;
                    }
                }
                else
                {
                    snprintf(activity_message, 1024, "%s %s", isCopy ? lang_strings[STR_COPYING] : lang_strings[STR_MOVING], entries[i].path);
                    prev_tick = Util::GetTick();
                    ret = CopyOrMoveLocalFile(entries[i].path, new_path, isCopy);
                    if (ret <= 0)
                    {
                        sprintf(status_message, "%s %s", isCopy ? lang_strings[STR_FAIL_COPY_MSG] : lang_strings[STR_FAIL_MOVE_MSG], entries[i].path);
                        free(new_path);
                        return ret;
                    }
                }
                free(new_path);
            }

            if (!isCopy && FS::FolderExists(src.path))
            {
                FS::RmDir(src.path);
            }
        }
        else
        {
            int path_length = strlen(dest) + strlen(src.name) + 2;
            char *new_path = (char *)malloc(path_length);
            snprintf(new_path, path_length, "%s%s%s", dest, FS::hasEndSlash(dest) ? "" : "/", src.name);
            snprintf(activity_message, 1024, "%s %s", isCopy ? lang_strings[STR_COPYING] : lang_strings[STR_MOVING], src.name);
            ret = CopyOrMoveLocalFile(src.path, new_path, isCopy);
            if (ret <= 0)
            {
                free(new_path);
                sprintf(status_message, "%s %s", isCopy ? lang_strings[STR_FAIL_COPY_MSG] : lang_strings[STR_FAIL_MOVE_MSG], src.name);
                return 0;
            }
            free(new_path);
        }
        return 1;
    }

    void *MoveLocalFilesThread(void *argp)
    {
        Activities::Guard activity(ACTIVITY_MOVE, STR_MOVING);
        TransferJob *job = static_cast<TransferJob *>(argp);
        std::vector<DirEntry> files = job->files;
        std::string target = job->target;
        delete job;

        int failed = 0;
        int total = 0;
        for (std::vector<DirEntry>::iterator it = files.begin(); it != files.end(); ++it)
        {
            if (Activities::Stopped())
                break;

            snprintf(activity_message, 1024, "%s %s", lang_strings[STR_MOVING], it->path);
            Activities::Item(activity.id, it->path);
            if (strcmp(it->directory, target.c_str()) == 0)
                continue;

            if (it->isDir)
            {
                if (strncmp(target.c_str(), it->path, strlen(it->path)) == 0)
                {
                    sprintf(status_message, "%s", lang_strings[STR_CANT_MOVE_TO_SUBDIR_MSG]);
                    continue;
                }
                char new_dir[512];
                sprintf(new_dir, "%s%s%s", target.c_str(), FS::hasEndSlash(target.c_str()) ? "" : "/", it->name);
                total++;
                if (CopyOrMove(*it, new_dir, false) == 0)
                    failed++;
            }
            else
            {
                total++;
                if (CopyOrMove(*it, target.c_str(), false) == 0)
                    failed++;
            }
        }
        if (failed > 0)
        {
            if (failed < total)
                activity.partial(status_message);
            else
                activity.fail(status_message);
        }

        local_paste_files.clear();
        selected_action = ACTION_REFRESH_LOCAL_FILES;
        return NULL;
    }

    void MoveLocalFiles()
    {
        sprintf(status_message, "%s", "");
        TransferJob *job = new TransferJob();
        job->files.assign(local_paste_files.begin(), local_paste_files.end());
        job->target = local_directory;

        int thread_res = pthread_create(&bk_activity_thid, NULL, MoveLocalFilesThread, job);
        if (thread_res != 0)
        {
            local_paste_files.clear();
        }
    }

    static bool LocalPathExists(const std::string &path)
    {
        return FS::FileExists(path.c_str()) || FS::IsFolder(path.c_str()) == 1;
    }

    // "name" -> "name (2)", "file.txt" -> "file (2).txt"
    static std::string UniqueLocalName(const std::string &dir, const std::string &name)
    {
        std::string sep = FS::hasEndSlash(dir.c_str()) ? "" : "/";
        if (!LocalPathExists(dir + sep + name))
            return name;

        size_t dot = name.find_last_of('.');
        std::string base = (dot == std::string::npos || dot == 0) ? name : name.substr(0, dot);
        std::string ext = (dot == std::string::npos || dot == 0) ? "" : name.substr(dot);

        for (int index = 2; index < 10000; index++)
        {
            char candidate[512];
            snprintf(candidate, sizeof(candidate), "%s (%d)%s", base.c_str(), index, ext.c_str());
            if (!LocalPathExists(dir + sep + candidate))
                return std::string(candidate);
        }

        return name;
    }

    void *CopyLocalFilesThread(void *argp)
    {
        Activities::Guard activity(ACTIVITY_COPY, STR_COPYING);
        TransferJob *job = static_cast<TransferJob *>(argp);
        std::vector<DirEntry> files = job->files;
        std::string target = job->target;
        delete job;

        int failed = 0;
        int total = 0;
        for (std::vector<DirEntry>::iterator it = files.begin(); it != files.end(); ++it)
        {
            if (Activities::Stopped())
                break;

            snprintf(activity_message, 1024, "%s %s", lang_strings[STR_COPYING], it->path);
            Activities::Item(activity.id, it->path);
            if (it->isDir)
            {
                if (strncmp(target.c_str(), it->path, strlen(it->path)) == 0)
                {
                    sprintf(status_message, "%s", lang_strings[STR_CANT_COPY_TO_SUBDIR_MSG]);
                    continue;
                }
                // UniqueLocalName returns the original name when nothing collides.
                std::string name = UniqueLocalName(target.c_str(), it->name);
                char new_dir[512];
                sprintf(new_dir, "%s%s%s", target.c_str(), FS::hasEndSlash(target.c_str()) ? "" : "/", name.c_str());
                total++;
                if (CopyOrMove(*it, new_dir, true) == 0)
                    failed++;
            }
            else
            {
                std::string target = std::string(target.c_str()) +
                    (FS::hasEndSlash(target.c_str()) ? "" : "/") + UniqueLocalName(target.c_str(), it->name);
                total++;
                if (CopyOrMoveLocalFile(it->path, target.c_str(), true) == 0)
                    failed++;
            }
        }
        if (failed > 0)
        {
            if (failed < total)
                activity.partial(status_message);
            else
                activity.fail(status_message);
        }

        local_paste_files.clear();
        selected_action = ACTION_REFRESH_LOCAL_FILES;
        return NULL;
    }

    void CopyLocalFiles()
    {
        sprintf(status_message, "%s", "");
        TransferJob *job = new TransferJob();
        job->files.assign(local_paste_files.begin(), local_paste_files.end());
        job->target = local_directory;

        int thread_res = pthread_create(&bk_activity_thid, NULL, CopyLocalFilesThread, job);
        if (thread_res != 0)
        {
            local_paste_files.clear();
        }
    }

    int CopyOrMoveRemoteFile(const std::string &src, const std::string &dest, bool isCopy, RemoteClient *client)
    {
        int ret;
        if (Activities::OverwriteType() == OVERWRITE_PROMPT && client->FileExists(dest))
        {
            PromptOverwrite(dest.c_str());
        }
        else if (Activities::OverwriteType() == OVERWRITE_NONE && client->FileExists(dest))
        {
            confirm_state = CONFIRM_NO;
        }
        else
        {
            confirm_state = CONFIRM_YES;
        }

        if (confirm_state == CONFIRM_YES)
        {
            prev_tick = Util::GetTick();
            if (isCopy)
                return client->Copy(src, dest);
            else
                return client->Move(src, dest);
        }

        return 1;
    }

    void *MoveRemoteFilesThread(void *argp)
    {
        Activities::Guard activity(ACTIVITY_MOVE, STR_MOVING, ACTIVITY_NATIVE, ACTIVITY_REMOTE);
        TransferJob *job = static_cast<TransferJob *>(argp);
        std::vector<DirEntry> files = job->files;
        std::string target = job->target;
        delete job;
        int failed = 0;
        int total = 0;


        RemoteClient *client = RemotePool::Acquire(*remote_settings);

        if (client == nullptr)
        {
            RemotePool::Release(client, true);
            return NULL;
        }
        for (std::vector<DirEntry>::iterator it = files.begin(); it != files.end(); ++it)
        {
            if (Activities::Stopped())
                break;

            if (strcmp(it->directory, target.c_str()) == 0)
                continue;

            char new_path[1024];
            sprintf(new_path, "%s%s%s", target.c_str(), FS::hasEndSlash(target.c_str()) ? "" : "/", it->name);
            if (it->isDir)
            {
                if (strncmp(target.c_str(), it->path, strlen(it->path)) == 0)
                {
                    sprintf(status_message, "%s", lang_strings[STR_CANT_MOVE_TO_SUBDIR_MSG]);
                    continue;
                }
            }

            snprintf(activity_message, 1024, "%s %s", lang_strings[STR_MOVING], it->path);
            Activities::Item(activity.id, it->path);
            total++;
            int res = CopyOrMoveRemoteFile(it->path, new_path, false, client);
            if (res == 0)
                failed++;
                sprintf(status_message, "%s - %s", it->name, lang_strings[STR_FAIL_COPY_MSG]);
        }
        RemotePool::Release(client, true);
        if (failed > 0)
        {
            if (failed < total)
                activity.partial(status_message);
            else
                activity.fail(status_message);
        }

        remote_paste_files.clear();
        selected_action = ACTION_REFRESH_REMOTE_FILES;
        return NULL;
    }

    void MoveRemoteFiles()
    {
        sprintf(status_message, "%s", "");
        TransferJob *job = new TransferJob();
        job->files.assign(remote_paste_files.begin(), remote_paste_files.end());
        job->target = remote_directory;

        int thread_res = pthread_create(&bk_activity_thid, NULL, MoveRemoteFilesThread, job);
        if (thread_res != 0)
        {
            remote_paste_files.clear();
        }
    }

    int CopyRemotePath(const DirEntry &src, const char *dest, RemoteClient *client)
    {
        if (Activities::Stopped())
            return 1;

        int ret;
        if (src.isDir)
        {
            int err;
            std::vector<DirEntry> entries = client->ListDir(src.path);
            client->Mkdir(dest);
            for (int i = 0; i < entries.size(); i++)
            {
                if (Activities::Stopped())
                    return 1;

                int path_length = strlen(dest) + strlen(entries[i].name) + 2;
                char *new_path = (char *)malloc(path_length);
                snprintf(new_path, path_length, "%s%s%s", dest, FS::hasEndSlash(dest) ? "" : "/", entries[i].name);

                if (entries[i].isDir)
                {
                    if (strcmp(entries[i].name, "..") == 0)
                    {
                        free(new_path);
                        continue;
                    }

                    client->Mkdir(new_path);
                    ret = CopyRemotePath(entries[i], new_path, client);
                    if (ret <= 0)
                    {
                        free(new_path);
                        return ret;
                    }
                }
                else
                {
                    snprintf(activity_message, 1024, "%s %s", lang_strings[STR_COPYING], entries[i].path);
                    prev_tick = Util::GetTick();
                    ret = CopyOrMoveRemoteFile(entries[i].path, new_path, true, client);
                    if (ret <= 0)
                    {
                        sprintf(status_message, "%s %s", lang_strings[STR_FAIL_COPY_MSG], entries[i].path);
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
            snprintf(activity_message, 1024, "%s %s", lang_strings[STR_COPYING], src.name);
            ret = CopyOrMoveRemoteFile(src.path, new_path, true, client);
            if (ret <= 0)
            {
                free(new_path);
                sprintf(status_message, "%s %s", lang_strings[STR_FAIL_COPY_MSG], src.name);
                return 0;
            }
            free(new_path);
        }
        return 1;
    }

    void *CopyRemoteFilesThread(void *argp)
    {
        Activities::Guard activity(ACTIVITY_COPY, STR_COPYING, ACTIVITY_NATIVE, ACTIVITY_REMOTE);
        TransferJob *job = static_cast<TransferJob *>(argp);
        std::vector<DirEntry> files = job->files;
        std::string target = job->target;
        delete job;
        int failed = 0;
        int total = 0;


        RemoteClient *client = RemotePool::Acquire(*remote_settings);

        if (client == nullptr)
        {
            RemotePool::Release(client, true);
            return NULL;
        }
        for (std::vector<DirEntry>::iterator it = files.begin(); it != files.end(); ++it)
        {
            if (Activities::Stopped())
                break;

            snprintf(activity_message, 1024, "%s %s", lang_strings[STR_COPYING], it->path);
            Activities::Item(activity.id, it->path);
            if (strcmp(it->directory, target.c_str()) == 0)
                continue;

            char new_path[1024];
            sprintf(new_path, "%s%s%s", target.c_str(), FS::hasEndSlash(target.c_str()) ? "" : "/", it->name);
            if (it->isDir)
            {
                if (strncmp(target.c_str(), it->path, strlen(it->path)) == 0)
                {
                    sprintf(status_message, "%s", lang_strings[STR_CANT_COPY_TO_SUBDIR_MSG]);
                    continue;
                }
                total++;
                int res = CopyRemotePath(*it, new_path, client);
                if (res == 0)
                    failed++;
                    sprintf(status_message, "%s - %s", it->name, lang_strings[STR_FAIL_COPY_MSG]);
            }
            else
            {
                total++;
                int res = CopyRemotePath(*it, target.c_str(), client);
                if (res == 0)
                    failed++;
                    sprintf(status_message, "%s - %s", it->name, lang_strings[STR_FAIL_COPY_MSG]);
            }
        }
        RemotePool::Release(client, true);
        if (failed > 0)
        {
            if (failed < total)
                activity.partial(status_message);
            else
                activity.fail(status_message);
        }

        remote_paste_files.clear();
        selected_action = ACTION_REFRESH_REMOTE_FILES;
        return NULL;
    }

    void CopyRemoteFiles()
    {
        sprintf(status_message, "%s", "");
        TransferJob *job = new TransferJob();
        job->files.assign(remote_paste_files.begin(), remote_paste_files.end());
        job->target = remote_directory;

        int thread_res = pthread_create(&bk_activity_thid, NULL, CopyRemoteFilesThread, job);
        if (thread_res != 0)
        {
            remote_paste_files.clear();
        }
    }

    int DownloadAndInstallPkg(const std::string &filename, pkg_header *header, RemoteClient *client)
    {
        char local_file[2000];
        uint64_t tick = Util::GetTick();
        sprintf(local_file, "%s/%lu.pkg", temp_folder, tick);

        sprintf(activity_message, "%s %s to %s", lang_strings[STR_DOWNLOADING], filename.c_str(), local_file);
        uint64_t install_size = 0;
        client->Size(filename, &install_size);
        prev_tick = Util::GetTick();

        int ret = client->Get(local_file, filename);
        if (ret == 0)
            return 0;

        return INSTALLER::InstallLocalPkg(local_file, header, true);
    }

    void CreateLocalFile(char *filename)
    {
        std::string new_file = FS::GetPath(local_directory, filename);
        std::string temp_file = new_file;
        int i = 1;
        while (true)
        {
            if (!FS::FileExists(temp_file))
                break;
            temp_file = new_file + "." + std::to_string(i);
            i++;
        }
        FILE* f = FS::Create(temp_file);
        FS::Close(f);
        RefreshLocalFiles(false);
        sprintf(local_file_to_select, "%s", temp_file.c_str());
    }

    void CreateRemoteFile(char *filename)
    {
        std::string new_file = FS::GetPath(remote_directory, filename);
        std::string temp_file = new_file;
        int i = 1;
        while (true)
        {
            if (!remoteclient->FileExists(temp_file))
                break;
            temp_file = new_file + "." + std::to_string(i);
            i++;
        }

        uint64_t tick = Util::GetTick();
        std::string local_tmp = std::string(DATA_PATH) + "/" + std::to_string(tick);
        FILE *f = FS::Create(local_tmp);
        FS::Close(f);
        remoteclient->Put(local_tmp, temp_file);
        FS::Rm(local_tmp);
        RefreshRemoteFiles(false);
        sprintf(remote_file_to_select, "%s", temp_file.c_str());
    }

    void RestartServer()
    {
        StopServer();
        sleep(2);
        INSTALLER::StartEzRemoteServer();
        sleep(2);
    }

    void StopServer()
    {
        CHTTPClient::HttpResponse res;
        CHTTPClient::HeadersMap headers;
        CHTTPClient tmp_client([](const std::string& log){});
        tmp_client.InitSession(true, CHTTPClient::SettingsFlag::NO_FLAGS);
        tmp_client.SetCertificateFile(CACERT_FILE);
        tmp_client.SetTimeout(1);

        tmp_client.Get("http://localhost:" + std::to_string(http_int_server_port) + "/stop", headers, res);
    }

    int ClearDownloadHistory()
    {
        CHTTPClient::HttpResponse res;
        CHTTPClient::HeadersMap headers;
        CHTTPClient tmp_client([](const std::string& log){});
        tmp_client.InitSession(true, CHTTPClient::SettingsFlag::NO_FLAGS);
        tmp_client.SetCertificateFile(CACERT_FILE);
        tmp_client.SetTimeout(2);
        headers["Content-Type"] = "application/json";

        std::string url = std::string("http://localhost:") + std::to_string(http_int_server_port) + "/clear_download_history";
        if (tmp_client.Post(url, headers, "{}", res) && HTTP_SUCCESS(res.iCode))
        {
            bg_download_progress.clear();
            return 1;
        }

        return 0;
    }

    void GetBackgroundDownloadProgress()
    {
        CHTTPClient::HttpResponse res;
        CHTTPClient::HeadersMap headers;
        CHTTPClient tmp_client([](const std::string& log){});
        tmp_client.InitSession(true, CHTTPClient::SettingsFlag::NO_FLAGS);
        tmp_client.SetCertificateFile(CACERT_FILE);
        tmp_client.SetTimeout(1);

        if (tmp_client.Get("http://localhost:" + std::to_string(http_int_server_port) + "/get_download_state", headers, res))
        {
			if (HTTP_SUCCESS(res.iCode))
			{
                bg_download_progress.clear();
                
                json_object *jobj = json_tokener_parse(res.strBody.data());
                JsonObjectRef jobj_ref(jobj);
                if (jobj != nullptr && json_object_get_type(jobj) == json_type_array)
                {
                    struct array_list *progress_list = json_object_get_array(jobj);

                    for (size_t idx = 0; idx < progress_list->length; ++idx)
                    {
                        DownloadProgress progress;

                        json_object *progress_obj = (json_object *)array_list_get_idx(progress_list, idx);
                        progress.path = JsonGetString(progress_obj, "path");
                        progress.bytes_transfered = json_object_get_uint64(json_object_object_get(progress_obj, "bytes_transfered"));
                        progress.file_size = json_object_get_uint64(json_object_object_get(progress_obj, "file_size"));
                        int state = json_object_get_int(json_object_object_get(progress_obj, "state"));
                        if (state < 0 || state >= (int)(sizeof(state_strings) / sizeof(state_strings[0])))
                            state = 0;
                        progress.state = state_strings[state];
                        progress.timestamp = json_object_get_uint64(json_object_object_get(progress_obj, "timestamp"));

                        bg_download_progress.push_back(progress);
                    }
                }
            }
			else
			{
                snprintf(status_message, 1024, "Failed to get the download progress from ezRemote Server");
			}
        }
    }
}
