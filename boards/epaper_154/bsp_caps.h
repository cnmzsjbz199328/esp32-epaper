#pragma once

#define BSP_BOARD_ID            "epaper_154"
#define BSP_BOARD_NAME          "Waveshare ESP32-S3-ePaper-1.54"
#define BSP_BOARD_MODULE        "ESP32-S3-PICO-1-N8R8"

#define BSP_BOARD_VER_MAJOR     0
#define BSP_BOARD_VER_MINOR     9
#define BSP_BOARD_VER_PATCH     0

#define BSP_BOARD_API_MAJOR     1
#define BSP_BOARD_MATURITY      "draft"

#define BSP_HAS_DISPLAY         1
#define BSP_HAS_EPD_VISIBLE_SMOKE_TEST 1
#define BSP_HAS_BACKLIGHT       0
#define BSP_UI_REALTIME         0
#define BSP_HAS_TOUCH           1
#define BSP_HAS_AUDIO_OUT       1
#define BSP_HAS_AUDIO_IN        1
#define BSP_HAS_RGB_LED         0
#define BSP_HAS_SDCARD          1
#define BSP_HAS_BATTERY         1
#define BSP_HAS_PWR_LATCH       0
#define BSP_HAS_WIFI            1

/* Ecosystem BLE protocol capabilities implemented by the family-video app. */
#define BSP_CAP_INPUT_REMOTE_KEY  1
#define BSP_CAP_INPUT_REMOTE_TEXT 1
#define BSP_CAP_CONFIG_WIFI       1

/* 应用工程里关掉交互式全测试入口 —— 这里跑的是全家福应用，不是 bring-up。
 * 但**不要删除** selftest.cpp 和这个宏：开机时按住 BOOT 会进诊断模式，
 * 走的就是这条路径。当应用现象和 v0.7.10 的结论冲突时，它是回到已知硬件
 * 真值的唯一入口（见 docs/EPAPER_154_APP_GUIDE.md「排错边界」）。 */
#define BSP_EPAPER_INTERACTIVE_TEST_APP 0
