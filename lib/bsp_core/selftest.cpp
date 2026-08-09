#include <Arduino.h>
#include <WiFi.h>
#include <stdarg.h>
#include "bsp.h"

/* ===========================================================================
 * 自检汇报与计数
 *
 * 计数器放共享层而不是板级：串口日志末尾那行 "N PASS / M WARN / K FAIL"
 * 是回归比对的锚点（EVALUATION.md B2），每块板都要，且与硬件无关。
 * 重构期间这套计数整体丢失过，导致「11 PASS」这类声明无从核对。
 * ======================================================================== */

static int s_pass = 0;
static int s_warn = 0;
static int s_fail = 0;

void bsp_selftest_begin(void)
{
    s_pass = s_warn = s_fail = 0;
}

void bsp_selftest_report(bsp_selftest_result_t r, const char* name, const char* fmt, ...)
{
    const char* tag;
    switch (r) {
        case BSP_SELFTEST_PASS: tag = "PASS"; s_pass++; break;
        case BSP_SELFTEST_WARN: tag = "WARN"; s_warn++; break;
        default:                tag = "FAIL"; s_fail++; break;
    }

    char detail[96];
    va_list args;
    va_start(args, fmt);
    vsnprintf(detail, sizeof(detail), fmt, args);
    va_end(args);

    bsp_ui_printf("[%s] %-6s %s\n", tag, name, detail);
}

int bsp_selftest_count(bsp_selftest_result_t r)
{
    switch (r) {
        case BSP_SELFTEST_PASS: return s_pass;
        case BSP_SELFTEST_WARN: return s_warn;
        default:                return s_fail;
    }
}

void bsp_selftest_summary(void)
{
    bsp_ui_printf("%d PASS / %d WARN / %d FAIL\n", s_pass, s_warn, s_fail);
}

/* ===========================================================================
 * 与板级引脚无关的通用检测项
 * ======================================================================== */

/* v0.21.0 起本项从「报容量」升级为「读写比对」。
 *
 * **容量对证不到能用。** `ESP.getPsramSize()` 读的是初始化阶段探测出来的
 * 配置值，片子焊虚、某几根地址线不通、或者时序临界，它照样报满容量 ——
 * 与「ES8311 在 I2C 上应答不能说明 I2S 六根线接对」是同一类错误。
 *
 * 判据：分配一块 → 写伪随机 → 读回**逐字节**比对。
 *   - 伪随机而不是固定花样（0xAA/0x55）：固定花样在**地址线短路**时也能全对
 *     ——两个地址映射到同一块物理内存，写进去的值一样，读出来当然一样。
 *     每个字位置写一个由**下标**决定的值，地址错了值就对不上。
 *   - LCG 而不是 rand()：要能在失败时精确说出「第 N 个字期望 X 实得 Y」，
 *     那要求序列可重放，rand() 的实现不保证跨平台一致。
 *
 * 分配大小 min(1MB, 容量/8)：五块板容量不同（geek 2MB、touch_lcd_154 8MB），
 * 写死会在小容量板上抢光堆。取 1/8 是为了不与 LCD 帧缓冲等既有用途打架 ——
 * **这一项的目的是证明 PSRAM 能用，不是压测它的极限。** */
#define PSRAM_RW_MAX_BYTES   (1024u * 1024u)
#define PSRAM_LCG_SEED       0x12345678u

static inline uint32_t psram_lcg(uint32_t x)
{
    /* Numerical Recipes 的常数。取哪一组不重要，可复现才重要。 */
    return x * 1664525u + 1013904223u;
}

void test_psram(void)
{
    const size_t sz = ESP.getPsramSize();
    if (sz == 0) {
        bsp_selftest_report(BSP_SELFTEST_FAIL, "PSRAM", "0 B");
        return;
    }

    size_t want = sz / 8;
    if (want > PSRAM_RW_MAX_BYTES) want = PSRAM_RW_MAX_BYTES;
    want &= ~(size_t)3;                      /* 对齐到 4 字节 */

    uint32_t* buf = (uint32_t*)ps_malloc(want);
    if (!buf) {
        /* 容量报得出来但分配不到 —— 这本身就是个结论，比只报容量强。 */
        bsp_selftest_report(BSP_SELFTEST_WARN, "PSRAM", "%.1fMB, ps_malloc %uKB 失败",
                            sz / 1048576.0, (unsigned)(want / 1024));
        return;
    }

    const size_t words = want / 4;
    uint32_t v = PSRAM_LCG_SEED;
    for (size_t i = 0; i < words; i++) { v = psram_lcg(v); buf[i] = v; }

    /* 重放同一条序列比对。**不是拿 buf 跟 buf 比** —— 那样连读都不用做。 */
    v = PSRAM_LCG_SEED;
    size_t   bad = 0;
    size_t   first_bad = 0;
    uint32_t exp_bad = 0, got_bad = 0;
    for (size_t i = 0; i < words; i++) {
        v = psram_lcg(v);
        if (buf[i] != v) {
            if (bad == 0) { first_bad = i; exp_bad = v; got_bad = buf[i]; }
            bad++;
        }
    }
    free(buf);

    if (bad) {
        bsp_selftest_report(BSP_SELFTEST_FAIL, "PSRAM",
                            "%.1fMB rw%uKB 坏%u字 @%u 期望%08X得%08X",
                            sz / 1048576.0, (unsigned)(want / 1024), (unsigned)bad,
                            (unsigned)first_bad, (unsigned)exp_bad, (unsigned)got_bad);
        return;
    }

    /* 容量仍然报出来，但它不再是判据 —— 判据是 rwOK。
     * 两个数都留着，是为了让「容量对而读写坏」这种情况看得见。 */
    bsp_selftest_report(BSP_SELFTEST_PASS, "PSRAM", "%u B (%.1fMB) rw%uKB OK",
                        (unsigned)sz, sz / 1048576.0, (unsigned)(want / 1024));
}

void test_flash(void)
{
    const uint32_t sz = ESP.getFlashChipSize();
    bsp_selftest_report(sz >= 8u * 1024 * 1024 ? BSP_SELFTEST_PASS : BSP_SELFTEST_WARN,
                        "Flash", "%.1fMB", sz / 1048576.0);
}

void test_wifi(void)
{
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    const int n = WiFi.scanNetworks();
    if (n > 0) {
        bsp_selftest_report(BSP_SELFTEST_PASS, "WiFi", "%d APs, RSSI %d", n, WiFi.RSSI(0));
    } else {
        bsp_selftest_report(BSP_SELFTEST_WARN, "WiFi", "scan empty");
    }
    WiFi.scanDelete();
    WiFi.mode(WIFI_OFF);
}
