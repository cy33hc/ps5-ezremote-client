#include <stdio.h>
#include "unistd.h"
#include <string>

#include "common.h"
#include "split_file.h"

SplitFile::SplitFile(const std::string &path, size_t block_size, size_t file_size)
{
    this->block_size = block_size;
    this->file_size = file_size;
    this->path = path;
    this->complete = false;
    this->block_index = 0;

    // Number of fixed-size blocks needed to hold the file. The final block may
    // be partial. Always keep at least one slot so Close() can publish a final
    // (possibly empty) is_last block even for a zero-byte file.
    this->num_blocks = (block_size > 0) ? ((file_size + block_size - 1) / block_size) : 0;
    if (this->num_blocks == 0)
        this->num_blocks = 1;

    // Size the slot array once; it is never resized, so slot addresses are
    // stable and can be published/consumed lock-free.
    this->file_blocks = std::vector<std::atomic<FileBlock *>>(this->num_blocks);
    for (size_t i = 0; i < this->num_blocks; i++)
        this->file_blocks[i].store(nullptr, std::memory_order_relaxed);

    sem_init(&this->block_ready, 0, 0);
}

SplitFile::~SplitFile()
{
    for (size_t i = 0; i < this->num_blocks; i++)
    {
        FileBlock *block = this->file_blocks[i].load(std::memory_order_relaxed);
        if (block != nullptr && block->status != BLOCK_STATUS_DELETED)
        {
            if (block->fd != nullptr)
            {
                fclose(block->fd);
            }
            remove(block->block_file.c_str());
            delete block;
            this->file_blocks[i].store(nullptr, std::memory_order_relaxed);
        }
    }
    sem_destroy(&this->block_ready);
};

int SplitFile::Open()
{
    this->block_in_progress = NewBlock();
    return (block_in_progress->fd == nullptr);
}

ssize_t SplitFile::Read(char *buf, size_t buf_size, size_t offset)
{
    int first_block_num, block_num;
    size_t block_offset;
    size_t remaining;
    size_t bytes_read;
    ssize_t total_bytes_read;
    FileBlock *block;
    FILE *fd;
    char *p;

    first_block_num = offset / this->block_size;
    block_num = first_block_num;
    block_offset = offset % this->block_size;

    // Wait until the requested block has been published, or the writer is done.
    // Readiness is determined by the slot pointer, not by a growing count: the
    // slot array has its final size from construction.
    while (block_num < (int)this->num_blocks &&
           this->file_blocks[block_num].load(std::memory_order_acquire) == nullptr &&
           !this->complete)
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 2;
        sem_timedwait(&this->block_ready, &ts);
    }

    // Offset beyond the last block, or block never arrived before completion.
    if (block_num >= (int)this->num_blocks)
        return 0;

    block = this->file_blocks[block_num].load(std::memory_order_acquire);
    if (block == nullptr)
        return 0;

    if (block->status == BLOCK_STATUS_DELETED)
    {
        return -1;
    }

    if (block_offset > block->size - 1 && this->complete)
    {
        // requested offset is pass the end of split file
        return 0;
    }

    remaining = buf_size;
    bool eof = false;
    total_bytes_read = 0;
    p = buf;

    while (remaining > 0 && !eof)
    {
        fd = block->fd;
        if (fd == nullptr)
        {
            fd = fopen(block->block_file.c_str(), "rb");
            block->fd = fd;
        }

        fseek(fd, block_offset, SEEK_SET);
        bytes_read = fread(p, 1, remaining, fd);

        if (bytes_read == remaining)
        {
            p += bytes_read;
            total_bytes_read += bytes_read;
        }
        else
        {
            if (feof(fd))
            {
                p += bytes_read;
                total_bytes_read += bytes_read;
                if (block->is_last)
                {
                    eof = true;
                    continue;
                }
            }
            else
                return -1;
        }

        remaining -= bytes_read;

        if (remaining == 0)
            continue;

        block_num++;
        block_offset = 0;

        // Reached the end of the slot array: no more data.
        if (block_num >= (int)this->num_blocks)
            break;

        // Wait for the next block to be published, unless the writer is done.
        while (this->file_blocks[block_num].load(std::memory_order_acquire) == nullptr &&
               !this->complete)
        {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 2;
            sem_timedwait(&this->block_ready, &ts);
        }

        block = this->file_blocks[block_num].load(std::memory_order_acquire);
        if (block == nullptr)
            break;
    }

    // delete blocks before the first read offset block. Assumuption, that reads are always
    // forward and won't read previously already read blocks. For safety, keeping only current block and 2 previous blocks
    for (int j = 0; j < first_block_num - 13; j++)
    {
        FileBlock *old = this->file_blocks[j].load(std::memory_order_acquire);
        if (old != nullptr && old->status == BLOCK_STATUS_CREATED)
        {
            if (old->fd != nullptr)
            {
                fclose(old->fd);
                old->fd = nullptr;
            }
            old->status = BLOCK_STATUS_DELETED;
            remove(old->block_file.c_str());
            delete old;
            this->file_blocks[j].store(nullptr, std::memory_order_release);
        }
    }

    this->read_offset = offset + total_bytes_read;
    return total_bytes_read;
}

ssize_t SplitFile::Write(char *buf, size_t buf_size)
{
    size_t bytes_written = 0;
    size_t block_space_remaining;
    size_t bytes_to_write;

    char *p = buf;
    ssize_t total_bytes_written = 0;
    size_t remaining_to_write = buf_size;

    if (this->IsClosed())
        return -1;

    while (remaining_to_write > 0 && !this->complete)
    {
        block_space_remaining = this->block_size - block_in_progress->size;
        bytes_to_write = MIN(remaining_to_write, block_space_remaining);

        bytes_written = fwrite(p, 1, bytes_to_write, block_in_progress->fd);
        block_in_progress->size += bytes_written;
        total_bytes_written += bytes_written;
        remaining_to_write -= bytes_written;
        block_space_remaining -= bytes_written;
        p += bytes_written;

        // error if bytes_to_write != bytes_written
        if (bytes_written != bytes_to_write)
        {
            break;
        }

        if (block_space_remaining == 0)
        {
            fflush(block_in_progress->fd);
            fclose(block_in_progress->fd);
            block_in_progress->fd = nullptr;
            block_in_progress->status = BLOCK_STATUS_CREATED;

            // Publish the finished block into its slot (release), then advance.
            // Guard against an index overrun if the written size exceeds the
            // file_size the array was sized for.
            if (this->block_index < this->num_blocks)
            {
                this->file_blocks[this->block_index].store(block_in_progress, std::memory_order_release);
                this->block_index++;
            }

            sem_post(&this->block_ready);

            block_in_progress = NewBlock();
        }
    }
    this->write_offset += total_bytes_written;

    return total_bytes_written;
}

int SplitFile::Close()
{
    bool expected = false;
    // Publish completion exactly once. If already complete, nothing to do.
    if (!this->complete.compare_exchange_strong(expected, true))
        return 0;

    if (block_in_progress->fd != nullptr)
    {
        fflush(block_in_progress->fd);
        fclose(block_in_progress->fd);
        block_in_progress->fd = nullptr;
    }
    block_in_progress->status = BLOCK_STATUS_CREATED;
    block_in_progress->is_last = true;

    // Publish the final (possibly partial) block into its slot.
    if (this->block_index < this->num_blocks)
    {
        this->file_blocks[this->block_index].store(block_in_progress, std::memory_order_release);
        this->block_index++;
    }
    sem_post(&this->block_ready);

    // Wait until file is fully read, if file isn't full read
    // in time then go ahead and delete all file chunks
    int retries = 10;
    size_t prev_read_offset = 0;
    while (this->read_offset != this->write_offset && retries > 0)
    {
        if (prev_read_offset == this->read_offset)
            retries--;
        prev_read_offset = this->read_offset;
        sleep(1);
    }
    sleep(5);

    for (size_t j = 0; j < this->num_blocks; j++)
    {
        FileBlock *block = this->file_blocks[j].load(std::memory_order_acquire);
        if (block != nullptr && block->status == BLOCK_STATUS_CREATED)
        {
            remove(block->block_file.c_str());
        }
    }
    return 0;
}

bool SplitFile::IsClosed()
{
    return this->complete;
}

FileBlock *SplitFile::NewBlock()
{
    FileBlock *block = new FileBlock{};

    block->is_last = false;
    block->size = 0;
    block->block_file = this->path + "." + std::to_string(this->block_index);
    block->fd = fopen(block->block_file.c_str(), "w");

    return block;
}
