#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BSP_BTN_BOOT = 0,   /* 所有 ESP32 板都有 */
    BSP_BTN_A,          /* 主功能键：0.85 丝印 KEY / GEEK 丝印 KEY0 */
    BSP_BTN_B,
    BSP_BTN_PWR,
    BSP_BTN_COUNT
} bsp_btn_t;

typedef enum {
    BSP_SELFTEST_PASS = 0,
    BSP_SELFTEST_WARN,
    BSP_SELFTEST_FAIL
} bsp_selftest_result_t;

typedef struct {
    const char* item;
    bsp_selftest_result_t outcome;
    char detail[64];
} bsp_selftest_record_t;

#ifdef __cplusplus
}
#endif
