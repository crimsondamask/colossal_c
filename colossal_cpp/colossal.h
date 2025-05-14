
#pragma once
#include <stdbool.h>

/**
** The colossal type is a singleton that holds all the imgui data.
**/
typedef struct colossal {
    int id;

    /// This is used as a switch to run some code at startup.
    bool is_first_scan;

} colossal_t;
