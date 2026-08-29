#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>

#include "bsp_types.h"
#include "bsp_version.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 板级基础与电源 ───────────────────────────────────────── */
void        bsp_board_init(void);
void        bsp_power_off(void);
bool        bsp_button_pressed(bsp_btn_t b);  /* 本板没有该键时恒返回 false */
const char* bsp_button_name(bsp_btn_t b);     /* 返回丝印名；没有该键返回 NULL */
bool        bsp_shtc3_read(float* t_c, float* rh);
uint32_t    bsp_battery_mv(void);
int         bsp_battery_level(void);

/* ── 自检汇报（实现在 lib/bsp_core/selftest.cpp，板级共用） ──
 * 所有自检项必须走 bsp_selftest_report()，不要直接 bsp_ui_printf 打 [PASS] ——
 * 否则计数器统计不到，串口日志末尾的 "N PASS / M WARN / K FAIL" 就会失真，
 * 而那一行正是 EVALUATION.md B2 的判读依据。
 */
void bsp_selftest_begin(void);
void bsp_selftest_report(bsp_selftest_result_t r, const char* name, const char* fmt, ...);
void bsp_selftest_summary(void);   /* 打印汇总行 */
int  bsp_selftest_count(bsp_selftest_result_t r);
size_t bsp_selftest_record_count(void);
bool bsp_selftest_record_at(size_t index, bsp_selftest_record_t* out);

/* 板级专属自检项，由各板 selftest.cpp 实现 */
void bsp_selftest_board_specific(void);

/* ── UI抽象层（共享层自检汇报使用，板级做具体实现） ───────── */
void     bsp_ui_init(void);
void     bsp_ui_clear(void);
void     bsp_ui_printf(const char* fmt, ...);              /* 追加一行，自动滚动 */
void     bsp_ui_status(int line, const char* fmt, ...);    /* 定位行覆盖写 */
void     bsp_ui_bar(int slot, uint16_t value, uint16_t max);
void     bsp_ui_flush(void);       /* 墨水屏在此真正刷屏；LCD 上是空实现 */
uint16_t bsp_ui_cols(void);
uint16_t bsp_ui_rows(void);

/* ── 位图 / framebuffer 层 ─────────────────────────────────
 * fb 是面板原生格式：bit 1 = 白，bit 0 = 黑墨，行优先，每行 W/8 字节，
 * 行内 MSB 是最左像素。整帧直接喂给面板，中途不做取反。
 *
 * 与之相反，素材位图里 1 = 黑墨；取反只发生在 bsp_ui_draw_bitmap() 内部。
 */
uint8_t* bsp_ui_fb(void);
uint32_t bsp_ui_fb_len(void);
void     bsp_ui_fb_clear(uint8_t value);       /* 0xFF = 全白，0x00 = 全黑 */
void     bsp_ui_fb_fill_rect(int x, int y, int w, int h, bool black);
void     bsp_ui_fb_draw_text(int x, int y, const char* text, uint8_t scale);

/* mask 传 NULL = 整个 w*h 矩形都算图形。抠像人物**必须**给 mask，
 * 否则矩形框会连同框内背景一起糊上去。见 docs/FAMILY_PHOTO_APP.md。 */
void     bsp_ui_draw_bitmap(int x, int y, int w, int h,
                            const uint8_t* bits,   /* 1 = 黑墨 */
                            const uint8_t* mask,   /* 1 = 属于图形 */
                            bool invert);

bool     bsp_ui_fb_flush_full(void);           /* 全刷当前 fb */

/* ── 局部刷新 ─────────────────────────────────────────────
 * 会话式：begin 把当前 fb 写成基准图（0x24+0x26）并切到局刷 LUT，
 * 之后每次 flush_partial 整帧推 0x24 + 0x22 0xCF。
 *
 * begin 之前屏上画面必须**已经等于当前 fb**（正常是刚做完 flush_full）。
 * 基准图与实际画面对不上 = 满屏鬼影。
 *
 * 插一次全刷必须走：partial_end → fb_flush_full → partial_begin。
 */
bool     bsp_ui_partial_begin(void);
bool     bsp_ui_flush_partial(void);
void     bsp_ui_partial_end(void);
bool     bsp_ui_partial_active(void);

bool     bsp_epd_last_refresh_attempted(void);
bool     bsp_epd_last_refresh_ok(void);
uint32_t bsp_epd_last_busy_ms(void);
const char* bsp_epd_last_detail(void);

#ifdef __cplusplus
}
#endif
