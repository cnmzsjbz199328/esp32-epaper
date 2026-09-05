#include "audio.h"

#include <Wire.h>
#include <math.h>
#include <string.h>
#include "driver/i2s.h"
#include "es8311.h"
#include "bsp_pins.h"

#define BSP_ES8311_ADDR      0x18

static const i2s_port_t I2S_PORT = I2S_NUM_0;
static const int MCLK_MULTIPLE = 256;

static es8311_handle_t s_es = nullptr;
static uint32_t s_rate = 0;
static bool s_audio_ready = false;
static bool s_mic_ready = false;
static uint8_t s_chip_id[2] = {0, 0};

static void codec_mute(bool mute)
{
    if (!s_es) return;
    const esp_err_t err = es8311_voice_mute(s_es, mute);
    if (err != ESP_OK) {
        Serial.printf("[audio] codec mute(%d) failed: %s\n", mute ? 1 : 0,
                      esp_err_to_name(err));
    }
}

static bool read_chip_reg(uint8_t reg, uint8_t* out)
{
    Wire.beginTransmission(BSP_ES8311_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)BSP_ES8311_ADDR, 1) != 1) return false;
    *out = Wire.read();
    return true;
}

static void read_chip_id(void)
{
    (void)read_chip_reg(0xFD, &s_chip_id[0]);
    (void)read_chip_reg(0xFE, &s_chip_id[1]);
}

static bool codec_init(uint32_t sample_rate, int volume)
{
    Wire.begin(BSP_PIN_I2C_SDA, BSP_PIN_I2C_SCL, BSP_I2C_FREQ);

    Wire.beginTransmission(BSP_ES8311_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.printf("[audio] ES8311 @0x%02X no ACK\n", BSP_ES8311_ADDR);
        return false;
    }
    read_chip_id();

    s_es = es8311_create(I2C_NUM_0, BSP_ES8311_ADDR);
    if (!s_es) {
        Serial.println("[audio] es8311_create failed");
        return false;
    }

    es8311_clock_config_t clk = {};
    clk.mclk_inverted = false;
    clk.sclk_inverted = false;
    clk.mclk_from_mclk_pin = true;
    clk.mclk_frequency = sample_rate * MCLK_MULTIPLE;
    clk.sample_frequency = sample_rate;

    if (es8311_init(s_es, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) {
        Serial.println("[audio] es8311_init failed");
        return false;
    }
    es8311_sample_frequency_config(s_es, sample_rate * MCLK_MULTIPLE, sample_rate);
    es8311_voice_volume_set(s_es, volume, nullptr);
    es8311_voice_fade(s_es, ES8311_FADE_512LRCK);
    codec_mute(true);
    es8311_microphone_config(s_es, false);
    es8311_microphone_gain_set(s_es, ES8311_MIC_GAIN_30DB);
    return true;
}

static bool i2s_init(uint32_t sample_rate)
{
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);
    cfg.sample_rate = sample_rate;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 8;
    cfg.dma_buf_len = 256;
    cfg.use_apll = true;
    cfg.tx_desc_auto_clear = true;
    cfg.fixed_mclk = sample_rate * MCLK_MULTIPLE;
    cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    esp_err_t err = i2s_driver_install(I2S_PORT, &cfg, 0, nullptr);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        Serial.printf("[audio] i2s_driver_install failed: %s\n", esp_err_to_name(err));
        return false;
    }

    i2s_pin_config_t pins = {};
    pins.mck_io_num = BSP_PIN_I2S_MCLK;
    pins.bck_io_num = BSP_PIN_I2S_SCLK;
    pins.ws_io_num = BSP_PIN_I2S_LRCK;
    pins.data_out_num = BSP_PIN_I2S_DSDIN;
    pins.data_in_num = BSP_PIN_I2S_ASDOUT;

    if (i2s_set_pin(I2S_PORT, &pins) != ESP_OK) {
        Serial.println("[audio] i2s_set_pin failed");
        i2s_driver_uninstall(I2S_PORT);
        return false;
    }
    i2s_zero_dma_buffer(I2S_PORT);
    return true;
}

bool bsp_audio_init(uint32_t sample_rate, int volume)
{
    if (s_audio_ready) return true;

    /* BSP_PIN_PA_EN (IO42) is set up once in bsp_board_init() and never
     * touched again here. Waveshare's own board profile for this exact
     * board (codec_board/board_cfg.h, "S3_ePaper_1_54") only lists IO46 as
     * the ES8311 driver's PA pin -- IO42 isn't part of the audio driver at
     * all in their reference, it's a one-time board power-domain pin
     * (see bsp_board_init()'s comment). Re-driving it here on every
     * init/amp-toggle was redundant (always LOW -> LOW, no functional
     * effect) and has been removed. */
    pinMode(BSP_PIN_PA_CTRL, OUTPUT);
    digitalWrite(BSP_PIN_PA_CTRL, LOW);

    /* Start MCLK/I2S before opening the codec.  ES8311 register setup is more
     * reliable when its expected master clock is already present. */
    if (!i2s_init(sample_rate)) return false;
    if (!codec_init(sample_rate, volume)) {
        i2s_driver_uninstall(I2S_PORT);
        return false;
    }

    s_rate = sample_rate;
    s_audio_ready = true;
    Serial.printf("[audio] ES8311 ready: %lu Hz vol%d id%02X%02X\n",
                  (unsigned long)sample_rate, volume, s_chip_id[0], s_chip_id[1]);
    return true;
}

void bsp_audio_deinit(void)
{
    if (!s_audio_ready) return;
    bsp_audio_amp(false);
    i2s_driver_uninstall(I2S_PORT);
    if (s_es) {
        es8311_delete(s_es);
        s_es = nullptr;
    }
    s_audio_ready = false;
    s_mic_ready = false;
}

void bsp_audio_set_volume(int volume)
{
    if (s_es) es8311_voice_volume_set(s_es, volume, nullptr);
}

void bsp_audio_amp(bool on)
{
    if (on) {
        /* Keep the external PA off while clearing stale DMA data and
         * unmuting the codec.  The codec-side ramp is configured in init. */
        digitalWrite(BSP_PIN_PA_CTRL, LOW);
        codec_mute(true);
        i2s_zero_dma_buffer(I2S_PORT);
        delay(2);
        codec_mute(false);
        digitalWrite(BSP_PIN_PA_CTRL, HIGH);
    } else {
        codec_mute(true);
        digitalWrite(BSP_PIN_PA_CTRL, LOW);
        i2s_zero_dma_buffer(I2S_PORT);
    }
}

bool bsp_audio_ready(void)
{
    return s_audio_ready;
}

void bsp_audio_chip_id(uint8_t* id1, uint8_t* id2)
{
    if (id1) *id1 = s_chip_id[0];
    if (id2) *id2 = s_chip_id[1];
}

size_t bsp_audio_write_mono(const int16_t* samples, size_t count)
{
    if (!s_audio_ready || !samples) return 0;

    static int16_t stereo[256 * 2];
    size_t done = 0;
    while (done < count) {
        size_t n = count - done;
        if (n > 256) n = 256;
        for (size_t i = 0; i < n; i++) {
            stereo[i * 2] = samples[done + i];
            stereo[i * 2 + 1] = samples[done + i];
        }
        size_t written = 0;
        const esp_err_t err = i2s_write(I2S_PORT, stereo, n * 2 * sizeof(int16_t),
                                        &written, portMAX_DELAY);
        if (err != ESP_OK) {
            Serial.printf("[audio] i2s_write failed: %s\n", esp_err_to_name(err));
            break;
        }
        done += written / (2 * sizeof(int16_t));
        if (written == 0) break;
    }
    return done;
}

void bsp_audio_tone(uint32_t freq_hz, uint32_t duration_ms, uint8_t amplitude_pct)
{
    if (!s_audio_ready || s_rate == 0) return;
    if (amplitude_pct > 100) amplitude_pct = 100;

    const int16_t amp = (int16_t)(32767.0f * amplitude_pct / 100.0f);
    const size_t total = (size_t)((uint64_t)s_rate * duration_ms / 1000);
    static int16_t buf[256];
    float phase = 0.0f;
    const float step = 2.0f * PI * freq_hz / s_rate;

    bsp_audio_amp(true);
    for (size_t done = 0; done < total; ) {
        size_t n = total - done;
        if (n > 256) n = 256;
        for (size_t i = 0; i < n; i++) {
            buf[i] = (int16_t)(amp * sinf(phase));
            phase += step;
            if (phase > 2.0f * PI) phase -= 2.0f * PI;
        }
        bsp_audio_write_mono(buf, n);
        done += n;
    }
    memset(buf, 0, sizeof(buf));
    bsp_audio_write_mono(buf, 256);
    bsp_audio_amp(false);
}

void bsp_audio_beep_startup(void)
{
    bsp_audio_tone(523, 120);
    bsp_audio_tone(659, 120);
    bsp_audio_tone(784, 200);
}

bool bsp_mic_init(uint8_t gain_db)
{
    if (s_mic_ready) return true;
    if (!bsp_audio_ready() && !bsp_audio_init(16000, 75)) {
        Serial.println("[mic] audio init required for shared I2S");
        return false;
    }
    es8311_mic_gain_t gain = ES8311_MIC_GAIN_24DB;
    if (gain_db >= 42) gain = ES8311_MIC_GAIN_42DB;
    else if (gain_db >= 36) gain = ES8311_MIC_GAIN_36DB;
    else if (gain_db >= 30) gain = ES8311_MIC_GAIN_30DB;
    else if (gain_db >= 24) gain = ES8311_MIC_GAIN_24DB;
    else if (gain_db >= 18) gain = ES8311_MIC_GAIN_18DB;
    else if (gain_db >= 12) gain = ES8311_MIC_GAIN_12DB;
    else if (gain_db >= 6) gain = ES8311_MIC_GAIN_6DB;
    else gain = ES8311_MIC_GAIN_0DB;
    if (s_es) {
        es8311_microphone_config(s_es, false);
        es8311_microphone_gain_set(s_es, gain);
    }
    s_mic_ready = true;
    Serial.printf("[mic] ES8311 ADC ready: gain request %udB\n", gain_db);
    return true;
}

bool bsp_mic_ready(void)
{
    return s_mic_ready;
}

size_t bsp_mic_read(int16_t* out, size_t max_samples, uint32_t timeout_ms)
{
    if (!s_mic_ready || !out) return 0;

    static int16_t stereo[256 * 2];
    size_t got = 0;
    while (got < max_samples) {
        size_t want = max_samples - got;
        if (want > 256) want = 256;
        size_t bytes_read = 0;
        if (i2s_read(I2S_PORT, stereo, want * 2 * sizeof(int16_t),
                     &bytes_read, pdMS_TO_TICKS(timeout_ms)) != ESP_OK) break;
        if (bytes_read == 0) break;
        const size_t frames = bytes_read / (2 * sizeof(int16_t));
        for (size_t i = 0; i < frames; i++) out[got + i] = stereo[i * 2];
        got += frames;
        if (frames < want) break;
    }
    return got;
}

uint16_t bsp_mic_peak_level(void)
{
    if (!s_mic_ready) return 0;
    static int16_t buf[256];
    const size_t n = bsp_mic_read(buf, 256, 100);
    int32_t peak = 0;
    for (size_t i = 0; i < n; i++) {
        int32_t v = buf[i] < 0 ? -(int32_t)buf[i] : buf[i];
        if (v > peak) peak = v;
    }
    return (uint16_t)(peak > 32767 ? 32767 : peak);
}
