#ifndef REMOTECLIENT_H
#define REMOTECLIENT_H

#include <string>
#include <vector>
#include "common.h"
#include "http/httplib.h"
#include "split_file.h"

enum RemoteActions
{
    REMOTE_ACTION_NONE = 0,
    REMOTE_ACTION_CUT = 1,
    REMOTE_ACTION_COPY = 2,
    REMOTE_ACTION_PASTE = 4,
    REMOTE_ACTION_DELETE = 8,
    REMOTE_ACTION_RENAME = 16,
    REMOTE_ACTION_NEW_FOLDER = 32,
    REMOTE_ACTION_DOWNLOAD = 64,
    REMOTE_ACTION_UPLOAD = 128,
    REMOTE_ACTION_INSTALL = 256,
    REMOTE_ACTION_EDIT = 512,
    REMOTE_ACTION_NEW_FILE = 1024,
    REMOTE_ACTION_EXTRACT = 2048,
    REMOTE_ACTION_RAW_READ = 4096,
    REMOTE_ACTION_ALL = 8191
};

enum ClientType
{
    CLIENT_TYPE_FTP,
    CLIENT_TYPE_SFTP,
    CLIENT_TYPE_SMB,
    CLIENT_TYPE_WEBDAV,
    CLIENT_TYPE_HTTP_SERVER,
    CLIENT_TYPE_NFS,
    CLINET_TYPE_UNKNOWN
};

using namespace httplib;

class RemoteClient
{
public:
    RemoteClient(){};
    virtual ~RemoteClient(){};
    virtual int Connect(const std::string &url, const std::string &username, const std::string &password, bool send_ping=false) = 0;
    virtual int Mkdir(const std::string &path) = 0;
    virtual int Rmdir(const std::string &path, bool recursive) = 0;
    virtual int Size(const std::string &path, uint64_t *size) = 0;
    virtual int Get(const std::string &outputfile, const std::string &path, uint64_t offset=0) = 0;
    virtual int Get(SplitFile *split_file, const std::string &path, uint64_t offset=0) = 0;
    virtual int Put(const std::string &inputfile, const std::string &path, uint64_t offset=0) = 0;
    virtual int Rename(const std::string &src, const std::string &dst) = 0;
    virtual int Delete(const std::string &path) = 0;
    virtual int Copy(const std::string &from, const std::string &to) = 0;
    virtual int Move(const std::string &from, const std::string &to) = 0;
    virtual int Head(const std::string &path, void *buffer, uint64_t len) = 0;
    virtual int GetRange(const std::string &path, void *buffer, uint64_t size, uint64_t offset) = 0;
    virtual int GetRange(const std::string &path, DataSink &sink, uint64_t size, uint64_t offset) = 0;
    virtual int GetRange(void *fp, void *buffer, uint64_t size, uint64_t offset) = 0;
    virtual int GetRange(void *fp, DataSink &sink, uint64_t size, uint64_t offset) = 0;
    virtual bool FileExists(const std::string &path) = 0;
    virtual std::vector<DirEntry> ListDir(const std::string &path) = 0;
    virtual void *Open(const std::string &path, int flags) = 0;
    virtual void Close(void *fp) = 0;
    virtual std::string GetPath(std::string path1, std::string path2) = 0;
    virtual bool IsConnected() = 0;
    virtual bool Ping() = 0;
    virtual const char *LastResponse() = 0;
    virtual int Quit() = 0;
    virtual ClientType clientType() = 0;
    virtual uint32_t SupportedActions() = 0;
};

#endif
