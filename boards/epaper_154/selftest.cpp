#include <Arduino.h>
#include <Wire.h>
#include <driver/i2c.h>
#include <driver/gpio.h>

#include "bsp.h"
#include "bsp_pins.h"
#include "audio.h"
#include "audio_id.h"
#include "sdcard.h"
#include "touch.h"

void test_flash(void);
void test_psram(void);
void test_wifi(void);
bool bsp_epd_last_refresh_attempted(void);
bool bsp_epd_last_refresh_ok(void);
uint32_t bsp_epd_last_busy_ms(void);
const char* bsp_epd_last_detail(void);
void epd_visible_smoke_test(void);

static bool s_i2c_driver_installed = false;

#define BSP_ES8311_CHIP_ID1  0x83
#define BSP_ES8311_CHIP_ID2  0x11

uint32_t bsp_battery_adc_mv(void);

static void manual_page(const char* title, const char* line1, const char* line2)
{
    bsp_ui_clear();
    bsp_ui_printf("%s\n", bsp_version_string());
    bsp_ui_printf("%s\n", title);
    if (line1) bsp_ui_printf("%s\n", line1);
    if (line2) bsp_ui_printf("%s\n", line2);
    bsp_ui_flush();
}

static void test_epd_control(void)
{
    const int pwr = digitalRead(BSP_PIN_EPD_PWR_EN);
    const int rst = digitalRead(BSP_PIN_EPD_RST);
    const int cs = digitalRead(BSP_PIN_EPD_CS);
    const int busy = digitalRead(BSP_PIN_EPD_BUSY);

    if (pwr || !rst || !cs) {
        bsp_selftest_report(BSP_SELFTEST_FAIL, "EPD", "pwr%d rst%d cs%d busy%d", pwr, rst, cs, busy);
        return;
    }
    if (!bsp_epd_last_refresh_attempted()) {
        bsp_selftest_report(BSP_SELFTEST_WARN, "EPD", "pwr%d rst%d cs%d busy%d no-refresh", pwr, rst, cs, busy);
        return;
    }

    bsp_selftest_report(bsp_epd_last_refresh_ok() ? BSP_SELFTEST_WARN : BSP_SELFTEST_FAIL,
                        "EPD", "pwr%d rst%d cs%d busy%d %s %lums vis?",
                        pwr, rst, cs, busy, bsp_epd_last_detail(),
                        (unsigned long)bsp_epd_last_busy_ms());
}

static void reset_touch_official_timing(void)
{
    digitalWrite(BSP_PIN_TOUCH_RST, HIGH);
    delay(100);
    digitalWrite(BSP_PIN_TOUCH_RST, LOW);
    delay(100);
    digitalWrite(BSP_PIN_TOUCH_RST, HIGH);
    delay(100);
}

static void delete_i2c_driver_if_needed(void)
{
    if (!s_i2c_driver_installed) return;
    i2c_driver_delete(I2C_NUM_0);
    s_i2c_driver_installed = false;
}

static bool print_i2c_line_state(const char* phase)
{
    pinMode(BSP_PIN_I2C_SDA, INPUT);
    pinMode(BSP_PIN_I2C_SCL, INPUT);
    delay(5);
    const int raw_sda = digitalRead(BSP_PIN_I2C_SDA);
    const int raw_scl = digitalRead(BSP_PIN_I2C_SCL);

    pinMode(BSP_PIN_I2C_SDA, INPUT_PULLUP);
    pinMode(BSP_PIN_I2C_SCL, INPUT_PULLUP);
    delay(5);
    const int pu_sda = digitalRead(BSP_PIN_I2C_SDA);
    const int pu_scl = digitalRead(BSP_PIN_I2C_SCL);

    Serial.printf("[probe] I2C lines %-16s raw SDA%d SCL%d | pu SDA%d SCL%d\n",
                  phase, raw_sda, raw_scl, pu_sda, pu_scl);
    return pu_sda == HIGH && pu_scl == HIGH;
}

static esp_err_t install_i2c(uint32_t freq)
{
    const i2c_port_t port = I2C_NUM_0;
    Wire.end();
    delete_i2c_driver_if_needed();

    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = (gpio_num_t)BSP_PIN_I2C_SDA;
    conf.scl_io_num = (gpio_num_t)BSP_PIN_I2C_SCL;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = freq;

    esp_err_t err = i2c_param_config(port, &conf);
    if (err != ESP_OK) return err;
    err = i2c_driver_install(port, conf.mode, 0, 0, 0);
    if (err == ESP_OK) s_i2c_driver_installed = true;
    return err;
}

static bool i2c_probe_addr(uint8_t addr, TickType_t timeout)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    const bool ok = i2c_master_cmd_begin(I2C_NUM_0, cmd, timeout) == ESP_OK;
    i2c_cmd_link_delete(cmd);
    return ok;
}

static esp_err_t i2c_write_bytes(uint8_t addr, const uint8_t* data, size_t len, TickType_t timeout)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    if (len) i2c_master_write(cmd, (uint8_t*)data, len, true);
    i2c_master_stop(cmd);
    const esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, timeout);
    i2c_cmd_link_delete(cmd);
    return err;
}

static esp_err_t i2c_read_bytes(uint8_t addr, uint8_t* data, size_t len, TickType_t timeout)
{
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
    if (len > 1) i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    const esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, timeout);
    i2c_cmd_link_delete(cmd);
    return err;
}

static esp_err_t i2c_write_read(uint8_t addr, const uint8_t* wr, size_t wr_len,
                                uint8_t* rd, size_t rd_len, TickType_t timeout)
{
    if (!rd || rd_len == 0) return ESP_ERR_INVALID_ARG;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    if (wr_len) i2c_master_write(cmd, (uint8_t*)wr, wr_len, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
    if (rd_len > 1) i2c_master_read(cmd, rd, rd_len - 1, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, rd + rd_len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    const esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, timeout);
    i2c_cmd_link_delete(cmd);
    return err;
}

static void scan_i2c_all(uint32_t freq, const char* label)
{
    char found[128] = {0};
    size_t used = 0;

    if (!print_i2c_line_state(label)) {
        Serial.printf("[probe] I2C scan %-12s skip bus-low\n", label);
        return;
    }

    const esp_err_t err = install_i2c(freq);
    if (err != ESP_OK) {
        Serial.printf("[probe] I2C scan %-12s drvfail %s\n", label, esp_err_to_name(err));
        return;
    }

    for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
        if (!i2c_probe_addr(addr, pdMS_TO_TICKS(10))) continue;
        if (used < sizeof(found) - 5) {
            used += snprintf(found + used, sizeof(found) - used, "%s%02X", used ? " " : "", addr);
        }
    }
    delete_i2c_driver_if_needed();

    if (used == 0) snprintf(found, sizeof(found), "-");
    Serial.printf("[probe] I2C scan %-12s %s\n", label, found);
}

static void i2c_bus_unwedge(void)
{
    delete_i2c_driver_if_needed();

    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << BSP_PIN_I2C_SDA) | (1ULL << BSP_PIN_I2C_SCL);
    cfg.mode = GPIO_MODE_INPUT_OUTPUT_OD;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);

    gpio_set_level((gpio_num_t)BSP_PIN_I2C_SDA, 1);
    gpio_set_level((gpio_num_t)BSP_PIN_I2C_SCL, 1);
    delay(5);

    for (int i = 0; i < 16; i++) {
        gpio_set_level((gpio_num_t)BSP_PIN_I2C_SCL, 0);
        delayMicroseconds(8);
        gpio_set_level((gpio_num_t)BSP_PIN_I2C_SCL, 1);
        delayMicroseconds(8);
    }

    gpio_set_level((gpio_num_t)BSP_PIN_I2C_SDA, 0);
    delayMicroseconds(8);
    gpio_set_level((gpio_num_t)BSP_PIN_I2C_SCL, 1);
    delayMicroseconds(8);
    gpio_set_level((gpio_num_t)BSP_PIN_I2C_SDA, 1);
    delay(5);

    Serial.printf("[probe] I2C unwedge done SDA%d SCL%d\n",
                  gpio_get_level((gpio_num_t)BSP_PIN_I2C_SDA),
                  gpio_get_level((gpio_num_t)BSP_PIN_I2C_SCL));
}

static uint8_t probe_i2c_expected(uint32_t freq, bool vbat_on, char* list, size_t list_len)
{
    digitalWrite(BSP_PIN_BAT_CTRL, vbat_on ? HIGH : LOW);
    delay(50);
    reset_touch_official_timing();

    const esp_err_t err = install_i2c(freq);
    if (err != ESP_OK) {
        snprintf(list, list_len, "drvfail:%s", esp_err_to_name(err));
        return 0;
    }

    const uint8_t expected[] = {
        BSP_TOUCH_I2C_ADDR,
        BSP_RTC_I2C_ADDR,
        BSP_SHTC3_I2C_ADDR,
    };

    uint8_t hits[0x78] = {0};
    for (int pass = 0; pass < 3; pass++) {
        for (size_t i = 0; i < sizeof(expected); i++) {
            const uint8_t addr = expected[i];
            if (i2c_probe_addr(addr, pdMS_TO_TICKS(20))) hits[addr]++;
        }
        delay(20);
    }
    delete_i2c_driver_if_needed();

    size_t used = 0;
    for (size_t i = 0; i < sizeof(expected); i++) {
        const uint8_t addr = expected[i];
        if (hits[addr] != 3) continue;
        if (used < list_len - 5) {
            used += snprintf(list + used, list_len - used, "%s%02X", used ? " " : "", addr);
        }
    }
    if (used == 0) snprintf(list, list_len, "-");

    return (uint8_t)((hits[BSP_TOUCH_I2C_ADDR] == 3 ? 1 : 0) |
                     (hits[BSP_RTC_I2C_ADDR] == 3 ? 2 : 0) |
                     (hits[BSP_SHTC3_I2C_ADDR] == 3 ? 4 : 0));
}

static void append_miss(char* miss, size_t miss_len, uint8_t mask)
{
    size_t mused = 0;
    if (!(mask & 1) && mused < miss_len - 5) mused += snprintf(miss + mused, miss_len - mused, "%s%02X", mused ? " " : "", BSP_TOUCH_I2C_ADDR);
    if (!(mask & 2) && mused < miss_len - 5) mused += snprintf(miss + mused, miss_len - mused, "%s%02X", mused ? " " : "", BSP_RTC_I2C_ADDR);
    if (!(mask & 4) && mused < miss_len - 5) mused += snprintf(miss + mused, miss_len - mused, "%s%02X", mused ? " " : "", BSP_SHTC3_I2C_ADDR);
    if (mused == 0) snprintf(miss, miss_len, "-");
}

static void test_i2c(void)
{
    char off100[24] = {0};
    char on100[24] = {0};
    char on400[24] = {0};

    print_i2c_line_state("boot");
    digitalWrite(BSP_PIN_EPD_PWR_EN, LOW);
    delay(50);
    print_i2c_line_state("epd_pwr_on");
    digitalWrite(BSP_PIN_BAT_CTRL, LOW);
    delay(50);
    print_i2c_line_state("vbat_off");
    digitalWrite(BSP_PIN_BAT_CTRL, HIGH);
    delay(50);
    print_i2c_line_state("vbat_on");
    reset_touch_official_timing();
    print_i2c_line_state("touch_reset");

    const uint8_t m_off100 = probe_i2c_expected(100000, false, off100, sizeof(off100));
    const uint8_t m_on100 = probe_i2c_expected(100000, true, on100, sizeof(on100));
    const uint8_t m_on400 = probe_i2c_expected(BSP_I2C_FREQ, true, on400, sizeof(on400));

    Serial.printf("[probe] I2C vbat0 100k: %s\n", off100);
    Serial.printf("[probe] I2C vbat1 100k: %s\n", on100);
    Serial.printf("[probe] I2C vbat1 400k: %s\n", on400);

    digitalWrite(BSP_PIN_BAT_CTRL, HIGH);
    reset_touch_official_timing();
    scan_i2c_all(100000, "pre 100k");
    scan_i2c_all(BSP_I2C_FREQ, "pre 400k");
    i2c_bus_unwedge();
    print_i2c_line_state("post_unwedge");
    scan_i2c_all(100000, "post 100k");
    scan_i2c_all(BSP_I2C_FREQ, "post 400k");

    const uint8_t best = (m_on400 == 7 || m_on400 >= m_on100) ? m_on400 : m_on100;
    char miss[24] = {0};
    append_miss(miss, sizeof(miss), best);

    bsp_selftest_report(best == 0 ? BSP_SELFTEST_FAIL : (best == 7 ? BSP_SELFTEST_PASS : BSP_SELFTEST_WARN),
                        "I2C", "%s| miss:%s", best == m_on400 ? on400 : on100, miss);
}

static bool touch_read_reg(uint8_t reg, uint8_t* out, size_t len)
{
    return i2c_write_read(BSP_TOUCH_I2C_ADDR, &reg, 1, out, len, pdMS_TO_TICKS(50)) == ESP_OK;
}

static void test_touch_registers(void)
{
    reset_touch_official_timing();
    if (install_i2c(BSP_I2C_FREQ) != ESP_OK) {
        bsp_selftest_report(BSP_SELFTEST_FAIL, "Touch", "drvfail");
        return;
    }

    uint8_t head[7] = {0};
    const bool head_ok = touch_read_reg(0x00, head, sizeof(head));
    uint8_t ids[3] = {0};
    const bool id_a3 = touch_read_reg(0xA3, &ids[0], 1);
    const bool id_a6 = touch_read_reg(0xA6, &ids[1], 1);
    const bool id_a8 = touch_read_reg(0xA8, &ids[2], 1);
    delete_i2c_driver_if_needed();

    if (!head_ok) {
        bsp_selftest_report(BSP_SELFTEST_FAIL, "Touch", "reg-read-fail");
        return;
    }

    const uint8_t gesture = head[1];
    const uint8_t points = head[2] & 0x0F;
    const uint16_t x = (uint16_t)(((head[3] & 0x0F) << 8) | head[4]);
    const uint16_t y = (uint16_t)(((head[5] & 0x0F) << 8) | head[6]);
    const bool count_ok = points <= 2;
    const bool coord_ok = points == 0 || (x < BSP_EPD_W && y < BSP_EPD_H);
    const int int_level = digitalRead(BSP_PIN_TOUCH_INT);

    Serial.printf("[probe] Touch raw00 %02X %02X %02X %02X %02X %02X %02X idA3%s%02X A6%s%02X A8%s%02X int%d\n",
                  head[0], head[1], head[2], head[3], head[4], head[5], head[6],
                  id_a3 ? "=" : "!", ids[0], id_a6 ? "=" : "!", ids[1],
                  id_a8 ? "=" : "!", ids[2], int_level);

    bsp_selftest_report((count_ok && coord_ok) ? BSP_SELFTEST_PASS : BSP_SELFTEST_WARN,
                        "Touch", "n%u g%02X x%u y%u id%02X/%02X/%02X int%d",
                        points, gesture, x, y, ids[0], ids[1], ids[2], int_level);
}

static bool touch_read_point(uint8_t* points_out, uint16_t* x_out, uint16_t* y_out, uint8_t* gesture_out)
{
    uint8_t head[7] = {0};
    if (!touch_read_reg(0x00, head, sizeof(head))) return false;
    const uint8_t points = head[2] & 0x0F;
    if (points_out) *points_out = points;
    if (gesture_out) *gesture_out = head[1];
    if (x_out) *x_out = (uint16_t)(((head[3] & 0x0F) << 8) | head[4]);
    if (y_out) *y_out = (uint16_t)(((head[5] & 0x0F) << 8) | head[6]);
    return true;
}

static void test_touch_press_window(void)
{
    reset_touch_official_timing();
    if (install_i2c(BSP_I2C_FREQ) != ESP_OK) {
        bsp_selftest_report(BSP_SELFTEST_FAIL, "TouchP", "drvfail");
        return;
    }

    Serial.println("[probe] TouchPress: touch/hold the panel for 8s");
    const uint32_t start = millis();
    bool read_fail = false;
    bool seen = false;
    bool coord_ok = false;
    uint8_t best_n = 0, best_g = 0;
    uint16_t best_x = 0, best_y = 0;

    while (millis() - start < 8000) {
        uint8_t n = 0, g = 0;
        uint16_t x = 0, y = 0;
        if (!touch_read_point(&n, &x, &y, &g)) {
            read_fail = true;
            break;
        }
        if (n > 0) {
            seen = true;
            best_n = n;
            best_g = g;
            best_x = x;
            best_y = y;
            coord_ok = n <= 2 && x < BSP_EPD_W && y < BSP_EPD_H;
            if (coord_ok) break;
        }
        delay(80);
    }
    delete_i2c_driver_if_needed();

    Serial.printf("[probe] TouchPress seen%d n%u g%02X x%u y%u ok%d fail%d\n",
                  seen ? 1 : 0, best_n, best_g, best_x, best_y,
                  coord_ok ? 1 : 0, read_fail ? 1 : 0);

    if (read_fail) {
        bsp_selftest_report(BSP_SELFTEST_FAIL, "TouchP", "read-fail");
        return;
    }
    if (!seen) {
        bsp_selftest_report(BSP_SELFTEST_WARN, "TouchP", "no-press-seen");
        return;
    }
    bsp_selftest_report(coord_ok ? BSP_SELFTEST_PASS : BSP_SELFTEST_WARN,
                        "TouchP", "n%u g%02X x%u y%u",
                        best_n, best_g, best_x, best_y);
}

static void test_touch_quadrants(void)
{
    manual_page("Touch4", "Tap four corners", "20s window");
    reset_touch_official_timing();
    if (install_i2c(BSP_I2C_FREQ) != ESP_OK) {
        bsp_selftest_report(BSP_SELFTEST_FAIL, "Touch4", "drvfail");
        return;
    }

    Serial.println("[probe] Touch4: tap each corner within 20s");
    const uint32_t start = millis();
    uint8_t mask = 0;
    uint16_t last_x = 0, last_y = 0;
    uint8_t last_n = 0;
    while (millis() - start < 20000 && mask != 0x0F) {
        uint8_t n = 0, g = 0;
        uint16_t x = 0, y = 0;
        if (touch_read_point(&n, &x, &y, &g) && n > 0 && x < BSP_EPD_W && y < BSP_EPD_H) {
            last_n = n;
            last_x = x;
            last_y = y;
            uint8_t bit = 0;
            if (x < 80 && y < 80) bit = 0x01;                         /* top-left */
            else if (x >= 120 && y < 80) bit = 0x02;                   /* top-right */
            else if (x < 80 && y >= 120) bit = 0x04;                   /* bottom-left */
            else if (x >= 120 && y >= 120) bit = 0x08;                 /* bottom-right */
            if (bit && !(mask & bit)) {
                mask |= bit;
                Serial.printf("[probe] Touch4 hit mask%X n%u x%u y%u g%02X\n", mask, n, x, y, g);
            }
        }
        delay(80);
    }
    delete_i2c_driver_if_needed();

    bsp_selftest_report(mask == 0x0F ? BSP_SELFTEST_PASS : BSP_SELFTEST_WARN,
                        "Touch4", "mask%X last n%u x%u y%u",
                        mask, last_n, last_x, last_y);
}

static uint8_t bcd_to_u8(uint8_t v)
{
    return (uint8_t)(((v >> 4) * 10) + (v & 0x0F));
}

static bool bcd_in_range(uint8_t v, uint8_t max)
{
    return (v & 0x0F) <= 9 && ((v >> 4) & 0x0F) <= 9 && bcd_to_u8(v) <= max;
}

static bool rtc_read_time(uint8_t* out, size_t len)
{
    if (install_i2c(BSP_I2C_FREQ) != ESP_OK) return false;
    const uint8_t reg = 0x04; /* PCF85063 seconds register; reads sec,min,hour,day,wday,month,year. */
    const esp_err_t err = i2c_write_read(BSP_RTC_I2C_ADDR, &reg, 1, out, len, pdMS_TO_TICKS(50));
    delete_i2c_driver_if_needed();
    return err == ESP_OK;
}

static void test_rtc_registers(void)
{
    uint8_t a[7] = {0};
    uint8_t b[7] = {0};
    if (!rtc_read_time(a, sizeof(a))) {
        bsp_selftest_report(BSP_SELFTEST_FAIL, "RTC", "read-fail");
        return;
    }
    delay(1100);
    if (!rtc_read_time(b, sizeof(b))) {
        bsp_selftest_report(BSP_SELFTEST_FAIL, "RTC", "read2-fail");
        return;
    }

    const uint8_t sec_a = a[0] & 0x7F;
    const uint8_t sec_b = b[0] & 0x7F;
    const bool os = (a[0] & 0x80) || (b[0] & 0x80);
    const bool valid = bcd_in_range(sec_a, 59) && bcd_in_range(sec_b, 59) &&
                       bcd_in_range(a[1] & 0x7F, 59) && bcd_in_range(b[1] & 0x7F, 59) &&
                       bcd_in_range(a[2] & 0x3F, 23) && bcd_in_range(b[2] & 0x3F, 23) &&
                       bcd_in_range(a[3] & 0x3F, 31) && bcd_in_range(b[3] & 0x3F, 31) &&
                       bcd_in_range(a[5] & 0x1F, 12) && bcd_in_range(b[5] & 0x1F, 12) &&
                       bcd_in_range(a[6], 99) && bcd_in_range(b[6], 99);
    const uint8_t sa = bcd_to_u8(sec_a);
    const uint8_t sb = bcd_to_u8(sec_b);
    const uint8_t delta = (uint8_t)((sb + 60 - sa) % 60);

    Serial.printf("[probe] RTC raw %02X %02X %02X %02X %02X %02X %02X -> %02X %02X %02X %02X %02X %02X %02X delta%u os%d\n",
                  a[0], a[1], a[2], a[3], a[4], a[5], a[6],
                  b[0], b[1], b[2], b[3], b[4], b[5], b[6], delta, os ? 1 : 0);

    if (!valid) {
        bsp_selftest_report(BSP_SELFTEST_WARN, "RTC", "bcd-invalid raw%02X%02X%02X", b[2], b[1], b[0]);
        return;
    }
    bsp_selftest_report((delta >= 1 && delta <= 3 && !os) ? BSP_SELFTEST_PASS : BSP_SELFTEST_WARN,
                        "RTC", "%02u:%02u:%02u d%02u m%02u y%02u +%us os%d",
                        bcd_to_u8(b[2] & 0x3F), bcd_to_u8(b[1] & 0x7F), sb,
                        bcd_to_u8(b[3] & 0x3F), bcd_to_u8(b[5] & 0x1F), bcd_to_u8(b[6]),
                        delta, os ? 1 : 0);
}

static uint8_t shtc3_crc8(const uint8_t* data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static esp_err_t shtc3_cmd(uint16_t cmd)
{
    const uint8_t bytes[2] = { (uint8_t)(cmd >> 8), (uint8_t)cmd };
    return i2c_write_bytes(BSP_SHTC3_I2C_ADDR, bytes, sizeof(bytes), pdMS_TO_TICKS(50));
}

static void test_shtc3_sensor(void)
{
    if (install_i2c(BSP_I2C_FREQ) != ESP_OK) {
        bsp_selftest_report(BSP_SELFTEST_FAIL, "SHTC3", "drvfail");
        return;
    }

    uint8_t id_raw[3] = {0};
    esp_err_t err = shtc3_cmd(0x3517); /* wake */
    delay(2);
    if (err == ESP_OK) err = shtc3_cmd(0xEFC8); /* read ID */
    if (err == ESP_OK) err = i2c_read_bytes(BSP_SHTC3_I2C_ADDR, id_raw, sizeof(id_raw), pdMS_TO_TICKS(50));

    uint8_t meas[6] = {0};
    if (err == ESP_OK) err = shtc3_cmd(0x7866); /* normal power, temp first, clock stretching disabled */
    delay(15);
    if (err == ESP_OK) err = i2c_read_bytes(BSP_SHTC3_I2C_ADDR, meas, sizeof(meas), pdMS_TO_TICKS(100));
    (void)shtc3_cmd(0xB098); /* sleep; best-effort */
    delete_i2c_driver_if_needed();

    if (err != ESP_OK) {
        bsp_selftest_report(BSP_SELFTEST_FAIL, "SHTC3", "cmd:%s", esp_err_to_name(err));
        return;
    }

    const bool id_crc_ok = shtc3_crc8(id_raw, 2) == id_raw[2];
    const bool t_crc_ok = shtc3_crc8(meas, 2) == meas[2];
    const bool h_crc_ok = shtc3_crc8(meas + 3, 2) == meas[5];
    const uint16_t id = ((uint16_t)id_raw[0] << 8) | id_raw[1];
    const uint16_t raw_t = ((uint16_t)meas[0] << 8) | meas[1];
    const uint16_t raw_h = ((uint16_t)meas[3] << 8) | meas[4];
    const float temp_c = -45.0f + 175.0f * ((float)raw_t / 65536.0f);
    const float rh = 100.0f * ((float)raw_h / 65536.0f);
    const bool sane = temp_c > -20.0f && temp_c < 80.0f && rh >= 0.0f && rh <= 100.0f;

    Serial.printf("[probe] SHTC3 id%04X crc%d rawT%04X rawH%04X T%.1f RH%.1f crc%d%d\n",
                  id, id_crc_ok ? 1 : 0, raw_t, raw_h, temp_c, rh,
                  t_crc_ok ? 1 : 0, h_crc_ok ? 1 : 0);

    bsp_selftest_report((id_crc_ok && t_crc_ok && h_crc_ok && sane) ? BSP_SELFTEST_PASS : BSP_SELFTEST_WARN,
                        "SHTC3", "id%04X %.1fC %.1f%% crc%d%d%d",
                        id, temp_c, rh, id_crc_ok ? 1 : 0, t_crc_ok ? 1 : 0, h_crc_ok ? 1 : 0);
}

static void test_es8311_id(void)
{
    uint8_t id1 = 0, id2 = 0, ver = 0;
    if (!bsp_audio_chip_id_read(&id1, &id2, &ver)) {
        bsp_selftest_report(BSP_SELFTEST_FAIL, "ES8311", "id-read");
        return;
    }
    Serial.printf("[probe] ES8311 id %02X %02X ver%02X\n", id1, id2, ver);
    bsp_selftest_report((id1 == BSP_ES8311_CHIP_ID1 && id2 == BSP_ES8311_CHIP_ID2) ? BSP_SELFTEST_PASS : BSP_SELFTEST_WARN,
                        "ES8311", "id%02X%02X ver%02X", id1, id2, ver);
}

static uint32_t audio_avg_abs(const int16_t* samples, size_t n)
{
    if (!samples || n == 0) return 0;
    uint64_t sum = 0;
    for (size_t i = 0; i < n; i++) {
        const int32_t v = samples[i] < 0 ? -(int32_t)samples[i] : samples[i];
        sum += (uint32_t)v;
    }
    return (uint32_t)(sum / n);
}

static uint16_t audio_peak(const int16_t* samples, size_t n)
{
    int32_t peak = 0;
    for (size_t i = 0; i < n; i++) {
        const int32_t v = samples[i] < 0 ? -(int32_t)samples[i] : samples[i];
        if (v > peak) peak = v;
    }
    return (uint16_t)(peak > 32767 ? 32767 : peak);
}

static void test_audio_record_playback(void)
{
    manual_page("Audio", "1k tone, then speak", "record 3s + replay");
    Serial.println("[probe] Audio: listen for 1kHz tone");
    if (!bsp_audio_init(16000, 80)) {
        bsp_selftest_report(BSP_SELFTEST_FAIL, "Audio", "init-fail");
        return;
    }

    uint8_t id1 = 0, id2 = 0;
    bsp_audio_chip_id(&id1, &id2);
    bsp_audio_tone(1000, 700, 45);

    if (!bsp_mic_init(30)) {
        bsp_selftest_report(BSP_SELFTEST_FAIL, "Mic", "init-fail");
        return;
    }

    static const uint32_t kRate = 16000;
    static const uint32_t kSeconds = 3;
    const size_t sample_count = kRate * kSeconds;
    int16_t* rec = (int16_t*)ps_malloc(sample_count * sizeof(int16_t));
    if (!rec) {
        bsp_selftest_report(BSP_SELFTEST_FAIL, "Mic", "alloc");
        return;
    }

    Serial.println("[probe] AudioRec: speak toward the board for 3s");
    manual_page("Audio REC", "Speak now", "3s then replay");
    size_t got = 0;
    const uint32_t start = millis();
    while (got < sample_count && millis() - start < 4200) {
        got += bsp_mic_read(rec + got, sample_count - got, 250);
    }

    const uint16_t peak = audio_peak(rec, got);
    const uint32_t avg = audio_avg_abs(rec, got);
    Serial.printf("[probe] AudioRec got%u peak%u avg%lu\n",
                  (unsigned)got, peak, (unsigned long)avg);

    Serial.println("[probe] AudioReplay: listen for recorded voice");
    manual_page("Audio PLAY", "Replaying capture", "listen now");
    bsp_audio_amp(true);
    const size_t played = bsp_audio_write_mono(rec, got);
    static int16_t silence[256] = {0};
    bsp_audio_write_mono(silence, 256);
    bsp_audio_amp(false);
    free(rec);

    Serial.printf("[probe] AudioReplay played%u id%02X%02X\n", (unsigned)played, id1, id2);
    if (id1 != BSP_ES8311_CHIP_ID1 || id2 != BSP_ES8311_CHIP_ID2) {
        bsp_selftest_report(BSP_SELFTEST_FAIL, "Audio", "id%02X%02X", id1, id2);
        return;
    }
    if (played < kRate || peak == 0) {
        bsp_selftest_report(BSP_SELFTEST_WARN, "Mic", "got%u peak%u avg%lu",
                            (unsigned)got, peak, (unsigned long)avg);
        return;
    }
    bsp_selftest_report(BSP_SELFTEST_PASS, "Audio", "id%02X%02X tone+replay heard", id1, id2);
    bsp_selftest_report(avg > 20 ? BSP_SELFTEST_PASS : BSP_SELFTEST_WARN,
                        "Mic", "got%u peak%u avg%lu replay",
                        (unsigned)got, peak, (unsigned long)avg);
}

static uint32_t adc_avg_raw(int pin, int samples)
{
    uint32_t sum = 0;
    for (int i = 0; i < samples; i++) {
        sum += analogRead(pin);
        delay(5);
    }
    return sum / (uint32_t)samples;
}

static uint32_t adc_avg_mv(int pin, int samples)
{
    uint32_t sum = 0;
    for (int i = 0; i < samples; i++) {
        sum += analogReadMilliVolts(pin);
        delay(5);
    }
    return sum / (uint32_t)samples;
}

static void test_battery_adc(void)
{
    pinMode(BSP_PIN_BAT_CTRL, OUTPUT);
    digitalWrite(BSP_PIN_BAT_CTRL, LOW);
    delay(120);
    const uint32_t off_raw = adc_avg_raw(BSP_PIN_BAT_ADC, 12);
    const uint32_t off_mv = bsp_battery_adc_mv();

    digitalWrite(BSP_PIN_BAT_CTRL, HIGH);
    delay(120);
    const uint32_t on_raw = adc_avg_raw(BSP_PIN_BAT_ADC, 12);
    const uint32_t on_mv = bsp_battery_adc_mv();

    Serial.printf("[probe] BAT adc off raw%lu mv%lu on raw%lu mv%lu\n",
                  (unsigned long)off_raw, (unsigned long)off_mv,
                  (unsigned long)on_raw, (unsigned long)on_mv);
    bsp_selftest_report(on_raw > 20 ? BSP_SELFTEST_PASS : BSP_SELFTEST_WARN,
                        "BatADC", "off%lu/%lumV on%lu/%lumV",
                        (unsigned long)off_raw, (unsigned long)off_mv,
                        (unsigned long)on_raw, (unsigned long)on_mv);
}

static void test_sd_mmc_rw(void)
{
    if (!bsp_sd_init()) {
        bsp_selftest_report(BSP_SELFTEST_WARN, "SD", "mount-fail");
        return;
    }
    const uint64_t mb = bsp_sd_size_mb();
    const char* type = bsp_sd_type();
    const bool ok = bsp_sd_rw_check();
    Serial.printf("[probe] SD rw %s type%s size%lluMB\n",
                  ok ? "ok" : "bad", type, (unsigned long long)mb);
    bsp_sd_deinit();
    bsp_selftest_report(ok ? BSP_SELFTEST_PASS : BSP_SELFTEST_FAIL,
                        "SD", "%s %lluMB rw%s", type, (unsigned long long)mb, ok ? "OK" : "BAD");
}

static void test_buttons_dynamic(void)
{
    manual_page("Buttons", "Press BOOT + PWR", "12s window");
    Serial.println("[probe] Buttons: press BOOT and PWR/BAT_KEY within 12s");
    const uint32_t start = millis();
    bool boot_seen = false;
    bool pwr_seen = false;
    while (millis() - start < 12000 && (!boot_seen || !pwr_seen)) {
        if (digitalRead(BSP_PIN_BTN_BOOT) == LOW) boot_seen = true;
        if (digitalRead(BSP_PIN_BAT_KEY) == LOW) pwr_seen = true;
        delay(20);
    }
    Serial.printf("[probe] Buttons dynamic boot%d pwr%d idle BOOT%d PWR%d\n",
                  boot_seen ? 1 : 0, pwr_seen ? 1 : 0,
                  digitalRead(BSP_PIN_BTN_BOOT), digitalRead(BSP_PIN_BAT_KEY));
    bsp_selftest_report((boot_seen && pwr_seen) ? BSP_SELFTEST_PASS : BSP_SELFTEST_WARN,
                        "BtnDyn", "BOOT%d PWR%d", boot_seen ? 1 : 0, pwr_seen ? 1 : 0);
}

static void test_buttons_idle(void)
{
    const int boot = digitalRead(BSP_PIN_BTN_BOOT);
    const int pwr = digitalRead(BSP_PIN_BAT_KEY);
    bsp_selftest_report((boot && pwr) ? BSP_SELFTEST_PASS : BSP_SELFTEST_WARN,
                        "Btn", "idle BOOT%d PWR%d", boot, pwr);
}

static void test_epd_visible_page(void)
{
    Wire.end();
    epd_visible_smoke_test();
    test_epd_control();
}

typedef void (*epaper_test_fn_t)(void);

typedef struct {
    const char* name;
    epaper_test_fn_t fn;
    bool ran;
    int pass;
    int warn;
    int fail;
} epaper_test_page_t;

static epaper_test_page_t s_pages[] = {
    { "EPD",     test_epd_visible_page,       false, 0, 0, 0 },
    { "PSRAM",   test_psram,                  false, 0, 0, 0 },
    { "Flash",   test_flash,                  false, 0, 0, 0 },
    { "I2C",     test_i2c,                    false, 0, 0, 0 },
    { "Touch",   test_touch_registers,        false, 0, 0, 0 },
    { "TouchP",  test_touch_press_window,     false, 0, 0, 0 },
    { "Touch4",  test_touch_quadrants,        false, 0, 0, 0 },
    { "RTC",     test_rtc_registers,          false, 0, 0, 0 },
    { "SHTC3",   test_shtc3_sensor,           false, 0, 0, 0 },
    { "ES8311",  test_es8311_id,              false, 0, 0, 0 },
    { "Audio",   test_audio_record_playback,  false, 0, 0, 0 },
    { "BatADC",  test_battery_adc,            false, 0, 0, 0 },
    { "SD",      test_sd_mmc_rw,              false, 0, 0, 0 },
    { "BtnDyn",  test_buttons_dynamic,        false, 0, 0, 0 },
    { "WiFi",    test_wifi,                   false, 0, 0, 0 },
    { "Btn",     test_buttons_idle,           false, 0, 0, 0 },
};

static const int k_page_count = (int)(sizeof(s_pages) / sizeof(s_pages[0]));

static void page_totals(int* pass, int* warn, int* fail, int* ran)
{
    int p = 0, w = 0, f = 0, r = 0;
    for (int i = 0; i < k_page_count; i++) {
        if (!s_pages[i].ran) continue;
        p += s_pages[i].pass;
        w += s_pages[i].warn;
        f += s_pages[i].fail;
        r++;
    }
    if (pass) *pass = p;
    if (warn) *warn = w;
    if (fail) *fail = f;
    if (ran) *ran = r;
}

static const char* page_status(const epaper_test_page_t& p)
{
    if (!p.ran) return "TODO";
    if (p.fail > 0) return "FAIL";
    if (p.warn > 0) return "WARN";
    return "PASS";
}

static void draw_summary_page(void)
{
    int pass = 0, warn = 0, fail = 0, ran = 0;
    page_totals(&pass, &warn, &fail, &ran);

    bsp_ui_clear();
    bsp_ui_printf("%s\n", bsp_version_string());
    bsp_ui_printf("SUMMARY %d/%d\n", ran, k_page_count);
    bsp_ui_printf("%d PASS / %d WARN / %d FAIL\n", pass, warn, fail);
    for (int i = 0; i < k_page_count && i < 11; i++) {
        bsp_ui_printf("%02d %-7s %s\n", i + 1, s_pages[i].name, page_status(s_pages[i]));
    }
    if (k_page_count > 11) {
        bsp_ui_printf("... %d more in log\n", k_page_count - 11);
    }
    bsp_ui_printf("LEFT prev  RIGHT rerun\n");
    bsp_ui_flush();

    Serial.printf("[app] SUMMARY ran%d/%d pass%d warn%d fail%d\n",
                  ran, k_page_count, pass, warn, fail);
    for (int i = 0; i < k_page_count; i++) {
        Serial.printf("[app] %-7s %s P%d W%d F%d\n",
                      s_pages[i].name, page_status(s_pages[i]),
                      s_pages[i].pass, s_pages[i].warn, s_pages[i].fail);
    }
}

static void draw_test_header(int index)
{
    bsp_ui_clear();
    bsp_ui_printf("%s\n", bsp_version_string());
    bsp_ui_printf("%02d/%02d %s\n", index + 1, k_page_count, s_pages[index].name);
    bsp_ui_printf("Running...\n");
    bsp_ui_flush();
}

static void run_test_page(int index)
{
    epaper_test_page_t& page = s_pages[index];
    draw_test_header(index);

    Serial.printf("[app] page %02d/%02d %s begin\n", index + 1, k_page_count, page.name);
    bsp_ui_clear();
    bsp_ui_printf("%s\n", bsp_version_string());
    bsp_ui_printf("%02d/%02d %s\n", index + 1, k_page_count, page.name);

    bsp_selftest_begin();
    page.fn();
    page.pass = bsp_selftest_count(BSP_SELFTEST_PASS);
    page.warn = bsp_selftest_count(BSP_SELFTEST_WARN);
    page.fail = bsp_selftest_count(BSP_SELFTEST_FAIL);
    page.ran = true;

    int total_pass = 0, total_warn = 0, total_fail = 0, total_ran = 0;
    page_totals(&total_pass, &total_warn, &total_fail, &total_ran);
    bsp_ui_printf("=> %s P%d W%d F%d\n", page_status(page), page.pass, page.warn, page.fail);
    bsp_ui_printf("TOTAL %d/%d P%d W%d F%d\n", total_ran, k_page_count,
                  total_pass, total_warn, total_fail);
    bsp_ui_printf("LEFT prev  RIGHT next\n");
    bsp_ui_flush();

    Serial.printf("[app] page %02d/%02d %s %s P%d W%d F%d\n",
                  index + 1, k_page_count, page.name, page_status(page),
                  page.pass, page.warn, page.fail);
}

static int wait_nav_touch(void)
{
    (void)bsp_touch_init();
    Serial.println("[app] nav: touch LEFT for previous, RIGHT for next");

    uint32_t stable_down_at = 0;
    while (true) {
        bsp_touch_point_t p;
        if (bsp_touch_read(&p) && p.down) {
            if (stable_down_at == 0) stable_down_at = millis();
            if (millis() - stable_down_at >= 80) {
                const int dir = (p.x < (BSP_EPD_W / 2)) ? -1 : 1;
                Serial.printf("[app] nav %s n%u x%u y%u\n",
                              dir < 0 ? "prev" : "next", p.points, p.x, p.y);
                while (bsp_touch_read(&p) && p.down) delay(40);
                delay(180);
                return dir;
            }
        } else {
            stable_down_at = 0;
        }
        delay(40);
    }
}

void bsp_epaper_154_interactive_test_app(void)
{
    Serial.println("[app] epaper_154 interactive all-test app begin");
    Serial.println("[app] Every page reruns its test on entry. LEFT goes back, RIGHT advances.");

    int page = 0;
    while (true) {
        if (page < k_page_count) run_test_page(page);
        else draw_summary_page();

        const int dir = wait_nav_touch();
        page += dir;
        if (page < 0) page = k_page_count;
        if (page > k_page_count) page = 0;
    }
}

void bsp_selftest_board_specific(void)
{
    test_psram();
    test_flash();
    test_epd_control();
    test_i2c();
    test_touch_registers();
    test_touch_press_window();
    test_touch_quadrants();
    test_rtc_registers();
    test_shtc3_sensor();
    test_es8311_id();
    test_audio_record_playback();
    test_battery_adc();
    test_sd_mmc_rw();
    test_buttons_dynamic();
    test_wifi();
    test_buttons_idle();
}
