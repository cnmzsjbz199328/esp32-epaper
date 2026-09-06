#include "file_sync_model.h"

#include <string.h>

void file_sync_model_refresh(file_sync_model_t* model)
{
    if (!model) return;
    file_sync_server_get_state(&model->server);
    const file_storage::SpaceInfo storage = file_storage::space();
    model->free_bytes = storage.free_bytes;
    model->total_bytes = storage.total_bytes;
    file_storage::FileInfo all_entries[FILE_SYNC_VISIBLE_ENTRIES * 2] = {};
    size_t count = 0;
    const file_storage::Status status = file_storage::list(
        model->current_path, all_entries, FILE_SYNC_VISIBLE_ENTRIES * 2, &count);
    model->entry_count = 0;
    if (status == file_storage::Status::Ok) {
        for (size_t i = 0; i < count && model->entry_count < FILE_SYNC_VISIBLE_ENTRIES; i++) {
            const char* base = strrchr(all_entries[i].path, '/');
            base = base ? base + 1 : all_entries[i].path;
            if (strncmp(base, ".sync_", 6) == 0) continue;
            model->entries[model->entry_count++] = all_entries[i];
        }
    }
    model->file_count = (uint32_t)model->entry_count;
    if (model->selected >= (int)model->entry_count) {
        model->selected = model->entry_count > 0 ? (int)model->entry_count - 1 : 0;
    }
}
