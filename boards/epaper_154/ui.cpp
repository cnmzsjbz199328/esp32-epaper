#include <Arduino.h>
#include <driver/i2c.h>
#include <driver/spi_master.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <stdarg.h>
#include <string.h>
#include "bsp.h"
#include "bsp_pins.h"

/* 
 * 墨水屏 (ePaper) UI 抽象层实现
 * 墨水屏刷新以秒计，且带有严重闪烁与残影问题。
 * 因此在 UI 抽象层中：
 * 1. 所有 printf / status / bar 操作仅在内存/缓冲区累积。
 * 2. bsp_ui_flush() 是唯一触发真正物理刷屏 (Full/Partial Refresh) 的出口。
 * 3. 针对 BSP_UI_REALTIME=0，跳过任何高频实时绘制逻辑。
 */

static char s_frame_buf[16][32];
static int  s_row_count = 0;

static spi_device_handle_t s_epd_spi = nullptr;
static bool s_epd_spi_ready = false;
static bool s_epd_last_refresh_attempted = false;
static bool s_epd_last_refresh_ok = false;
static uint32_t s_epd_last_busy_ms = 0;
static char s_epd_last_detail[48] = "not-run";
/* 掉电判据 —— RTC 慢速内存里的魔数。
 * RTC_NOINIT_ATTR 的变量跨软复位/深睡保留，只有真正断电才会丢失。因此它能回答
 * 「刚才那次到底断没断电」，而复位原因寄存器会被主机打开串口的动作顶掉，不可靠。
 * 用途：判定 v0.6.1 的 I2C bus-low → v0.6.2 四器件应答，是不是原厂固件的残留状态。 */
#define BOOT_MAGIC 0xC01DB007u
RTC_NOINIT_ATTR static uint32_t s_boot_magic;
RTC_NOINIT_ATTR static uint32_t s_boot_count;

static void report_boot_power_state(void)
{
    const bool cold = (s_boot_magic != BOOT_MAGIC);
    if (cold) {
        s_boot_magic = BOOT_MAGIC;
        s_boot_count = 0;
    }
    s_boot_count++;
    Serial.printf("[probe] BOOT %s count=%lu rst=%d\n",
                  cold ? "COLD-power-was-lost" : "WARM-power-never-lost",
                  (unsigned long)s_boot_count, (int)esp_reset_reason());
}

/* 探针用降频值：官方 BSP_EPD_SPI_FREQ 是 40MHz（bsp_pins.h [REF]），v0.6.1 起降到 4MHz
 * 以排除高速 SPI 边沿余量。这是固件调参不是硬件事实，故不进 bsp_pins.h 真值表。 */
#define EPD_SPI_FREQ_PROBE 4000000

static uint32_t s_epd_spi_freq = EPD_SPI_FREQ_PROBE;
static bool epd_board_identity_ok(void);

static const uint8_t WF_Full_1IN54[159] = {
    0x80, 0x48, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x48, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x48, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x48, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x01, 0x00, 0x08, 0x01, 0x00, 0x02,
    0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x00, 0x00, 0x00,
    0x22, 0x17, 0x41, 0x00, 0x32, 0x20
};

/* 变体 C 在 epd_factory.cpp，刻意不共用本文件任何东西 —— 理由见该文件头注释 */
bool epd_factory_run(uint32_t* busy_ms, const char** fail_reason);
bool epd_factory_run_digit(int digit, uint32_t* busy_ms, const char** fail_reason);
bool epd_factory_display_frame(const uint8_t* frame, size_t len, uint32_t* busy_ms, const char** fail_reason);
void epd_factory_release(void);
bool epd_factory_partial_begin(const uint8_t* base_frame, uint32_t* busy_ms, const char** fail_reason);
bool epd_factory_partial_frame(const uint8_t* frame, uint32_t* busy_ms, const char** fail_reason);
void epd_factory_partial_end(void);
bool epd_factory_partial_active(void);

/* 大号数字 5x7 字形，bit4 为最左列。变体编号画在屏上，人眼只需看一次。 */
static const uint8_t DIGIT_5X7[3][7] = {
    { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },   /* 1 */
    { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F },   /* 2 */
    { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E },   /* 3 */
};

static void epd_set_last(bool ok, const char* detail, uint32_t busy_ms)
{
    s_epd_last_refresh_attempted = true;
    s_epd_last_refresh_ok = ok;
    s_epd_last_busy_ms = busy_ms;
    /* detail 可能就指向 s_epd_last_detail 自身（init 失败时沿用 epd_spi_init 写入的原因），
     * snprintf 源目标重叠是 UB，故先判同一缓冲区再决定是否拷贝。 */
    if (detail != s_epd_last_detail) {
        snprintf(s_epd_last_detail, sizeof(s_epd_last_detail), "%s", detail);
    }
}

static bool epd_wait_idle(uint32_t timeout_ms, uint32_t* waited_ms)
{
    const uint32_t start = millis();
    while (digitalRead(BSP_PIN_EPD_BUSY) == HIGH) {
        if (millis() - start >= timeout_ms) {
            if (waited_ms) *waited_ms = millis() - start;
            return false;
        }
        delay(5);
    }
    if (waited_ms) *waited_ms = millis() - start;
    return true;
}

static bool epd_spi_init(void)
{
    if (s_epd_spi_ready) return true;

    spi_bus_config_t buscfg = {};
    buscfg.miso_io_num = -1;
    buscfg.mosi_io_num = BSP_PIN_EPD_DIN;
    buscfg.sclk_io_num = BSP_PIN_EPD_CLK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = (BSP_EPD_W * BSP_EPD_H) / 8;

    spi_device_interface_config_t devcfg = {};
    devcfg.spics_io_num = -1;
    devcfg.clock_speed_hz = s_epd_spi_freq;
    devcfg.mode = 0;
    devcfg.queue_size = 1;

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        snprintf(s_epd_last_detail, sizeof(s_epd_last_detail), "spi_bus:%s", esp_err_to_name(err));
        return false;
    }
    err = spi_bus_add_device(SPI2_HOST, &devcfg, &s_epd_spi);
    if (err != ESP_OK) {
        snprintf(s_epd_last_detail, sizeof(s_epd_last_detail), "spi_dev:%s", esp_err_to_name(err));
        return false;
    }

    s_epd_spi_ready = true;
    return true;
}

/* 变体 C 要用 40MHz 重开同一条 SPI2_HOST。不释放的话 spi_bus_initialize 会返回
 * ESP_ERR_INVALID_STATE，C 就会继续跑在 A/B 的 4MHz 上 —— 那它就不是「原厂配置」了，
 * 实验失去意义。所以 A/B 跑完必须显式归还总线。 */
static void epd_spi_deinit(void)
{
    if (s_epd_spi) { spi_bus_remove_device(s_epd_spi); s_epd_spi = nullptr; }
    if (s_epd_spi_ready) { spi_bus_free(SPI2_HOST); s_epd_spi_ready = false; }
}

static bool epd_write_byte(uint8_t value)
{
    spi_transaction_t t = {};
    t.length = 8;
    t.tx_buffer = &value;
    return spi_device_polling_transmit(s_epd_spi, &t) == ESP_OK;
}

static bool epd_write_bytes(const uint8_t* data, size_t len)
{
    spi_transaction_t t = {};
    t.length = len * 8;
    t.tx_buffer = data;
    return spi_device_polling_transmit(s_epd_spi, &t) == ESP_OK;
}

static bool epd_command(uint8_t cmd)
{
    digitalWrite(BSP_PIN_EPD_DC, LOW);
    digitalWrite(BSP_PIN_EPD_CS, LOW);
    const bool ok = epd_write_byte(cmd);
    digitalWrite(BSP_PIN_EPD_CS, HIGH);
    return ok;
}

static bool epd_data(uint8_t data)
{
    digitalWrite(BSP_PIN_EPD_DC, HIGH);
    digitalWrite(BSP_PIN_EPD_CS, LOW);
    const bool ok = epd_write_byte(data);
    digitalWrite(BSP_PIN_EPD_CS, HIGH);
    return ok;
}

static bool epd_data_bulk(const uint8_t* data, size_t len)
{
    digitalWrite(BSP_PIN_EPD_DC, HIGH);
    digitalWrite(BSP_PIN_EPD_CS, LOW);
    const bool ok = epd_write_bytes(data, len);
    digitalWrite(BSP_PIN_EPD_CS, HIGH);
    return ok;
}

static bool epd_set_lut(void)
{
    return epd_command(0x32) && epd_data_bulk(WF_Full_1IN54, 153) &&
           epd_command(0x3F) && epd_data(WF_Full_1IN54[153]) &&
           epd_command(0x03) && epd_data(WF_Full_1IN54[154]) &&
           epd_command(0x04) && epd_data(WF_Full_1IN54[155]) &&
           epd_data(WF_Full_1IN54[156]) && epd_data(WF_Full_1IN54[157]) &&
           epd_command(0x2C) && epd_data(WF_Full_1IN54[158]);
}

static bool epd_set_window_and_cursor(void)
{
    return epd_command(0x44) && epd_data(0x00) && epd_data((BSP_EPD_W - 1) >> 3) &&
           epd_command(0x45) && epd_data((BSP_EPD_H - 1) & 0xFF) &&
           epd_data(((BSP_EPD_H - 1) >> 8) & 0xFF) && epd_data(0x00) && epd_data(0x00) &&
           epd_command(0x4E) && epd_data(0x00) &&
           epd_command(0x4F) && epd_data((BSP_EPD_H - 1) & 0xFF) &&
           epd_data(((BSP_EPD_H - 1) >> 8) & 0xFF);
}

/* 电源域探针 —— 用 I2C 双线做仪表。
 * 板上 SDA/SCL 有外部上拉；外设域通电时内部上拉读回为高，断电时器件 ESD 钳位把线拽低。
 * 因此不装 I2C driver 也能判断「这一组电源电平有没有把外设域点起来」。
 * 参照：baseline/factory_epaper_154_boot.log 中 shtc3 ID:0887 证明总线本身完好。 */
static int s_pwr_combo = -1;

static bool epd_rail_is_up(void)
{
    pinMode(BSP_PIN_I2C_SDA, INPUT_PULLUP);
    pinMode(BSP_PIN_I2C_SCL, INPUT_PULLUP);
    delay(5);
    return digitalRead(BSP_PIN_I2C_SDA) == HIGH && digitalRead(BSP_PIN_I2C_SCL) == HIGH;
}

static void epd_apply_power(int combo)
{
    pinMode(BSP_PIN_EPD_PWR_EN, OUTPUT);
    pinMode(BSP_PIN_BAT_CTRL, OUTPUT);
    pinMode(BSP_PIN_PA_EN, OUTPUT);
    digitalWrite(BSP_PIN_EPD_PWR_EN, ((combo >> 2) & 1) ? LOW : HIGH);
    digitalWrite(BSP_PIN_BAT_CTRL,   ((combo >> 1) & 1) ? HIGH : LOW);
    digitalWrite(BSP_PIN_PA_EN,      ( combo       & 1) ? LOW : HIGH);
}

static void epd_log_ctrl(const char* phase)
{
    Serial.printf("[probe] EPD ctrl %-12s pwr%d rst%d dc%d cs%d busy%d\n",
                  phase,
                  digitalRead(BSP_PIN_EPD_PWR_EN),
                  digitalRead(BSP_PIN_EPD_RST),
                  digitalRead(BSP_PIN_EPD_DC),
                  digitalRead(BSP_PIN_EPD_CS),
                  digitalRead(BSP_PIN_EPD_BUSY));
}

/* v0.7.0 起默认关闭：这一轮扫描已在 v0.6.2 给出过它的答案（8 组组合下 I2C rail 全 UP，
 * 结论「三根使能均未门控该域」已进 CHANGELOG），再跑一次不会有新信息，却要占掉 18 秒 ——
 * 而这 18 秒正落在用户盯屏幕的窗口里。代码留在树上、可随时置 1 重跑，不删。 */
#define EPD_PROBE_POWER_SWEEP 0

#if EPD_PROBE_POWER_SWEEP
static int epd_probe_power_domain(void)
{
    int winner = -1;

    /* 从 7 (三根全高) 往下扫，全高与原厂 GPIO 配置最接近，命中即优先采用。
     * 停留 1500ms：v0.6.2 首测用 200ms 时 8 组全部 UP，无法归因 —— 负载开关后的储能
     * 电容放不完，读到的是残余电荷而不是「这一组电平真的把域点起来了」。 */
    for (int combo = 7; combo >= 0; combo--) {
        epd_apply_power(combo);
        delay(1500);
        const bool up = epd_rail_is_up();
        Serial.printf("[probe] PWR EPD%d VBAT%d AUD%d -> i2c-rail %s\n",
                      (combo >> 2) & 1, (combo >> 1) & 1, combo & 1, up ? "UP" : "low");
        if (up && winner < 0) winner = combo;
    }

    /* 单变量复核：固定 EPD/VBAT 为高，只翻 AUD(IO42)，连翻两轮看线状态跟不跟随 */
    for (int rep = 0; rep < 2; rep++) {
        for (int aud = 1; aud >= 0; aud--) {
            epd_apply_power(0x6 | aud);
            delay(1500);
            Serial.printf("[probe] PWR isolate AUD%d (EPD1 VBAT1) -> i2c-rail %s\n",
                          aud, epd_rail_is_up() ? "UP" : "low");
        }
    }

    if (winner < 0) {
        Serial.println("[probe] PWR no combo brings i2c rail up, fallback 111");
        winner = 7;
    } else {
        Serial.printf("[probe] PWR winner EPD%d VBAT%d AUD%d\n",
                      (winner >> 2) & 1, (winner >> 1) & 1, winner & 1);
    }

    epd_apply_power(winner);
    delay(200);
    return winner;
}
#endif /* EPD_PROBE_POWER_SWEEP */

/**
 * 面板初始化。use_custom_lut=false 即变体 A —— 跳过 0x32/0x3F/0x03/0x04/0x2C，
 * 让 0x22 0xB1 从 OTP 装载的内置波形与默认门极/源极/VCOM 电压留在原位。
 * 除这一处外命令链与官方 EPD_Init() 完全一致，保证 A 是单变量实验。
 */
static bool epd_init_panel(bool use_custom_lut, uint32_t* busy_ms)
{
    /* 电源电平由调用方选定后保持，此处不得再无条件拉高 IO6 */
    delay(20);

    if (!epd_spi_init()) return false;

    digitalWrite(BSP_PIN_EPD_RST, HIGH);
    delay(50);
    digitalWrite(BSP_PIN_EPD_RST, LOW);
    delay(20);
    digitalWrite(BSP_PIN_EPD_RST, HIGH);
    delay(50);

    uint32_t waited = 0;
    if (!epd_wait_idle(2000, &waited)) {
        if (busy_ms) *busy_ms = waited;
        return false;
    }
    if (!epd_command(0x12)) return false;
    if (!epd_wait_idle(3000, &waited)) {
        if (busy_ms) *busy_ms = waited;
        return false;
    }
    if (busy_ms) *busy_ms = waited;

    if (!epd_command(0x01) || !epd_data(0xC7) || !epd_data(0x00) || !epd_data(0x01)) return false;
    if (!epd_command(0x11) || !epd_data(0x01)) return false;
    if (!epd_set_window_and_cursor()) return false;
    if (!epd_command(0x3C) || !epd_data(0x01)) return false;
    if (!epd_command(0x18) || !epd_data(0x80)) return false;
    if (!epd_command(0x22) || !epd_data(0xB1) || !epd_command(0x20)) return false;
    if (!epd_wait_idle(3000, &waited)) {
        if (busy_ms) *busy_ms = waited;
        return false;
    }
    if (!epd_set_window_and_cursor()) return false;
    if (use_custom_lut && !epd_set_lut()) return false;
    return true;
}

static void epd_make_checker(uint8_t* buf, size_t len)
{
    memset(buf, 0xFF, len);
    for (uint16_t y = 0; y < BSP_EPD_H; y++) {
        for (uint16_t x = 0; x < BSP_EPD_W; x++) {
            const bool border = x < 4 || y < 4 || x >= BSP_EPD_W - 4 || y >= BSP_EPD_H - 4;
            const bool cross = (x >= 96 && x < 104) || (y >= 96 && y < 104);
            const bool checker = ((x / 25) + (y / 25)) & 1;
            if (!border && !cross && !checker) continue;
            const size_t idx = y * (BSP_EPD_W / 8) + (x >> 3);
            buf[idx] &= ~(uint8_t)(0x80 >> (x & 7));
        }
    }
}

static void epd_make_solid(uint8_t* buf, size_t len, bool black)
{
    memset(buf, black ? 0x00 : 0xFF, len);
}

/* 变体编号 + 粗边框。边框的作用是让「刷成功但字画错了」与「压根没刷」在人眼里可分：
 * 只要边框出现，这一次刷新就是可见的，哪怕数字读不出来。 */
static void epd_make_digit(uint8_t* buf, size_t len, int digit)
{
    memset(buf, 0xFF, len);
    const uint8_t* glyph = DIGIT_5X7[(digit - 1) % 3];
    for (uint16_t y = 0; y < BSP_EPD_H; y++) {
        for (uint16_t x = 0; x < BSP_EPD_W; x++) {
            bool ink = (x < 6 || y < 6 || x >= BSP_EPD_W - 6 || y >= BSP_EPD_H - 6);
            if (!ink && x >= 40 && x < 160 && y >= 16 && y < 184) {
                ink = (glyph[(y - 16) / 24] >> (4 - (x - 40) / 24)) & 1;
            }
            if (!ink) continue;
            buf[y * (BSP_EPD_W / 8) + (x >> 3)] &= ~(uint8_t)(0x80 >> (x & 7));
        }
    }
}

/**
 * @param also_old_ram  变体 B：同一份图同时写 0x24(新) 与 0x26(旧)，对齐官方
 *                      EPD_DisplayPartBaseImage()，排除「旧图 RAM 上电是随机值」。
 * @param update_mode   0xC7 = 官方全刷；0xF7 = 变体 A 用，附带从 OTP 装载温度与波形。
 */
static bool epd_display_buffer(const uint8_t* buf, size_t len, bool also_old_ram,
                               uint8_t update_mode, uint32_t* busy_ms)
{
    if (!epd_set_window_and_cursor()) return false;
    if (!epd_command(0x24) || !epd_data_bulk(buf, len)) return false;
    if (also_old_ram) {
        /* 0x26 与 0x24 共用同一个地址计数器，写完新图后计数器已经跑到末尾，
         * 不重设光标就会从越界处继续写。官方 DisplayPartBaseImage 之所以没这句，
         * 是因为它紧接在 Init 之后、计数器还在原位；这里不是。 */
        if (!epd_set_window_and_cursor()) return false;
        if (!epd_command(0x26) || !epd_data_bulk(buf, len)) return false;
    }
    if (!epd_command(0x22) || !epd_data(update_mode) || !epd_command(0x20)) return false;
    return epd_wait_idle(15000, busy_ms);
}

void bsp_ui_init(void)
{
    bsp_ui_clear();
}

void bsp_ui_clear(void)
{
    memset(s_frame_buf, 0, sizeof(s_frame_buf));
    s_row_count = 0;
}

void bsp_ui_printf(const char* fmt, ...)
{
    char buf[128];
    if (s_row_count >= 16) return;

    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    Serial.print(buf);
    snprintf(s_frame_buf[s_row_count], sizeof(s_frame_buf[0]), "%s", buf);
    s_row_count++;
}

void bsp_ui_status(int line, const char* fmt, ...)
{
    if (line < 0 || line >= 16) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(s_frame_buf[line], sizeof(s_frame_buf[0]), fmt, args);
    va_end(args);
}

void bsp_ui_bar(int slot, uint16_t value, uint16_t max)
{
    // 墨水屏实时 ProgressBar 无效，仅打印数字比例
    bsp_ui_status(10 + slot, "Bar %d: %u/%u", slot, value, max);
}

static const uint8_t* ui_glyph_5x7(char c)
{
    static const uint8_t qmark[5] = { 0x02, 0x01, 0x51, 0x09, 0x06 };
    static const uint8_t space[5] = { 0x00, 0x00, 0x00, 0x00, 0x00 };

    if (c >= 'a' && c <= 'z') c = (char)(c - ('a' - 'A'));
    switch (c) {
        case ' ': return space;
        case '!': { static const uint8_t g[5] = { 0x00, 0x00, 0x5F, 0x00, 0x00 }; return g; }
        case '"': { static const uint8_t g[5] = { 0x00, 0x07, 0x00, 0x07, 0x00 }; return g; }
        case '#': { static const uint8_t g[5] = { 0x14, 0x7F, 0x14, 0x7F, 0x14 }; return g; }
        case '%': { static const uint8_t g[5] = { 0x23, 0x13, 0x08, 0x64, 0x62 }; return g; }
        case '&': { static const uint8_t g[5] = { 0x36, 0x49, 0x55, 0x22, 0x50 }; return g; }
        case '\'': { static const uint8_t g[5] = { 0x00, 0x05, 0x03, 0x00, 0x00 }; return g; }
        case '(': { static const uint8_t g[5] = { 0x00, 0x1C, 0x22, 0x41, 0x00 }; return g; }
        case ')': { static const uint8_t g[5] = { 0x00, 0x41, 0x22, 0x1C, 0x00 }; return g; }
        case '*': { static const uint8_t g[5] = { 0x14, 0x08, 0x3E, 0x08, 0x14 }; return g; }
        case '+': { static const uint8_t g[5] = { 0x08, 0x08, 0x3E, 0x08, 0x08 }; return g; }
        case ',': { static const uint8_t g[5] = { 0x00, 0x50, 0x30, 0x00, 0x00 }; return g; }
        case '-': { static const uint8_t g[5] = { 0x08, 0x08, 0x08, 0x08, 0x08 }; return g; }
        case '.': { static const uint8_t g[5] = { 0x00, 0x60, 0x60, 0x00, 0x00 }; return g; }
        case '/': { static const uint8_t g[5] = { 0x20, 0x10, 0x08, 0x04, 0x02 }; return g; }
        case '0': { static const uint8_t g[5] = { 0x3E, 0x51, 0x49, 0x45, 0x3E }; return g; }
        case '1': { static const uint8_t g[5] = { 0x00, 0x42, 0x7F, 0x40, 0x00 }; return g; }
        case '2': { static const uint8_t g[5] = { 0x42, 0x61, 0x51, 0x49, 0x46 }; return g; }
        case '3': { static const uint8_t g[5] = { 0x21, 0x41, 0x45, 0x4B, 0x31 }; return g; }
        case '4': { static const uint8_t g[5] = { 0x18, 0x14, 0x12, 0x7F, 0x10 }; return g; }
        case '5': { static const uint8_t g[5] = { 0x27, 0x45, 0x45, 0x45, 0x39 }; return g; }
        case '6': { static const uint8_t g[5] = { 0x3C, 0x4A, 0x49, 0x49, 0x30 }; return g; }
        case '7': { static const uint8_t g[5] = { 0x01, 0x71, 0x09, 0x05, 0x03 }; return g; }
        case '8': { static const uint8_t g[5] = { 0x36, 0x49, 0x49, 0x49, 0x36 }; return g; }
        case '9': { static const uint8_t g[5] = { 0x06, 0x49, 0x49, 0x29, 0x1E }; return g; }
        case ':': { static const uint8_t g[5] = { 0x00, 0x36, 0x36, 0x00, 0x00 }; return g; }
        case ';': { static const uint8_t g[5] = { 0x00, 0x56, 0x36, 0x00, 0x00 }; return g; }
        case '<': { static const uint8_t g[5] = { 0x08, 0x14, 0x22, 0x41, 0x00 }; return g; }
        case '=': { static const uint8_t g[5] = { 0x14, 0x14, 0x14, 0x14, 0x14 }; return g; }
        case '>': { static const uint8_t g[5] = { 0x00, 0x41, 0x22, 0x14, 0x08 }; return g; }
        case '?': return qmark;
        case '@': { static const uint8_t g[5] = { 0x32, 0x49, 0x79, 0x41, 0x3E }; return g; }
        case 'A': { static const uint8_t g[5] = { 0x7E, 0x11, 0x11, 0x11, 0x7E }; return g; }
        case 'B': { static const uint8_t g[5] = { 0x7F, 0x49, 0x49, 0x49, 0x36 }; return g; }
        case 'C': { static const uint8_t g[5] = { 0x3E, 0x41, 0x41, 0x41, 0x22 }; return g; }
        case 'D': { static const uint8_t g[5] = { 0x7F, 0x41, 0x41, 0x22, 0x1C }; return g; }
        case 'E': { static const uint8_t g[5] = { 0x7F, 0x49, 0x49, 0x49, 0x41 }; return g; }
        case 'F': { static const uint8_t g[5] = { 0x7F, 0x09, 0x09, 0x09, 0x01 }; return g; }
        case 'G': { static const uint8_t g[5] = { 0x3E, 0x41, 0x49, 0x49, 0x7A }; return g; }
        case 'H': { static const uint8_t g[5] = { 0x7F, 0x08, 0x08, 0x08, 0x7F }; return g; }
        case 'I': { static const uint8_t g[5] = { 0x00, 0x41, 0x7F, 0x41, 0x00 }; return g; }
        case 'J': { static const uint8_t g[5] = { 0x20, 0x40, 0x41, 0x3F, 0x01 }; return g; }
        case 'K': { static const uint8_t g[5] = { 0x7F, 0x08, 0x14, 0x22, 0x41 }; return g; }
        case 'L': { static const uint8_t g[5] = { 0x7F, 0x40, 0x40, 0x40, 0x40 }; return g; }
        case 'M': { static const uint8_t g[5] = { 0x7F, 0x02, 0x0C, 0x02, 0x7F }; return g; }
        case 'N': { static const uint8_t g[5] = { 0x7F, 0x04, 0x08, 0x10, 0x7F }; return g; }
        case 'O': { static const uint8_t g[5] = { 0x3E, 0x41, 0x41, 0x41, 0x3E }; return g; }
        case 'P': { static const uint8_t g[5] = { 0x7F, 0x09, 0x09, 0x09, 0x06 }; return g; }
        case 'Q': { static const uint8_t g[5] = { 0x3E, 0x41, 0x51, 0x21, 0x5E }; return g; }
        case 'R': { static const uint8_t g[5] = { 0x7F, 0x09, 0x19, 0x29, 0x46 }; return g; }
        case 'S': { static const uint8_t g[5] = { 0x46, 0x49, 0x49, 0x49, 0x31 }; return g; }
        case 'T': { static const uint8_t g[5] = { 0x01, 0x01, 0x7F, 0x01, 0x01 }; return g; }
        case 'U': { static const uint8_t g[5] = { 0x3F, 0x40, 0x40, 0x40, 0x3F }; return g; }
        case 'V': { static const uint8_t g[5] = { 0x1F, 0x20, 0x40, 0x20, 0x1F }; return g; }
        case 'W': { static const uint8_t g[5] = { 0x3F, 0x40, 0x38, 0x40, 0x3F }; return g; }
        case 'X': { static const uint8_t g[5] = { 0x63, 0x14, 0x08, 0x14, 0x63 }; return g; }
        case 'Y': { static const uint8_t g[5] = { 0x07, 0x08, 0x70, 0x08, 0x07 }; return g; }
        case 'Z': { static const uint8_t g[5] = { 0x61, 0x51, 0x49, 0x45, 0x43 }; return g; }
        case '[': { static const uint8_t g[5] = { 0x00, 0x7F, 0x41, 0x41, 0x00 }; return g; }
        case '\\': { static const uint8_t g[5] = { 0x02, 0x04, 0x08, 0x10, 0x20 }; return g; }
        case ']': { static const uint8_t g[5] = { 0x00, 0x41, 0x41, 0x7F, 0x00 }; return g; }
        case '^': { static const uint8_t g[5] = { 0x04, 0x02, 0x01, 0x02, 0x04 }; return g; }
        case '_': { static const uint8_t g[5] = { 0x40, 0x40, 0x40, 0x40, 0x40 }; return g; }
        default: return qmark;
    }
}

static void ui_frame_pixel(uint8_t* frame, uint16_t x, uint16_t y, bool black)
{
    if (x >= BSP_EPD_W || y >= BSP_EPD_H) return;
    const size_t idx = y * (BSP_EPD_W / 8) + (x >> 3);
    const uint8_t mask = (uint8_t)(0x80 >> (x & 7));
    if (black) frame[idx] &= ~mask;
    else       frame[idx] |= mask;
}

static void ui_draw_char(uint8_t* frame, uint16_t x, uint16_t y, char c)
{
    const uint8_t* glyph = ui_glyph_5x7(c);
    for (uint8_t col = 0; col < 5; col++) {
        for (uint8_t row = 0; row < 7; row++) {
            if (glyph[col] & (uint8_t)(1u << row)) {
                ui_frame_pixel(frame, x + col, y + row, true);
            }
        }
    }
}

static inline void fb_set_pixel(int x, int y, bool black);

void bsp_ui_fb_draw_text(int x, int y, const char* text, uint8_t scale)
{
    if (!text || scale == 0) return;
    for (const char* p = text; *p; p++) {
        if (*p == '\n') {
            y += 8 * scale;
            x = 0;
            continue;
        }
        const uint8_t* glyph = ui_glyph_5x7(*p);
        for (uint8_t col = 0; col < 5; col++) {
            for (uint8_t row = 0; row < 7; row++) {
                if (!(glyph[col] & (uint8_t)(1u << row))) continue;
                for (uint8_t sy = 0; sy < scale; sy++)
                    for (uint8_t sx = 0; sx < scale; sx++)
                        fb_set_pixel(x + col * scale + sx, y + row * scale + sy, true);
            }
        }
        x += 6 * scale;
    }
}

static void ui_render_text_frame(uint8_t* frame, size_t len)
{
    memset(frame, 0xFF, len);
    for (uint16_t x = 0; x < BSP_EPD_W; x++) {
        ui_frame_pixel(frame, x, 0, true);
        ui_frame_pixel(frame, x, BSP_EPD_H - 1, true);
    }
    for (uint16_t y = 0; y < BSP_EPD_H; y++) {
        ui_frame_pixel(frame, 0, y, true);
        ui_frame_pixel(frame, BSP_EPD_W - 1, y, true);
    }

    for (uint8_t row = 0; row < 16; row++) {
        const uint16_t y = 4 + row * 12;
        for (uint8_t col = 0; col < 30; col++) {
            const char c = s_frame_buf[row][col];
            if (c == '\0' || c == '\n' || c == '\r') break;
            ui_draw_char(frame, 4 + col * 6, y, c);
        }
    }
}

void bsp_ui_flush(void)
{
    static uint8_t frame[(BSP_EPD_W * BSP_EPD_H) / 8];

    if (!epd_board_identity_ok()) {
        epd_set_last(false, "wrong-board", 0);
        return;
    }

    s_pwr_combo = 7;
    epd_apply_power(s_pwr_combo);
    delay(200);
    ui_render_text_frame(frame, sizeof(frame));

    epd_spi_deinit();
    uint32_t busy_ms = 0;
    const char* fail = "";
    const bool ok = epd_factory_display_frame(frame, sizeof(frame), &busy_ms, &fail);
    Serial.printf("[probe] UI flush %s busy%lums %s\n",
                  ok ? "done" : "fail", (unsigned long)busy_ms,
                  ok ? "" : fail);
    epd_factory_release();
    epd_set_last(ok, ok ? "ui-flush" : fail, busy_ms);
}

uint16_t bsp_ui_cols(void) { return 30; }
uint16_t bsp_ui_rows(void) { return 16; }

/* ============================================================================
 * 位图 / framebuffer 层（M0）
 *
 * 【位语义】fb 用面板原生格式：bit 1 = 白，bit 0 = 黑墨。这与 fac_draw_pixel()
 * 和 ui_frame_pixel() 一致，因此 fb 可以原样喂给 epd_factory_display_frame()，
 * 中间不做任何取反 —— 少一次取反就少一个「整屏黑白反了」的故障模式。
 *
 * 【应用素材的位语义相反】app_person_t.bits 里 1 = 黑墨（人看着更自然，工具侧也
 * 更好写）。取反只发生在 bsp_ui_draw_bitmap() 内部这一个地方。
 *
 * 【为什么必须有 mask】墨水屏没有 alpha。抠像人物是不规则轮廓，只有 bits 时，
 * 「人物身上的白衣服」和「人物轮廓外的背景」在数据里长得一模一样，结果是整个
 * 矩形框糊到画面上。mask 的 1 表示「这个像素属于图形」，叠加是三操作数运算：
 *     fb = (fb & ~mask) | (ink & mask)
 * 详见 docs/FAMILY_PHOTO_APP.md「素材：双位平面」。
 * ========================================================================== */

#define UI_FB_STRIDE  (BSP_EPD_W / 8)          /* 25 */
#define UI_FB_LEN     (UI_FB_STRIDE * BSP_EPD_H) /* 5000 */

static uint8_t s_fb[UI_FB_LEN];

uint8_t* bsp_ui_fb(void) { return s_fb; }
uint32_t bsp_ui_fb_len(void) { return UI_FB_LEN; }

void bsp_ui_fb_clear(uint8_t value)
{
    memset(s_fb, value, sizeof(s_fb));
}

static inline void fb_set_pixel(int x, int y, bool black)
{
    if ((unsigned)x >= BSP_EPD_W || (unsigned)y >= BSP_EPD_H) return;
    uint8_t* p = &s_fb[y * UI_FB_STRIDE + (x >> 3)];
    const uint8_t bit = (uint8_t)(0x80 >> (x & 7));
    if (black) *p &= (uint8_t)~bit;
    else       *p |= bit;
}

/* 源位图取位：行优先，每行按 8 像素补齐，MSB 是行内最左像素 —— 与 fb 同序，
 * 所以将来若做整字节快路径不需要再对齐一次。 */
static inline bool src_bit(const uint8_t* buf, int w, int sx, int sy)
{
    const int stride = (w + 7) / 8;
    return (buf[sy * stride + (sx >> 3)] >> (7 - (sx & 7))) & 1;
}

/**
 * 把一张 1bpp 位图叠加到 framebuffer。
 *
 * @param bits   1 = 黑墨。
 * @param mask   1 = 该像素属于图形。传 NULL 表示整个 w*h 矩形都属于图形
 *               （只有背景图这类满幅矩形才该传 NULL）。
 * @param invert 黑白反相，调试用；正常素材传 false。
 *
 * 逐像素实现。最大 200x200 = 40000 次，在 240MHz 上不到 1ms，而一次刷屏是几百到
 * 一千多毫秒 —— 为这点开销做整字节移位快路径不划算，反而会引入对齐 bug。
 */
void bsp_ui_draw_bitmap(int x, int y, int w, int h,
                        const uint8_t* bits, const uint8_t* mask, bool invert)
{
    if (!bits || w <= 0 || h <= 0) return;

    for (int sy = 0; sy < h; sy++) {
        const int dy = y + sy;
        if (dy < 0 || dy >= BSP_EPD_H) continue;
        for (int sx = 0; sx < w; sx++) {
            const int dx = x + sx;
            if (dx < 0 || dx >= BSP_EPD_W) continue;
            if (mask && !src_bit(mask, w, sx, sy)) continue;   /* 不属于图形，保留背景 */
            bool ink = src_bit(bits, w, sx, sy);
            if (invert) ink = !ink;
            fb_set_pixel(dx, dy, ink);
        }
    }
}

void bsp_ui_fb_fill_rect(int x, int y, int w, int h, bool black)
{
    for (int dy = y; dy < y + h; dy++)
        for (int dx = x; dx < x + w; dx++)
            fb_set_pixel(dx, dy, black);
}

/**
 * 全刷当前 framebuffer。走的是 v0.7.10 已验证可见的 epd_factory_display_frame()
 * 路径，与 bsp_ui_flush() 同源，只是帧内容来自 fb 而不是文本缓冲。
 *
 * 每次调用都会重新 init + release SPI/面板。局刷需要跨帧保持会话，那是 M1 的事，
 * 不要把局刷硬塞进这个函数。
 */
bool bsp_ui_fb_flush_full(void)
{
    if (!epd_board_identity_ok()) {
        epd_set_last(false, "wrong-board", 0);
        return false;
    }

    /* 全刷会换掉 LUT，也让 0x26 里的基准图失效。会话开着就先收掉 —— 否则
     * 之后的局刷是在一张对不上的基准图上做的，现象是满屏鬼影。
     * 调用方需要继续局刷的话，全刷之后必须重新 bsp_ui_partial_begin()。 */
    if (epd_factory_partial_active()) {
        Serial.println("[fb] full flush: closing active partial session first");
        epd_factory_partial_end();
    }

    s_pwr_combo = 7;
    epd_apply_power(s_pwr_combo);
    delay(200);

    epd_spi_deinit();
    uint32_t busy_ms = 0;
    const char* fail = "";
    const bool ok = epd_factory_display_frame(s_fb, sizeof(s_fb), &busy_ms, &fail);
    Serial.printf("[fb] full flush %s busy%lums %s\n",
                  ok ? "done" : "FAIL", (unsigned long)busy_ms, ok ? "" : fail);
    epd_factory_release();
    epd_set_last(ok, ok ? "fb-flush" : fail, busy_ms);
    return ok;
}

/* ── 局部刷新（M1）─────────────────────────────────────────
 * 用法：
 *     bsp_ui_fb_clear(0xFF); ...画背景...;  bsp_ui_fb_flush_full();
 *     bsp_ui_partial_begin();                  // 拿当前 fb 当基准图
 *     ...改 fb...;  bsp_ui_flush_partial();    // 每次点击一帧
 *     bsp_ui_partial_end();
 *
 * 中途插全刷必须：partial_end() → flush_full() → partial_begin()。
 * bsp_ui_fb_flush_full() 里已经做了「会话开着就先收掉」的保护，但重开会话
 * 是应用的决定，BSP 不替它做。
 */
bool bsp_ui_partial_begin(void)
{
    if (!epd_board_identity_ok()) {
        epd_set_last(false, "wrong-board", 0);
        return false;
    }

    s_pwr_combo = 7;
    epd_apply_power(s_pwr_combo);
    delay(200);
    epd_spi_deinit();

    uint32_t busy_ms = 0;
    const char* fail = "";
    const bool ok = epd_factory_partial_begin(s_fb, &busy_ms, &fail);
    Serial.printf("[part] begin %s base-busy%lums %s\n",
                  ok ? "ok" : "FAIL", (unsigned long)busy_ms, ok ? "" : fail);
    if (!ok) epd_factory_partial_end();
    epd_set_last(ok, ok ? "part-begin" : fail, busy_ms);
    return ok;
}

bool bsp_ui_flush_partial(void)
{
    uint32_t busy_ms = 0;
    const char* fail = "";
    const bool ok = epd_factory_partial_frame(s_fb, &busy_ms, &fail);

    /* BUSY 毫秒数每帧都打。这是 M1 的验收判据本身 —— 局刷若仍是 ~1755ms，
     * 说明局刷 LUT 没生效，画面「看着好像对了」也不能算通过。 */
    Serial.printf("[part] frame %s busy%lums %s\n",
                  ok ? "ok" : "FAIL", (unsigned long)busy_ms, ok ? "" : fail);
    epd_set_last(ok, ok ? "part-frame" : fail, busy_ms);
    return ok;
}

void bsp_ui_partial_end(void)
{
    epd_factory_partial_end();
    Serial.println("[part] session closed");
}

bool bsp_ui_partial_active(void) { return epd_factory_partial_active(); }

/* ============================================================================
 * v0.7.0 变体轮次 —— A / B / C 串成一轮跑完，人眼只判读一次
 *
 * 判读协议（写在这里是因为它和代码必须同步改）：
 *   - 每个变体成功刷新后，屏上是它自己的编号 1 / 2 / 3，外加一圈粗边框。
 *   - 变体之间停 5 秒，给人眼留出捕捉「中途闪过」的窗口。
 *   - 墨水屏保图：一轮跑完停在屏上的数字 = 最后一个刷成功的变体。
 *   - 阳性对照：起始画面是原厂固件留下的图。它被任何东西替掉 = 至少刷成功过一次；
 *     一轮跑完它还在 = 三个变体全灭。后者同样是明确结论，不是「没看清」。
 *
 * 串口侧（这一半我自己判，不需要人）：
 *   - 每个变体的命令链是否走完、BUSY 等了多久。
 *   - BUSY 时长本身是判据：v0.6.2 三次全刷各 1755ms，与自定义 LUT 时序段
 *     (10 + (8+1+0+8+1+0)*3 + 10 = 74 帧) 吻合。变体 A 走 OTP 波形，若它的
 *     BUSY 明显偏离 1755ms，就证明 LUT 装载路径确实被换掉了 —— 这一条能把
 *     「A 不亮」区分成「OTP 也不亮」和「A 根本没生效」两种，缺了它 A 是废实验。
 * ========================================================================== */

static bool s_variant_ok[3] = { false, false, false };

static bool run_variant_ab(int digit, bool use_custom_lut, bool also_old_ram, uint8_t update_mode)
{
    static uint8_t frame[(BSP_EPD_W * BSP_EPD_H) / 8];
    uint32_t init_busy = 0, disp_busy = 0;
    const char tag = (char)('A' + digit - 1);

    Serial.printf("[probe] %c begin digit%d lut=%s oldram=%s upd=0x%02X spi%lu\n",
                  tag, digit, use_custom_lut ? "custom" : "OTP",
                  also_old_ram ? "yes" : "no", update_mode,
                  (unsigned long)s_epd_spi_freq);

    if (!epd_init_panel(use_custom_lut, &init_busy)) {
        Serial.printf("[probe] %c init fail %s swreset-busy%lums\n",
                      tag, s_epd_last_detail[0] ? s_epd_last_detail : "cmdfail",
                      (unsigned long)init_busy);
        return false;
    }
    Serial.printf("[probe] %c init ok swreset-busy%lums\n", tag, (unsigned long)init_busy);

    epd_make_digit(frame, sizeof(frame), digit);
    const bool ok = epd_display_buffer(frame, sizeof(frame), also_old_ram, update_mode, &disp_busy);
    Serial.printf("[probe] %c display %s busy%lums\n",
                  tag, ok ? "done" : "fail", (unsigned long)disp_busy);
    return ok;
}

/* 开跑前先确认「脚下这块板真的是 epaper_154」。
 *
 * 这条检查是拿一整轮实验换来的：v0.7.0 首次抓取时手边只插着一块板，我没核对就烧了，
 * 结果整轮 A/B/C 跑在 ESP32-S3-GEEK 上 —— 它同样是 S3、同样枚举成 COM 口、同样跑完
 * 我们的固件并打出 "A1 B1 C0 chain-ok"。**假阳性长得和真结果一模一样**：GEEK 的 IO8
 * 恰好读回低电平，epd_wait_idle 立刻返回，命令链「全过」。
 *
 * 指纹取 Flash 8MB + PSRAM 8MB（N8R8）：GEEK 是 16MB flash + 2MB 内置 PSRAM，
 * touch_lcd_154 / amoled_206 也各自不同，一测即分。不匹配就**拒绝驱动 EPD 引脚** ——
 * 在别人的板子上乱拉 IO6/9/10/11/12/13 没有意义，而且会把结论污染成噪声。 */
static bool epd_board_identity_ok(void)
{
    const uint32_t flash_mb = ESP.getFlashChipSize() / (1024 * 1024);
    const uint32_t psram_mb = ESP.getPsramSize() / (1024 * 1024);
    if (flash_mb == 8 && psram_mb >= 7) return true;

    Serial.printf("[probe] ABORT wrong board: flash %luMB psram %luMB, expected 8MB/8MB (N8R8)\n",
                  (unsigned long)flash_mb, (unsigned long)psram_mb);
    Serial.println("[probe] this firmware is epaper_154; refusing to drive EPD pins on another board");
    return false;
}

static void epd_touch_reset_for_probe(void)
{
    pinMode(BSP_PIN_TOUCH_RST, OUTPUT);
    digitalWrite(BSP_PIN_TOUCH_RST, HIGH);
    delay(100);
    digitalWrite(BSP_PIN_TOUCH_RST, LOW);
    delay(100);
    digitalWrite(BSP_PIN_TOUCH_RST, HIGH);
    delay(100);
}

static bool epd_touch_read_point(uint8_t* points_out, uint16_t* x_out, uint16_t* y_out)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    uint8_t buf[7] = {0};

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BSP_TOUCH_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, 0x00, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BSP_TOUCH_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, buf, sizeof(buf) - 1, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, buf + sizeof(buf) - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    const esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    if (err != ESP_OK) return false;

    if (points_out) *points_out = buf[2] & 0x0F;
    if (x_out) *x_out = (uint16_t)(((buf[3] & 0x0F) << 8) | buf[4]);
    if (y_out) *y_out = (uint16_t)(((buf[5] & 0x0F) << 8) | buf[6]);
    return true;
}

static bool epd_wait_right_touch(uint32_t window_ms, uint16_t* hit_x, uint16_t* hit_y)
{
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = (gpio_num_t)BSP_PIN_I2C_SDA;
    conf.scl_io_num = (gpio_num_t)BSP_PIN_I2C_SCL;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = BSP_I2C_FREQ;

    if (i2c_param_config(I2C_NUM_0, &conf) != ESP_OK) return false;
    if (i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0) != ESP_OK) return false;

    epd_touch_reset_for_probe();
    Serial.println("[probe] Touch page test: press RIGHT half to advance 1 -> 2");

    const uint32_t start = millis();
    bool advanced = false;
    while (millis() - start < window_ms) {
        uint8_t points = 0;
        uint16_t x = 0, y = 0;
        if (epd_touch_read_point(&points, &x, &y) && points > 0) {
            Serial.printf("[probe] Touch page hit n%u x%u y%u\n", points, x, y);
            if (x >= BSP_EPD_W / 2 && x < BSP_EPD_W && y < BSP_EPD_H) {
                if (hit_x) *hit_x = x;
                if (hit_y) *hit_y = y;
                advanced = true;
                break;
            }
        }
        delay(80);
    }

    i2c_driver_delete(I2C_NUM_0);
    return advanced;
}

void epd_visible_smoke_test(void)
{
    report_boot_power_state();

    if (!epd_board_identity_ok()) {
        epd_set_last(false, "wrong-board", 0);
        return;
    }

    /* v0.6.2 的扫描结论是 8 组组合全 UP、无一组能门控该域；沿用全高（= 原厂 GPIO 配置） */
    s_pwr_combo = 7;
    epd_apply_power(s_pwr_combo);
    delay(200);
#if EPD_PROBE_POWER_SWEEP
    s_pwr_combo = epd_probe_power_domain();
#endif

    /* 清空 detail：初值 "not-run" 会让下面的 ?: 恒真，把「没跑过」误报成失败原因。 */
    s_epd_last_detail[0] = '\0';
    s_epd_spi_freq = EPD_SPI_FREQ_PROBE;

    Serial.printf("[probe] ==== EPD single digit v0.7.1 pwr%d%d%d ====\n",
                  (s_pwr_combo >> 2) & 1, (s_pwr_combo >> 1) & 1, s_pwr_combo & 1);
    Serial.println("[probe] watch the panel: single large digit 1");

    /* Minimal target: use the factory-style path only and draw one large "1". */
    epd_spi_deinit();
    uint32_t c_busy = 0;
    const char* c_fail = "";
    const bool ok = epd_factory_run_digit(1, &c_busy, &c_fail);
    Serial.printf("[probe] digit1 display %s busy%lums %s\n",
                  ok ? "done" : "fail", (unsigned long)c_busy,
                  ok ? "" : c_fail);
    epd_log_ctrl("C-before-rel");
    epd_factory_release();
    epd_log_ctrl("C-after-rel");
    pinMode(BSP_PIN_EPD_RST, OUTPUT);
    digitalWrite(BSP_PIN_EPD_RST, HIGH);
    pinMode(BSP_PIN_EPD_DC, OUTPUT);
    digitalWrite(BSP_PIN_EPD_DC, LOW);
    pinMode(BSP_PIN_EPD_CS, OUTPUT);
    digitalWrite(BSP_PIN_EPD_CS, HIGH);
    epd_log_ctrl("restored");

    bool final_ok = ok;
    bool touch_advanced = false;
    uint32_t final_busy = c_busy;
    uint16_t tx = 0, ty = 0;
    if (ok && epd_wait_right_touch(15000, &tx, &ty)) {
        touch_advanced = true;
        epd_spi_deinit();
        uint32_t next_busy = 0;
        const char* next_fail = "";
        const bool next_ok = epd_factory_run_digit(2, &next_busy, &next_fail);
        Serial.printf("[probe] touch-next digit2 display %s busy%lums x%u y%u %s\n",
                      next_ok ? "done" : "fail", (unsigned long)next_busy, tx, ty,
                      next_ok ? "" : next_fail);
        epd_factory_release();
        pinMode(BSP_PIN_EPD_RST, OUTPUT);
        digitalWrite(BSP_PIN_EPD_RST, HIGH);
        pinMode(BSP_PIN_EPD_DC, OUTPUT);
        digitalWrite(BSP_PIN_EPD_DC, LOW);
        pinMode(BSP_PIN_EPD_CS, OUTPUT);
        digitalWrite(BSP_PIN_EPD_CS, HIGH);
        final_ok = next_ok;
        final_busy = next_busy;
    }

    snprintf(s_epd_last_detail, sizeof(s_epd_last_detail), "digit%d C%d chain-ok",
             touch_advanced ? 2 : 1, final_ok ? 1 : 0);
    epd_set_last(final_ok, s_epd_last_detail, final_busy);
    Serial.printf("[probe] ==== single digit done %s ====\n",
                  s_epd_last_detail);
}

void bsp_display_calibration_pattern(void)
{
    epd_visible_smoke_test();
}

bool bsp_epd_last_refresh_attempted(void) { return s_epd_last_refresh_attempted; }
bool bsp_epd_last_refresh_ok(void) { return s_epd_last_refresh_ok; }
uint32_t bsp_epd_last_busy_ms(void) { return s_epd_last_busy_ms; }
const char* bsp_epd_last_detail(void) { return s_epd_last_detail; }
