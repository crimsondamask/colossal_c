#include "data_buffer.h"
#include "mb_device.h"
#include <stdlib.h>
#include <threads.h>

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

bool buf_put(Buffer *buf_ptr, MbDevice data)
{
    // TODO
    return false;
}
bool buf_get(Buffer *but_ptr, MbDevice *data_ptr, int sec)
{
    // TODO
    return false;
}
