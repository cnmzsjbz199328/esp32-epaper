/**
 * bsp_pins.h — Waveshare ESP32-S3-Touch-ePaper-1.54 引脚与硬件参数总表
 *
 * 这是本板唯一的引脚真值来源。新写任何代码都从这里取值，不要再抄文档。
 *
 * 【状态标记】
 *   [OK]     实机烧录验证通过，可直接信任 —— 本板一个都没有，没有实物
 *   [REF]    来自官方原理图 或 芯片数据手册（一手设计/规格资料）
 *   [DOC]    仅来自 wiki 正文 / 丝印图，未与原理图交叉核对
 *
 * v0.2.0 起本表以官方原理图 ESP32-S3-Touch-ePaper-1.54-Schematic 为基准重建
 * （docs/refs/ESP32-S3-Touch-ePaper-1.54-Schematic.pdf）。
 *
 * v0.3.0 起补入官方 Demo 仓库（github.com/waveshareteam/ESP32-S3-ePaper-1.54）
 * 作为第 3 层来源，与原理图互证；摘录的源文件在 docs/refs/demo/。
 *
 * ⚠️ v0.1.0 的 BUSY/RST/DC/CS/CLK/DIN 6 个引脚宏已撤回，理由见 CHANGELOG。
 */
#pragma once

#include <driver/spi_master.h>

// ============================================================================
// EPD 墨水屏 —— 1.54" 黑白 Eink (SSD1681 驱动，200x200 分辨率)
// 原理图 U8 (ESP32-S3) 与 EPD 接口/IO 映射表逐行给出各信号对应的 GPIO；
// Datasheet (1.54inch e-Paper V2) 确认 BUSY 高有效（High=Busy）、驱动芯片寄存器。
// ============================================================================
#define BSP_PIN_EPD_BUSY    8    // [REF] 原理图 IO8 (EPD_BUSY)
#define BSP_PIN_EPD_RST     9    // [REF] 原理图 IO9 (EPD_RST)
#define BSP_PIN_EPD_DC      10   // [REF] 原理图 IO10 (EPD_D/C)
#define BSP_PIN_EPD_CS      11   // [REF] 原理图 IO11 (EPD_CS)
#define BSP_PIN_EPD_CLK     12   // [REF] 原理图 IO12 (EPD_SCLK)
#define BSP_PIN_EPD_DIN     13   // [REF] 原理图 IO13 (EPD_SDI)
#define BSP_PIN_EPD_PWR_EN  6    // [REF] 原理图 IO6 (EPD3V3_EN), active-low: ON=LOW, OFF=HIGH

// --- EPD 逻辑控制与物理特性 ---
#define BSP_EPD_BUSY_ACTIVE_HIGH 1 // [REF] Datasheet Note 1.5-4 + 示例 boards/epaper_154/docs/refs/demo/epaper_driver_bsp.cpp read_busy() while(level==1)
#define BSP_EPD_PARTIAL_8PIXEL_ALIGN 1 // [REF] Datasheet 0x44 A[5:0] 单位 1 byte + 示例 boards/epaper_154/docs/refs/demo/epaper_driver_bsp.cpp EPD_SetWindows (Xstart>>3)

#define BSP_EPD_SPI_FREQ    40000000 // [REF] 示例 boards/epaper_154/docs/refs/demo/epaper_driver_bsp.cpp devcfg.clock_speed_hz = 40*1000*1000（同行注释写 10 MHz，与代码不符，见 SOURCES §4.6）
// ⚠️ 当前 ui.cpp 的探针固件跑的不是上面这个值 —— v0.6.1 起降频至 4MHz 排除高速边沿余量。
//    降频值是固件调参、不是硬件事实，故不在本表定义，见 ui.cpp EPD_SPI_FREQ_PROBE。
//    在 EPD 可见刷新确认之前，不要把 BSP_EPD_SPI_FREQ 当成「本板实际工作频率」。

// --- EPD 面板与 GRAM 几何 ---
#define BSP_EPD_W           200  // [REF] Datasheet 1.54" 200(H)x200(V)
#define BSP_EPD_H           200  // [REF] Datasheet 1.54" 200(H)x200(V)

// ============================================================================
// I2C 总线 —— 挂载 触摸 IC、RTC (PCF85063)、温湿度 (SHTC3)
// 原理图 IO47 (SDA), IO48 (SCL) 挂载全板所有 I2C 器件。
// ============================================================================
#define BSP_PIN_I2C_SDA     47   // [REF] 原理图 IO47 (SDA)
#define BSP_PIN_I2C_SCL     48   // [REF] 原理图 IO48 (SCL)
#define BSP_I2C_FREQ        400000 // [REF] 首轮实机测试用标准 Fast-mode；待实测后转 [OK]

// --- 触摸屏 (EPD TP) —— 控制器为 FT6336 ---
#define BSP_PIN_TOUCH_RST   7    // [REF] 原理图 IO7 (EPD_TP_RST)
#define BSP_PIN_TOUCH_INT   21   // [REF] 原理图 IO21 (EPD_TP_INT)
#define BSP_TOUCH_I2C_ADDR  0x38 // [REF] 示例 boards/epaper_154/docs/refs/demo/user_config.h I2C_FT6336_DEV_Address
// v0.7.7 起 BSP_HAS_TOUCH=1：touch.cpp 提供 FT6336 轮询坐标 API。
// 当前 API 沿用 raw 坐标，尚未提供旋转/事件队列。

// --- RTC (PCF85063) ---
#define BSP_PIN_RTC_INT     5    // [REF] 原理图 IO5 (RTC_INT)
#define BSP_RTC_I2C_ADDR    0x51 // [REF] 示例 boards/epaper_154/docs/refs/demo/user_config.h I2C_RTC_DEV_Address

// --- 温湿度 (SHTC3) ---
#define BSP_SHTC3_I2C_ADDR  0x70 // [REF] 示例 boards/epaper_154/docs/refs/demo/user_config.h I2C_SHTC3_DEV_Address

// ============================================================================
// Micro SD (TF) 卡座 —— SD_MMC **1-bit (SDIO)，不是 SPI**
//
// 官方 Demo 04_SD_Card/sdcard_bsp.cpp 用的是 sdmmc_slot_config_t 且 width = 1，
// 三根线是 CLK / CMD / D0。**因此本板根本没有 SD_CS** —— v0.2.1 记在「已知缺口」
// 里的「SD_CS 归属未明」是个伪问题，原理图 SD_CS 处的 R41 NC 与此完全吻合。
//
// ⚠️ 原理图把 IO40/IO41 标成 SD_MISO / SD_MOSI，是 SPI 味的命名，
//    但物理形态是 SDIO（SOURCE_HARVESTING §4.3：名字对不上物理归属）。
//    lcd_085 在同一件事上栽过 —— 那次是文档写成 SPI，照着写怎么都初始化不了。
// ============================================================================
#define BSP_PIN_SD_CLK      39   // [REF] 原理图 IO39 (SD_CLK) + 示例 boards/epaper_154/docs/refs/demo/sdcard_bsp.cpp SDMMC_CLK_PIN
#define BSP_PIN_SD_CMD      41   // [REF] 原理图 IO41 (SD_MOSI) + 示例 boards/epaper_154/docs/refs/demo/sdcard_bsp.cpp SDMMC_CMD_PIN
#define BSP_PIN_SD_D0       40   // [REF] 原理图 IO40 (SD_MISO) + 示例 boards/epaper_154/docs/refs/demo/sdcard_bsp.cpp SDMMC_D0_PIN
#define BSP_SD_BUS_WIDTH    1    // [REF] 示例 boards/epaper_154/docs/refs/demo/sdcard_bsp.cpp slot_config.width = 1

// ============================================================================
// 音频 (I2S Codec ES8311 + PA)
// 原理图 U8 IO 映射表标注 I2S 信号及 PA 控制脚
// ============================================================================
#define BSP_PIN_I2S_MCLK    14   // [REF] 原理图 IO14 (I2S_MCLK)
#define BSP_PIN_I2S_SCLK    15   // [REF] 原理图 IO15 (I2S_SCLK)
#define BSP_PIN_I2S_ASDOUT  16   // [REF] 原理图 IO16 (I2S_ASDOUT)
#define BSP_PIN_I2S_LRCK    38   // [REF] 原理图 IO38 (I2S_LRCK)
#define BSP_PIN_I2S_DSDIN   45   // [REF] 原理图 IO45 (I2S_DSDIN)
#define BSP_PIN_PA_EN       42   // [REF] 原理图 IO42 (PA_EN)
#define BSP_PIN_PA_CTRL     46   // [REF] 原理图 IO46 (PA_CTRL)

// ============================================================================
// 电源与电池 ADC 检测
// 原理图 U8 IO 映射表及 Power&ADC 模块
// ============================================================================
#define BSP_PIN_BAT_ADC     4    // [REF] 原理图 IO4 (BAT_ADC)
#define BSP_PIN_BAT_CTRL    17   // [REF] 原理图 IO17 (BAT_Control)
#define BSP_PIN_BAT_KEY     18   // [REF] 原理图 IO18 (BAT_KEY)

// ============================================================================
// 板载按键
// 原理图 U8 IO 映射表
// ============================================================================
#define BSP_PIN_BTN_BOOT    0    // [REF] 原理图 IO0 (BOOT0)

