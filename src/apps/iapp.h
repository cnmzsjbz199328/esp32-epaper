#pragma once

#include <stdint.h>
#include <stdbool.h>

#define SHELL_ICON_W 80
#define SHELL_ICON_H 80

typedef struct app_entry {
    const char* id;
    const char* title;
    const uint8_t* icon;
    bool fullscreen;
    void (*on_enter)(void);
    void (*on_exit)(void);
    void (*tick)(void);
    void (*on_key)(const char* key, const char* event);
} app_entry_t;
