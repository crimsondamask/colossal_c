#include "link.h"
#include "mb_device.h"
#include <stdbool.h>
#include <threads.h>

/// @file
/// Ring buffer to hold the data polled by the polling thread.
/// The data is later consumed by main.
typedef struct Buffer
{
    /// A pointer to the link data.
    Link *link;
    /// A pointer array to the tags data
    Tag **tags_ptrs;
    /// Maximum and current number of elements.
    size_t size, count;
    size_t tip, tail; /// Index of the next free spot.
    /// A mutex to protect the data and the conditional variables.
    mtx_t mtx;
    /// Conditional variables to communicate signals.
    cnd_t cnd_put, cnd_get;

} Buffer;

/// Used to send configuration updates from main to the device threads.
/// We need a way to communicate data from GUI -> threads.
typedef struct ConfigUpdate
{
    mtx_t mtx;
    Link *new_link_update;
    bool pending_update;
    bool reconnect_required;
} ConfigUpdate;

bool config_update_init(ConfigUpdate *config_update);
bool config_update_put(ConfigUpdate *config_update_ptr, Link *config_src, bool reconnect_required);
bool config_update_get(ConfigUpdate *config_update_ptr, Link *config_dst, bool *reconnect_required);

bool buf_init(Buffer *buf_ptr, size_t size);
void buf_destroy(Buffer *buf_ptr);
bool buf_put(Buffer *buf_ptr, Link data);
bool buf_get(Buffer *but_ptr, Link *data_ptr, int sec);
bool buf_peek_last(Buffer *but_ptr, Link *data_ptr);
