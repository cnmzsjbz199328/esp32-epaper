#include <Arduino.h>
#include <string.h>

#include "app_diagnostics.h"
#include "bsp.h"

void bsp_epaper_154_interactive_test_app(void);

#define TEST_BMP_W   96
#define TEST_BMP_H   128
#define TEST_BMP_STRIDE ((TEST_BMP_W + 7) / 8)
#define M1_STEPS 20
#define M1_BOX   24

static uint8_t s_test_bits[TEST_BMP_STRIDE * TEST_BMP_H];
static uint8_t s_test_mask[TEST_BMP_STRIDE * TEST_BMP_H];

static void bmp_set(uint8_t* buf, int x, int y, bool on)
{
    uint8_t* p = &buf[y * TEST_BMP_STRIDE + (x >> 3)];
    const uint8_t bit = (uint8_t)(0x80 >> (x & 7));
    if (on) *p |= bit;
    else    *p &= (uint8_t)~bit;
}

static void build_test_bitmap(void)
{
    memset(s_test_bits, 0x00, sizeof(s_test_bits));
    memset(s_test_mask, 0x00, sizeof(s_test_mask));

    const float cx = TEST_BMP_W / 2.0f, cy = TEST_BMP_H / 2.0f;
    const float rx = TEST_BMP_W / 2.0f - 1.0f, ry = TEST_BMP_H / 2.0f - 1.0f;

    for (int y = 0; y < TEST_BMP_H; y++) {
        for (int x = 0; x < TEST_BMP_W; x++) {
            const float nx = (x - cx) / rx, ny = (y - cy) / ry;
            if (nx * nx + ny * ny > 1.0f) continue;
            bmp_set(s_test_mask, x, y, true);
            const bool white_band = (y >= TEST_BMP_H / 2 - 12) && (y < TEST_BMP_H / 2 + 12);
            bmp_set(s_test_bits, x, y, !white_band);
        }
    }
}

static void draw_frame_border(void)
{
    bsp_ui_fb_fill_rect(0, 0, 200, 3, true);
    bsp_ui_fb_fill_rect(0, 197, 200, 3, true);
    bsp_ui_fb_fill_rect(0, 0, 3, 200, true);
    bsp_ui_fb_fill_rect(197, 0, 3, 200, true);
}

bool app_boot_held_for_diagnostics(void)
{
    const uint32_t t0 = millis();
    while (millis() - t0 < 800) {
        if (bsp_button_pressed(BSP_BTN_BOOT)) return true;
        delay(20);
    }
    return false;
}

void app_run_boot_diagnostics(void)
{
    Serial.println("[app] BOOT held -> diagnostics (v0.7.10 interactive all-test)");
    bsp_epaper_154_interactive_test_app();
}

void app_run_m0_diagnostics(void)
{
    build_test_bitmap();
    bsp_ui_fb_clear(0xFF);
    uint8_t* fb = bsp_ui_fb();
    for (int y = 0; y < 200; y++)
        for (int x = 0; x < 200; x++)
            if (((x + y) % 8) == 0)
                fb[y * 25 + (x >> 3)] &= (uint8_t)~(0x80 >> (x & 7));
    draw_frame_border();

    bsp_ui_draw_bitmap(56, 40, TEST_BMP_W, TEST_BMP_H, s_test_bits, s_test_mask, false);
    Serial.printf("[m0] static frame %s busy=%lums\n",
                  bsp_ui_fb_flush_full() ? "OK" : "FAIL",
                  (unsigned long)bsp_epd_last_busy_ms());
    Serial.println("[m0] expect diagonal background + masked oval + white band");
    Serial.println("[m0] a rectangular block means bitmap mask handling regressed");
}

void app_run_m1_diagnostics(void)
{
    app_run_m0_diagnostics();
    delay(10000);

    bsp_ui_fb_clear(0xFF);
    draw_frame_border();
    if (!bsp_ui_fb_flush_full())  { Serial.println("[m1] ABORT: base flush failed"); return; }
    if (!bsp_ui_partial_begin())  { Serial.println("[m1] ABORT: partial_begin failed"); return; }

    Serial.println("[m1] walking box; record busy_ms and visible ghosting threshold");
    for (int i = 0; i < M1_STEPS; i++) {
        const int x = 8 + (i % 9) * 20;
        const int y = 20 + (i / 9) * 40;
        bsp_ui_fb_clear(0xFF);
        draw_frame_border();
        bsp_ui_fb_fill_rect(x, y, M1_BOX, M1_BOX, true);
        const bool ok = bsp_ui_flush_partial();
        Serial.printf("[m1] step=%d/%d x=%d y=%d mode=partial busy=%lums %s\n",
                      i + 1, M1_STEPS, x, y,
                      (unsigned long)bsp_epd_last_busy_ms(), ok ? "ok" : "FAIL");
        if (!ok) break;
        delay(600);
    }

    bsp_ui_partial_end();
    bsp_ui_fb_clear(0xFF);
    bsp_ui_fb_fill_rect(20, 88, 160, 24, true);
    bsp_ui_fb_flush_full();
    Serial.printf("[m1] done, final full refresh busy=%lums\n",
                  (unsigned long)bsp_epd_last_busy_ms());
}
