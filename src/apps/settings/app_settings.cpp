#include "app_settings.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "diag_runner.h"
#include "../../app_diagnostics.h"
#include "bsp.h"
#include "bsp_pins.h"
#include "audio.h"
#include "../../shell/shell.h"
#include "../../../lib/nvs_settings/nvs_settings.h"
#include "../usage/usage_model.h"

namespace {
enum settings_page_t { PAGE_LIST, PAGE_RESULTS, PAGE_DETAIL, PAGE_VOLUME };
settings_page_t s_page = PAGE_LIST;
int s_selected = 0;
int s_result_scroll = 0;
constexpr int ITEM_COUNT = 8;
const char* const ITEMS[ITEM_COUNT] = {
    "HARDWARE TEST", "M0 STATIC FRAME", "M1 PARTIAL WALK", "BOOT DIAGNOSTIC", "WIFI STATUS", "ABOUT", "USAGE PUSH", "VOLUME"
};
uint32_t s_usage_seq_seen = 0;
constexpr uint8_t VOLUME_STEP = 5;
uint8_t s_volume = 80;

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
    bsp_ui_fb_fill_rect(0, 26, BSP_EPD_W, BSP_EPD_H - 26, false);
    bsp_ui_fb_draw_text(5, 30, "WIFI STATUS", 1);
    bsp_ui_fb_draw_text(5, 52, configured ? "CONFIGURED" : "NOT CONFIGURED", 1);
    if (configured) bsp_ui_fb_draw_text(5, 70, creds.ssid.c_str(), 1);
    bsp_ui_fb_draw_text(5, 92, "SET VIA BLE config.wifi", 1);
    bsp_ui_fb_draw_text(5, 184, "ESC back", 1);
}

void render_usage(void)
{
    bsp_ui_fb_fill_rect(0, 26, BSP_EPD_W, BSP_EPD_H - 26, false);
    bsp_ui_fb_draw_text(5, 30, "USAGE PUSH", 1);
    if (usage_model_seq() == 0) {
        bsp_ui_fb_draw_text(5, 52, "NO HOST", 1);
    } else {
        const int count = usage_model_count();
        for (int i = 0; i < count && i < 3; i++) {
            const usage_snapshot_t* snapshot = usage_model_at(i);
            if (!snapshot) continue;
            const uint32_t age = usage_model_age_ms(snapshot->provider);
            char line[38];
            char age_text[10];
            if (age == UINT32_MAX) snprintf(age_text, sizeof(age_text), "never");
            else if (age >= 3600000u) snprintf(age_text, sizeof(age_text), "%luh", (unsigned long)(age / 3600000u));
            else snprintf(age_text, sizeof(age_text), "%lus", (unsigned long)(age / 1000u));
            snprintf(line, sizeof(line), "%-7s %-5s %.15s %s", snapshot->provider, age_text,
                     snapshot->limit_state, snapshot->ok ? "ok" : "bad");
            bsp_ui_fb_draw_text(5, 52 + i * 20, line, 1);
        }
    }
    bsp_ui_fb_draw_text(5, 184, "ESC back", 1);
}

void render_volume(void)
{
    bsp_ui_fb_fill_rect(0, 26, BSP_EPD_W, BSP_EPD_H - 26, false);
    bsp_ui_fb_draw_text(5, 30, "VOLUME", 1);
    char line[16];
    snprintf(line, sizeof(line), "%3u %%", (unsigned)s_volume);
    bsp_ui_fb_draw_text(5, 52, line, 1);
    const int bar_w = BSP_EPD_W - 10;
    bsp_ui_fb_fill_rect(5, 70, bar_w, 10, false);
    const int fill_w = (int)((long)bar_w * s_volume / 100);
    if (fill_w > 0) bsp_ui_fb_fill_rect(5, 70, fill_w, 10, true);
    bsp_ui_fb_draw_text(5, 184, "UP/DN vol  ENTER test  ESC back", 1);
}

void refresh_page(void)
{
    if (s_page == PAGE_RESULTS) render_results();
    else if (s_page == PAGE_VOLUME) render_volume();
    else render_list();
    if (!bsp_ui_flush_partial()) Serial.println("[settings] partial refresh failed");
}

void run_selected(void)
{
    if (s_selected == 0) {
        s_page = PAGE_RESULTS;
        s_result_scroll = 0;
        render_results();
        (void)bsp_ui_flush_partial();
        shell_suspend_active();
        diag_runner_run_full();
        shell_resume_active();
    } else if (s_selected == 1) {
        shell_suspend_active();
        app_run_m0_diagnostics();
        shell_resume_active();
    } else if (s_selected == 2) {
        shell_suspend_active();
        app_run_m1_diagnostics();
        shell_resume_active();
    } else if (s_selected == 3) {
        shell_suspend_active();
        app_run_boot_diagnostics();
    } else if (s_selected == 4) {
        s_page = PAGE_DETAIL;
        render_wifi();
        (void)bsp_ui_flush_partial();
    } else if (s_selected == 5) {
        s_page = PAGE_DETAIL;
        render_about();
        (void)bsp_ui_flush_partial();
    } else if (s_selected == 6) {
        s_page = PAGE_DETAIL;
        s_usage_seq_seen = usage_model_seq();
        render_usage();
        (void)bsp_ui_flush_partial();
    } else if (s_selected == 7) {
        s_page = PAGE_VOLUME;
        s_volume = ecp::settings::get_volume(80);
        render_volume();
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
    else if (s_page == PAGE_VOLUME) render_volume();
    else if (s_page == PAGE_DETAIL) {
        if (s_selected == 4) render_wifi();
        else if (s_selected == 6) {
            s_usage_seq_seen = usage_model_seq();
            render_usage();
        } else render_about();
    }
    else render_list();
    (void)bsp_ui_flush_partial();
}

void app_settings_on_exit(void) {}

void app_settings_tick(void)
{
    const app_entry_t* active = shell_active_app();
    handle_event(shell_wait_event(30000));
    if (shell_active_app() == active && s_page == PAGE_DETAIL && s_selected == 6 && usage_model_seq() != s_usage_seq_seen) {
        s_usage_seq_seen = usage_model_seq();
        render_usage();
        (void)bsp_ui_flush_partial();
    }
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
        if (strcmp(key, "back") == 0 || strcmp(key, "esc") == 0) { s_page = PAGE_LIST; refresh_page(); }
        return;
    }
    if (s_page == PAGE_VOLUME) {
        if (strcmp(key, "up") == 0 || strcmp(key, "down") == 0) {
            int next = (int)s_volume + (strcmp(key, "up") == 0 ? VOLUME_STEP : -(int)VOLUME_STEP);
            s_volume = (uint8_t)max(0, min(100, next));
            ecp::settings::set_volume(s_volume);
            if (bsp_audio_ready()) bsp_audio_set_volume(s_volume);
            refresh_page();
        } else if (strcmp(key, "enter") == 0) {
            if (!bsp_audio_ready()) bsp_audio_init(16000, s_volume);
            else bsp_audio_set_volume(s_volume);
            bsp_audio_tone(880, 150, 60);
        } else if (strcmp(key, "back") == 0 || strcmp(key, "esc") == 0) {
            s_page = PAGE_LIST;
            refresh_page();
        }
        return;
    }
    if (strcmp(key, "up") == 0) { s_selected = (s_selected + ITEM_COUNT - 1) % ITEM_COUNT; refresh_page(); }
    else if (strcmp(key, "down") == 0) { s_selected = (s_selected + 1) % ITEM_COUNT; refresh_page(); }
    else if (strcmp(key, "enter") == 0) run_selected();
    else if (strcmp(key, "back") == 0 || strcmp(key, "esc") == 0) shell_show_launcher();
}
