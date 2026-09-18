#include "shell.h"

#include <Arduino.h>
#include <string.h>

#include "app_registry.h"
#include "bsp.h"
#include "bsp_pins.h"
#include "launcher.h"
#include "status_bar.h"
#include "ecosystem_ble.h"
#include "touch.h"

namespace {
constexpr uint32_t TOUCH_POLL_MS = 40;
constexpr uint32_t TOUCH_STABLE_MS = 50;
constexpr uint32_t TOUCH_RELEASE_MS = 150;
constexpr uint32_t TOUCH_RELEASE_TIMEOUT_MS = 500;
constexpr uint32_t TOUCH_HOLD_TIMEOUT_MS = 1000;
constexpr uint32_t TOUCH_DEBOUNCE_MS = 180;
constexpr uint32_t TOUCH_LONG_MS = 1500;
constexpr uint32_t TOUCH_MOVE_TOLERANCE = 8;
constexpr uint32_t PWR_LONG_MS = 3000;
constexpr uint32_t BOOT_LONG_MS = 1500;
constexpr size_t KEY_QUEUE_SIZE = 8;

struct queued_key_t {
    char key[16];
    char event[8];
};

queued_key_t s_key_queue[KEY_QUEUE_SIZE];
volatile size_t s_key_head = 0;
volatile size_t s_key_tail = 0;
const app_entry_t* s_active = nullptr;

bool pop_key(queued_key_t* out)
{
    if (!out || s_key_head == s_key_tail) return false;
    *out = s_key_queue[s_key_head];
    s_key_head = (s_key_head + 1) % KEY_QUEUE_SIZE;
    return true;
}

bool wait_released(uint32_t stable_ms)
{
    uint32_t clear_since = 0;
    const uint32_t started = millis();
    while (true) {
        ecp_ble_loop();
        bsp_touch_point_t point;
        if (bsp_touch_read(&point) && point.down) clear_since = 0;
        else {
            if (clear_since == 0) clear_since = millis();
            if (millis() - clear_since >= stable_ms) return true;
        }
        if (millis() - started >= TOUCH_RELEASE_TIMEOUT_MS) {
            Serial.println("[shell] touch release timeout; continuing event loop");
            return true;
        }
        delay(TOUCH_POLL_MS);
    }
}

bool read_stable_touch(bsp_touch_point_t* first)
{
    bsp_touch_point_t start;
    if (!bsp_touch_read(&start) || !start.down) return false;
    const uint32_t t0 = millis();
    while (millis() - t0 < TOUCH_STABLE_MS) {
        ecp_ble_loop();
        bsp_touch_point_t point;
        if (!bsp_touch_read(&point) || !point.down) return false;
        if (abs((int)point.x - (int)start.x) > (int)TOUCH_MOVE_TOLERANCE ||
            abs((int)point.y - (int)start.y) > (int)TOUCH_MOVE_TOLERANCE) return false;
        delay(TOUCH_POLL_MS);
    }
    if (first) *first = start;
    return true;
}

void handle_power_poll()
{
    static uint32_t pwr_down_at = 0;
    ecp_ble_loop();
    if (bsp_button_pressed(BSP_BTN_PWR)) {
        if (pwr_down_at == 0) pwr_down_at = millis();
        else if (millis() - pwr_down_at >= PWR_LONG_MS) {
            Serial.println("[shell] PWR long press -> clear and power off");
            bsp_ui_fb_clear(0xFF);
            if (bsp_ui_partial_active()) bsp_ui_partial_end();
            (void)bsp_ui_fb_flush_full();
            bsp_power_off();
            pwr_down_at = UINT32_MAX;
        }
    } else {
        pwr_down_at = 0;
    }
}

bool rebase_base(void)
{
    if (bsp_ui_partial_active()) bsp_ui_partial_end();
    bsp_ui_fb_clear(0xFF);
    if (!s_active || !s_active->fullscreen) status_bar_render_into_fb();
    if (!bsp_ui_fb_flush_full()) return false;
    return bsp_ui_partial_begin();
}
}

extern "C" void bsp_remote_key_event(const char* key, const char* event, const char* char_val)
{
    (void)char_val;
    if (!key || !event || (strcmp(event, "press") != 0 && strcmp(event, "down") != 0 && strcmp(event, "repeat") != 0)) return;
    const size_t next = (s_key_tail + 1) % KEY_QUEUE_SIZE;
    if (next == s_key_head) {
        Serial.println("[shell] remote key queue full");
        return;
    }
    snprintf(s_key_queue[s_key_tail].key, sizeof(s_key_queue[0].key), "%s", key);
    snprintf(s_key_queue[s_key_tail].event, sizeof(s_key_queue[0].event), "%s", event);
    s_key_tail = next;
}

bool shell_ble_begin_with_retry(void)
{
    bool ok = false;
    for (int attempt = 1; attempt <= 3 && !ok; attempt++) {
        ok = ecp_ble_begin();
        if (!ok) {
            Serial.printf("[BLE] init attempt %d/3 FAILED\n", attempt);
            delay(500 * attempt);
        }
    }
    if (!ok) Serial.println("[BLE] ALL ATTEMPTS FAILED - remote input unavailable");
    return ok;
}

shell_event_t shell_wait_event(uint32_t timeout_ms)
{
    shell_event_t event = {};
    event.kind = SHELL_EV_NONE;
    const uint32_t start = millis();
    wait_released(TOUCH_RELEASE_MS);

    while (millis() - start < timeout_ms) {
        handle_power_poll();
        if (bsp_button_pressed(BSP_BTN_BOOT)) {
            const uint32_t boot_at = millis();
            while (bsp_button_pressed(BSP_BTN_BOOT)) {
                ecp_ble_loop();
                if (millis() - boot_at >= BOOT_LONG_MS) {
                    while (bsp_button_pressed(BSP_BTN_BOOT)) delay(20);
                    event.kind = SHELL_EV_HOME;
                    return event;
                }
                delay(TOUCH_POLL_MS);
            }
        }

        queued_key_t key;
        if (pop_key(&key)) {
            if (strcmp(key.key, "esc") == 0 || strcmp(key.key, "back") == 0) event.kind = SHELL_EV_BACK;
            else if (strcmp(key.key, "home") == 0) event.kind = SHELL_EV_HOME;
            else {
                event.kind = SHELL_EV_KEY;
                snprintf(event.key, sizeof(event.key), "%s", key.key);
                snprintf(event.event, sizeof(event.event), "%s", key.event);
            }
            return event;
        }

        bsp_touch_point_t point;
        if (read_stable_touch(&point)) {
            const uint32_t down_at = millis();
            const uint16_t x = point.x;
            const uint16_t y = point.y;
            const uint32_t hold_started = millis();
            while (bsp_touch_read(&point) && point.down &&
                   millis() - hold_started < TOUCH_HOLD_TIMEOUT_MS) {
                handle_power_poll();
                delay(TOUCH_POLL_MS);
            }
            if (millis() - hold_started >= TOUCH_HOLD_TIMEOUT_MS) {
                Serial.println("[shell] touch hold timeout; continuing event loop");
            }
            wait_released(TOUCH_RELEASE_MS);
            event.kind = SHELL_EV_TAP;
            event.x = x;
            event.y = y;
            event.long_press = millis() - down_at >= TOUCH_LONG_MS;
            delay(TOUCH_DEBOUNCE_MS);
            return event;
        }
        delay(TOUCH_POLL_MS);
    }
    event.kind = SHELL_EV_TIMEOUT;
    return event;
}

void shell_begin(void)
{
    s_active = nullptr;
    bsp_ui_fb_clear(0xFF);
    status_bar_render_into_fb();
    launcher_render_into_fb();
    if (!bsp_ui_fb_flush_full()) Serial.println("[shell] launcher full refresh failed");
    if (!bsp_ui_partial_begin()) Serial.println("[shell] launcher partial session failed");
}

void shell_open_app(const app_entry_t* app)
{
    if (bsp_ui_partial_active()) bsp_ui_partial_end();
    if (s_active && s_active->on_exit) s_active->on_exit();
    s_active = app;

    bsp_ui_fb_clear(0xFF);
    if (!s_active || !s_active->fullscreen) status_bar_render_into_fb();
    if (!s_active) launcher_render_into_fb();
    if (!bsp_ui_fb_flush_full()) {
        Serial.println("[shell] full refresh transition failed");
        return;
    }
    if (!bsp_ui_partial_begin()) {
        Serial.println("[shell] partial session transition failed");
        return;
    }
    if (s_active && s_active->on_enter) s_active->on_enter();
}

void shell_show_launcher(void)
{
    shell_open_app(nullptr);
}

void shell_tick(void)
{
    if (s_active && s_active->tick) s_active->tick();
    else launcher_tick();
}

void shell_suspend_active(void)
{
    if (bsp_ui_partial_active()) bsp_ui_partial_end();
}

bool shell_rebase_active(void)
{
    return rebase_base();
}

bool shell_full_refresh_current(void)
{
    if (bsp_ui_partial_active()) bsp_ui_partial_end();
    if (!bsp_ui_fb_flush_full()) return false;
    return bsp_ui_partial_begin();
}

void shell_resume_active(void)
{
    if (!s_active) {
        shell_show_launcher();
        return;
    }
    if (!rebase_base()) {
        Serial.println("[shell] resume full refresh failed");
        return;
    }
    if (s_active->on_enter) s_active->on_enter();
}

const app_entry_t* shell_active_app(void)
{
    return s_active;
}
