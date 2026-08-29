#include "status_bar.h"

#include <Arduino.h>
#include <string.h>

#include "bsp.h"
#include "bsp_pins.h"
#include "ecosystem_ble.h"

namespace {
constexpr int STATUS_H = 26;
constexpr uint32_t STATUS_INTERVAL_MS = 30000;
uint32_t s_last_update = 0;

void draw_status(void)
{
    bsp_ui_fb_fill_rect(0, 0, BSP_EPD_W, STATUS_H, false);

    float temperature = 0.0f;
    float humidity = 0.0f;
    char left[32];
    if (bsp_shtc3_read(&temperature, &humidity)) {
        snprintf(left, sizeof(left), "%.1fC %.0f%%RH", temperature, humidity);
    } else {
        snprintf(left, sizeof(left), "--.-C --%%RH");
    }

    char right[24];
    snprintf(right, sizeof(right), "BLE%c %d%%", ecp_ble_connected() ? '+' : '-', bsp_battery_level());
    bsp_ui_fb_draw_text(2, 7, left, 1);
    const int right_x = BSP_EPD_W - (int)strlen(right) * 6 - 2;
    bsp_ui_fb_draw_text(right_x, 7, right, 1);
    bsp_ui_fb_fill_rect(0, STATUS_H - 1, BSP_EPD_W, 1, true);
}
}

void status_bar_render_into_fb(void)
{
    draw_status();
    s_last_update = millis();
}

bool status_bar_refresh_if_due(uint32_t now_ms)
{
    if (now_ms - s_last_update < STATUS_INTERVAL_MS) return false;
    draw_status();
    s_last_update = now_ms;
    return true;
}
