// colossal_cpp.cpp : This file contains the 'main' function. Program execution
// begins and ends there.
//

#include "mb_device.h"
#include <stdio.h>
#include <stdlib.h>

#define N_CHANNELS 10
int
main()
{

    cl_mb_device* device;
    device = cl_device_init_tcp("PLC_1", N_CHANNELS);

    for (int i = 0; i < N_CHANNELS; i++) {
        printf("%d. %s %d %f\n",
               i,
               device->channels[i].name,
               device->channels[i].id,
               device->channels[i].value);
    }

    cl_device_destroy(device);
    device = NULL;

    return EXIT_SUCCESS;
}
