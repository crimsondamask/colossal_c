#include "mb_device.h"
#include <stdbool.h>
#include <threads.h>

/// @file
/// Ring buffer to hold the data polled by the polling thread.
/// The data is later consumed by main.
typedef struct Buffer
{
    /// A pointer to the device data.
    MbDevice *device_data;
    /// Maximum and current number of elements.
    size_t size, count;
    size_t tip, tail; /// Index of the next free spot.
    /// A mutex to protect the data and the conditional variables.
    mtx_t mtx;
    /// Conditional variables to communicate signals.
    cnd_t cnd_put, cnd_get;

} Buffer;

bool buf_init(Buffer *buf_ptr, size_t size);
void buf_destroy(Buffer *buf_ptr);

bool buf_put(Buffer *buf_ptr, MbDevice data);
bool buf_get(Buffer *but_ptr, MbDevice *data_ptr, int sec);
