#include "story_audio.h"

#include <Arduino.h>
#include <SD_MMC.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "audio.h"
#include "sdcard.h"
#include "../../../lib/file_storage/file_storage_internal.h"
#include "../../../lib/nvs_settings/nvs_settings.h"

namespace {

constexpr uint32_t AUDIO_SAMPLE_RATE = 16000;
constexpr size_t AUDIO_BLOCK_SAMPLES = 256;
constexpr size_t COMMAND_PATH_LEN = 128;
enum audio_source_t {
    AUDIO_SOURCE_NONE,
    AUDIO_SOURCE_SD_WAV,
};

TaskHandle_t s_task = nullptr;
volatile uint32_t s_command_version = 0;
volatile bool s_pause_requested = false;
volatile bool s_playing = false;
volatile bool s_paused = false;
volatile uint32_t s_total_samples = 0;
volatile uint32_t s_played_samples = 0;
volatile uint32_t s_current_sample_rate = 0;
char s_command_path[COMMAND_PATH_LEN] = {};
audio_source_t s_command_source = AUDIO_SOURCE_NONE;
portMUX_TYPE s_command_mux = portMUX_INITIALIZER_UNLOCKED;

File open_sd_file(const char* normalized_path)
{
    char sd_path[COMMAND_PATH_LEN] = {};
    if (!file_storage::internal::to_sd_path(normalized_path, sd_path, sizeof(sd_path))) return {};
    return SD_MMC.open(sd_path, FILE_READ);
}

struct wav_info_t {
    uint32_t sample_rate = 0;
    uint16_t channels = 0;
    uint16_t bits_per_sample = 0;
    size_t data_offset = 0;
    size_t data_length = 0;
};

bool read_bytes(File& file, void* data, size_t len)
{
    return file.read(reinterpret_cast<uint8_t*>(data), len) == (int)len;
}

uint16_t le16(const uint8_t* p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

uint32_t le32(const uint8_t* p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

bool parse_wav(File& file, wav_info_t* out)
{
    if (!out || !file) return false;
    const size_t file_size = file.size();
    if (file_size < 12) return false;

    uint8_t riff[12] = {};
    if (!file.seek(0) || !read_bytes(file, riff, sizeof(riff)) ||
        memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
        return false;
    }

    bool have_fmt = false;
    bool have_data = false;
    size_t offset = 12;
    while (offset + 8 <= file_size) {
        uint8_t chunk_header[8] = {};
        if (!file.seek(offset) || !read_bytes(file, chunk_header, sizeof(chunk_header))) {
            return false;
        }
        const uint32_t chunk_length = le32(chunk_header + 4);
        const size_t payload = offset + 8;
        if (payload > file_size || chunk_length > file_size - payload) return false;

        if (memcmp(chunk_header, "fmt ", 4) == 0) {
            if (chunk_length < 16) return false;
            uint8_t fmt[16] = {};
            if (!file.seek(payload) || !read_bytes(file, fmt, sizeof(fmt))) return false;
            if (le16(fmt) != 1) return false; /* PCM only for the first player. */
            out->channels = le16(fmt + 2);
            out->sample_rate = le32(fmt + 4);
            out->bits_per_sample = le16(fmt + 14);
            have_fmt = true;
        } else if (memcmp(chunk_header, "data", 4) == 0 && !have_data) {
            out->data_offset = payload;
            out->data_length = chunk_length;
            have_data = true;
        }

        const size_t padded = (size_t)chunk_length + (chunk_length & 1U);
        if (padded > file_size - payload) return false;
        offset = payload + padded;
    }

    return have_fmt && have_data && out->channels == 1 &&
           out->sample_rate == AUDIO_SAMPLE_RATE && out->bits_per_sample == 16;
}

bool command_cancelled(uint32_t version)
{
    return version != s_command_version;
}

void wait_while_paused(uint32_t version)
{
    if (!s_pause_requested) return;
    bsp_audio_amp(false);
    s_paused = true;
    while (s_pause_requested && !command_cancelled(version)) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    s_paused = false;
    if (!command_cancelled(version)) bsp_audio_amp(true);
}

void play_wav(const char* path, uint32_t version)
{
    File file = open_sd_file(path);
    if (!file) {
        Serial.printf("[story-audio] open failed: %s\n", path);
        return;
    }

    wav_info_t info;
    if (!parse_wav(file, &info)) {
        Serial.printf("[story-audio] unsupported WAV (need mono 16k/16bit PCM): %s\n", path);
        file.close();
        return;
    }
    if (!file.seek(info.data_offset)) {
        file.close();
        return;
    }

    int16_t samples[AUDIO_BLOCK_SAMPLES] = {};
    size_t remaining = info.data_length & ~((size_t)1);
    size_t played = 0;
    s_total_samples = info.data_length / sizeof(int16_t);
    s_played_samples = 0;
    s_current_sample_rate = info.sample_rate;
    s_playing = true;
    s_paused = false;
    bsp_audio_amp(true);

    while (remaining > 0 && !command_cancelled(version)) {
        wait_while_paused(version);
        if (command_cancelled(version)) break;

        size_t bytes_to_read = remaining;
        if (bytes_to_read > sizeof(samples)) bytes_to_read = sizeof(samples);
        bytes_to_read &= ~((size_t)1);
        const size_t got = file.read(reinterpret_cast<uint8_t*>(samples), bytes_to_read);
        if (got == 0) break;

        const size_t sample_count = got / sizeof(int16_t);
        const size_t written = bsp_audio_write_mono(samples, sample_count);
        if (written == 0) break;
        played += written * sizeof(int16_t);
        s_played_samples = played / sizeof(int16_t);
        remaining -= written * sizeof(int16_t);
        memset(samples, 0, sizeof(samples));
    }

    bsp_audio_amp(false);
    memset(samples, 0, sizeof(samples));
    file.close();
    s_playing = false;
    s_paused = false;
    s_total_samples = 0;
    s_played_samples = 0;
    s_current_sample_rate = 0;

    Serial.printf("[story-audio] scene audio %s bytes=%u/%u %s\n", path,
                  (unsigned)played, (unsigned)info.data_length,
                  command_cancelled(version) ? "cancelled" : "done");
}

void story_audio_task(void*)
{
    uint32_t handled_version = s_command_version;
    for (;;) {
        const uint32_t version = s_command_version;
        if (version != handled_version) {
            char path[COMMAND_PATH_LEN] = {};
            audio_source_t source;
            portENTER_CRITICAL(&s_command_mux);
            snprintf(path, sizeof(path), "%s", s_command_path);
            source = s_command_source;
            portEXIT_CRITICAL(&s_command_mux);
            handled_version = version;
            if (bsp_audio_ready()) {
                if (source == AUDIO_SOURCE_SD_WAV && path[0] != '\0') {
                    play_wav(path, version);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void request_command(audio_source_t source, const char* path,
                     const int16_t*, const uint8_t*, size_t,
                     size_t, uint32_t)
{
    portENTER_CRITICAL(&s_command_mux);
    if (path) snprintf(s_command_path, sizeof(s_command_path), "%s", path);
    else s_command_path[0] = '\0';
    s_command_source = source;
    s_pause_requested = false;
    s_command_version++;
    portEXIT_CRITICAL(&s_command_mux);
}

}

void story_audio_init(void)
{
    if (s_task) return;
    const uint8_t volume = ecp::settings::get_volume(80);
    if (!bsp_audio_init(AUDIO_SAMPLE_RATE, volume)) {
        Serial.println("[story-audio] audio init failed; story playback remains visual-only");
        return;
    }
    /* bsp_audio_init() only applies `volume` on the very first call (the
     * codec singleton may already be up from an earlier Settings-page
     * volume preview); re-apply explicitly so the persisted value always
     * wins regardless of init order. */
    bsp_audio_set_volume(volume);
    const BaseType_t result = xTaskCreatePinnedToCore(
        story_audio_task, "story_audio", 4096, nullptr, 2, &s_task, 1);
    if (result != pdPASS) {
        s_task = nullptr;
        Serial.println("[story-audio] task create failed; story playback remains visual-only");
    } else {
        Serial.println("[story-audio] ready: mono 16k/16bit PCM WAV");
    }
}

void story_audio_stop(void)
{
    request_command(AUDIO_SOURCE_NONE, nullptr, nullptr, nullptr, 0, 0, 0);
}

bool story_audio_play_scene(const char* story_path, int scene_index)
{
    if (!story_path || scene_index < 0) return false;
    story_audio_init();
    if (!s_task || !bsp_audio_ready() || !bsp_sd_ready()) return false;

    char path[COMMAND_PATH_LEN] = {};
    if (snprintf(path, sizeof(path), "%s/audio/%03d.wav", story_path, scene_index) >=
        (int)sizeof(path)) return false;
    File file = open_sd_file(path);
    if (!file) {
        Serial.printf("[story-audio] scene audio not found: %s\n", path);
        story_audio_stop();
        return false;
    }
    file.close();
    request_command(AUDIO_SOURCE_SD_WAV, path, nullptr, nullptr, 0, 0, 0);
    Serial.printf("[story-audio] queue scene=%d path=%s\n", scene_index, path);
    return true;
}

bool story_audio_play_opening(const char* story_path, const char* relative_path)
{
    if (!story_path || !relative_path || !relative_path[0]) return false;
    story_audio_init();
    if (!s_task || !bsp_audio_ready() || !bsp_sd_ready()) return false;

    char path[COMMAND_PATH_LEN] = {};
    if (snprintf(path, sizeof(path), "%s/%s", story_path, relative_path) >=
        (int)sizeof(path)) return false;
    File file = open_sd_file(path);
    if (!file) {
        Serial.printf("[story-audio] opening audio not found: %s\n", path);
        story_audio_stop();
        return false;
    }
    file.close();
    request_command(AUDIO_SOURCE_SD_WAV, path, nullptr, nullptr, 0, 0, 0);
    Serial.printf("[story-audio] queue opening path=%s\n", path);
    return true;
}

void story_audio_toggle_pause(void)
{
    if (!s_playing) return;
    s_pause_requested = !s_pause_requested;
    Serial.printf("[story-audio] %s\n", s_pause_requested ? "pause" : "resume");
}

bool story_audio_is_playing(void) { return s_playing; }
bool story_audio_is_paused(void) { return s_paused; }

uint32_t story_audio_remaining_ms(void)
{
    const uint32_t total = s_total_samples;
    const uint32_t played = s_played_samples;
    const uint32_t rate = s_current_sample_rate;
    if (!s_playing || rate == 0 || played >= total) return 0;
    return (uint32_t)(((uint64_t)(total - played) * 1000ULL) / rate);
}
