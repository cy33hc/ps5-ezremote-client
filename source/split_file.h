#ifndef EZ_SPLIT_FILE_H
#define EZ_SPLIT_FILE_H

#include <string>
#include <vector>
#include <atomic>
#include <semaphore.h>
#include <pthread.h>

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

// SplitFile buffers a logical file as fixed-size chunks on disk, acting as a
// single-producer / single-consumer pipe: one thread writes (Open/Write/Close)
// while another reads (Read).
//
// Because the total file size is known up front, the block slot array is sized
// once in the constructor and never resized. This keeps slot addresses stable,
// so the writer can publish a finished block by storing its pointer into the
// slot (release) while the reader loads the slot (acquire) with no lock. The
// semaphore is used only to wake a reader that is waiting for the next block.
class SplitFile
{
public:
    SplitFile(const std::string& path, size_t block_size, size_t file_size);
    ~SplitFile();
    ssize_t Read(char* buf, size_t buf_size, size_t offset);
    ssize_t Write(char* buf, size_t buf_size);
    int Open();
    int Close();
    bool IsClosed();

private:
    // One atomic pointer slot per block; sized once and never resized.
    std::vector<std::atomic<FileBlock*>> file_blocks;
    size_t num_blocks;
    std::atomic<size_t> write_offset{0};
    size_t block_size;
    size_t file_size;
    std::atomic<size_t> read_offset{0};
    std::string path;
    int write_error;
    std::atomic<bool> complete{false};
    FileBlock *block_in_progress;
    size_t block_index;   // next slot the writer will publish into
    sem_t block_ready;

    FileBlock *NewBlock();
};

#endif
