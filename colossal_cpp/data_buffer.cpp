#include "data_buffer.h"
#include "mb_device.h"
#include <ctime>
#include <stdlib.h>
#include <threads.h>

/// @file
/// Initialize a buffer, allocate memory for the data and initialize the mtx and cnd variables.
bool buf_init(Buffer *buf_ptr, size_t size)
{

    if ((buf_ptr->device_data = (MbDevice *)malloc(size * sizeof(MbDevice))) == nullptr)
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

    free(buf_ptr->device_data);
}

/// Insert a new product into the buffer.
bool buf_put(Buffer *buf_ptr, MbDevice data)
{
    mtx_lock(&buf_ptr->mtx);

    // If the buffer is full wait for cnd.
    while (buf_ptr->count == buf_ptr->size)
    {
        if (cnd_wait(&buf_ptr->cnd_put, &buf_ptr->mtx) != thrd_success)
        {
            mtx_unlock(&buf_ptr->mtx);
            return false;
        }
    }
    // Insert new product at tip.
    buf_ptr->device_data[buf_ptr->tip] = data;

    // Update tip. If tip is > size, wrap back to start.
    buf_ptr->tip = (buf_ptr->tip + 1) % buf_ptr->size;

    ++buf_ptr->count;

    mtx_unlock(&buf_ptr->mtx);
    // cnd_signal(&buf_ptr->cnd_get);

    return true;
}

/// Get product from the ring buffer and remove it.
/// If the buffer is empty, wait sec * seconds.
bool buf_get(Buffer *buf_ptr, MbDevice *data_ptr, int sec)
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

    *data_ptr = buf_ptr->device_data[buf_ptr->tail];
    buf_ptr->tail = (buf_ptr->tail + 1) % buf_ptr->size;
    --buf_ptr->count;

    mtx_unlock(&buf_ptr->mtx);
    cnd_signal(&buf_ptr->cnd_put);

    return true;
}
