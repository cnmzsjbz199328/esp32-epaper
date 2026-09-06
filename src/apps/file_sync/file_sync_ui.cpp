#include "file_sync_ui.h"

#include <Arduino.h>
#include <string.h>

#include "bsp.h"
#include "bsp_pins.h"

namespace {

const char* basename_of(const char* path)
{
    const char* slash = path ? strrchr(path, '/') : nullptr;
    return slash ? slash + 1 : (path ? path : "?");
}

uint32_t megabytes(uint64_t bytes)
{
    return (uint32_t)(bytes / 1048576ULL);
}

}  // namespace

void file_sync_ui_render(const file_sync_model_t& model)
{
    bsp_ui_fb_fill_rect(0, 26, BSP_EPD_W, BSP_EPD_H - 26, false);
    bsp_ui_fb_draw_text(5, 29, "FILE SYNC", 1);
    char line[40] = {};
    snprintf(line, sizeof(line), "WIFI %s  %s", model.server.wifi_connected ? "OK" : "--",
             model.server.ip);
    bsp_ui_fb_draw_text(5, 45, line, 1);
    snprintf(line, sizeof(line), "SD %luMB free", (unsigned long)megabytes(model.free_bytes));
    bsp_ui_fb_draw_text(5, 59, line, 1);
    snprintf(line, sizeof(line), "PATH %.25s", model.current_path);
    bsp_ui_fb_draw_text(5, 73, line, 1);
    for (size_t i = 0; i < model.entry_count && i < FILE_SYNC_VISIBLE_ENTRIES; i++) {
        const file_storage::FileInfo& entry = model.entries[i];
        snprintf(line, sizeof(line), "%c%s %.12s %s",
                 i == (size_t)model.selected ? '>' : ' ',
                 entry.is_directory ? "[D]" : "   ",
                 basename_of(entry.path),
                 entry.is_directory ? "" : "file");
        bsp_ui_fb_draw_text(5, 91 + (int)i * 13, line, 1);
    }
    snprintf(line, sizeof(line), "SYNC: %.20s", model.server.last_status);
    bsp_ui_fb_draw_text(5, 172, line, 1);
    bsp_ui_fb_draw_text(5, 184, "ENTER open  HOLD del  ESC back", 1);
}

void file_sync_ui_render_delete_confirmation(const file_sync_model_t& model)
{
    bsp_ui_fb_fill_rect(0, 26, BSP_EPD_W, BSP_EPD_H - 26, false);
    bsp_ui_fb_draw_text(5, 35, "DELETE FILE?", 1);
    if (model.entry_count > 0 && model.selected < (int)model.entry_count) {
        bsp_ui_fb_draw_text(5, 65, basename_of(model.entries[model.selected].path), 1);
    }
    bsp_ui_fb_draw_text(5, 105, "ENTER confirm", 1);
    bsp_ui_fb_draw_text(5, 123, "ESC cancel", 1);
}

void file_sync_ui_render_detail(const file_sync_model_t& model)
{
    bsp_ui_fb_fill_rect(0, 26, BSP_EPD_W, BSP_EPD_H - 26, false);
    bsp_ui_fb_draw_text(5, 35, "FILE DETAIL", 1);
    if (model.entry_count > 0 && model.selected < (int)model.entry_count) {
        const file_storage::FileInfo& entry = model.entries[model.selected];
        char line[38] = {};
        snprintf(line, sizeof(line), "NAME %.25s", basename_of(entry.path));
        bsp_ui_fb_draw_text(5, 55, line, 1);
        snprintf(line, sizeof(line), "SIZE %lluB", (unsigned long long)entry.size);
        bsp_ui_fb_draw_text(5, 71, line, 1);
        snprintf(line, sizeof(line), "TIME %lu", (unsigned long)entry.modified);
        bsp_ui_fb_draw_text(5, 87, line, 1);
        snprintf(line, sizeof(line), "SHA %.16s", model.detail_hash);
        bsp_ui_fb_draw_text(5, 103, line, 1);
        snprintf(line, sizeof(line), "    %.16s", model.detail_hash + 16);
        bsp_ui_fb_draw_text(5, 117, line, 1);
    }
    bsp_ui_fb_draw_text(5, 184, "ESC back  HOLD del", 1);
}
