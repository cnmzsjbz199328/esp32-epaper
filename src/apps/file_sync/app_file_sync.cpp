#include "app_file_sync.h"

#include <Arduino.h>
#include <string.h>

#include "app_file_sync.h"
#include "file_sync_model.h"
#include "file_sync_ui.h"
#include "file_storage.h"
#include "bsp.h"
#include "../../shell/shell.h"
#include "../../../lib/nvs_settings/nvs_settings.h"

namespace {

file_sync_model_t s_model;

String public_path(const char* path)
{
    if (!path) return "/";
    const size_t root_length = strlen(file_storage::ROOT_PATH);
    if (strncmp(path, file_storage::ROOT_PATH, root_length) == 0) {
        return path[root_length] ? String(path + root_length) : String("/");
    }
    return String(path);
}

void parent_path(char* path, size_t capacity)
{
    if (!path || strcmp(path, "/") == 0) return;
    char* slash = strrchr(path, '/');
    if (!slash || slash == path) snprintf(path, capacity, "/");
    else *slash = '\0';
}

void render()
{
    if (s_model.confirm_delete) file_sync_ui_render_delete_confirmation(s_model);
    else if (s_model.detail) file_sync_ui_render_detail(s_model);
    else file_sync_ui_render(s_model);
    if (!bsp_ui_flush_partial()) Serial.println("[file-sync] display refresh failed");
}

void refresh()
{
    file_sync_model_refresh(&s_model);
    render();
}

void activate_selected()
{
    if (s_model.entry_count == 0 || s_model.selected >= (int)s_model.entry_count) return;
    const file_storage::FileInfo& entry = s_model.entries[s_model.selected];
    if (entry.is_directory) {
        const String path = public_path(entry.path);
        snprintf(s_model.current_path, sizeof(s_model.current_path), "%s", path.c_str());
        s_model.selected = 0;
        refresh();
    } else {
        s_model.detail = true;
        memset(s_model.detail_hash, 0, sizeof(s_model.detail_hash));
        file_storage::sha256_file(entry.path, s_model.detail_hash);
        render();
    }
}

void delete_selected()
{
    if (s_model.entry_count == 0 || s_model.selected >= (int)s_model.entry_count) return;
    const file_storage::FileInfo& entry = s_model.entries[s_model.selected];
    const file_storage::Status status = file_storage::remove(
        entry.path, entry.is_directory, entry.is_directory);
    if (status != file_storage::Status::Ok) {
        Serial.printf("[file-sync] delete failed: %s\n", file_storage::status_name(status));
    }
    s_model.confirm_delete = false;
    s_model.detail = false;
    refresh();
}

void handle_event(const shell_event_t& event)
{
    if (event.kind == SHELL_EV_HOME) { shell_show_launcher(); return; }
    if (event.kind == SHELL_EV_BACK) {
        if (s_model.confirm_delete) { s_model.confirm_delete = false; render(); return; }
        if (s_model.detail) { s_model.detail = false; render(); return; }
        if (strcmp(s_model.current_path, "/") == 0) shell_show_launcher();
        else { parent_path(s_model.current_path, sizeof(s_model.current_path)); s_model.selected = 0; refresh(); }
        return;
    }
    if (event.kind == SHELL_EV_TAP) {
        if (event.long_press && !s_model.confirm_delete &&
            s_model.entry_count > 0 && s_model.selected < (int)s_model.entry_count) {
            if (!ecp::settings::get_allow_delete(false)) {
                Serial.println("[file-sync] delete disabled by settings");
                return;
            }
            s_model.confirm_delete = true;
            render();
        }
        return;
    }
    if (event.kind == SHELL_EV_KEY) app_file_sync_on_key(event.key, event.event);
}

}  // namespace

void app_file_sync_on_enter(void)
{
    snprintf(s_model.current_path, sizeof(s_model.current_path), "/");
    s_model.selected = 0;
    s_model.confirm_delete = false;
    s_model.detail = false;
    refresh();
}

void app_file_sync_on_exit(void)
{
    s_model.confirm_delete = false;
    s_model.detail = false;
}

void app_file_sync_tick(void)
{
    handle_event(shell_wait_event(1000));
    if (!s_model.confirm_delete) {
        file_sync_model_refresh(&s_model);
        /* Network and SD status are useful while the page is open, but avoid
         * refreshing the display on every HTTP polling iteration. */
        static uint32_t last_render_ms = 0;
        if (millis() - last_render_ms > 5000) {
            last_render_ms = millis();
            render();
        }
    }
}

void app_file_sync_on_key(const char* key, const char* event)
{
    if (!key || !event || (strcmp(event, "press") != 0 && strcmp(event, "down") != 0)) return;
    if (s_model.confirm_delete) {
        if (strcmp(key, "enter") == 0) delete_selected();
        else if (strcmp(key, "back") == 0 || strcmp(key, "esc") == 0) {
            s_model.confirm_delete = false;
            render();
        }
        return;
    }
    if (s_model.detail) {
        if (strcmp(key, "back") == 0 || strcmp(key, "esc") == 0) {
            s_model.detail = false;
            render();
        }
        return;
    }
    if (strcmp(key, "up") == 0 && s_model.entry_count > 0) {
        s_model.selected = (s_model.selected + (int)s_model.entry_count - 1) % (int)s_model.entry_count;
        render();
    } else if (strcmp(key, "down") == 0 && s_model.entry_count > 0) {
        s_model.selected = (s_model.selected + 1) % (int)s_model.entry_count;
        render();
    } else if (strcmp(key, "enter") == 0) activate_selected();
    else if (strcmp(key, "r") == 0) refresh();
    else if (strcmp(key, "back") == 0 || strcmp(key, "esc") == 0) {
        if (strcmp(s_model.current_path, "/") == 0) shell_show_launcher();
        else { parent_path(s_model.current_path, sizeof(s_model.current_path)); s_model.selected = 0; refresh(); }
    }
}
