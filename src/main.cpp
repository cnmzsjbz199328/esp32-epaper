/**
 * 全家福视频抽帧应用 —— 入口
 *
 * 上电全刷第一帧，此后右半屏前进一帧，左半屏后退一帧。
 * 设计见 docs/FAMILY_PHOTO_APP.md，硬件事实见 docs/EPAPER_154_APP_GUIDE.md。
 *
 * 开机时按住 BOOT 进诊断模式，跑 v0.7.10 的交互式全测试。应用行为和基线结论
 * 冲突时先跑它 —— 保留这条路是刻意的，不要为了「精简」删掉。
 */

#include <Arduino.h>
#include <string.h>
#include "bsp.h"
#include "bsp_caps.h"
#include "bsp_pins.h"   /* BSP_EPD_W：左右半屏判据要用 */
#include "touch.h"
#include "family_video_assets.h"

void bsp_epaper_154_interactive_test_app(void);

/* 运行模式。M0/M1 是里程碑验证程序，出问题时切回去缩小排查面：
 * 它们不碰素材，画面不对就只可能是驱动。 */
#define APP_MODE_M0       1   /* 静态图全刷 + mask 判读 */
#define APP_MODE_M1       2   /* 方块右移局刷验证 */
#define APP_MODE_VIDEO    3   /* 视频抽帧：整帧静态阶段 */

#ifndef APP_MODE
#define APP_MODE APP_MODE_VIDEO
#endif

/* 连续局刷上限，超过则插一次全刷。**这个值是猜的**，M1/M3 实测后再改：
 * 局刷残影阈值在这块板上没有实测数据。 */
#define APP_PARTIAL_MAX_STREAK 6

/* ========================================================================== */
/* 全家福视频抽帧应用                                                          */
/* ========================================================================== */

static int s_streak = 0;   /* 自上次全刷以来的连续局刷次数 */
static int s_video_frame = 0;

/**
 * 等一次触摸导航。返回 -1 左半屏 / +1 右半屏 / 0 超时。
 *
 * bsp_touch_read() 给的是 raw 坐标，没有事件队列，必须自己去抖：
 * 等按下 -> 等抬起 -> 再延 180ms。少了这一段，一次按压会被读成好几次，
 * 现象是「点一下出来两个人」。
 *
 * 刷屏期间不会走到这里 —— 刷新是阻塞的，天然屏蔽了 BUSY 中途重入。
 */
static int app_wait_nav(uint32_t timeout_ms)
{
    const uint32_t t0 = millis();
    bsp_touch_point_t p;

    while (millis() - t0 < timeout_ms) {
        static uint32_t pwr_down_at = 0;
        if (bsp_button_pressed(BSP_BTN_PWR)) {
            if (pwr_down_at == 0) pwr_down_at = millis();
            else if (millis() - pwr_down_at > 3000) {
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

        if (bsp_touch_read(&p) && p.down) {
            const int dir = (p.x < BSP_EPD_W / 2) ? -1 : 1;
            while (bsp_touch_read(&p) && p.down) delay(40);
            delay(180);
            return dir;
        }
        delay(40);
    }
    return 0;
}

static void app_video_build_fb(int index)
{
    if (index < 0) index = 0;
    if (index >= APP_VIDEO_FRAME_COUNT) index = APP_VIDEO_FRAME_COUNT - 1;
    memcpy(bsp_ui_fb(), APP_VIDEO_FRAMES[index], APP_VIDEO_FRAME_LEN);
}

static bool app_video_full_then_rebase(void)
{
    if (bsp_ui_partial_active()) bsp_ui_partial_end();
    if (!bsp_ui_fb_flush_full()) return false;
    s_streak = 0;
    return bsp_ui_partial_begin();
}

static void app_video_show_frame(int target)
{
    if (target < 0) target = 0;
    if (target >= APP_VIDEO_FRAME_COUNT) target = APP_VIDEO_FRAME_COUNT - 1;

    if (target == s_video_frame) {
        Serial.printf("[video] already at frame %d/%d\n", s_video_frame + 1, APP_VIDEO_FRAME_COUNT);
        return;
    }

    s_video_frame = target;
    app_video_build_fb(s_video_frame);
    Serial.printf("[video] frame %d/%d\n", s_video_frame + 1, APP_VIDEO_FRAME_COUNT);

    if (s_video_frame >= APP_VIDEO_FRAME_COUNT - 1) {
        Serial.println("[video] final extracted frame -> full refresh");
        if (bsp_ui_partial_active()) bsp_ui_partial_end();
        bsp_ui_fb_flush_full();
        return;
    }

    if (!bsp_ui_partial_active()) {
        Serial.println("[video] partial inactive -> full refresh and rebase");
        app_video_full_then_rebase();
        return;
    }

    if (s_streak >= APP_PARTIAL_MAX_STREAK) {
        Serial.printf("[video] streak %d -> inserting a full refresh\n", s_streak);
        app_video_full_then_rebase();
        return;
    }

    if (!bsp_ui_flush_partial()) {
        Serial.println("[video] partial failed -> falling back to full refresh");
        app_video_full_then_rebase();
        return;
    }
    s_streak++;
}

static void app_video_setup(void)
{
    Serial.printf("[story] solo prelude + staged group app, %d frames\n", APP_VIDEO_FRAME_COUNT);
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
    Serial.println("[video] touch RIGHT = next frame, LEFT = previous frame, hold PWR 3s = off");
}

static void app_video_loop(void)
{
    const int dir = app_wait_nav(60000);
    if (dir > 0)      app_video_show_frame(s_video_frame + 1);
    else if (dir < 0) app_video_show_frame(s_video_frame - 1);
}

/* ========================================================================== */
/* M0 / M1 里程碑验证程序                                                       */
/* ========================================================================== */
#if APP_MODE == APP_MODE_M0 || APP_MODE == APP_MODE_M1

/* M0 测试图：三件事一次看清 —— fb 通路通了（有斜纹背景）、mask 起作用了
 * （圆外仍是斜纹而不是一整块方框）、白区能擦掉背景（圆里横带切断斜纹）。
 * 第二条最关键：没有 mask 的实现在这张图上表现为一个矩形块，一眼可分。 */
#define TEST_BMP_W   96
#define TEST_BMP_H   128
#define TEST_BMP_STRIDE ((TEST_BMP_W + 7) / 8)

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

static void m0_static_frame(void)
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
    Serial.printf("[m0] static frame %s\n", bsp_ui_fb_flush_full() ? "OK" : "FAIL");
    Serial.println("[m0] 判读：斜纹背景 + 椭圆黑块 + 椭圆中部白带；");
    Serial.println("[m0] 若看到的是一个矩形块 -> mask 没生效");
}

/* M1：24x24 方块每帧右移 20px，共 20 帧。判读三条 ——
 *   1. busy 毫秒数显著低于全刷的 1755ms（预期 300~500）
 *   2. 刷新时不闪黑
 *   3. 记下残影从第几帧起肉眼可见 -> 定 APP_PARTIAL_MAX_STREAK  */
#define M1_STEPS 20
#define M1_BOX   24

static void m1_partial_walk(void)
{
    bsp_ui_fb_clear(0xFF);
    draw_frame_border();
    if (!bsp_ui_fb_flush_full())  { Serial.println("[m1] ABORT: base flush failed"); return; }
    if (!bsp_ui_partial_begin())  { Serial.println("[m1] ABORT: partial_begin failed"); return; }

    Serial.println("[m1] walking box; watch busy_ms and look for flashing");
    for (int i = 0; i < M1_STEPS; i++) {
        const int x = 8 + (i % 9) * 20;
        const int y = 20 + (i / 9) * 40;
        bsp_ui_fb_clear(0xFF);
        draw_frame_border();
        bsp_ui_fb_fill_rect(x, y, M1_BOX, M1_BOX, true);
        const bool ok = bsp_ui_flush_partial();
        Serial.printf("[m1] step %2d/%d x=%3d y=%3d %s\n", i + 1, M1_STEPS, x, y, ok ? "ok" : "FAIL");
        if (!ok) break;
        delay(600);
    }

    bsp_ui_partial_end();
    bsp_ui_fb_clear(0xFF);
    bsp_ui_fb_fill_rect(20, 88, 160, 24, true);
    bsp_ui_fb_flush_full();
    Serial.println("[m1] done, finished with a full refresh");
}
#endif /* APP_MODE == APP_MODE_M0 || APP_MODE == APP_MODE_M1 */

/* ========================================================================== */

/* 开机按住 BOOT 进诊断模式。上电瞬间读一次不够可靠（键还没稳），
 * 这里在 800ms 窗口内轮询，任意时刻按下都算。 */
static bool boot_held_for_diagnostics(void)
{
    const uint32_t t0 = millis();
    while (millis() - t0 < 800) {
        if (bsp_button_pressed(BSP_BTN_BOOT)) return true;
        delay(20);
    }
    return false;
}

void setup()
{
    bsp_board_init();      // 必须第一句
    bsp_ui_init();

    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 2000) delay(10);

    Serial.println(bsp_version_string());

    if (boot_held_for_diagnostics()) {
        Serial.println("[app] BOOT held -> diagnostics (v0.7.10 interactive all-test)");
        bsp_touch_init();
        bsp_epaper_154_interactive_test_app();
        return;
    }

    if (!bsp_touch_init()) {
        Serial.println("[touch] 初始化失败，本次开机无触摸");
    }

#if APP_MODE == APP_MODE_VIDEO
    app_video_setup();
#elif APP_MODE == APP_MODE_M0
    m0_static_frame();
#elif APP_MODE == APP_MODE_M1
    m0_static_frame();
    delay(10000);
    m1_partial_walk();
#endif
}

void loop()
{
#if APP_MODE == APP_MODE_VIDEO
    app_video_loop();
#else
    delay(1000);
#endif
}
