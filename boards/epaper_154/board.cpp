#include <Arduino.h>
#include "bsp.h"
#include "bsp_pins.h"

void bsp_board_init(void)
{
    /* 板载外设电源域：与原厂固件的 GPIO 配置对齐 —— 它在跑任何外设前把 IO6/IO17/IO42
     * 三根全部配成输出（baseline/factory_epaper_154_boot.log 的 OutputEn:1 三行）。
     * IO42 在原理图上叫 PA_EN，但官方 user_config.h 把它归在 DEV POWER 段叫
     * Audio_PWR_PIN，与 EPD_PWR/VBAT_PWR 并列（SOURCE_HARVESTING §4.3 同名不同归属）。
     *
     * ⚠️ 这里驱动 IO42 只是「对齐原厂配置」，**不是**已证实的修复。
     *    v0.6.2 的电源域探针（1500ms 停留、单变量隔离、两轮复核）显示：8 组电平组合
     *    下 I2C rail 全部为 UP，包括三根使能全低。故 IO6/IO17/IO42 均未门控 I2C 上拉域，
     *    v0.6.1 的 bus-low 到 v0.6.2 的四器件应答，成因尚未归因，禁止据此升 [OK]。 */
    pinMode(BSP_PIN_EPD_PWR_EN, OUTPUT);
    digitalWrite(BSP_PIN_EPD_PWR_EN, LOW);
    pinMode(BSP_PIN_PA_EN, OUTPUT);
    digitalWrite(BSP_PIN_PA_EN, LOW);

    pinMode(BSP_PIN_EPD_RST, OUTPUT);
    digitalWrite(BSP_PIN_EPD_RST, HIGH);
    pinMode(BSP_PIN_EPD_DC, OUTPUT);
    digitalWrite(BSP_PIN_EPD_DC, LOW);
    pinMode(BSP_PIN_EPD_CS, OUTPUT);
    digitalWrite(BSP_PIN_EPD_CS, HIGH);
    pinMode(BSP_PIN_EPD_BUSY, INPUT);

    pinMode(BSP_PIN_TOUCH_RST, OUTPUT);
    digitalWrite(BSP_PIN_TOUCH_RST, HIGH);
    pinMode(BSP_PIN_TOUCH_INT, INPUT_PULLUP);

    pinMode(BSP_PIN_RTC_INT, INPUT_PULLUP);
    pinMode(BSP_PIN_BAT_CTRL, OUTPUT);
    digitalWrite(BSP_PIN_BAT_CTRL, HIGH);
    pinMode(BSP_PIN_BTN_BOOT, INPUT_PULLUP);
    pinMode(BSP_PIN_BAT_KEY, INPUT_PULLUP);
}
void bsp_power_off(void) {}
bool bsp_button_pressed(bsp_btn_t b)
{
    switch (b) {
        case BSP_BTN_BOOT: return digitalRead(BSP_PIN_BTN_BOOT) == LOW;
        case BSP_BTN_PWR:  return digitalRead(BSP_PIN_BAT_KEY) == LOW;
        default:           return false;
    }
}
const char* bsp_button_name(bsp_btn_t b)
{
    switch (b) {
        case BSP_BTN_BOOT: return "BOOT";
        case BSP_BTN_PWR:  return "PWR";
        default:           return NULL;
    }
}

/* bsp_ui_* 由本板的 ui.cpp 实现（墨水屏走缓冲 + flush 模型），此处不得再放空桩 */

void bsp_led_selftest_sweep(void) {}
void bsp_led_rainbow_step(void) {}
void bsp_led_clear(void) {}
void bsp_button_monitor(void) {}

void bsp_mic_meter(void)
{
    /* ePaper is not a realtime display; audio selftest prints numeric peak/RMS instead. */
}

uint32_t bsp_battery_adc_mv(void)
{
    analogReadResolution(12);
    analogSetPinAttenuation(BSP_PIN_BAT_ADC, ADC_11db);
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) {
        sum += analogReadMilliVolts(BSP_PIN_BAT_ADC);
        delay(2);
    }
    return sum / 16;
}

uint32_t bsp_battery_mv(void)
{
    /* The divider ratio is not present in the schematic. Return the calibrated ADC pin mV,
     * not an invented VBAT estimate. */
    return bsp_battery_adc_mv();
}

int bsp_battery_level(void)
{
    const uint32_t mv = bsp_battery_mv();
    if (mv < 1000) return 0;
    if (mv > 2400) return 100;
    return (int)((mv - 1000) * 100 / 1400);
}
bool bsp_charge_raw(void) { return false; }

const char* bsp_version_string(void)
{
    return BSP_VERSION_STRING;
}
