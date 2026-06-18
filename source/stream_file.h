#ifndef EZ_STREAM_FILE_H
#define EZ_STREAM_FILE_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

class StreamFile
{
public:
    virtual ~StreamFile() {}
    virtual int Open() = 0;
    virtual size_t Read(char *buf, size_t buf_size, size_t offset) = 0;
    virtual ssize_t Write(char *buf, size_t buf_size) = 0;
    virtual int Close() = 0;
    virtual bool IsClosed() = 0;
};

#endif // EZ_STREAM_FILE_H
