#pragma once

#include <stdint.h>

#include "file_sync_server.h"
#include "file_storage.h"

constexpr size_t FILE_SYNC_VISIBLE_ENTRIES = 7;

struct file_sync_model_t {
    file_sync_server_state_t server = {};
    uint64_t free_bytes = 0;
    uint64_t total_bytes = 0;
    uint32_t file_count = 0;
    char current_path[192] = "/";
    file_storage::FileInfo entries[FILE_SYNC_VISIBLE_ENTRIES] = {};
    size_t entry_count = 0;
    int selected = 0;
    bool confirm_delete = false;
    bool detail = false;
    char detail_hash[file_storage::SHA256_HEX_LENGTH + 1] = {};
};

void file_sync_model_refresh(file_sync_model_t* model);
