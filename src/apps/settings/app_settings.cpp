#include "app_settings.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "diag_runner.h"
#include "../../app_diagnostics.h"
#include "bsp.h"
#include "bsp_pins.h"
#include "../../shell/shell.h"
#include "../../../lib/nvs_settings/nvs_settings.h"
#include "../file_sync/file_sync_server.h"
#include "file_storage.h"

namespace {
enum settings_page_t { PAGE_LIST, PAGE_RESULTS, PAGE_DETAIL };
settings_page_t s_page = PAGE_LIST;
int s_selected = 0;
int s_result_scroll = 0;
constexpr int ITEM_COUNT = 2;
const char* const ITEMS[ITEM_COUNT] = { "WIFI + SYNC", "TF DIAGNOSTIC" };

void draw_line(int y, const char* text, bool selected)
{
    if (selected) bsp_ui_fb_fill_rect(2, y - 2, BSP_EPD_W - 4, 13, true);
    if (selected) {
        uint8_t* fb = bsp_ui_fb();
        /* Text is black-on-white; invert selected rows by using a small white
         * background glyph pass after the selection stripe. */
        (void)fb;
    }
    bsp_ui_fb_draw_text(7, y, text, 1);
}

void render_list(void)
{
    bsp_ui_fb_fill_rect(0, 26, BSP_EPD_W, BSP_EPD_H - 26, false);
    bsp_ui_fb_draw_text(5, 29, "SETTINGS", 1);
    for (int i = 0; i < ITEM_COUNT; i++) {
        char line[28];
        snprintf(line, sizeof(line), "%c %s", i == s_selected ? '>' : ' ', ITEMS[i]);
        draw_line(42 + i * 21, line, false);
    }
    bsp_ui_fb_draw_text(5, 184, "ENTER run  ESC home", 1);
}

const char* result_tag(bsp_selftest_result_t outcome)
{
    switch (outcome) {
        case BSP_SELFTEST_PASS: return "PASS";
        case BSP_SELFTEST_WARN: return "WARN";
        default: return "FAIL";
    }
}

void render_results(void)
{
    bsp_ui_fb_fill_rect(0, 26, BSP_EPD_W, BSP_EPD_H - 26, false);
    char line[40];
    snprintf(line, sizeof(line), "TEST P%d W%d F%d",
             bsp_selftest_count(BSP_SELFTEST_PASS),
             bsp_selftest_count(BSP_SELFTEST_WARN),
             bsp_selftest_count(BSP_SELFTEST_FAIL));
    bsp_ui_fb_draw_text(5, 29, line, 1);
    const int count = (int)bsp_selftest_record_count();
    const int visible = 9;
    for (int row = 0; row < visible; row++) {
        const int index = s_result_scroll + row;
        if (index >= count) break;
        bsp_selftest_record_t record;
        if (!bsp_selftest_record_at((size_t)index, &record)) continue;
        snprintf(line, sizeof(line), "%02d %s %s", index + 1, result_tag(record.outcome), record.item ? record.item : "?");
        bsp_ui_fb_draw_text(5, 45 + row * 15, line, 1);
    }
    bsp_ui_fb_draw_text(5, 184, "UP/DN scroll  ESC back", 1);
}

void render_about(void)
{
    bsp_ui_fb_fill_rect(0, 26, BSP_EPD_W, BSP_EPD_H - 26, false);
    bsp_ui_fb_draw_text(5, 30, "ABOUT", 1);
    bsp_ui_fb_draw_text(5, 48, bsp_version_string(), 1);
    bsp_ui_fb_draw_text(5, 66, "BLE remote + SD FVID", 1);
    bsp_ui_fb_draw_text(5, 84, "BOOT hold: diagnostics", 1);
    bsp_ui_fb_draw_text(5, 184, "ESC back", 1);
}

void render_wifi(void)
{
    ecp::settings::WifiCredentials creds;
    const bool configured = ecp::settings::get_wifi_credentials(creds);
    file_sync_server_state_t server_state = {};
    file_sync_server_get_state(&server_state);
    const file_storage::SpaceInfo storage = file_storage::space();
    bsp_ui_fb_fill_rect(0, 26, BSP_EPD_W, BSP_EPD_H - 26, false);
    bsp_ui_fb_draw_text(5, 29, "WIFI + SYNC", 1);
    char line[38] = {};
    snprintf(line, sizeof(line), "WIFI %s %s", configured ? "CFG" : "--", server_state.ip);
    bsp_ui_fb_draw_text(5, 45, line, 1);
    snprintf(line, sizeof(line), "NAME %.22s", ecp::settings::get_device_name("EPAPER-154").c_str());
    bsp_ui_fb_draw_text(5, 59, line, 1);
    const String token = ecp::settings::get_sync_token();
    snprintf(line, sizeof(line), "TOKEN %.16s", token.c_str());
    bsp_ui_fb_draw_text(5, 73, line, 1);
    snprintf(line, sizeof(line), "      %.16s", token.length() > 16 ? token.c_str() + 16 : "");
    bsp_ui_fb_draw_text(5, 87, line, 1);
    snprintf(line, sizeof(line), "AUTO SYNC %s", ecp::settings::get_auto_sync(false) ? "ON" : "OFF");
    bsp_ui_fb_draw_text(5, 103, line, 1);
    snprintf(line, sizeof(line), "DELETE    %s", ecp::settings::get_allow_delete(false) ? "ON" : "OFF");
    bsp_ui_fb_draw_text(5, 117, line, 1);
    snprintf(line, sizeof(line), "STORY SCAN %s", ecp::settings::get_auto_story_scan(true) ? "ON" : "OFF");
    bsp_ui_fb_draw_text(5, 131, line, 1);
    snprintf(line, sizeof(line), "SD %luMB free", (unsigned long)(storage.free_bytes / 1048576ULL));
    bsp_ui_fb_draw_text(5, 147, line, 1);
    bsp_ui_fb_draw_text(5, 184, "ENTER auto  D del  S scan", 1);
}

void refresh_page(void)
{
    if (s_page == PAGE_RESULTS) render_results();
    else render_list();
    if (!bsp_ui_flush_partial()) Serial.println("[settings] partial refresh failed");
}

void run_selected(void)
{
    if (s_selected == 0) {
        s_page = PAGE_DETAIL;
        render_wifi();
        (void)bsp_ui_flush_partial();
    } else if (s_selected == 1) {
        shell_suspend_active();
        diag_runner_run_full();
        shell_resume_active();
        s_page = PAGE_RESULTS;
        render_results();
        (void)bsp_ui_flush_partial();
    }
}

void handle_event(const shell_event_t& event)
{
    if (event.kind == SHELL_EV_HOME) { shell_show_launcher(); return; }
    if (event.kind == SHELL_EV_BACK) {
        if (s_page != PAGE_LIST) { s_page = PAGE_LIST; refresh_page(); }
        else shell_show_launcher();
        return;
    }
    if (event.kind == SHELL_EV_TAP) {
        if (s_page == PAGE_RESULTS) return;
        if (event.y >= 37 && event.y < 37 + ITEM_COUNT * 21) {
            s_selected = min(ITEM_COUNT - 1, max(0, (int)((event.y - 37) / 21)));
            run_selected();
        }
        return;
    }
    if (event.kind == SHELL_EV_KEY) app_settings_on_key(event.key, event.event);
}
}

void app_settings_on_enter(void)
{
    if (s_page == PAGE_RESULTS) render_results();
    else if (s_page == PAGE_DETAIL) {
        render_wifi();
    }
    else render_list();
    (void)bsp_ui_flush_partial();
}

void app_settings_on_exit(void) {}

void app_settings_tick(void)
{
    const app_entry_t* active = shell_active_app();
    handle_event(shell_wait_event(30000));
    (void)active;
}

void app_settings_on_key(const char* key, const char* event)
{
    if (!key || !event || (strcmp(event, "press") != 0 && strcmp(event, "down") != 0)) return;
    if (s_page == PAGE_RESULTS) {
        if (strcmp(key, "up") == 0 && s_result_scroll > 0) { s_result_scroll--; refresh_page(); }
        else if (strcmp(key, "down") == 0 && s_result_scroll + 9 < (int)bsp_selftest_record_count()) { s_result_scroll++; refresh_page(); }
        else if (strcmp(key, "back") == 0 || strcmp(key, "esc") == 0) { s_page = PAGE_LIST; refresh_page(); }
        return;
    }
    if (s_page == PAGE_DETAIL) {
        if (s_selected == 0 && strcmp(key, "enter") == 0) {
            ecp::settings::set_auto_sync(!ecp::settings::get_auto_sync(false));
            render_wifi();
            (void)bsp_ui_flush_partial();
        } else if (s_selected == 0 && strcmp(key, "d") == 0) {
            ecp::settings::set_allow_delete(!ecp::settings::get_allow_delete(false));
            render_wifi();
            (void)bsp_ui_flush_partial();
        } else if (s_selected == 0 && strcmp(key, "s") == 0) {
            ecp::settings::set_auto_story_scan(!ecp::settings::get_auto_story_scan(true));
            render_wifi();
            (void)bsp_ui_flush_partial();
        } else if (strcmp(key, "back") == 0 || strcmp(key, "esc") == 0) { s_page = PAGE_LIST; refresh_page(); }
        return;
    }
    if (strcmp(key, "up") == 0) { s_selected = (s_selected + ITEM_COUNT - 1) % ITEM_COUNT; refresh_page(); }
    else if (strcmp(key, "down") == 0) { s_selected = (s_selected + 1) % ITEM_COUNT; refresh_page(); }
    else if (strcmp(key, "enter") == 0) run_selected();
    else if (strcmp(key, "back") == 0 || strcmp(key, "esc") == 0) shell_show_launcher();
}
