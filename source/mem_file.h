#ifndef EZ_MEM_FILE_H
#define EZ_MEM_FILE_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include "stream_file.h"

class MemFile : public StreamFile
{
public:
    explicit MemFile(size_t capacity, size_t peek_window = 0);
    ~MemFile() override;
    int Open() override;
    size_t Read(char *buf, size_t buf_size, size_t offset) override;
    ssize_t Write(char *buf, size_t buf_size) override;
    int Close() override;
    bool IsClosed() override;

    void Abort();
    bool IsAborted() const { return m_aborted; }

private:
    uint8_t        *m_buf;
    size_t          m_capacity;
    size_t          m_peek_window;

    size_t          m_write_pos;
    size_t          m_read_pos;
    size_t          m_peek_pos;
    size_t          m_used;
    size_t          m_peek_used;
    size_t          m_total_read;

    bool            m_closed;
    bool            m_aborted;

    pthread_mutex_t m_mutex;
    pthread_cond_t  m_cond_not_full;
    pthread_cond_t  m_cond_not_empty;

    MemFile(const MemFile &) = delete;
    MemFile &operator=(const MemFile &) = delete;
};

#endif // EZ_MEM_FILE_H
