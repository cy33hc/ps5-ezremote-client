#include "mem_file.h"

#include <stdlib.h>
#include <string.h>
#include <algorithm>

MemFile::MemFile(size_t capacity)
    : m_buf(nullptr)
    , m_capacity(capacity)
    , m_write_pos(0)
    , m_read_pos(0)
    , m_used(0)
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

int MemFile::Write(const void *buf, size_t len)
{
    if (m_buf == nullptr || len == 0)
        return -1;

    const uint8_t *src   = static_cast<const uint8_t *>(buf);
    size_t         total = 0;

    pthread_mutex_lock(&m_mutex);

    while (total < len)
    {
        // Wait while the buffer is full
        while (m_used == m_capacity && !m_aborted && !m_closed)
            pthread_cond_wait(&m_cond_not_full, &m_mutex);

        if (m_aborted || m_closed)
        {
            pthread_mutex_unlock(&m_mutex);
            return -1;
        }

        // How much space is available before wrap-around
        size_t free_linear = m_capacity - m_write_pos; // to end of buffer
        size_t free_total  = m_capacity - m_used;
        size_t to_write    = std::min({ len - total, free_total, free_linear });

        memcpy(m_buf + m_write_pos, src + total, to_write);
        m_write_pos = (m_write_pos + to_write) % m_capacity;
        m_used     += to_write;
        total      += to_write;

        pthread_cond_signal(&m_cond_not_empty);
    }

    pthread_mutex_unlock(&m_mutex);
    return (int)total;
}

int MemFile::Read(void *buf, size_t len)
{
    if (m_buf == nullptr || len == 0)
        return -1;

    uint8_t *dst   = static_cast<uint8_t *>(buf);
    size_t   total = 0;

    pthread_mutex_lock(&m_mutex);

    while (total < len)
    {
        // Wait while buffer is empty and writer hasn't finished
        while (m_used == 0 && !m_closed && !m_aborted)
            pthread_cond_wait(&m_cond_not_empty, &m_mutex);

        if (m_aborted)
        {
            pthread_mutex_unlock(&m_mutex);
            return -1;
        }

        if (m_used == 0)
        {
            // Buffer empty and writer closed — EOF
            pthread_mutex_unlock(&m_mutex);
            return (int)total;
        }

        // How many contiguous bytes can be read before wrap-around
        size_t avail_linear = m_capacity - m_read_pos;
        size_t to_read      = std::min({ len - total, m_used, avail_linear });

        memcpy(dst + total, m_buf + m_read_pos, to_read);
        m_read_pos = (m_read_pos + to_read) % m_capacity;
        m_used    -= to_read;
        total     += to_read;

        pthread_cond_signal(&m_cond_not_full);

        break;
    }

    pthread_mutex_unlock(&m_mutex);
    return (int)total;
}

void MemFile::Close()
{
    pthread_mutex_lock(&m_mutex);
    m_closed = true;
    pthread_cond_broadcast(&m_cond_not_empty); // wake any blocked reader
    pthread_mutex_unlock(&m_mutex);
}

void MemFile::Abort()
{
    pthread_mutex_lock(&m_mutex);
    m_aborted = true;
    pthread_cond_broadcast(&m_cond_not_full);  // wake blocked writer
    pthread_cond_broadcast(&m_cond_not_empty); // wake blocked reader
    pthread_mutex_unlock(&m_mutex);
}

size_t MemFile::Available() const
{
    pthread_mutex_lock(const_cast<pthread_mutex_t *>(&m_mutex));
    size_t v = m_used;
    pthread_mutex_unlock(const_cast<pthread_mutex_t *>(&m_mutex));
    return v;
}

size_t MemFile::FreeSpace() const
{
    pthread_mutex_lock(const_cast<pthread_mutex_t *>(&m_mutex));
    size_t v = m_capacity - m_used;
    pthread_mutex_unlock(const_cast<pthread_mutex_t *>(&m_mutex));
    return v;
}
