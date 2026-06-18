#include "mem_file.h"

#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include "dbglogger.h"

MemFile::MemFile(size_t capacity, size_t peek_window)
    : m_buf(nullptr)
    , m_capacity(capacity)
    , m_peek_window(peek_window)
    , m_write_pos(0)
    , m_read_pos(0)
    , m_peek_pos(0)
    , m_used(0)
    , m_peek_used(0)
    , m_total_read(0)
    , m_closed(false)
    , m_aborted(false)
{
    m_buf = static_cast<uint8_t *>(malloc(capacity));
    pthread_mutex_init(&m_mutex, nullptr);
    pthread_cond_init(&m_cond_not_full,  nullptr);
    pthread_cond_init(&m_cond_not_empty, nullptr);
}

MemFile::~MemFile()
{
    Abort();
    pthread_cond_destroy(&m_cond_not_empty);
    pthread_cond_destroy(&m_cond_not_full);
    pthread_mutex_destroy(&m_mutex);
    free(m_buf);
}

int MemFile::Open()
{
    return 0;
}

ssize_t MemFile::Write(char *buf, size_t buf_size)
{
    if (m_buf == nullptr || buf_size == 0)
        return -1;

    const uint8_t *src   = reinterpret_cast<const uint8_t *>(buf);
    size_t         total = 0;

    dbglogger_log("MemFile::Write called with buf_size=%zu before mutex_lock", buf_size);
    pthread_mutex_lock(&m_mutex);

    while (total < buf_size)
    {
        // Wait while the buffer is full (based on peek position)
        while (m_peek_used == m_capacity && !m_aborted && !m_closed)
        {
            pthread_cond_wait(&m_cond_not_full, &m_mutex);
        }

        if (m_aborted || m_closed)
        {
            pthread_mutex_unlock(&m_mutex);
            return -1;
        }

        // Free space is determined by distance from m_peek_pos to m_write_pos
        size_t free_linear = m_capacity - m_write_pos;
        size_t free_total  = m_capacity - m_peek_used;
        size_t to_write    = std::min({ buf_size - total, free_total, free_linear });

        memcpy(m_buf + m_write_pos, src + total, to_write);
        m_write_pos  = (m_write_pos + to_write) % m_capacity;
        m_used      += to_write;
        m_peek_used += to_write;
        total       += to_write;

        pthread_cond_signal(&m_cond_not_empty);
    }

    pthread_mutex_unlock(&m_mutex);
    dbglogger_log("MemFile::Write finished writing, total=%zu", m_total_read);
    return (ssize_t)total;
}

size_t MemFile::Read(char *buf, size_t buf_size, size_t offset)
{
    //dbglogger_log("MemFile::Read called with buf_size=%zu, offset=%zu", buf_size, offset);
    if (m_buf == nullptr || buf_size == 0)
        return 0;

    uint8_t *dst   = reinterpret_cast<uint8_t *>(buf);
    size_t   total = 0;
    size_t   len   = buf_size;

    pthread_mutex_lock(&m_mutex);

    // Fail if offset is before the peek window (data already overwritten)
    size_t earliest_available = (m_total_read > m_peek_window) ? (m_total_read - m_peek_window) : 0;
    if (offset < earliest_available)
    {
        dbglogger_log("MemFile::Read failed, offset %zu is before earliest available %zu", offset, earliest_available);
        pthread_mutex_unlock(&m_mutex);
        return 0;
    }

    // Phase 1: If offset is within the peek window, read historical data first
    if (offset < m_total_read)
    {
        size_t rewind_amount = m_total_read - offset;
        size_t read_pos = (m_read_pos + m_capacity - rewind_amount) % m_capacity;
        size_t hist_available = rewind_amount;

        size_t hist_to_read = std::min(len, hist_available);
        size_t hist_read = 0;

        while (hist_read < hist_to_read)
        {
            size_t avail_linear = m_capacity - read_pos;
            size_t to_copy = std::min(hist_to_read - hist_read, avail_linear);

            memcpy(dst + total, m_buf + read_pos, to_copy);
            read_pos = (read_pos + to_copy) % m_capacity;
            hist_read += to_copy;
            total += to_copy;
        }

        dbglogger_log("MemFile::Read read %zu bytes from peek window, total=%zu", hist_read, total);
        // If we've satisfied the full request from the peek window, done
        if (total >= len)
        {
            pthread_mutex_unlock(&m_mutex);
            return total;
        }
    }

    // Phase 2: Read forward from m_read_pos (advances pointers)
    while (total < len)
    {
        while (m_used == 0 && !m_closed && !m_aborted)
        {
            dbglogger_log("MemFile::Read waiting, total=%zu, used=%zu", total, m_used);
            pthread_cond_wait(&m_cond_not_empty, &m_mutex);
        }

        if (m_aborted)
        {
            dbglogger_log("MemFile::Read aborted, total=%zu", total);
            pthread_mutex_unlock(&m_mutex);
            return total;
        }

        if (m_used == 0)
        {
            dbglogger_log("MemFile::Read closed with no more data, total=%zu", total);
            pthread_mutex_unlock(&m_mutex);
            return total;
        }

        size_t avail_linear = m_capacity - m_read_pos;
        size_t to_read      = std::min({ len - total, m_used, avail_linear });

        memcpy(dst + total, m_buf + m_read_pos, to_read);
        m_read_pos   = (m_read_pos + to_read) % m_capacity;
        m_used      -= to_read;
        m_total_read += to_read;
        total       += to_read;

        // Advance m_peek_pos to maintain the peek window constraint
        size_t peek_lag = m_peek_used - m_used;
        if (peek_lag > m_peek_window)
        {
            size_t advance = peek_lag - m_peek_window;
            m_peek_pos   = (m_peek_pos + advance) % m_capacity;
            m_peek_used -= advance;
        }

        dbglogger_log("MemFile::Read signaling not_full, total=%zu", total);
        pthread_cond_signal(&m_cond_not_full);

        break;
    }

    pthread_mutex_unlock(&m_mutex);
    dbglogger_log("MemFile::Read finished reading, total=%zu", total);
    return total;
}

int MemFile::Close()
{
    pthread_mutex_lock(&m_mutex);
    m_closed = true;
    pthread_cond_broadcast(&m_cond_not_empty); // wake any blocked reader
    pthread_mutex_unlock(&m_mutex);
    return 0;
}

bool MemFile::IsClosed()
{
    return m_closed;
}

void MemFile::Abort()
{
    pthread_mutex_lock(&m_mutex);
    m_aborted = true;
    pthread_cond_broadcast(&m_cond_not_full);  // wake blocked writer
    pthread_cond_broadcast(&m_cond_not_empty); // wake blocked reader
    pthread_mutex_unlock(&m_mutex);
}
