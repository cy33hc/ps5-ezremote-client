#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <json-c/json.h>
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
    static int FtpCallback(int64_t xfered, void *arg)
    {
        bytes_transfered = xfered;
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

    void *DeleteSelectedLocalFilesThread(void *argp)
    {
        std::vector<DirEntry> files;
        if (multi_selected_local_files.size() > 0)
            std::copy(multi_selected_local_files.begin(), multi_selected_local_files.end(), std::back_inserter(files));
        else
            files.push_back(selected_local_file);

        for (std::vector<DirEntry>::iterator it = files.begin(); it != files.end(); ++it)
        {
            FS::RmRecursive(it->path);
        }
        activity_inprogess = false;
        Windows::SetModalMode(false);
        selected_action = ACTION_REFRESH_LOCAL_FILES;
        return NULL;
    }

    void DeleteSelectedLocalFiles()
    {
        int ret = pthread_create(&bk_activity_thid, NULL, DeleteSelectedLocalFilesThread, NULL);
        if (ret != 0)
        {
            activity_inprogess = false;
            Windows::SetModalMode(false);
            selected_action = ACTION_REFRESH_LOCAL_FILES;
        }
    }

    void *DeleteSelectedRemotesFilesThread(void *argp)
    {
        if (remoteclient->Ping())
        {
            std::vector<DirEntry> files;
            if (multi_selected_remote_files.size() > 0)
                std::copy(multi_selected_remote_files.begin(), multi_selected_remote_files.end(), std::back_inserter(files));
            else
                files.push_back(selected_remote_file);

            for (std::vector<DirEntry>::iterator it = files.begin(); it != files.end(); ++it)
            {
                if (it->isDir)
                    remoteclient->Rmdir(it->path, true);
                else
                {
                    sprintf(activity_message, "%s %s", lang_strings[STR_DELETING], it->path);
                    if (!remoteclient->Delete(it->path))
                    {
                        sprintf(status_message, "%s - %s", lang_strings[STR_FAILED], remoteclient->LastResponse());
                    }
                }
            }
            selected_action = ACTION_REFRESH_REMOTE_FILES;
        }
        else
        {
            sprintf(status_message, "%s", lang_strings[STR_CONNECTION_CLOSE_ERR_MSG]);
            Disconnect();
        }
        activity_inprogess = false;
        Windows::SetModalMode(false);
        return NULL;
    }

    void DeleteSelectedRemotesFiles()
    {
        int res = pthread_create(&bk_activity_thid, NULL, DeleteSelectedRemotesFilesThread, NULL);
        if (res != 0)
        {
            activity_inprogess = false;
            Windows::SetModalMode(false);
        }
    }

    int UploadFile(const char *src, const char *dest)
    {
        int ret;
        if (!remoteclient->Ping())
        {
            remoteclient->Quit();
            sprintf(status_message, "%s", lang_strings[STR_CONNECTION_CLOSE_ERR_MSG]);
            return 0;
        }

        if (overwrite_type == OVERWRITE_PROMPT && remoteclient->FileExists(dest))
        {
            sprintf(confirm_message, "%s %s?", lang_strings[STR_OVERWRITE], dest);
            confirm_state = CONFIRM_WAIT;
            action_to_take = selected_action;
            activity_inprogess = false;
            while (confirm_state == CONFIRM_WAIT)
            {
                usleep(100000);
            }
            activity_inprogess = true;
            selected_action = action_to_take;
        }
        else if (overwrite_type == OVERWRITE_NONE && remoteclient->FileExists(dest))
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
            return remoteclient->Put(src, dest);
        }

        sceSystemServicePowerTick();
        return 1;
    }

    int Upload(const DirEntry &src, const char *dest)
    {
        if (stop_activity)
            return 1;

        int ret;
        if (src.isDir)
        {
            int err;
            std::vector<DirEntry> entries = FS::ListDir(src.path, &err);
            remoteclient->Mkdir(dest);
            for (int i = 0; i < entries.size(); i++)
            {
                if (stop_activity)
                    return 1;

                int path_length = strlen(dest) + strlen(entries[i].name) + 2;
                char *new_path = (char *)malloc(path_length);
                snprintf(new_path, path_length, "%s%s%s", dest, FS::hasEndSlash(dest) ? "" : "/", entries[i].name);

                if (entries[i].isDir)
                {
                    if (strcmp(entries[i].name, "..") == 0)
                        continue;

                    remoteclient->Mkdir(new_path);
                    ret = Upload(entries[i], new_path);
                    if (ret <= 0)
                    {
                        free(new_path);
                        return ret;
                    }
                }
                else
                {
                    snprintf(activity_message, 1024, "%s %s", lang_strings[STR_UPLOADING], entries[i].path);
                    bytes_to_download = entries[i].file_size;
                    bytes_transfered = 0;
                    prev_tick = Util::GetTick();
                    ret = UploadFile(entries[i].path, new_path);
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
            bytes_to_download = src.file_size;
            ret = UploadFile(src.path, new_path);
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
        file_transfering = true;
        std::vector<DirEntry> files;
        if (multi_selected_local_files.size() > 0)
            std::copy(multi_selected_local_files.begin(), multi_selected_local_files.end(), std::back_inserter(files));
        else
            files.push_back(selected_local_file);

        for (std::vector<DirEntry>::iterator it = files.begin(); it != files.end(); ++it)
        {
            if (it->isDir)
            {
                char new_dir[512];
                sprintf(new_dir, "%s%s%s", remote_directory, FS::hasEndSlash(remote_directory) ? "" : "/", it->name);
                Upload(*it, new_dir);
            }
            else
            {
                Upload(*it, remote_directory);
            }
        }
        activity_inprogess = false;
        file_transfering = false;
        multi_selected_local_files.clear();
        Windows::SetModalMode(false);
        selected_action = ACTION_REFRESH_REMOTE_FILES;
        return NULL;
    }

    void UploadFiles()
    {
        sprintf(status_message, "%s", "");
        int res = pthread_create(&bk_activity_thid, NULL, UploadFilesThread, NULL);
        if (res != 0)
        {
            activity_inprogess = false;
            file_transfering = false;
            multi_selected_local_files.clear();
            Windows::SetModalMode(false);
            selected_action = ACTION_REFRESH_REMOTE_FILES;
        }
    }

    int DownloadFile(const char *src, const char *dest)
    {
        bytes_transfered = 0;
        prev_tick = Util::GetTick();
        if (!remoteclient->Size(src, &bytes_to_download))
        {
            remoteclient->Quit();
            sprintf(status_message, "%s", lang_strings[STR_CONNECTION_CLOSE_ERR_MSG]);
            return 0;
        }

        if (overwrite_type == OVERWRITE_PROMPT && FS::FileExists(dest))
        {
            sprintf(confirm_message, "%s %s?", lang_strings[STR_OVERWRITE], dest);
            confirm_state = CONFIRM_WAIT;
            action_to_take = selected_action;
            activity_inprogess = false;
            while (confirm_state == CONFIRM_WAIT)
            {
                usleep(100000);
            }
            activity_inprogess = true;
            selected_action = action_to_take;
        }
        else if (overwrite_type == OVERWRITE_NONE && FS::FileExists(dest))
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
            return remoteclient->Get(dest, src);
        }

        sceSystemServicePowerTick();
        return 1;
    }

    int Download(const DirEntry &src, const char *dest)
    {
        if (stop_activity)
            return 1;

        int ret;
        if (src.isDir)
        {
            int err;
            std::vector<DirEntry> entries = remoteclient->ListDir(src.path);
            FS::MkDirs(dest);
            for (int i = 0; i < entries.size(); i++)
            {
                if (stop_activity)
                    return 1;

                int path_length = strlen(dest) + strlen(entries[i].name) + 2;
                char *new_path = (char *)malloc(path_length);
                snprintf(new_path, path_length, "%s%s%s", dest, FS::hasEndSlash(dest) ? "" : "/", entries[i].name);

                if (entries[i].isDir)
                {
                    if (strcmp(entries[i].name, "..") == 0)
                        continue;

                    FS::MkDirs(new_path);
                    ret = Download(entries[i], new_path);
                    if (ret <= 0)
                    {
                        free(new_path);
                        return ret;
                    }
                }
                else
                {
                    snprintf(activity_message, 1024, "%s %s", lang_strings[STR_DOWNLOADING], entries[i].path);
                    ret = DownloadFile(entries[i].path, new_path);
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
            ret = DownloadFile(src.path, new_path);
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
        file_transfering = true;
        std::vector<DirEntry> files;
        if (multi_selected_remote_files.size() > 0)
            std::copy(multi_selected_remote_files.begin(), multi_selected_remote_files.end(), std::back_inserter(files));
        else
            files.push_back(selected_remote_file);

        for (std::vector<DirEntry>::iterator it = files.begin(); it != files.end(); ++it)
        {
            if (it->isDir)
            {
                char new_dir[512];
                sprintf(new_dir, "%s%s%s", local_directory, FS::hasEndSlash(local_directory) ? "" : "/", it->name);
                Download(*it, new_dir);
            }
            else
            {
                Download(*it, local_directory);
            }
        }
        file_transfering = false;
        activity_inprogess = false;
        multi_selected_remote_files.clear();
        Windows::SetModalMode(false);
        selected_action = ACTION_REFRESH_LOCAL_FILES;
        return NULL;
    }

    void DownloadFiles()
    {
        sprintf(status_message, "%s", "");
        int res = pthread_create(&bk_activity_thid, NULL, DownloadFilesThread, NULL);
        if (res != 0)
        {
            file_transfering = false;
            activity_inprogess = false;
            multi_selected_remote_files.clear();
            Windows::SetModalMode(false);
            selected_action = ACTION_REFRESH_LOCAL_FILES;
        }
    }

    void *InstallRemotePkgsThread(void *argp)
    {
        int failed = 0;
        int success = 0;
        int skipped = 0;

        std::vector<DirEntry> files;
        if (multi_selected_remote_files.size() > 0)
            std::copy(multi_selected_remote_files.begin(), multi_selected_remote_files.end(), std::back_inserter(files));
        else
            files.push_back(selected_remote_file);

        bool download_and_install = false;
        if (remote_settings->enable_rpi)
        {
            std::string url = INSTALLER::getRemoteUrl(files.begin()->path);
            sprintf(activity_message, "%s", lang_strings[STR_CHECKING_REMOTE_SERVER_MSG]);
            file_transfering = false;
            if (!INSTALLER::canInstallRemotePkg(url))
            {
                confirm_state = CONFIRM_WAIT;
                action_to_take = selected_action;
                activity_inprogess = false;
                while (confirm_state == CONFIRM_WAIT)
                {
                    usleep(100000);
                }
                activity_inprogess = true;
                selected_action = action_to_take;

                if (confirm_state == CONFIRM_YES)
                {
                    download_and_install = true;
                }
                else
                {
                    goto finish;
                }
            }
        }
        else
        {
            download_and_install = true;
        }
        
        for (std::vector<DirEntry>::iterator it = files.begin(); it != files.end(); ++it)
        {
            if (stop_activity)
                break;
            sprintf(activity_message, "%s %s", lang_strings[STR_INSTALLING], it->name);

            if (!it->isDir)
            {
                std::string path = std::string(it->path);
                path = Util::ToLower(path);
                if (path.size() > 4 && path.substr(path.size() - 4) == ".pkg")
                {
                    pkg_header header;
                    memset(&header, 0, sizeof(header));

                    if (remoteclient->Head(it->path, (void *)&header, sizeof(header)) == 0)
                        failed++;
                    else
                    {
                        if (BE32(header.pkg_magic) == PS4_PKG_MAGIC)
                        {
                            if (download_and_install)
                            {
                                if (DownloadAndInstallPkg(it->path, &header) == 0)
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
                                    SplitFile *sp = new SplitFile(install_pkg_path, INSTALL_ARCHIVE_PKG_SPLIT_SIZE/2);

                                    install_data->split_file = sp;
                                    install_data->remote_client = remoteclient;
                                    install_data->path = it->path;
                                    remoteclient->Size(it->path, &install_data->size);
                                    install_data->stop_write_thread = false;
                                    install_data->delete_client = false;

                                    int ret = pthread_create(&install_data->thread, NULL, DownloadSplitPkg, install_data);

                                    if (INSTALLER::InstallSplitPkg(it->path, install_data, false) == 0)
                                        failed++;
                                    else
                                        success++;
                                }
                                else
                                {
                                    std::string url = INSTALLER::getRemoteUrl(it->path, true);
                                    std::string title = INSTALLER::GetRemotePkgTitle(remoteclient, it->path, &header);
                                    if (INSTALLER::InstallRemotePkg(url, &header, title, true) == 0)
                                        failed++;
                                    else
                                        success++;
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
                            SplitFile *sp = new SplitFile(install_pkg_path, INSTALL_ARCHIVE_PKG_SPLIT_SIZE);
                            
                            install_data->archive_entry = entry;
                            install_data->split_file = sp;
                            install_data->stop_write_thread = false;

                            int res = pthread_create(&install_data->thread, NULL, ExtractArchivePkg, install_data);

                            INSTALLER::InstallArchivePkg(entry->filename, install_data);

                            ArchiveEntry *previos = entry;
                            entry = ZipUtil::GetNextPackageEntry(entry);
                            free(previos);
                        }
                        success++;
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
        }
    finish:
        activity_inprogess = false;
        multi_selected_remote_files.clear();
        Windows::SetModalMode(false);
        return NULL;
    }

    void InstallRemotePkgs()
    {
        sprintf(status_message, "%s", "");
        int res = pthread_create(&bk_activity_thid, NULL, InstallRemotePkgsThread, NULL);
        if (res != 0)
        {
            activity_inprogess = false;
            file_transfering = false;
            multi_selected_remote_files.clear();
            Windows::SetModalMode(false);
        }
    }

    void *ExtractArchivePkg(void *argp)
    {
        ssize_t len;
        char *buffer = (char*) malloc(ARCHIVE_TRANSFER_SIZE);

        ArchivePkgInstallData *install_data = (ArchivePkgInstallData*) argp;
        SplitFile *sp = install_data->split_file;

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
        }

        sp->Close();
        free(buffer);
        return NULL;
    }

    void *DownloadSplitPkg(void *argp)
    {
        SplitPkgInstallData *install_data = (SplitPkgInstallData*) argp;
        SplitFile *sp = install_data->split_file;

        /* loop over file contents and write to fd */
        sp->Open();
        install_data->remote_client->Get(sp, install_data->path);
        sp->Close();
        return NULL;
    }

    void *InstallLocalPkgsThread(void *argp)
    {
        int failed = 0;
        int success = 0;
        int skipped = 0;
        int ret;

        std::vector<DirEntry> files;
        if (multi_selected_local_files.size() > 0)
            std::copy(multi_selected_local_files.begin(), multi_selected_local_files.end(), std::back_inserter(files));
        else
            files.push_back(selected_local_file);

        for (std::vector<DirEntry>::iterator it = files.begin(); it != files.end(); ++it)
        {
            if (stop_activity)
                break;
            sprintf(activity_message, "%s %s", lang_strings[STR_INSTALLING], it->name);

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
                        if (BE32(header.pkg_magic) == PS4_PKG_MAGIC)
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
                            SplitFile *sp = new SplitFile(install_pkg_path, INSTALL_ARCHIVE_PKG_SPLIT_SIZE);
                            
                            install_data->archive_entry = entry;
                            install_data->split_file = sp;
                            install_data->stop_write_thread = false;

                            int res = pthread_create(&install_data->thread, NULL, ExtractArchivePkg, install_data);

                            INSTALLER::InstallArchivePkg(entry->filename, install_data);

                            ArchiveEntry *previous = entry;
                            entry = ZipUtil::GetNextPackageEntry(entry);
                            free(previous);
                        }
                        success++;
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
        }
        activity_inprogess = false;
        multi_selected_local_files.clear();
        Windows::SetModalMode(false);
        return NULL;
    }

    void InstallLocalPkgs()
    {
        sprintf(status_message, "%s", "");
        int res = pthread_create(&bk_activity_thid, NULL, InstallLocalPkgsThread, NULL);
        if (res != 0)
        {
            activity_inprogess = false;
            multi_selected_local_files.clear();
            Windows::SetModalMode(false);
        }
    }

    void *ExtractZipThread(void *argp)
    {
        FS::MkDirs(extract_zip_folder);
        std::vector<DirEntry> files;
        if (multi_selected_local_files.size() > 0)
            std::copy(multi_selected_local_files.begin(), multi_selected_local_files.end(), std::back_inserter(files));
        else
            files.push_back(selected_local_file);

        for (std::vector<DirEntry>::iterator it = files.begin(); it != files.end(); ++it)
        {
            if (stop_activity)
                break;
            if (!it->isDir)
            {
                int ret = ZipUtil::Extract(*it, extract_zip_folder);
                if (ret == 0)
                {
                    sprintf(status_message, "%s %s", lang_strings[STR_FAILED_TO_EXTRACT], it->name);
                    usleep(100000);
                }
            }
        }
        activity_inprogess = false;
        multi_selected_local_files.clear();
        Windows::SetModalMode(false);
        selected_action = ACTION_REFRESH_LOCAL_FILES;
        return NULL;
    }

    void ExtractLocalZips()
    {
        sprintf(status_message, "%s", "");
        int res = pthread_create(&bk_activity_thid, NULL, ExtractZipThread, NULL);
        if (res != 0)
        {
            file_transfering = false;
            activity_inprogess = false;
            multi_selected_local_files.clear();
            Windows::SetModalMode(false);
        }
    }

    void *ExtractRemoteZipThread(void *argp)
    {
        FS::MkDirs(extract_zip_folder);
        std::vector<DirEntry> files;
        if (multi_selected_remote_files.size() > 0)
            std::copy(multi_selected_remote_files.begin(), multi_selected_remote_files.end(), std::back_inserter(files));
        else
            files.push_back(selected_remote_file);

        for (std::vector<DirEntry>::iterator it = files.begin(); it != files.end(); ++it)
        {
            if (stop_activity)
                break;
            if (!it->isDir)
            {
                int ret = ZipUtil::Extract(*it, extract_zip_folder, remoteclient);
                if (ret == 0)
                {
                    sprintf(status_message, "%s %s", lang_strings[STR_FAILED_TO_EXTRACT], it->name);
                    usleep(100000);
                }
            }
        }
        activity_inprogess = false;
        multi_selected_remote_files.clear();
        Windows::SetModalMode(false);
        selected_action = ACTION_REFRESH_LOCAL_FILES;
        return NULL;
    }

    void ExtractRemoteZips()
    {
        sprintf(status_message, "%s", "");
        int res = pthread_create(&bk_activity_thid, NULL, ExtractRemoteZipThread, NULL);
        if (res != 0)
        {
            file_transfering = false;
            activity_inprogess = false;
            multi_selected_remote_files.clear();
            Windows::SetModalMode(false);
        }
    }

    void *MakeZipThread(void *argp)
    {
        zipFile zf = zipOpen64(zip_file_path, APPEND_STATUS_CREATE);
        if (zf != NULL)
        {
            std::vector<DirEntry> files;
            if (multi_selected_local_files.size() > 0)
                std::copy(multi_selected_local_files.begin(), multi_selected_local_files.end(), std::back_inserter(files));
            else
                files.push_back(selected_local_file);

            for (std::vector<DirEntry>::iterator it = files.begin(); it != files.end(); ++it)
            {
                if (stop_activity)
                    break;
                int res = ZipUtil::ZipAddPath(zf, it->path, strlen(it->directory) + 1, Z_DEFAULT_COMPRESSION);
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
        activity_inprogess = false;
        multi_selected_local_files.clear();
        Windows::SetModalMode(false);
        selected_action = ACTION_REFRESH_LOCAL_FILES;
        return NULL;
    }

    void MakeLocalZip()
    {
        sprintf(status_message, "%s", "");
        int res = pthread_create(&bk_activity_thid, NULL, MakeZipThread, NULL);
        if (res != 0)
        {
            file_transfering = false;
            activity_inprogess = false;
            multi_selected_local_files.clear();
            Windows::SetModalMode(false);
        }
    }

    void *InstallLocalUrlPkgThread(void *argp)
    {
        bytes_transfered = 0;
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
            activity_inprogess = false;
            Windows::SetModalMode(false);
            return NULL;
        }
        full_url = filehost->GetDownloadUrl();
        delete(filehost);

        if (full_url.empty())
        {
            sprintf(status_message, "%s", lang_strings[STR_FAIL_TO_OBTAIN_GG_DL_MSG]);
            activity_inprogess = false;
            Windows::SetModalMode(false);
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

        int ret = tmp_client.Size(path, &bytes_to_download);
        if (ret == 0)
        {
            sprintf(status_message, "%s", tmp_client.LastResponse());
            tmp_client.Quit();
            activity_inprogess = false;
            Windows::SetModalMode(false);
            return NULL;
        }

        file_transfering = 1;
        int is_performed = tmp_client.Get(filename, path);

        if (is_performed == 0)
        {
            sprintf(status_message, "%s - %s", lang_strings[STR_FAILED], tmp_client.LastResponse());
            tmp_client.Quit();
            activity_inprogess = false;
            Windows::SetModalMode(false);
            return NULL;
        }
        tmp_client.Quit();

        FILE *in = FS::OpenRead(filename);
        if (in == NULL)
        {
            sprintf(status_message, "%s - Error opening temp pkg file - %s", lang_strings[STR_FAILED], filename);
            activity_inprogess = false;
            Windows::SetModalMode(false);
            return NULL;
        }

        FS::Read(in, (void *)&header, s);
        FS::Close(in);
        if (BE32(header.pkg_magic) == PS4_PKG_MAGIC)
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

        activity_inprogess = false;
        Windows::SetModalMode(false);
        return NULL;
    }

    void *InstallRpiUrlPkgThread(void *argp)
    {
        json_object *params = json_object_new_object();
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
                if (jobj != nullptr)
                {
                    json_object *result = json_object_object_get(jobj, "result");
                    if (result != nullptr)
                    {
                        bool success = json_object_get_boolean(json_object_object_get(result, "success"));
                        if (!success)
                        {
                            const char* error_message = json_object_get_string(json_object_object_get(result, "error"));
                            sprintf(status_message, "%s", error_message);
                            activity_inprogess = false;
                            Windows::SetModalMode(false);
                        }
                    }
                }
                else
                {
                    activity_inprogess = false;
                    Windows::SetModalMode(false);
                }
            }
            else
            {
                activity_inprogess = false;
                Windows::SetModalMode(false);
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
            activity_inprogess = false;
            Windows::SetModalMode(false);
        }
    }

    void Connect()
    {
        CONFIG::SaveConfig();

        if (strncmp(remote_settings->server, "https://", 8) == 0 || strncmp(remote_settings->server, "http://", 7) == 0)
        {
            if (strcmp(remote_settings->http_server_type, HTTP_SERVER_APACHE) == 0)
                remoteclient = new ApacheClient();
            else if (strcmp(remote_settings->http_server_type, HTTP_SERVER_MS_IIS) == 0)
                remoteclient = new IISClient();
            else if (strcmp(remote_settings->http_server_type, HTTP_SERVER_NGINX) == 0)
                remoteclient = new NginxClient();
            else if (strcmp(remote_settings->http_server_type, HTTP_SERVER_NPX_SERVE) == 0)
                remoteclient = new NpxServeClient();
            else if (strcmp(remote_settings->http_server_type, HTTP_SERVER_RCLONE) == 0)
                remoteclient = new RCloneClient();
            else if (strcmp(remote_settings->http_server_type, HTTP_SERVER_ARCHIVEORG) == 0)
                remoteclient = new ArchiveOrgClient();
            else if (strcmp(remote_settings->http_server_type, HTTP_SERVER_MYRIENT) == 0)
                remoteclient = new MyrientClient();
            else if (strcmp(remote_settings->http_server_type, HTTP_SERVER_GITHUB) == 0)
                remoteclient = new GithubClient();
        }
        else if (strncmp(remote_settings->server, "webdavs://", 10) == 0 || strncmp(remote_settings->server, "webdav://", 9) == 0)
        {
            remoteclient = new WebDAVClient();
        }
        else if (strncmp(remote_settings->server, "smb://", 6) == 0)
        {
            remoteclient = new SmbClient();
        }
        else if (strncmp(remote_settings->server, "ftp://", 6) == 0)
        {
            FtpClient *client = new FtpClient();
            client->SetConnmode(FtpClient::pasv);
            client->SetCallbackBytes(1);
            client->SetCallbackXferFunction(FtpCallback);
            remoteclient = client;
        }
        else if (strncmp(remote_settings->server, "sftp://", 7) == 0)
        {
            remoteclient = new SFTPClient();
        }
        else if (strncmp(remote_settings->server, "nfs://", 6) == 0)
        {
            remoteclient = new NfsClient();
        }
        else
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
        if (overwrite_type == OVERWRITE_PROMPT && FS::FileExists(dest))
        {
            sprintf(confirm_message, "%s %s?", lang_strings[STR_OVERWRITE], dest);
            confirm_state = CONFIRM_WAIT;
            action_to_take = selected_action;
            activity_inprogess = false;
            while (confirm_state == CONFIRM_WAIT)
            {
                usleep(100000);
            }
            activity_inprogess = true;
            selected_action = action_to_take;
        }
        else if (overwrite_type == OVERWRITE_NONE && FS::FileExists(dest))
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
        if (stop_activity)
            return 1;

        int ret;
        if (src.isDir)
        {
            int err;
            std::vector<DirEntry> entries = FS::ListDir(src.path, &err);
            FS::MkDirs(dest);
            for (int i = 0; i < entries.size(); i++)
            {
                if (stop_activity)
                    return 1;

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
                    snprintf(activity_message, 1024, "%s %s", isCopy ? lang_strings[STR_COPYING] : lang_strings[STR_MOVING], entries[i].path);
                    bytes_to_download = entries[i].file_size;
                    bytes_transfered = 0;
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
        }
        else
        {
            int path_length = strlen(dest) + strlen(src.name) + 2;
            char *new_path = (char *)malloc(path_length);
            snprintf(new_path, path_length, "%s%s%s", dest, FS::hasEndSlash(dest) ? "" : "/", src.name);
            snprintf(activity_message, 1024, "%s %s", isCopy ? lang_strings[STR_COPYING] : lang_strings[STR_MOVING], src.name);
            bytes_to_download = src.file_size;
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
        file_transfering = true;
        for (std::vector<DirEntry>::iterator it = local_paste_files.begin(); it != local_paste_files.end(); ++it)
        {
            if (stop_activity)
                break;

            if (strcmp(it->directory, local_directory) == 0)
                continue;

            if (it->isDir)
            {
                if (strncmp(local_directory, it->path, strlen(it->path)) == 0)
                {
                    sprintf(status_message, "%s", lang_strings[STR_CANT_MOVE_TO_SUBDIR_MSG]);
                    continue;
                }
                char new_dir[512];
                sprintf(new_dir, "%s%s%s", local_directory, FS::hasEndSlash(local_directory) ? "" : "/", it->name);
                CopyOrMove(*it, new_dir, false);
                FS::RmRecursive(it->path);
            }
            else
            {
                CopyOrMove(*it, local_directory, false);
            }
        }
        activity_inprogess = false;
        file_transfering = false;
        local_paste_files.clear();
        Windows::SetModalMode(false);
        selected_action = ACTION_REFRESH_LOCAL_FILES;
        return NULL;
    }

    void MoveLocalFiles()
    {
        sprintf(status_message, "%s", "");
        int res = pthread_create(&bk_activity_thid, NULL, MoveLocalFilesThread, NULL);
        if (res != 0)
        {
            file_transfering = false;
            activity_inprogess = false;
            local_paste_files.clear();
            Windows::SetModalMode(false);
        }
    }

    void *CopyLocalFilesThread(void *argp)
    {
        file_transfering = true;
        for (std::vector<DirEntry>::iterator it = local_paste_files.begin(); it != local_paste_files.end(); ++it)
        {
            if (stop_activity)
                break;

            if (strcmp(it->directory, local_directory) == 0)
                continue;

            if (it->isDir)
            {
                if (strncmp(local_directory, it->path, strlen(it->path)) == 0)
                {
                    sprintf(status_message, "%s", lang_strings[STR_CANT_COPY_TO_SUBDIR_MSG]);
                    continue;
                }
                char new_dir[512];
                sprintf(new_dir, "%s%s%s", local_directory, FS::hasEndSlash(local_directory) ? "" : "/", it->name);
                CopyOrMove(*it, new_dir, true);
            }
            else
            {
                CopyOrMove(*it, local_directory, true);
            }
        }
        activity_inprogess = false;
        file_transfering = false;
        local_paste_files.clear();
        Windows::SetModalMode(false);
        selected_action = ACTION_REFRESH_LOCAL_FILES;
        return NULL;
    }

    void CopyLocalFiles()
    {
        sprintf(status_message, "%s", "");
        int res = pthread_create(&bk_activity_thid, NULL, CopyLocalFilesThread, NULL);
        if (res != 0)
        {
            file_transfering = false;
            activity_inprogess = false;
            local_paste_files.clear();
            Windows::SetModalMode(false);
        }
    }

    int CopyOrMoveRemoteFile(const std::string &src, const std::string &dest, bool isCopy)
    {
        int ret;
        if (overwrite_type == OVERWRITE_PROMPT && remoteclient->FileExists(dest))
        {
            sprintf(confirm_message, "%s %s?", lang_strings[STR_OVERWRITE], dest.c_str());
            confirm_state = CONFIRM_WAIT;
            action_to_take = selected_action;
            activity_inprogess = false;
            while (confirm_state == CONFIRM_WAIT)
            {
                usleep(100000);
            }
            activity_inprogess = true;
            selected_action = action_to_take;
        }
        else if (overwrite_type == OVERWRITE_NONE && remoteclient->FileExists(dest))
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
                return remoteclient->Copy(src, dest);
            else
                return remoteclient->Move(src, dest);
        }

        return 1;
    }

    void *MoveRemoteFilesThread(void *argp)
    {
        file_transfering = false;
        for (std::vector<DirEntry>::iterator it = remote_paste_files.begin(); it != remote_paste_files.end(); ++it)
        {
            if (stop_activity)
                break;

            if (strcmp(it->directory, remote_directory) == 0)
                continue;

            char new_path[1024];
            sprintf(new_path, "%s%s%s", remote_directory, FS::hasEndSlash(remote_directory) ? "" : "/", it->name);
            if (it->isDir)
            {
                if (strncmp(remote_directory, it->path, strlen(it->path)) == 0)
                {
                    sprintf(status_message, "%s", lang_strings[STR_CANT_MOVE_TO_SUBDIR_MSG]);
                    continue;
                }
            }

            snprintf(activity_message, 1024, "%s %s", lang_strings[STR_MOVING], it->path);
            int res = CopyOrMoveRemoteFile(it->path, new_path, false);
            if (res == 0)
                sprintf(status_message, "%s - %s", it->name, lang_strings[STR_FAIL_COPY_MSG]);
        }
        activity_inprogess = false;
        file_transfering = false;
        remote_paste_files.clear();
        Windows::SetModalMode(false);
        selected_action = ACTION_REFRESH_REMOTE_FILES;
        return NULL;
    }

    void MoveRemoteFiles()
    {
        sprintf(status_message, "%s", "");
        int res = pthread_create(&bk_activity_thid, NULL, MoveRemoteFilesThread, NULL);
        if (res != 0)
        {
            file_transfering = false;
            activity_inprogess = false;
            remote_paste_files.clear();
            Windows::SetModalMode(false);
        }
    }

    int CopyRemotePath(const DirEntry &src, const char *dest)
    {
        if (stop_activity)
            return 1;

        int ret;
        if (src.isDir)
        {
            int err;
            std::vector<DirEntry> entries = remoteclient->ListDir(src.path);
            remoteclient->Mkdir(dest);
            for (int i = 0; i < entries.size(); i++)
            {
                if (stop_activity)
                    return 1;

                int path_length = strlen(dest) + strlen(entries[i].name) + 2;
                char *new_path = (char *)malloc(path_length);
                snprintf(new_path, path_length, "%s%s%s", dest, FS::hasEndSlash(dest) ? "" : "/", entries[i].name);

                if (entries[i].isDir)
                {
                    if (strcmp(entries[i].name, "..") == 0)
                        continue;

                    remoteclient->Mkdir(new_path);
                    ret = CopyRemotePath(entries[i], new_path);
                    if (ret <= 0)
                    {
                        free(new_path);
                        return ret;
                    }
                }
                else
                {
                    snprintf(activity_message, 1024, "%s %s", lang_strings[STR_COPYING], entries[i].path);
                    bytes_to_download = entries[i].file_size;
                    bytes_transfered = 0;
                    prev_tick = Util::GetTick();
                    ret = CopyOrMoveRemoteFile(entries[i].path, new_path, true);
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
            ret = CopyOrMoveRemoteFile(src.path, new_path, true);
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
        file_transfering = false;
        for (std::vector<DirEntry>::iterator it = remote_paste_files.begin(); it != remote_paste_files.end(); ++it)
        {
            if (stop_activity)
                break;

            if (strcmp(it->directory, remote_directory) == 0)
                continue;

            char new_path[1024];
            sprintf(new_path, "%s%s%s", remote_directory, FS::hasEndSlash(remote_directory) ? "" : "/", it->name);
            if (it->isDir)
            {
                if (strncmp(remote_directory, it->path, strlen(it->path)) == 0)
                {
                    sprintf(status_message, "%s", lang_strings[STR_CANT_COPY_TO_SUBDIR_MSG]);
                    continue;
                }
                int res = CopyRemotePath(*it, new_path);
                if (res == 0)
                    sprintf(status_message, "%s - %s", it->name, lang_strings[STR_FAIL_COPY_MSG]);
            }
            else
            {
                int res = CopyRemotePath(*it, remote_directory);
                if (res == 0)
                    sprintf(status_message, "%s - %s", it->name, lang_strings[STR_FAIL_COPY_MSG]);
            }
        }
        activity_inprogess = false;
        file_transfering = false;
        remote_paste_files.clear();
        Windows::SetModalMode(false);
        selected_action = ACTION_REFRESH_REMOTE_FILES;
        return NULL;
    }

    void CopyRemoteFiles()
    {
        sprintf(status_message, "%s", "");
        int res = pthread_create(&bk_activity_thid, NULL, CopyRemoteFilesThread, NULL);
        if (res != 0)
        {
            file_transfering = false;
            activity_inprogess = false;
            remote_paste_files.clear();
            Windows::SetModalMode(false);
        }
    }

    int DownloadAndInstallPkg(const std::string &filename, pkg_header *header)
    {
        char local_file[2000];
        uint64_t tick = Util::GetTick();
        sprintf(local_file, "%s/%lu.pkg", temp_folder, tick);

        sprintf(activity_message, "%s %s to %s", lang_strings[STR_DOWNLOADING], filename.c_str(), local_file);
        remoteclient->Size(filename, &bytes_to_download);
        bytes_transfered = 0;
        prev_tick = Util::GetTick();

        file_transfering = true;
        int ret = remoteclient->Get(local_file, filename);
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

}
