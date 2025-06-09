#pragma once
#include "data_buffer.h"
#include "imgui/imgui.h"
#include "link.h"
#include "mb_device.h"
#include <stdbool.h>

#define CURL_STATICLIB

/// Special struct to hold data shared between threads.
/// This will be protected by a mutex.
typedef struct ThreadData
{
    MbDevice device;
} ThreadData;

typedef struct ThreadArg
{
    int id;
    Buffer *buf_ptr;
    Link link;
    ConfigUpdate *config_update_ptr;
} ThreadArg;

/**
** The colossal type is a singleton that holds all the imgui data.
**/
typedef struct Colossal
{
    /// This is used as a switch to run some code at startup.
    bool is_first_scan = true;
    bool show_demo_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.0f);
    /// Protected data coming from the thread.
    ThreadData device_data;

} Colossal;

typedef struct UiMenuState
{
    bool devices_menu;
    bool tag_menu;
    bool help_menu;
    bool logging_menu;
    bool plot_menu;
} UiMenuState;

// typedef struct UiMbDeviceBuffer
// {
//     char const *name;
//     char const *ip;
//     int port;
//     char const *com_port;

// } UiMbDeviceBuffer;
