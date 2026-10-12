#ifndef EZ_SPLIT_FILE_H
#define EZ_SPLIT_FILE_H

#include <string>
#include <vector>
#include <mutex>
#include <time.h>
#include <semaphore.h>
#include <pthread.h>
#include <shared_mutex>

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

class SplitFile
{
public:
    // Default pacing multiplier for the write throttle.
    static constexpr double DEFAULT_WRITE_SPEEDUP = 1.5;

    SplitFile(const std::string& path, size_t block_size, size_t write_throttle_distance = 0,
              double write_speedup = DEFAULT_WRITE_SPEEDUP);
    ~SplitFile();
    ssize_t Read(char* buf, size_t buf_size, size_t offset);
    ssize_t Write(char* buf, size_t buf_size);
    int Open();
    int Close();
    bool IsClosed();

    void SetWriteSpeedup(double write_speedup);
    double GetWriteSpeedup() const { return this->write_speedup; }

    double GetReadSpeed();   // bytes/s, rate of the time since the last sample
    size_t GetReadBytes();   // total bytes returned by Read()

private:
    std::vector<FileBlock*> file_blocks;
    size_t write_offset = 0;
    size_t block_size;
    size_t read_offset = 0;
    std::string path;
    int write_error;
    bool complete;
    FileBlock *block_in_progress;
    sem_t block_ready;
    std::shared_mutex mutex_;

    size_t read_bytes_total = 0;
    bool read_speed_sampling = false;
    size_t read_speed_sample_bytes = 0;
    double read_speed = 0.0;
    struct timespec read_speed_sample_time = {0, 0};

    size_t write_throttle_distance = 0;
    double write_speedup = DEFAULT_WRITE_SPEEDUP;
    bool write_throttle_sampling = false;
    size_t write_throttle_sample_offset = 0;
    struct timespec write_throttle_sample_time = {0, 0};

    void ThrottleWrite(size_t pending_write_offset);
    FileBlock *NewBlock();
};

#endif
