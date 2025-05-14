#include "mb_device.h"
#include <stdlib.h>

/// @file
/// Initialize the Modbus device.
cl_mb_device*
cl_device_init_tcp(char const* name,
                   /// Initial number of channels in the device.
                   /// The functions should allocate enough space
                   /// for all the channel data.
                   size_t n_channels)
{

    cl_mb_device* device;

    device = malloc(sizeof(*device));

    device->name = name;
    device->id = 0;
    device->channel_count = 0;
    device->name_identifier = "AI";

    device->ip = "127.0.0.1";
    device->port = 5502;

    cl_mb_channel* channels =
      (cl_mb_channel*)malloc(n_channels * sizeof(cl_mb_channel));

    device->channels = channels;

    for (int i = 0; i < n_channels; i++) {
        device->channels[i].id = i;
        device->channels[i].name = "CH";
        device->channels[i].value_type = Int;
        device->channels[i].value = (float)i;
        device->channel_count++;
    }
    return device;
}

/// Clear the device and free its memory including channel memory.
int
cl_device_destroy(cl_mb_device* device)
{
    free(device->channels);

    free(device);

    return EXIT_SUCCESS;
}
