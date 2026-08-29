#include "app_family_video.h"

#include <Arduino.h>
#include <string.h>

#include "frame_source.h"
#include "family_video_assets.h"
#include "bsp.h"
#include "bsp_pins.h"
#include "../../shell/shell.h"

#ifndef APP_PARTIAL_MAX_STREAK
#define APP_PARTIAL_MAX_STREAK 4
#endif

namespace {
int s_frame = 0;
int s_streak = 0;
const frame_source_t* s_source = nullptr;

bool show_frame(int target, bool force_full)
{
    if (!s_source) s_source = frame_source_init();
    const int count = s_source->count();
    if (count <= 0) return false;
    if (target < 0) target = 0;
    if (target >= count) target = count - 1;
    if (target == s_frame && !force_full) return true;

    uint8_t* fb = bsp_ui_fb();
    if (!s_source->load(target, fb, bsp_ui_fb_len())) {
        Serial.printf("[video] %s load frame %d failed\n", s_source->name, target);
        if (s_source != frame_source_rom()) {
            frame_source_use_rom();
            s_source = frame_source_current();
            if (!s_source->load(target, fb, bsp_ui_fb_len())) return false;
        } else {
            return false;
        }
    }

    const bool backwards = target < s_frame;
    const bool final_frame = target == count - 1;
    const int hint = s_source->hint(target);
    const bool needs_new_base = !force_full && (final_frame || backwards ||
                          hint != APP_REFRESH_PARTIAL || s_streak >= APP_PARTIAL_MAX_STREAK ||
                          !bsp_ui_partial_active());
    s_frame = target;

    if (needs_new_base) {
        /* Keep the target frame in fb while shell performs the only legal
         * session transition: partial_end -> full(target) -> partial_begin. */
        const bool ok = shell_full_refresh_current();
        if (ok) s_streak = 0;
        Serial.printf("[video] frame=%d/%d source=%s mode=%s hint=%d busy=%lums %s\n",
                      s_frame + 1, count, s_source->name,
                      "full", hint,
                      (unsigned long)bsp_epd_last_busy_ms(), ok ? "ok" : "FAIL");
        return ok;
    }

    const bool ok = bsp_ui_flush_partial();
    if (ok) s_streak++;
    Serial.printf("[video] frame=%d/%d source=%s mode=partial streak=%d busy=%lums %s\n",
                  s_frame + 1, count, s_source->name, s_streak,
                  (unsigned long)bsp_epd_last_busy_ms(), ok ? "ok" : "FAIL");
    return ok;
}

void handle_event(const shell_event_t& event)
{
    if (event.kind == SHELL_EV_HOME || event.kind == SHELL_EV_BACK) {
        shell_show_launcher();
        return;
    }
    if (event.kind == SHELL_EV_KEY) {
        app_family_video_on_key(event.key, event.event);
        return;
    }
    if (event.kind != SHELL_EV_TAP) return;

    const bool left = event.x < BSP_EPD_W / 2;
    if (event.long_press) show_frame(left ? 0 : s_source->count() - 1, false);
    else show_frame(s_frame + (left ? -1 : 1), false);
}
}

void app_family_video_on_enter(void)
{
    s_source = frame_source_init();
    s_frame = 0;
    s_streak = 0;
    Serial.printf("[video] enter source=%s frames=%d\n", s_source->name, s_source->count());
    if (!show_frame(0, true)) Serial.println("[video] initial frame FAIL");
}

void app_family_video_on_exit(void) {}

void app_family_video_tick(void)
{
    handle_event(shell_wait_event(60000));
}

void app_family_video_on_key(const char* key, const char* event)
{
    if (!key || !event || strcmp(event, "press") != 0) return;
    if (!s_source) s_source = frame_source_init();
    if (strcmp(key, "right") == 0) show_frame(s_frame + 1, false);
    else if (strcmp(key, "left") == 0) show_frame(s_frame - 1, false);
    else if (strcmp(key, "home") == 0 || strcmp(key, "back") == 0 || strcmp(key, "esc") == 0) shell_show_launcher();
}
