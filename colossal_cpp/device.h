#pragma once

/// Base device struct
typedef struct Device
{
    int id;
    char name[32];
    bool is_error;
    const char *error_msg;
    int logging_type;
    char token[256];
    unsigned long log_count;

    void (*cl_device_init)(void *);
    void (*cl_device_connect)(void *);
    void (*cl_device_poll)(void *);
    void (*cl_device_destroy)(void *);

} Device;
