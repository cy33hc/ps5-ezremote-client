
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <minizip/unzip.h>
#include <minizip/zip.h>
#include <archive.h>
#include <archive_entry.h>

#include "clients/remote_client.h"
#include "config.h"
#include "common.h"
#include "fs.h"
#include "ime_dialog.h"
#include "lang.h"
#include "windows.h"
#include "util.h"
#include "zip_util.h"

namespace ZipUtil
{
    static char filename_extracted[256];
    static char password[128];

    void callback_7zip(const char *fileName, unsigned long fileSize, unsigned fileNum, unsigned numFiles)
    {
        sprintf(activity_message, "%s %s: %s", lang_strings[STR_EXTRACTING], filename_extracted, fileName);
    }

    void convertToZipTime(time_t time, tm_zip *tmzip)
    {
        struct tm tm = *localtime(&time);
        tmzip->tm_sec = tm.tm_sec;
        tmzip->tm_min = tm.tm_min;
        tmzip->tm_hour = tm.tm_hour;
        tmzip->tm_mday = tm.tm_mday;
        tmzip->tm_mon = tm.tm_mon;
        tmzip->tm_year = tm.tm_year;
    }

    int ZipAddFile(zipFile zf, const std::string &path, int filename_start, int level)
    {
        int res;
        // Get file stat
        struct stat file_stat;
        memset(&file_stat, 0, sizeof(file_stat));
        res = stat(path.c_str(), &file_stat);
        if (res < 0)
            return res;

        // Get file local time
        zip_fileinfo zi;
        memset(&zi, 0, sizeof(zip_fileinfo));
        convertToZipTime(file_stat.st_mtim.tv_sec, &zi.tmz_date);

        bytes_transfered = 0;
        prev_tick = Util::GetTick();

        bytes_to_download = file_stat.st_size;

        // Large file?
        int use_zip64 = (file_stat.st_size >= 0xFFFFFFFF);

        // Open new file in zip
        res = zipOpenNewFileInZip3_64(zf, path.substr(filename_start).c_str(), &zi,
                                      NULL, 0, NULL, 0, NULL,
                                      (level != 0) ? Z_DEFLATED : 0,
                                      level, 0,
                                      -MAX_WBITS, DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY,
                                      NULL, 0, use_zip64);
        if (res < 0)
            return res;

        // Open file to add
        FILE *fd = FS::OpenRead(path);
        if (fd == NULL)
        {
            zipCloseFileInZip(zf);
            return 0;
        }

        // Add file to zip
        void *buf = nullptr;
        posix_memalign(&buf, 4096, ARCHIVE_TRANSFER_SIZE);
        uint64_t seek = 0;

        while (1)
        {
            int read = FS::Read(fd, buf, ARCHIVE_TRANSFER_SIZE);
            if (read < 0)
            {
                free(buf);
                FS::Close(fd);
                zipCloseFileInZip(zf);
                return read;
            }

            if (read == 0)
                break;

            int written = zipWriteInFileInZip(zf, buf, read);
            if (written < 0)
            {
                free(buf);
                FS::Close(fd);
                zipCloseFileInZip(zf);
                return written;
            }

            seek += written;
            bytes_transfered += read;
        }

        free(buf);
        FS::Close(fd);
        zipCloseFileInZip(zf);

        return 1;
    }

    int ZipAddFolder(zipFile zf, const std::string &path, int filename_start, int level)
    {
        int res;

        // Get file stat
        struct stat file_stat;
        memset(&file_stat, 0, sizeof(file_stat));
        res = stat(path.c_str(), &file_stat);
        if (res < 0)
            return res;

        // Get file local time
        zip_fileinfo zi;
        memset(&zi, 0, sizeof(zip_fileinfo));
        convertToZipTime(file_stat.st_mtim.tv_sec, &zi.tmz_date);

        // Open new file in zip
        std::string folder = path.substr(filename_start);
        if (folder[folder.length() - 1] != '/')
            folder = folder + "/";

        res = zipOpenNewFileInZip3_64(zf, folder.c_str(), &zi,
                                      NULL, 0, NULL, 0, NULL,
                                      (level != 0) ? Z_DEFLATED : 0,
                                      level, 0,
                                      -MAX_WBITS, DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY,
                                      NULL, 0, 0);

        if (res < 0)
            return res;

        zipCloseFileInZip(zf);
        return 1;
    }

    int ZipAddPath(zipFile zf, const std::string &path, int filename_start, int level)
    {
        DIR *dfd = opendir(path.c_str());
        if (dfd != NULL)
        {
            int ret = ZipAddFolder(zf, path, filename_start, level);
            if (ret <= 0)
                return ret;

            struct dirent *dirent;
            do
            {
                dirent = readdir(dfd);
                if (stop_activity)
                    return 1;
                if (dirent != NULL && strcmp(dirent->d_name, ".") != 0 && strcmp(dirent->d_name, "..") != 0)
                {
                    int new_path_length = path.length() + strlen(dirent->d_name) + 2;
                    char *new_path = (char *)malloc(new_path_length);
                    snprintf(new_path, new_path_length, "%s%s%s", path.c_str(), FS::hasEndSlash(path.c_str()) ? "" : "/", dirent->d_name);

                    int ret = 0;

                    if (dirent->d_type & DT_DIR)
                    {
                        ret = ZipAddPath(zf, new_path, filename_start, level);
                    }
                    else
                    {
                        sprintf(activity_message, "%s %s", lang_strings[STR_COMPRESSING], new_path);
                        ret = ZipAddFile(zf, new_path, filename_start, level);
                    }

                    free(new_path);

                    // Some folders are protected and return 0x80010001. Bypass them
                    if (ret <= 0)
                    {
                        closedir(dfd);
                        return ret;
                    }
                }
            } while (dirent != NULL);

            closedir(dfd);
        }
        else
        {
            sprintf(activity_message, "%s %s", lang_strings[STR_COMPRESSING], path.c_str());
            return ZipAddFile(zf, path, filename_start, level);
        }

        return 1;
    }

    /* duplicate a path name, possibly converting to lower case */
    static char *pathdup(const char *path)
    {
        char *str;
        size_t i, len;

        if (path == NULL || path[0] == '\0')
            return (NULL);

        len = strlen(path);
        while (len && path[len - 1] == '/')
            len--;
        if ((str = (char *)malloc(len + 1)) == NULL)
        {
            errno = ENOMEM;
        }
        memcpy(str, path, len);

        str[len] = '\0';

        return (str);
    }

    /* concatenate two path names */
    static char *pathcat(const char *prefix, const char *path)
    {
        char *str;
        size_t prelen, len;

        prelen = prefix ? strlen(prefix) + 1 : 0;
        len = strlen(path) + 1;
        if ((str = (char *)malloc(prelen + len)) == NULL)
        {
            errno = ENOMEM;
        }
        if (prefix)
        {
            memcpy(str, prefix, prelen); /* includes zero */
            str[prelen - 1] = '/';       /* splat zero */
        }
        memcpy(str + prelen, path, len); /* includes zero */

        return (str);
    }

    /*
     * Extract a directory.
     */
    static void extract_dir(struct archive *a, struct archive_entry *e, const std::string &path)
    {
        int mode;

        if (path[0] == '\0')
            return;

        // disabled since next_header return incorrect folders
        // FS::MkDirs(path);

        archive_read_data_skip(a);
    }

    /*
     * Extract to a file descriptor
     */
    static int extract2fd(struct archive *a, const std::string &pathname, int fd)
    {
        ssize_t len;
        unsigned char *buffer = (unsigned char *)malloc(ARCHIVE_TRANSFER_SIZE);
        bytes_transfered = 0;
        prev_tick = Util::GetTick();

        /* loop over file contents and write to fd */
        for (int n = 0;; n++)
        {
            len = archive_read_data(a, buffer, ARCHIVE_TRANSFER_SIZE);

            if (len == 0)
            {
                free(buffer);
                return 1;
            }

            if (len < 0)
            {
                sprintf(status_message, "error archive_read_data('%s')", pathname.c_str());
                free(buffer);
                return 0;
            }
            bytes_transfered += len;

            if (write(fd, buffer, len) != len)
            {
                sprintf(status_message, "error write('%s')", pathname.c_str());
                free(buffer);
                return 0;
            }
        }

        free(buffer);
        return 1;
    }

    /*
     * Extract a regular file.
     */
    static void extract_file(struct archive *a, struct archive_entry *e, const std::string &path)
    {
        struct stat sb;
        int fd;
        const char *linkname;

        /* look for existing file of same name */
    recheck:
        if (lstat(path.c_str(), &sb) == 0)
        {
            (void)unlink(path.c_str());
        }

        /* process symlinks */
        linkname = archive_entry_symlink(e);
        if (linkname != NULL)
        {
            if (symlink(linkname, path.c_str()) != 0)
            {
                sprintf(status_message, "error symlink('%s')", path.c_str());
                return;
            }

            /* set access and modification time */
            return;
        }

        bytes_to_download = archive_entry_size(e);
        if ((fd = open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0777)) < 0)
        {
            sprintf(status_message, "error open('%s')", path.c_str());
            return;
        }

        extract2fd(a, path, fd);

        /* set access and modification time */
        if (close(fd) != 0)
        {
            return;
        }
    }

    static void extract(struct archive *a, struct archive_entry *e, const std::string &base_dir)
    {
        char *pathname, *realpathname;
        mode_t filetype;

        if ((pathname = pathdup(archive_entry_pathname(e))) == NULL)
        {
            archive_read_data_skip(a);
            return;
        }
        filetype = archive_entry_filetype(e);

        /* sanity checks */
        if (pathname[0] == '/' ||
            strncmp(pathname, "../", 3) == 0 ||
            strstr(pathname, "/../") != NULL)
        {
            archive_read_data_skip(a);
            free(pathname);
            return;
        }

        /* I don't think this can happen in a zipfile.. */
        if (!S_ISDIR(filetype) && !S_ISREG(filetype) && !S_ISLNK(filetype))
        {
            archive_read_data_skip(a);
            free(pathname);
            return;
        }

        realpathname = pathcat(base_dir.c_str(), pathname);

        if (S_ISDIR(filetype))
            extract_dir(a, e, realpathname);
        else
        {
            snprintf(activity_message, 255, "%s: %s", lang_strings[STR_EXTRACTING], pathname);
            
            /* ensure that parent directory exists */
            FS::MkDirs(realpathname, true);

            extract_file(a, e, realpathname);
        }

        free(realpathname);
        free(pathname);
    }

    /*
     * Callback function for reading passphrase.
     * Originally from cpio.c and passphrase.c, libarchive.
     */
    static const char *passphrase_callback(struct archive *a, void *_client_data)
    {
        Dialog::initImeDialog(lang_strings[STR_PASSWORD], password, 127, SCE_IME_TYPE_DEFAULT, 560, 200);
        int ime_result = Dialog::updateImeDialog();
        if (ime_result == IME_DIALOG_RESULT_FINISHED || ime_result == IME_DIALOG_RESULT_CANCELED)
        {
            if (ime_result == IME_DIALOG_RESULT_FINISHED)
            {
                snprintf(password, 127, "%s", (char *)Dialog::getImeDialogInputText());
                return password;
            }
            else
            {
                memset(password, 0, sizeof(password));
            }
        }

        memset(password, 0, sizeof(password));
        return password;
    }

    static RemoteArchiveData *OpenRemoteArchive(const std::string &file, RemoteClient *client)
    {
        RemoteArchiveData *data;

        data = (RemoteArchiveData *)malloc(sizeof(RemoteArchiveData));
        memset(data, 0, sizeof(RemoteArchiveData));

        data->offset = 0;
        client->Size(file, &data->size);
        data->client = client;
        data->path = file;
        if (client->SupportedActions() & REMOTE_ACTION_RAW_READ)
        {
            data->fp = client->Open(file, O_RDONLY);
        }
        return data;
    }

    static ssize_t ReadRemoteArchive(struct archive *a, void *client_data, const void **buff)
    {
        ssize_t to_read;
        int ret;
        RemoteArchiveData *data;

        data = (RemoteArchiveData *)client_data;
        *buff = data->buf;

        to_read = data->size - data->offset;
        if (to_read == 0)
            return 0;

        to_read = MIN(to_read, ARCHIVE_TRANSFER_SIZE);
        if (data->client->SupportedActions() & REMOTE_ACTION_RAW_READ)
            ret = data->client->GetRange((data->fp), data->buf, to_read, data->offset);
        else
            ret = data->client->GetRange(data->path, data->buf, to_read, data->offset);

        if (ret == 0)
            return -1;
        data->offset = data->offset + to_read;

        return to_read;
    }

    static int CloseRemoteArchive(struct archive *a, void *client_data)
    {
        if (client_data != nullptr)
        {
            RemoteArchiveData *data = (RemoteArchiveData *)client_data;
            if (data->client->SupportedActions() & REMOTE_ACTION_RAW_READ)
                data->client->Close(data->fp);
            free(client_data);
        }
        return 0;
    }

    int64_t SeekRemoteArchive(struct archive *, void *client_data, int64_t offset, int whence)
    {
        RemoteArchiveData *data = (RemoteArchiveData *)client_data;

        if (whence == SEEK_SET)
        {
            data->offset = offset;
        }
        else if (whence == SEEK_CUR)
        {
            data->offset = data->offset + offset - 1;
        }
        else if (whence == SEEK_END)
        {
            data->offset = data->size - 1;
        }
        else
            return ARCHIVE_FATAL;

        return data->offset;
    }

    int64_t SkipRemoteArchive(struct archive *, void *client_data, int64_t request)
    {
        RemoteArchiveData *data = (RemoteArchiveData *)client_data;

        data->offset = data->offset + request - 1;

        return request;
    }

    /*
     * Main loop: open the zipfile, iterate over its contents and decide what
     * to do with each entry.
     */
    int Extract(const DirEntry &file, const std::string &basepath, RemoteClient *client)
    {
        struct archive *a;
        struct archive_entry *e;
        RemoteArchiveData *client_data = nullptr;
        int ret;
        uintmax_t total_size, file_count, error_count;

        if ((a = archive_read_new()) == NULL)
            sprintf(status_message, "%s", "archive_read_new failed");

        archive_read_support_format_all(a);
        archive_read_support_filter_all(a);
        archive_read_set_passphrase_callback(a, NULL, &passphrase_callback);

        if (client == nullptr)
        {
            ret = archive_read_open_filename(a, file.path, ARCHIVE_TRANSFER_SIZE);
            if (ret < ARCHIVE_OK)
            {
                sprintf(status_message, "%s", "archive_read_open_filename failed");
                return 0;
            }
        }
        else
        {
            client_data = OpenRemoteArchive(file.path, client);
            if (client_data == nullptr)
            {
                sprintf(status_message, "%s", "archive_read_open_filename failed");
                return 0;
            }

            ret = archive_read_set_seek_callback(a, SeekRemoteArchive);
            if (ret < ARCHIVE_OK)
            {
                sprintf(status_message, "archive_read_set_seek_callback failed - %s", archive_error_string(a));
                return 0;
            }

            ret = archive_read_open2(a, client_data, NULL, ReadRemoteArchive, SkipRemoteArchive, CloseRemoteArchive);
            if (ret < ARCHIVE_OK)
            {
                sprintf(status_message, "%s", "archive_read_open failed");
                return 0;
            }
        }

        FS::MkDirs(basepath.c_str());
        for (;;)
        {
            if (stop_activity)
                break;

            ret = archive_read_next_header(a, &e);

            if (ret < ARCHIVE_OK)
            {
                sprintf(status_message, "%s", "archive_read_next_header failed");
                archive_read_free(a);
                return 0;
            }

            if (ret == ARCHIVE_EOF)
                break;

            extract(a, e, basepath);
        }

        archive_read_free(a);

        return 1;
    }

    ArchiveEntry *GetPackageEntry(const std::string &zip_file, RemoteClient *client)
    {
        struct archive *a;
        struct archive_entry *e;
        RemoteArchiveData *client_data = nullptr;
        char *pathname;
        mode_t filetype;
        ArchiveEntry *pkg_entry = nullptr;
        int ret;

        if ((a = archive_read_new()) == NULL)
        {
            sprintf(status_message, "%s", "archive_read_new failed");
            return nullptr;
        }

        archive_read_support_format_all(a);
        archive_read_support_filter_all(a);
        archive_read_set_passphrase_callback(a, NULL, &passphrase_callback);

        if (client == nullptr)
        {
            ret = archive_read_open_filename(a, zip_file.c_str(), ARCHIVE_TRANSFER_SIZE);
            if (ret < ARCHIVE_OK)
            {
                sprintf(status_message, "%s", "archive_read_open_filename failed");
                return nullptr;
            }
        }
        else
        {
            client_data = OpenRemoteArchive(zip_file, client);
            if (client_data == nullptr)
            {
                sprintf(status_message, "%s", "archive_read_open_filename failed");
                return nullptr;
            }

            ret = archive_read_set_seek_callback(a, SeekRemoteArchive);
            if (ret < ARCHIVE_OK)
            {
                sprintf(status_message, "archive_read_set_seek_callback failed - %s", archive_error_string(a));
                return 0;
            }

            ret = archive_read_open2(a, client_data, NULL, ReadRemoteArchive, SkipRemoteArchive, CloseRemoteArchive);
            if (ret < ARCHIVE_OK)
            {
                sprintf(status_message, "%s", "archive_read_open_filename failed");
                return nullptr;
            }
        }

        for (;;)
        {
            ret = archive_read_next_header(a, &e);

            if (ret < ARCHIVE_OK)
            {
                sprintf(status_message, "%s", "archive_read_next_header failed");
                archive_read_free(a);
                return nullptr;
            }

            if (ret == ARCHIVE_EOF)
            {
                break;
            }

            if ((pathname = pathdup(archive_entry_pathname(e))) == NULL)
            {
                archive_read_data_skip(a);
                continue;
            }

            filetype = archive_entry_filetype(e);

            /* sanity checks */
            if (pathname[0] == '/' ||
                strncmp(pathname, "../", 3) == 0 ||
                strstr(pathname, "/../") != NULL)
            {
                archive_read_data_skip(a);
                free(pathname);
                continue;
                ;
            }

            /* I don't think this can happen in a zipfile.. */
            if (!S_ISREG(filetype))
            {
                archive_read_data_skip(a);
                free(pathname);
                continue;
            }

            if (Util::EndsWith(Util::ToLower(pathname), ".pkg"))
            {
                pkg_entry = (ArchiveEntry *)malloc(sizeof(ArchiveEntry));
                memset(pkg_entry, 0, sizeof(ArchiveEntry));

                pkg_entry->archive = a;
                pkg_entry->entry = e;
                pkg_entry->client_data = client_data;
                pkg_entry->filename = pathname;
                pkg_entry->filesize = archive_entry_size(e);

                free(pathname);
                return pkg_entry;
            }

            free(pathname);
        }

        archive_read_free(a);

        return nullptr;
    }

    ArchiveEntry *GetNextPackageEntry(ArchiveEntry *archive_entry)
    {
        struct archive *a = archive_entry->archive;
        struct archive_entry *e = nullptr;
        RemoteArchiveData *client_data = archive_entry->client_data;
        char *pathname;
        mode_t filetype;
        ArchiveEntry *pkg_entry = nullptr;
        int ret;

        for (;;)
        {
            ret = archive_read_next_header(a, &e);

            if (ret < ARCHIVE_OK)
            {
                sprintf(status_message, "%s", "archive_read_next_header failed");
                archive_read_free(a);
                return nullptr;
            }

            if (ret == ARCHIVE_EOF)
                break;

            if ((pathname = pathdup(archive_entry_pathname(e))) == NULL)
            {
                archive_read_data_skip(a);
                continue;
            }

            filetype = archive_entry_filetype(e);

            /* sanity checks */
            if (pathname[0] == '/' ||
                strncmp(pathname, "../", 3) == 0 ||
                strstr(pathname, "/../") != NULL)
            {
                archive_read_data_skip(a);
                free(pathname);
                continue;
                ;
            }

            /* I don't think this can happen in a zipfile.. */
            if (!S_ISREG(filetype))
            {
                archive_read_data_skip(a);
                free(pathname);
                continue;
            }

            if (Util::EndsWith(Util::ToLower(pathname), ".pkg"))
            {
                pkg_entry = (ArchiveEntry *)malloc(sizeof(ArchiveEntry));
                memset(pkg_entry, 0, sizeof(ArchiveEntry));

                pkg_entry->archive = a;
                pkg_entry->entry = e;
                pkg_entry->client_data = client_data;
                pkg_entry->filename = pathname;
                pkg_entry->filesize = archive_entry_size(e);

                free(pathname);
                return pkg_entry;
            }

            free(pathname);
        }

        archive_read_free(a);

        return nullptr;
    }
}