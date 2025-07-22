#include "data_buffer.h"
#include "link.h"
#include <ctime>
#include <stdlib.h>
#include <threads.h>

/// @file
/// Initialize a buffer, allocate memory for the data and initialize the mtx and cnd variables.
bool buf_init(Buffer *buf_ptr, size_t size)
{

    if (!(buf_ptr->tags_ptrs = (Tag **)malloc(size * sizeof(Tag *))))
    {
        return false;
    }
    for (size_t i = 0; i < size; i++)
    {
        if (!(buf_ptr->tags_ptrs[i] = (Tag *)malloc(N_CHANNELS * sizeof(Tag))))
        {
            return false;
        }
    }
    if ((buf_ptr->link = (Link *)malloc(size * sizeof(Link))) == nullptr)
    {
        return false;
    }
    buf_ptr->size = size;
    buf_ptr->count = 0;
    buf_ptr->tip = 0;
    buf_ptr->tail = 0;

    return mtx_init(&buf_ptr->mtx, mtx_plain) == thrd_success && cnd_init(&buf_ptr->cnd_put) == thrd_success &&
           cnd_init(&buf_ptr->cnd_get) == thrd_success;
}

void buf_destroy(Buffer *buf_ptr)
{
    cnd_destroy(&buf_ptr->cnd_get);
    cnd_destroy(&buf_ptr->cnd_put);

    mtx_destroy(&buf_ptr->mtx);

    free(buf_ptr->link);
}

/// Insert a new product into the buffer.
bool buf_put(Buffer *buf_ptr, Link data)
{
    mtx_lock(&buf_ptr->mtx);

    // If the buffer is full wait for cnd.
    while (buf_ptr->count == buf_ptr->size)
    {
        mtx_unlock(&buf_ptr->mtx);
        return false;
    }
    // Insert new product at tip.
    // Save the tags value to the respective allocated space.
    for (size_t i = 0; i < N_CHANNELS; i++)
    {
        buf_ptr->tags_ptrs[buf_ptr->tip][i] = data.tags[i];
    }
    // Update the buffer link at index (tip).
    buf_ptr->link[buf_ptr->tip] = data;
    // Assign the link tags array to the respective allocated space.
    buf_ptr->link[buf_ptr->tip].tags = buf_ptr->tags_ptrs[buf_ptr->tip];

    // Update tip. If tip is > size, wrap back to start.
    buf_ptr->tip = (buf_ptr->tip + 1) % buf_ptr->size;

    ++buf_ptr->count;

    mtx_unlock(&buf_ptr->mtx);
    // cnd_signal(&buf_ptr->cnd_get);

    return true;
}

/// Get product from the ring buffer and remove it.
/// If the buffer is empty, wait sec * seconds.
bool buf_get(Buffer *buf_ptr, Link *data_ptr, int sec)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC); // current time.
    ts.tv_sec += sec;

    mtx_lock(&buf_ptr->mtx);

    while (buf_ptr->count == 0)
    {
        // if (cnd_timedwait(&buf_ptr->cnd_get, &buf_ptr->mtx, &ts) != thrd_success)
        // {
        // }
        mtx_unlock(&buf_ptr->mtx);
        return false;
    }

    *data_ptr = buf_ptr->link[buf_ptr->tail];
    buf_ptr->tail = (buf_ptr->tail + 1) % buf_ptr->size;
    --buf_ptr->count;

    mtx_unlock(&buf_ptr->mtx);
    cnd_signal(&buf_ptr->cnd_put);

    return true;
}

/// Get last product from the ring buffer.
bool buf_peek_last(Buffer *buf_ptr, Link *data_ptr)
{

    mtx_lock(&buf_ptr->mtx);

    while (buf_ptr->count == 0)
    {
        mtx_unlock(&buf_ptr->mtx);
        return false;
    }

    *data_ptr = buf_ptr->link[buf_ptr->tip - 1];

    mtx_unlock(&buf_ptr->mtx);

    return true;
}

bool config_update_init(ConfigUpdate *config_update)
{
    config_update->pending_update = false;

    return (mtx_init(&config_update->mtx, mtx_plain) == thrd_success);
}

/// Get the new device config (including channels config) from GUI (main)
/// and put it in ConfigUpdate pointer to be read from the thread.
bool config_update_put(ConfigUpdate *config_update_ptr, Link *config_src, bool reconnect_required)
{
    mtx_lock(&config_update_ptr->mtx);

    if (config_src == NULL)
    {
        mtx_unlock(&config_update_ptr->mtx);
        return false;
    }
    config_update_ptr->new_link_update = config_src;

    // Indication that there is a configuration update
    // otherwise the thread can't know when to update the config.
    config_update_ptr->pending_update = true;
    if (reconnect_required)
    {
        config_update_ptr->reconnect_required = true;
    }

    mtx_unlock(&config_update_ptr->mtx);

    return true;
}

bool config_update_get(ConfigUpdate *config_update_ptr, Link *config_dst, bool *reconnect_required)
{
    mtx_lock(&config_update_ptr->mtx);

    if (!config_update_ptr->pending_update || config_dst == NULL)
    {
        mtx_unlock(&config_update_ptr->mtx);
        return false;
    }

    *config_dst = *config_update_ptr->new_link_update;
    // Reset the flag
    config_update_ptr->pending_update = false;

    *reconnect_required = config_update_ptr->reconnect_required;

    mtx_unlock(&config_update_ptr->mtx);

    return true;
}
