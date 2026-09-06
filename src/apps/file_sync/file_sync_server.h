#pragma once

#include <stdint.h>

struct file_sync_server_state_t {
    bool wifi_connected;
    bool running;
    char ip[16];
    char last_status[32];
    char last_error[80];
};

void file_sync_server_begin(void);
void file_sync_server_tick(void);
void file_sync_server_stop(void);
bool file_sync_server_get_state(file_sync_server_state_t* out);
uint32_t file_sync_server_commit_generation(void);
