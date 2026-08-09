/**
 * 全家福渐显应用 —— 入口
 *
 * 当前进度：M0（工程骨架 + framebuffer 层 + 静态图全刷）
 * 设计见 docs/FAMILY_PHOTO_APP.md，硬件事实见 docs/EPAPER_154_APP_GUIDE.md。
 *
 * 开机时按住 BOOT 进诊断模式，跑 v0.7.10 的交互式全测试。应用行为和基线结论
 * 冲突时先跑它 —— 保留这条路是刻意的，不要为了「精简」删掉。
 */

#include <Arduino.h>
#include "bsp.h"
#include "bsp_caps.h"

bool bsp_touch_init(void);
void bsp_epaper_154_interactive_test_app(void);

/* 置 0 只跑 M0（静态图全刷），置 1 跑完 M0 再跑 M1（局刷验证）。
 * M0 判读出问题时把它关掉，屏上就一直停在那张测试图上，方便盯着看。 */
#define APP_RUN_M1 1

/* ── M0 测试图 ────────────────────────────────────────────
 * 这张图不是素材，是**验收判据**，三件事一次看清：
 *   1. fb 通路通了       → 屏上有斜纹背景，不是白屏
 *   2. mask 起作用了     → 圆形外的四个角仍是斜纹，不是一整块方框
 *   3. 白区能擦掉背景    → 圆里那条横带是纯白的，把斜纹切断了
 * 第 2 条最关键。没有 mask 的实现在这张图上表现为「一个方块」，一眼可分。
 */
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

/* 椭圆轮廓当人物占位。轮廓内是黑墨，中间横带留白 —— 那条白带模拟「人物身上的
 * 浅色衣服」，它必须把背景的黑斜纹擦掉，这正是局刷里残影最重的方向。 */
static void build_test_bitmap(void)
{
    memset(s_test_bits, 0x00, sizeof(s_test_bits));
    memset(s_test_mask, 0x00, sizeof(s_test_mask));

    const float cx = TEST_BMP_W / 2.0f;
    const float cy = TEST_BMP_H / 2.0f;
    const float rx = TEST_BMP_W / 2.0f - 1.0f;
    const float ry = TEST_BMP_H / 2.0f - 1.0f;

    for (int y = 0; y < TEST_BMP_H; y++) {
        for (int x = 0; x < TEST_BMP_W; x++) {
            const float nx = (x - cx) / rx;
            const float ny = (y - cy) / ry;
            if (nx * nx + ny * ny > 1.0f) continue;   /* 椭圆外：不属于图形 */

            bmp_set(s_test_mask, x, y, true);

            const bool white_band = (y >= TEST_BMP_H / 2 - 12) && (y < TEST_BMP_H / 2 + 12);
            bmp_set(s_test_bits, x, y, !white_band);  /* 带外黑墨，带内留白 */
        }
    }
}

/* 深浅可辨的斜纹背景。真实背景必须比这浅得多（见 FAMILY_PHOTO_APP.md
 * 「背景约束」），这里故意画得偏密，是为了让白区擦除效果看得出来。 */
static void draw_test_background(void)
{
    bsp_ui_fb_clear(0xFF);                       /* 全白 */
    uint8_t* fb = bsp_ui_fb();
    const int stride = 200 / 8;
    for (int y = 0; y < 200; y++) {
        for (int x = 0; x < 200; x++) {
            if (((x + y) % 8) != 0) continue;
            fb[y * stride + (x >> 3)] &= (uint8_t)~(0x80 >> (x & 7));
        }
    }
    bsp_ui_fb_fill_rect(0, 0, 200, 3, true);     /* 四边粗框：证明刷新真的发生了 */
    bsp_ui_fb_fill_rect(0, 197, 200, 3, true);
    bsp_ui_fb_fill_rect(0, 0, 3, 200, true);
    bsp_ui_fb_fill_rect(197, 0, 3, 200, true);
}

static void m0_static_frame(void)
{
    build_test_bitmap();
    draw_test_background();

    /* x 按 8 对齐：整帧局刷不要求，但将来若改窗口局刷就是硬约束。 */
    bsp_ui_draw_bitmap(56, 40, TEST_BMP_W, TEST_BMP_H,
                       s_test_bits, s_test_mask, false);

    const bool ok = bsp_ui_fb_flush_full();
    Serial.printf("[m0] static frame %s\n", ok ? "OK" : "FAIL");
    Serial.println("[m0] 判读：斜纹背景 + 椭圆黑块 + 椭圆中部白带；");
    Serial.println("[m0] 若看到的是一个矩形块 -> mask 没生效");
}

/* ── M1 局刷验证 ──────────────────────────────────────────
 * 一个 24x24 的方块，每帧右移 20px，走完一行再换下一行，共 20 帧。
 *
 * 为什么是方块不是真实素材：出问题时只有驱动一个变量。混进抠像素材以后，
 * 「画面不对」可能是 LUT、可能是基准图、也可能是转换工具错了，三个嫌疑人。
 *
 * 判读三条，缺一不可：
 *   1. 串口 busy 毫秒数显著低于全刷的 1755ms（预期 300~500）
 *      —— 若仍是 ~1755ms，局刷 LUT 没生效，后面都不用看了
 *   2. 刷新时**不闪黑**（全刷会整屏黑白翻转，局刷不该有）
 *   3. 连续 20 帧，记下残影从第几帧开始肉眼可见 -> 定 APP_PARTIAL_MAX_STREAK
 */
#define M1_STEPS      20
#define M1_BOX        24
#define M1_STEP_X     20

static void m1_partial_walk(void)
{
    /* 基准图必须等于屏上画面：先画一张干净底图并全刷，再拿同一份 fb 开会话。 */
    bsp_ui_fb_clear(0xFF);
    bsp_ui_fb_fill_rect(0, 0, 200, 2, true);
    bsp_ui_fb_fill_rect(0, 198, 200, 2, true);
    bsp_ui_fb_fill_rect(0, 0, 2, 200, true);
    bsp_ui_fb_fill_rect(198, 0, 2, 200, true);
    if (!bsp_ui_fb_flush_full()) {
        Serial.println("[m1] ABORT: base full flush failed");
        return;
    }

    if (!bsp_ui_partial_begin()) {
        Serial.println("[m1] ABORT: partial_begin failed");
        return;
    }

    Serial.println("[m1] walking box; watch busy_ms and look for flashing");
    for (int i = 0; i < M1_STEPS; i++) {
        const int x = 8 + (i % 9) * M1_STEP_X;
        const int y = 20 + (i / 9) * 40;

        /* 每帧只擦上一格、画新一格？—— 不。fb 是唯一真值，整帧重画最简单，
         * 而局刷本来就是整帧 5000 字节推过去，省不下传输量。 */
        bsp_ui_fb_clear(0xFF);
        bsp_ui_fb_fill_rect(0, 0, 200, 2, true);
        bsp_ui_fb_fill_rect(0, 198, 200, 2, true);
        bsp_ui_fb_fill_rect(0, 0, 2, 200, true);
        bsp_ui_fb_fill_rect(198, 0, 2, 200, true);
        bsp_ui_fb_fill_rect(x, y, M1_BOX, M1_BOX, true);

        const uint32_t t0 = millis();
        const bool ok = bsp_ui_flush_partial();
        Serial.printf("[m1] step %2d/%d x=%3d y=%3d %s wall=%lums\n",
                      i + 1, M1_STEPS, x, y, ok ? "ok" : "FAIL",
                      (unsigned long)(millis() - t0));
        if (!ok) break;
        delay(600);   /* 留出看清每一步的时间，也方便数残影从第几帧起可见 */
    }

    bsp_ui_partial_end();

    /* 结尾全刷定格：局刷叠出来的画面对比度低于全刷，而断电保图保的是最后这一帧。 */
    bsp_ui_fb_clear(0xFF);
    bsp_ui_fb_fill_rect(20, 88, 160, 24, true);
    bsp_ui_fb_flush_full();
    Serial.println("[m1] done, finished with a full refresh");
}

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
    Serial.println("[app] family-photo, milestone M0");

    if (boot_held_for_diagnostics()) {
        Serial.println("[app] BOOT held -> diagnostics (v0.7.10 interactive all-test)");
#if BSP_HAS_TOUCH
        bsp_touch_init();
#endif
        bsp_epaper_154_interactive_test_app();
        return;
    }

#if BSP_HAS_TOUCH
    if (!bsp_touch_init()) {
        Serial.println("[touch] 初始化失败，本次开机无触摸");
    }
#endif

    /* M0 先跑：它验证的是 fb/mask/全刷这条最短的通路。M0 不通就没必要看 M1，
     * 因为 M1 的基准图也是走同一条全刷路径写上去的。 */
    m0_static_frame();

#if APP_RUN_M1
    Serial.println("[app] 10s to inspect M0 frame, then M1 partial-refresh test");
    delay(10000);
    m1_partial_walk();
#endif
}

void loop()
{
    /* M0 只出一帧静态图。触摸叠加流程等 M1 局刷通路验证过再接。 */
    delay(1000);
}
