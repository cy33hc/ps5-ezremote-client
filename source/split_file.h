#ifndef EZ_SPLIT_FILE_H
#define EZ_SPLIT_FILE_H

#include <string>
#include <vector>
#include <mutex>
#include <semaphore.h>
#include <pthread.h>
#include <shared_mutex>
#include "stream_file.h"

enum FileBlockStatus
{
    BLOCK_STATUS_NOT_EXISTS,
    BLOCK_STATUS_CREATED,
    BLOCK_STATUS_DELETED
};

typedef struct
{
    std::string block_file;
    size_t size;
    FILE* fd;
    bool is_last;
    FileBlockStatus status;
} FileBlock;

class SplitFile : public StreamFile
{
public:
    SplitFile(const std::string& path, size_t block_size);
    ~SplitFile() override;
    size_t Read(char* buf, size_t buf_size, size_t offset) override;
    ssize_t Write(char* buf, size_t buf_size) override;
    int Open() override;
    int Close() override;
    bool IsClosed() override;

private:
    std::vector<FileBlock*> file_blocks;
    size_t write_offset = 0;
    size_t block_size;
    size_t read_offset;
    std::string path;
    int write_error;
    bool complete;
    FileBlock *block_in_progress;
    sem_t block_ready;
    std::shared_mutex mutex_;

    FileBlock *NewBlock();
};

#endif
