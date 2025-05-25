#include "mb_device.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// @file
/// Initialize the Modbus device.
MbDevice cl_device_init_tcp(char const *name, int id,
                            /// Initial number of channels in the device.
                            /// The functions should allocate enough space
                            /// for all the channel data.
                            size_t n_channels)
{

    MbDevice device;

    // device = (cl_mb_device *)malloc(sizeof(*device));

    device.id = id;
    device.channel_count = 0;
    device.logging_type = 0;
    device.log_count = 0;
    device.name_identifier = "AI";

    device.port = 5502;
    strcpy_s(device.ip, "127.0.0.1");
    sprintf_s(device.name, "DEVICE_%d", id);
    sprintf_s(device.db_table, "DEVICE_%d", id);
    sprintf_s(device.url, "http://127.0.0.1:8181/api/v3/write_lp?db=mydb&precision=auto");
    sprintf_s(device.token,
              "apiv3_sNXORMzaLV0jF3WWL2VrRWK-pkvKj9QXc5XSKsODkV4Ig63H1pAsxtC1wVEID0izrJgbelA9s0OznWyIP_pxgA");

    // device.token = "apiv3_sNXORMzaLV0jF3WWL2VrRWK-pkvKj9QXc5XSKsODkV4Ig63H1pAsxtC1wVEID0izrJgbelA9s0OznWyIP_pxgA";
    device.is_error = false;

    MbChannel *channels = (MbChannel *)malloc(n_channels * sizeof(MbChannel));

    device.channels = channels;

    for (int i = 0; i < n_channels; i++)
    {
        device.channels[i].id = i;
        device.timestamp = 0;
        sprintf_s(device.channels[i].tag, "TAG%d", i);
        strcpy_s(device.channels[i].description, "Example channel. No description.");
        strcpy_s(device.channels[i].unit, "-");
        device.channels[i].address = i * 2;
        device.channels[i].channel_type = 2;
        device.channels[i].enabled = true;
        device.channels[i].enabled = true;
        device.channels[i].value_type = MbChannelType::Real;
        device.channels[i].value = (float)i;
        device.channel_count++;
    }
    return device;
}

/// Clear the device and free its memory including channel memory.
int cl_device_destroy(MbDevice *device)
{
    free(device->channels);
    return EXIT_SUCCESS;
}
