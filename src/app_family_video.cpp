#include <Arduino.h>
#include <string.h>

#include "app_family_video.h"
#include "bsp.h"
#include "bsp_pins.h"
#include "family_video_assets.h"
#include "touch.h"

#ifndef APP_PARTIAL_MAX_STREAK
#define APP_PARTIAL_MAX_STREAK 4
#endif

#define APP_TOUCH_POLL_MS       40
#define APP_TOUCH_DEBOUNCE_MS   180
#define APP_TOUCH_LONG_MS       1500
#define APP_TOUCH_STABLE_MS     50
#define APP_TOUCH_RELEASE_MS    150
#define APP_TOUCH_MOVE_TOLERANCE 8
#define APP_PWR_LONG_MS         3000
#define APP_NAV_TIMEOUT_MS      60000
#define APP_REFRESH_FULL_DIFF_PCT 35

typedef enum {
    APP_NAV_NONE = 0,
    APP_NAV_PREV,
    APP_NAV_NEXT,
    APP_NAV_FIRST,
    APP_NAV_FINAL
} app_nav_action_t;

static int s_streak = 0;
static int s_video_frame = 0;

static uint32_t app_frame_change_pct(int from, int to)
{
    if (from < 0 || to < 0 ||
        from >= APP_VIDEO_FRAME_COUNT || to >= APP_VIDEO_FRAME_COUNT) {
        return 100;
    }

    uint32_t changed = 0;
    for (int i = 0; i < APP_VIDEO_FRAME_LEN; i++) {
        changed += __builtin_popcount((unsigned)(APP_VIDEO_FRAMES[from][i] ^ APP_VIDEO_FRAMES[to][i]));
    }
    return (changed * 100UL + ((APP_VIDEO_FRAME_W * APP_VIDEO_FRAME_H) / 2)) /
           (APP_VIDEO_FRAME_W * APP_VIDEO_FRAME_H);
}

static app_refresh_hint_t app_frame_hint(int target)
{
    if (target < 0 || target >= APP_VIDEO_FRAME_COUNT) return APP_REFRESH_FULL;
    return APP_VIDEO_REFRESH_HINTS[target];
}

static void app_video_build_fb(int index)
{
    if (index < 0) index = 0;
    if (index >= APP_VIDEO_FRAME_COUNT) index = APP_VIDEO_FRAME_COUNT - 1;
    memcpy(bsp_ui_fb(), APP_VIDEO_FRAMES[index], APP_VIDEO_FRAME_LEN);
}

static bool app_full_then_rebase(void)
{
    if (bsp_ui_partial_active()) bsp_ui_partial_end();
    if (!bsp_ui_fb_flush_full()) return false;
    s_streak = 0;
    return bsp_ui_partial_begin();
}

static void app_power_poll(void)
{
    static uint32_t pwr_down_at = 0;

    if (bsp_button_pressed(BSP_BTN_PWR)) {
        if (pwr_down_at == 0) {
            pwr_down_at = millis();
        } else if (millis() - pwr_down_at > APP_PWR_LONG_MS) {
            Serial.println("[app] PWR long press -> clear and power off");
            bsp_ui_fb_clear(0xFF);
            if (bsp_ui_partial_active()) bsp_ui_partial_end();
            bsp_ui_fb_flush_full();
            bsp_power_off();
            delay(2000);
            pwr_down_at = 0;
        }
    } else {
        pwr_down_at = 0;
    }
}

static bool app_read_stable_touch(bsp_touch_point_t* out)
{
    bsp_touch_point_t first;
    if (!bsp_touch_read(&first) || !first.down) return false;

    const uint32_t start = millis();
    while (millis() - start < APP_TOUCH_STABLE_MS) {
        app_power_poll();

        bsp_touch_point_t p;
        if (!bsp_touch_read(&p) || !p.down) return false;
        if (abs((int)p.x - (int)first.x) > APP_TOUCH_MOVE_TOLERANCE ||
            abs((int)p.y - (int)first.y) > APP_TOUCH_MOVE_TOLERANCE) {
            return false;
        }
        delay(APP_TOUCH_POLL_MS);
    }

    if (out) *out = first;
    return true;
}

static bool app_touch_is_down(void)
{
    bsp_touch_point_t p;
    return bsp_touch_read(&p) && p.down;
}

static bool app_touch_released_for(uint32_t stable_ms)
{
    uint32_t clear_since = 0;

    while (true) {
        app_power_poll();
        if (app_touch_is_down()) {
            clear_since = 0;
        } else {
            if (clear_since == 0) clear_since = millis();
            if (millis() - clear_since >= stable_ms) return true;
        }
        delay(APP_TOUCH_POLL_MS);
    }
}

static app_nav_action_t app_wait_nav(uint32_t timeout_ms)
{
    const uint32_t t0 = millis();
    bsp_touch_point_t p;

    app_touch_released_for(APP_TOUCH_RELEASE_MS);

    while (millis() - t0 < timeout_ms) {
        app_power_poll();

        if (app_read_stable_touch(&p)) {
            const bool left = p.x < BSP_EPD_W / 2;
            const uint32_t down_at = millis();
            while (bsp_touch_read(&p) && p.down) {
                app_power_poll();
                delay(APP_TOUCH_POLL_MS);
            }
            app_touch_released_for(APP_TOUCH_RELEASE_MS);
            delay(APP_TOUCH_DEBOUNCE_MS);

            const bool long_press = millis() - down_at >= APP_TOUCH_LONG_MS;
            Serial.printf("[touch] action=%s x=%u y=%u hold=%lums\n",
                          long_press ? (left ? "first" : "final") : (left ? "prev" : "next"),
                          p.x, p.y, (unsigned long)(millis() - down_at));
            if (long_press) return left ? APP_NAV_FIRST : APP_NAV_FINAL;
            return left ? APP_NAV_PREV : APP_NAV_NEXT;
        }
        delay(APP_TOUCH_POLL_MS);
    }
    return APP_NAV_NONE;
}

static void app_video_show_frame(int target)
{
    if (target < 0) target = 0;
    if (target >= APP_VIDEO_FRAME_COUNT) target = APP_VIDEO_FRAME_COUNT - 1;

    if (target == s_video_frame) {
        Serial.printf("[video] already at frame %d/%d\n", s_video_frame + 1, APP_VIDEO_FRAME_COUNT);
        return;
    }

    const int previous = s_video_frame;
    const bool backward = target < previous;
    const uint32_t changed_pct = app_frame_change_pct(previous, target);
    const app_refresh_hint_t hint = app_frame_hint(target);

    s_video_frame = target;
    app_video_build_fb(s_video_frame);

    const bool final_frame = s_video_frame >= APP_VIDEO_FRAME_COUNT - 1;
    const bool full_by_hint = hint == APP_REFRESH_FULL || hint == APP_REFRESH_FINAL_FULL;
    const bool full_by_diff = changed_pct > APP_REFRESH_FULL_DIFF_PCT;
    const bool full_by_streak = s_streak >= APP_PARTIAL_MAX_STREAK;
    const bool full_by_state = !bsp_ui_partial_active();
    const bool use_full = final_frame || backward || full_by_hint ||
                          full_by_diff || full_by_streak || full_by_state;

    if (use_full) {
        const char* reason = final_frame ? "final" :
                             backward ? "backward" :
                             full_by_hint ? "hint" :
                             full_by_diff ? "diff" :
                             full_by_streak ? "streak" : "inactive";
        bool ok = false;
        if (final_frame) {
            if (bsp_ui_partial_active()) bsp_ui_partial_end();
            ok = bsp_ui_fb_flush_full();
            s_streak = 0;
        } else {
            ok = app_full_then_rebase();
        }
        Serial.printf("[video] frame=%d/%d mode=full reason=%s streak=%d changed=%lu%% busy=%lums %s\n",
                      s_video_frame + 1, APP_VIDEO_FRAME_COUNT, reason, s_streak,
                      (unsigned long)changed_pct, (unsigned long)bsp_epd_last_busy_ms(),
                      ok ? "ok" : "FAIL");
        return;
    }

    const bool ok = bsp_ui_flush_partial();
    if (ok) {
        s_streak++;
        Serial.printf("[video] frame=%d/%d mode=partial streak=%d changed=%lu%% busy=%lums ok\n",
                      s_video_frame + 1, APP_VIDEO_FRAME_COUNT, s_streak,
                      (unsigned long)changed_pct, (unsigned long)bsp_epd_last_busy_ms());
        return;
    }

    Serial.printf("[video] frame=%d/%d mode=partial streak=%d changed=%lu%% busy=%lums FAIL -> full fallback\n",
                  s_video_frame + 1, APP_VIDEO_FRAME_COUNT, s_streak,
                  (unsigned long)changed_pct, (unsigned long)bsp_epd_last_busy_ms());
    const bool fallback_ok = app_full_then_rebase();
    Serial.printf("[video] frame=%d/%d mode=full reason=fallback streak=%d changed=%lu%% busy=%lums %s\n",
                  s_video_frame + 1, APP_VIDEO_FRAME_COUNT, s_streak,
                  (unsigned long)changed_pct, (unsigned long)bsp_epd_last_busy_ms(),
                  fallback_ok ? "ok" : "FAIL");
}

void app_family_video_setup(void)
{
    Serial.printf("[video] family full-frame app, %d frames\n", APP_VIDEO_FRAME_COUNT);
    if (APP_VIDEO_FRAME_LEN != bsp_ui_fb_len()) {
        Serial.printf("[video] ABORT: frame len %d != fb len %lu\n",
                      APP_VIDEO_FRAME_LEN, (unsigned long)bsp_ui_fb_len());
        return;
    }

    app_video_build_fb(0);
    if (!bsp_ui_fb_flush_full()) {
        Serial.println("[video] ABORT: first frame full flush failed");
        return;
    }
    if (!bsp_ui_partial_begin()) {
        Serial.println("[video] WARN: partial_begin failed; will run on full refresh only");
    }
    Serial.println("[video] touch RIGHT/LEFT = next/previous, long RIGHT/LEFT = final/first, hold PWR 3s = off");
}

void app_family_video_loop(void)
{
    switch (app_wait_nav(APP_NAV_TIMEOUT_MS)) {
    case APP_NAV_NEXT:
        app_video_show_frame(s_video_frame + 1);
        break;
    case APP_NAV_PREV:
        app_video_show_frame(s_video_frame - 1);
        break;
    case APP_NAV_FIRST:
        app_video_show_frame(0);
        break;
    case APP_NAV_FINAL:
        app_video_show_frame(APP_VIDEO_FRAME_COUNT - 1);
        break;
    default:
        break;
    }
}
