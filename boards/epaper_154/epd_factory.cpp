/**
 * epd_factory.cpp — 变体 C：把官方 epaper_driver_bsp.cpp 逐字节搬进来跑
 *
 * 【这个文件存在的理由】
 * 前两轮排障用的是「目视比对 ui.cpp 与官方驱动」，结论都是「命令链忠实」，但屏还是不亮。
 * 目视比对这个方法在本题上已经失败两次，不再用第三次。改成把参考实现当**可执行的判据**：
 * 不读它、不总结它、不「等价改写」它，而是原样跑一遍，让硬件来判。
 *
 * 因此本文件刻意**不复用** ui.cpp 的任何东西 —— 不共用 SPI 句柄、不共用 LUT 数组、
 * 不共用 GPIO 初始化、不共用 BSP_PIN_* 之外的任何抽象。ui.cpp 里任何一处「看起来等价」
 * 的改写如果其实不等价，这个文件必须能独立暴露出来。为此重复了 159 字节的 LUT 表，
 * 这是故意的冗余，不要去掉。
 *
 * 判据：
 *   C 亮  → 差异在 ui.cpp 驱动内部，逐段二分即可收敛
 *   C 不亮 → 差异不在驱动内部，而在初始化顺序 / Arduino-vs-IDF 框架层 / 板级上电时序，
 *            排障范围整体移出驱动，这也是明确结论
 *
 * 【与原文件的差异 —— 只有这些，全部列出】
 *  1. C++ class → 文件内 static 函数。行为等价，无逻辑改动。
 *  2. read_busy() 原文是无超时死循环 `while(gpio_get_level(busy)==1)`。这里加了超时，
 *     否则一块不应答的板会把整轮实验挂死在 C 上，用户看不到任何结果。超时只影响
 *     「失败时能否收场」，不影响成功路径的时序。
 *  3. 原文 assert(ret == ESP_OK) 改为返回 false 并记录 —— 同上，为了能收场。
 *  4. buffer 内容由本文件填（数字 "3" + 边框）而不是 EPD_Clear() 的全 0xFF，
 *     否则屏上是纯白，与「没刷成功」在人眼里无法区分。写入路径 (0x24 + writeBytes)
 *     完全不变。
 *  5. 原文 buffer_len 来自 lcd_spi_data 结构；这里按 EPD_DisplayPart() 里写死的 5000
 *     取值（200*200/8），与原文一致。
 *
 * 保持原样的（这些都曾被 ui.cpp 改掉过，是本变体的主要检验对象）：
 *   - clock_speed_hz = 40MHz（ui.cpp 探针降到了 4MHz）
 *   - queue_size = 7（ui.cpp 是 1）
 *   - max_transfer_sz = Width * Height = 40000（ui.cpp 是 /8 = 5000）
 *   - buffer 走 MALLOC_CAP_SPIRAM（ui.cpp 用的是内部 RAM 静态数组）
 *   - CS/DC/RST 用 gpio_config 且 pull_up_en = ENABLE（ui.cpp 走 Arduino pinMode）
 *   - EPD_SetLut 里 0x32 写完 153 字节后有一次 read_busy()（ui.cpp 没有）
 *   - EPD_Init 里 0x20 之后先 SetCursor 再 read_busy（ui.cpp 是先等再设）
 *   - EPD_Display 不重设窗口/光标（ui.cpp 每帧都重设）
 */

#include <Arduino.h>
#include <string.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>

#include "bsp_pins.h"

#define FAC_WIDTH   200
#define FAC_HEIGHT  200
#define FAC_BUFLEN  ((FAC_WIDTH * FAC_HEIGHT) / 8)   /* 5000，同原文 EPD_DisplayPart() */

/* 原文 epaper_driver_bsp.cpp 第 12-33 行，逐字节照抄（原文用 0x0/0xA 等简写，此处补零对齐） */
static const uint8_t FAC_WF_Full_1IN54[159] =
{
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

/* 局刷波形表。原文 epaper_driver_bsp.cpp 第 35-56 行 WF_PARTIAL_1IN54_0，逐字节照抄
 * （原文用 0xF/0x1 等简写，此处补零对齐）。与全刷表一样刻意不共享、不去重：
 * 这两张表出问题时的现象完全不同，混在一起会让二分排障失去着力点。 */
static const uint8_t FAC_WF_PARTIAL_1IN54[159] =
{
    0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
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
    0x02, 0x17, 0x41, 0xB0, 0x32, 0x28,
};

static spi_device_handle_t s_fac_spi = nullptr;
static uint8_t*            s_fac_buffer = nullptr;
static bool                s_fac_bus_owned = false;
static const char*         s_fac_fail = "";

/* 差异 #2/#3：原文无超时且 assert，此处可收场版本 */
#define FAC_BUSY_TIMEOUT_MS 20000

static void fac_set_rst(int v) { gpio_set_level((gpio_num_t)BSP_PIN_EPD_RST, v); }
static void fac_set_dc(int v)  { gpio_set_level((gpio_num_t)BSP_PIN_EPD_DC, v); }
static void fac_set_cs(int v)  { gpio_set_level((gpio_num_t)BSP_PIN_EPD_CS, v); }

static bool fac_read_busy(uint32_t* waited_ms)
{
    const uint32_t start = millis();
    while (gpio_get_level((gpio_num_t)BSP_PIN_EPD_BUSY) == 1) {
        if (millis() - start >= FAC_BUSY_TIMEOUT_MS) {
            if (waited_ms) *waited_ms = millis() - start;
            s_fac_fail = "busy-timeout";
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(5));   /* LOW: idle, HIGH: busy —— 同原文 */
    }
    if (waited_ms) *waited_ms = millis() - start;
    return true;
}

static bool fac_spi_send_byte(uint8_t data)
{
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8;
    t.tx_buffer = &data;
    if (spi_device_polling_transmit(s_fac_spi, &t) != ESP_OK) { s_fac_fail = "spi-byte"; return false; }
    return true;
}

static bool fac_send_data(uint8_t data)
{
    fac_set_dc(1);
    fac_set_cs(0);
    const bool ok = fac_spi_send_byte(data);
    fac_set_cs(1);
    return ok;
}

static bool fac_send_command(uint8_t command)
{
    fac_set_dc(0);
    fac_set_cs(0);
    const bool ok = fac_spi_send_byte(command);
    fac_set_cs(1);
    return ok;
}

static bool fac_write_bytes(const uint8_t* buffer, int len)
{
    fac_set_dc(1);
    fac_set_cs(0);
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8 * len;
    t.tx_buffer = buffer;
    const esp_err_t ret = spi_device_polling_transmit(s_fac_spi, &t);
    fac_set_cs(1);
    if (ret != ESP_OK) { s_fac_fail = "spi-bulk"; return false; }
    return true;
}

static bool fac_set_windows(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t Yend)
{
    return fac_send_command(0x44) &&
           fac_send_data((Xstart >> 3) & 0xFF) &&
           fac_send_data((Xend >> 3) & 0xFF) &&
           fac_send_command(0x45) &&
           fac_send_data(Ystart & 0xFF) &&
           fac_send_data((Ystart >> 8) & 0xFF) &&
           fac_send_data(Yend & 0xFF) &&
           fac_send_data((Yend >> 8) & 0xFF);
}

static bool fac_set_cursor(uint16_t Xstart, uint16_t Ystart)
{
    return fac_send_command(0x4E) &&
           fac_send_data(Xstart & 0xFF) &&
           fac_send_command(0x4F) &&
           fac_send_data(Ystart & 0xFF) &&
           fac_send_data((Ystart >> 8) & 0xFF);
}

static bool fac_set_lut(const uint8_t* lut)
{
    if (!fac_send_command(0x32)) return false;
    if (!fac_write_bytes(lut, 153)) return false;
    if (!fac_read_busy(nullptr)) return false;        /* 原文有，ui.cpp 没有 */

    if (!fac_send_command(0x3f) || !fac_send_data(lut[153])) return false;
    if (!fac_send_command(0x03) || !fac_send_data(lut[154])) return false;
    if (!fac_send_command(0x04) || !fac_send_data(lut[155]) ||
        !fac_send_data(lut[156]) || !fac_send_data(lut[157])) return false;
    if (!fac_send_command(0x2c) || !fac_send_data(lut[158])) return false;
    return true;
}

static bool fac_turn_on_display(uint32_t* busy_ms)
{
    if (!fac_send_command(0x22) || !fac_send_data(0xc7)) return false;
    if (!fac_send_command(0x20)) return false;
    return fac_read_busy(busy_ms);
}

static void fac_gpio_init(void)
{
    gpio_config_t gpio_conf = {};
    gpio_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_conf.mode = GPIO_MODE_OUTPUT;
    gpio_conf.pin_bit_mask = (0x1ULL << BSP_PIN_EPD_RST) |
                             (0x1ULL << BSP_PIN_EPD_DC) |
                             (0x1ULL << BSP_PIN_EPD_CS);
    gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&gpio_conf);

    gpio_conf.mode = GPIO_MODE_INPUT;
    gpio_conf.pin_bit_mask = (0x1ULL << BSP_PIN_EPD_BUSY);
    gpio_config(&gpio_conf);

    fac_set_rst(1);
}

static bool fac_spi_port_init(void)
{
    spi_bus_config_t buscfg = {};
    buscfg.miso_io_num = -1;
    buscfg.mosi_io_num = BSP_PIN_EPD_DIN;
    buscfg.sclk_io_num = BSP_PIN_EPD_CLK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = FAC_WIDTH * FAC_HEIGHT;   /* 原文如此：40000，不是 /8 */

    spi_device_interface_config_t devcfg = {};
    devcfg.spics_io_num = -1;
    devcfg.clock_speed_hz = 40 * 1000 * 1000;
    devcfg.mode = 0;
    devcfg.queue_size = 7;

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        /* ui.cpp 的变体 A/B 跑完必须已经 spi_bus_free 过，否则这里会拿到
         * ESP_ERR_INVALID_STATE，devcfg 的 40MHz 就落不下去 —— 那样 C 就不再是
         * 「原厂配置」，实验白做。所以这里不像 ui.cpp 那样容忍 INVALID_STATE。 */
        Serial.printf("[probe] C spi_bus_initialize %s\n", esp_err_to_name(ret));
        s_fac_fail = "spi-bus";
        return false;
    }
    s_fac_bus_owned = true;

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &s_fac_spi);
    if (ret != ESP_OK) {
        Serial.printf("[probe] C spi_bus_add_device %s\n", esp_err_to_name(ret));
        s_fac_fail = "spi-dev";
        return false;
    }
    return true;
}

static bool fac_epd_init(uint32_t* reset_busy_ms)
{
    fac_set_rst(1);
    vTaskDelay(pdMS_TO_TICKS(50));
    fac_set_rst(0);
    vTaskDelay(pdMS_TO_TICKS(20));
    fac_set_rst(1);
    vTaskDelay(pdMS_TO_TICKS(50));

    if (!fac_read_busy(nullptr)) return false;
    if (!fac_send_command(0x12)) return false;          /* SWRESET */
    if (!fac_read_busy(reset_busy_ms)) return false;

    if (!fac_send_command(0x01) || !fac_send_data(0xC7) ||
        !fac_send_data(0x00) || !fac_send_data(0x01)) return false;

    if (!fac_send_command(0x11) || !fac_send_data(0x01)) return false;

    if (!fac_set_windows(0, FAC_WIDTH - 1, FAC_HEIGHT - 1, 0)) return false;

    if (!fac_send_command(0x3C) || !fac_send_data(0x01)) return false;
    if (!fac_send_command(0x18) || !fac_send_data(0x80)) return false;

    if (!fac_send_command(0x22) || !fac_send_data(0xB1)) return false;
    if (!fac_send_command(0x20)) return false;

    /* 原文顺序：先 SetCursor 再 read_busy。ui.cpp 反过来。 */
    if (!fac_set_cursor(0, FAC_HEIGHT - 1)) return false;
    if (!fac_read_busy(nullptr)) return false;

    return fac_set_lut(FAC_WF_Full_1IN54);
}

/* 纯黑压力测试：当前屏上保留的是原厂 UI，整屏 0x00 若成功会非常明显。
 * 这轮专门排除「数字帧位序/颜色语义/窗口方向导致看不出来」这个分支。 */
#define FAC_SOLID_BLACK_TEST 0

/* 差异 #4：默认填数字 "3" + 边框，走原文的 EPD_DrawColorPixel 语义（1=白，0=黑） */
static const uint8_t FAC_DIGITS[3][7] = {
    { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },
    { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F },
    { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E },
};

static void fac_draw_pixel(uint16_t x, uint16_t y, bool black)
{
    if (x >= FAC_WIDTH || y >= FAC_HEIGHT) return;
    const uint16_t index = y * 25 + (x >> 3);
    const uint8_t bit = 7 - (x & 0x07);
    if (black) s_fac_buffer[index] &= ~(uint8_t)(0x01 << bit);
    else       s_fac_buffer[index] |= (uint8_t)(0x01 << bit);
}

static void fac_fill_digit(int digit)
{
    if (digit < 1 || digit > 3) digit = 1;
    const uint8_t* glyph = FAC_DIGITS[digit - 1];
    memset(s_fac_buffer, 0xFF, FAC_BUFLEN);
    for (uint16_t y = 0; y < FAC_HEIGHT; y++) {
        for (uint16_t x = 0; x < FAC_WIDTH; x++) {
            bool ink = (x < 6 || y < 6 || x >= FAC_WIDTH - 6 || y >= FAC_HEIGHT - 6);
            if (!ink && x >= 40 && x < 160 && y >= 16 && y < 184) {
                ink = (glyph[(y - 16) / 24] >> (4 - (x - 40) / 24)) & 1;
            }
            if (ink) fac_draw_pixel(x, y, true);
        }
    }
}

void epd_factory_release(void)
{
    if (s_fac_spi) { spi_bus_remove_device(s_fac_spi); s_fac_spi = nullptr; }
    if (s_fac_bus_owned) { spi_bus_free(SPI2_HOST); s_fac_bus_owned = false; }
}

/**
 * 变体 C 主入口。调用前 ui.cpp 必须已释放 SPI2_HOST。
 * 返回值只表示「命令链全程无错、BUSY 未超时」，**不表示屏上出现了东西** —— 那要人眼判。
 */
bool epd_factory_run_digit(int digit, uint32_t* busy_ms, const char** fail_reason)
{
    s_fac_fail = "";
    if (busy_ms) *busy_ms = 0;

    fac_gpio_init();
    if (!fac_spi_port_init()) { if (fail_reason) *fail_reason = s_fac_fail; return false; }

    /* 原文构造函数里就 malloc，且明确要 SPIRAM。ui.cpp 用的是内部 RAM 静态数组 ——
     * 这一行本身就是被检验对象之一（PSRAM 缓冲 + SPI DMA 的组合）。 */
    bool psram_buf = true;
    if (!s_fac_buffer) {
        s_fac_buffer = (uint8_t*)heap_caps_malloc(FAC_BUFLEN, MALLOC_CAP_SPIRAM);
    }
    if (!s_fac_buffer) {
        /* PSRAM 拿不到就退回内部 RAM 继续跑。原来这里直接 return false —— 结果是
         * 「C 一个字节都没送出去」和「C 送完了但屏没反应」在日志里长得一模一样，
         * 而这两者的结论天差地别。宁可降级也要让命令链跑完，代价只是 C 不再
         * 逐字节等同原厂（缓冲区落在内部 RAM），这一点在下面明着记进结果串。 */
        Serial.println("[probe] C heap_caps_malloc SPIRAM failed -> falling back to internal RAM");
        s_fac_buffer = (uint8_t*)heap_caps_malloc(FAC_BUFLEN, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        psram_buf = false;
    }
    if (!s_fac_buffer) {
        Serial.println("[probe] C no buffer at all");
        if (fail_reason) *fail_reason = "no-buffer";
        return false;
    }
    if (!psram_buf) Serial.println("[probe] C NOTE buffer=internal, not byte-identical to factory");

    uint32_t reset_ms = 0;
    if (!fac_epd_init(&reset_ms)) {
        Serial.printf("[probe] C init fail %s swreset-busy%lums\n",
                      s_fac_fail, (unsigned long)reset_ms);
        if (fail_reason) *fail_reason = s_fac_fail;
        return false;
    }
    Serial.printf("[probe] C init ok swreset-busy%lums spi40M buf=%s\n",
                  (unsigned long)reset_ms, psram_buf ? "psram" : "internal");

#if FAC_SOLID_BLACK_TEST
    memset(s_fac_buffer, 0x00, FAC_BUFLEN);
    Serial.println("[probe] C frame solid-black");
#else
    fac_fill_digit(digit);
    Serial.printf("[probe] C frame digit%d\n", digit);
#endif

    /* 原文 EPD_Display()：只有 0x24 + writeBytes + TurnOnDisplay，不重设窗口/光标 */
    if (!fac_send_command(0x24)) { if (fail_reason) *fail_reason = s_fac_fail; return false; }
    if (!fac_write_bytes(s_fac_buffer, FAC_BUFLEN)) { if (fail_reason) *fail_reason = s_fac_fail; return false; }
    if (!fac_turn_on_display(busy_ms)) { if (fail_reason) *fail_reason = s_fac_fail; return false; }

    return true;
}

bool epd_factory_display_frame(const uint8_t* frame, size_t len, uint32_t* busy_ms, const char** fail_reason)
{
    s_fac_fail = "";
    if (busy_ms) *busy_ms = 0;
    if (!frame || len != FAC_BUFLEN) {
        if (fail_reason) *fail_reason = "bad-frame";
        return false;
    }

    fac_gpio_init();
    if (!fac_spi_port_init()) { if (fail_reason) *fail_reason = s_fac_fail; return false; }

    bool psram_buf = true;
    if (!s_fac_buffer) {
        s_fac_buffer = (uint8_t*)heap_caps_malloc(FAC_BUFLEN, MALLOC_CAP_SPIRAM);
    }
    if (!s_fac_buffer) {
        Serial.println("[probe] UI heap_caps_malloc SPIRAM failed -> falling back to internal RAM");
        s_fac_buffer = (uint8_t*)heap_caps_malloc(FAC_BUFLEN, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        psram_buf = false;
    }
    if (!s_fac_buffer) {
        Serial.println("[probe] UI no buffer at all");
        if (fail_reason) *fail_reason = "no-buffer";
        return false;
    }

    uint32_t reset_ms = 0;
    if (!fac_epd_init(&reset_ms)) {
        Serial.printf("[probe] UI init fail %s swreset-busy%lums\n",
                      s_fac_fail, (unsigned long)reset_ms);
        if (fail_reason) *fail_reason = s_fac_fail;
        return false;
    }
    Serial.printf("[probe] UI init ok swreset-busy%lums spi40M buf=%s\n",
                  (unsigned long)reset_ms, psram_buf ? "psram" : "internal");

    memcpy(s_fac_buffer, frame, FAC_BUFLEN);

    if (!fac_send_command(0x24)) { if (fail_reason) *fail_reason = s_fac_fail; return false; }
    if (!fac_write_bytes(s_fac_buffer, FAC_BUFLEN)) { if (fail_reason) *fail_reason = s_fac_fail; return false; }
    if (!fac_turn_on_display(busy_ms)) { if (fail_reason) *fail_reason = s_fac_fail; return false; }
    return true;
}

bool epd_factory_run(uint32_t* busy_ms, const char** fail_reason)
{
    return epd_factory_run_digit(3, busy_ms, fail_reason);
}

/* ============================================================================
 * 局部刷新（M1）
 *
 * 命令序列同样照抄原文，不做「等价改写」——
 *   EPD_Init_Partial()          原文 297-328 行
 *   EPD_DisplayPartBaseImage()  原文 287-295 行
 *   EPD_DisplayPart()           原文 330-335 行
 *   EPD_TurnOnDisplayPart()     原文 229-234 行（0x22 0xCF，注意不是全刷的 0xC7）
 *
 * 【与全刷路径的结构差异】全刷每次都 init→刷→release，是一次性的。局刷不行：
 * 0x26 里的基准图必须跨帧存活，SPI/面板会话一旦释放就得从头再来。所以这里是
 * begin / frame×N / end 的会话式接口，中途**不得**调用 epd_factory_release()。
 *
 * 【判据】局刷 BUSY 应该显著短于全刷实测的 1755ms（预期 300~500ms）。如果实测
 * 仍是 ~1755ms，说明 LUT 根本没换掉，不要继续往上叠应用逻辑。
 * ========================================================================== */

static bool s_fac_partial_active = false;

/* 原文 EPD_Init_Partial() 里的硬复位不重设 0x11 entry mode 和 0x44/0x45 窗口，
 * 靠的是复位后寄存器默认值。官方示例这么用且能跑，所以默认照抄（=0）。
 *
 * 但复位后 0x11 默认是 0x03（X+/Y+），而 fac_epd_init() 设的是 0x01（X+/Y-）——
 * 万一这块板确实需要重设，现象会是「局刷后画面上下翻转或错行」。置 1 即可试，
 * 这是一个明确的单变量开关，不要靠猜改别处。 */
#define FAC_PARTIAL_REAPPLY_WINDOW 0

static bool fac_init_partial(void)
{
    fac_set_rst(1);
    vTaskDelay(pdMS_TO_TICKS(50));
    fac_set_rst(0);
    vTaskDelay(pdMS_TO_TICKS(20));
    fac_set_rst(1);
    vTaskDelay(pdMS_TO_TICKS(50));

    if (!fac_read_busy(nullptr)) return false;
    if (!fac_set_lut(FAC_WF_PARTIAL_1IN54)) return false;

    if (!fac_send_command(0x37)) return false;
    static const uint8_t opt37[10] = { 0x00, 0x00, 0x00, 0x00, 0x00,
                                       0x40, 0x00, 0x00, 0x00, 0x00 };
    for (int i = 0; i < 10; i++) {
        if (!fac_send_data(opt37[i])) return false;
    }

    if (!fac_send_command(0x3C) || !fac_send_data(0x80)) return false;   /* border */

#if FAC_PARTIAL_REAPPLY_WINDOW
    if (!fac_send_command(0x11) || !fac_send_data(0x01)) return false;
    if (!fac_set_windows(0, FAC_WIDTH - 1, FAC_HEIGHT - 1, 0)) return false;
    if (!fac_set_cursor(0, FAC_HEIGHT - 1)) return false;
#endif

    if (!fac_send_command(0x22) || !fac_send_data(0xC0)) return false;
    if (!fac_send_command(0x20)) return false;
    return fac_read_busy(nullptr);
}

static bool fac_turn_on_display_part(uint32_t* busy_ms)
{
    if (!fac_send_command(0x22) || !fac_send_data(0xCF)) return false;
    if (!fac_send_command(0x20)) return false;
    return fac_read_busy(busy_ms);
}

/**
 * 开启一次局刷会话：全刷初始化 → 切局刷 LUT → 把 base_frame 同时写进 0x24 和 0x26。
 *
 * base_frame 必须与**屏上当前画面完全一致**，否则 0x26 里的基准图与实际显示对不上，
 * 后续每一帧局刷都会带出鬼影。实际用法是：先 bsp_ui_fb_flush_full() 出背景，
 * 再拿同一份 fb 调这里。
 */
bool epd_factory_partial_begin(const uint8_t* base_frame, uint32_t* busy_ms,
                               const char** fail_reason)
{
    s_fac_fail = "";
    if (busy_ms) *busy_ms = 0;
    if (!base_frame) {
        if (fail_reason) *fail_reason = "bad-frame";
        return false;
    }

    /* 上一次会话没收干净就先收掉，否则 spi_bus_initialize 会拿到 INVALID_STATE，
     * 40MHz devcfg 落不下去，会话从一开始就不是参考配置。 */
    epd_factory_release();
    s_fac_partial_active = false;

    fac_gpio_init();
    if (!fac_spi_port_init()) { if (fail_reason) *fail_reason = s_fac_fail; return false; }

    if (!s_fac_buffer) {
        s_fac_buffer = (uint8_t*)heap_caps_malloc(FAC_BUFLEN, MALLOC_CAP_SPIRAM);
    }
    if (!s_fac_buffer) {
        s_fac_buffer = (uint8_t*)heap_caps_malloc(FAC_BUFLEN, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    }
    if (!s_fac_buffer) {
        if (fail_reason) *fail_reason = "no-buffer";
        return false;
    }

    uint32_t reset_ms = 0;
    if (!fac_epd_init(&reset_ms)) {
        Serial.printf("[part] begin: full-init fail %s\n", s_fac_fail);
        if (fail_reason) *fail_reason = s_fac_fail;
        return false;
    }

    if (!fac_init_partial()) {
        Serial.printf("[part] begin: init-partial fail %s\n", s_fac_fail);
        if (fail_reason) *fail_reason = s_fac_fail;
        return false;
    }

    /* DisplayPartBaseImage：0x24 与 0x26 各写一遍同一帧。
     * 原文不在两次写之间重设光标 —— 照抄。ui.cpp 的 epd_display_buffer() 曾经
     * 需要重设，是因为它紧接在别的写入之后；这里紧跟 init，计数器还在原位。 */
    memcpy(s_fac_buffer, base_frame, FAC_BUFLEN);
    if (!fac_send_command(0x24) || !fac_write_bytes(s_fac_buffer, FAC_BUFLEN)) {
        if (fail_reason) *fail_reason = s_fac_fail; return false;
    }
    if (!fac_send_command(0x26) || !fac_write_bytes(s_fac_buffer, FAC_BUFLEN)) {
        if (fail_reason) *fail_reason = s_fac_fail; return false;
    }
    if (!fac_turn_on_display(busy_ms)) {   /* 基准图这一次走 0xC7，同原文 */
        if (fail_reason) *fail_reason = s_fac_fail; return false;
    }

    s_fac_partial_active = true;
    Serial.printf("[part] begin ok swreset-busy%lums base-busy%lums\n",
                  (unsigned long)reset_ms, (unsigned long)(busy_ms ? *busy_ms : 0));
    return true;
}

/** 单帧局刷：整帧 5000 字节写 0x24，再 0x22 0xCF。不做窗口寻址。 */
bool epd_factory_partial_frame(const uint8_t* frame, uint32_t* busy_ms,
                               const char** fail_reason)
{
    s_fac_fail = "";
    if (busy_ms) *busy_ms = 0;
    if (!s_fac_partial_active) {
        if (fail_reason) *fail_reason = "no-session";
        return false;
    }
    if (!frame) {
        if (fail_reason) *fail_reason = "bad-frame";
        return false;
    }

    memcpy(s_fac_buffer, frame, FAC_BUFLEN);
    if (!fac_send_command(0x24) || !fac_write_bytes(s_fac_buffer, FAC_BUFLEN)) {
        if (fail_reason) *fail_reason = s_fac_fail; return false;
    }
    if (!fac_turn_on_display_part(busy_ms)) {
        if (fail_reason) *fail_reason = s_fac_fail; return false;
    }
    return true;
}

void epd_factory_partial_end(void)
{
    s_fac_partial_active = false;
    epd_factory_release();
}

bool epd_factory_partial_active(void) { return s_fac_partial_active; }
