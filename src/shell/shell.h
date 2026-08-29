#pragma once

#include <stdint.h>

#include "../apps/iapp.h"

typedef enum {
    SHELL_EV_NONE = 0,
    SHELL_EV_TIMEOUT,
    SHELL_EV_TAP,
    SHELL_EV_KEY,
    SHELL_EV_HOME,
    SHELL_EV_BACK
} shell_ev_kind_t;

typedef struct {
    shell_ev_kind_t kind;
    uint16_t x;
    uint16_t y;
    bool long_press;
    char key[16];
    char event[8];
} shell_event_t;

void shell_begin(void);
void shell_tick(void);
shell_event_t shell_wait_event(uint32_t timeout_ms);
void shell_open_app(const app_entry_t* app);
void shell_show_launcher(void);
void shell_resume_active(void);
void shell_suspend_active(void);
bool shell_rebase_active(void);
bool shell_full_refresh_current(void);
const app_entry_t* shell_active_app(void);
bool shell_ble_begin_with_retry(void);
