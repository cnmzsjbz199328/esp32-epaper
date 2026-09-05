#include "story_audio.h"

#include <Arduino.h>
#include <SD_MMC.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "audio.h"
#include "sdcard.h"
#include "../../../lib/nvs_settings/nvs_settings.h"

namespace {

constexpr uint32_t AUDIO_SAMPLE_RATE = 16000;
constexpr size_t AUDIO_BLOCK_SAMPLES = 256;
constexpr size_t COMMAND_PATH_LEN = 128;
constexpr size_t ADPCM_BLOCK_SAMPLES = 256;

enum audio_source_t {
    AUDIO_SOURCE_NONE,
    AUDIO_SOURCE_SD_WAV,
    AUDIO_SOURCE_ROM_PCM,
    AUDIO_SOURCE_ROM_ADPCM,
};

TaskHandle_t s_task = nullptr;
volatile uint32_t s_command_version = 0;
volatile bool s_pause_requested = false;
volatile bool s_playing = false;
volatile bool s_paused = false;
char s_command_path[COMMAND_PATH_LEN] = {};
audio_source_t s_command_source = AUDIO_SOURCE_NONE;
const int16_t* s_command_samples = nullptr;
size_t s_command_sample_count = 0;
uint32_t s_command_sample_rate = 0;
const uint8_t* s_command_data = nullptr;
size_t s_command_data_length = 0;
portMUX_TYPE s_command_mux = portMUX_INITIALIZER_UNLOCKED;

const int8_t IMA_INDEX_TABLE[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
const int16_t IMA_STEP_TABLE[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
    143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
    494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
    1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
    4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767,
};

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
    File file = SD_MMC.open(path, FILE_READ);
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
        remaining -= written * sizeof(int16_t);
        memset(samples, 0, sizeof(samples));
    }

    bsp_audio_amp(false);
    memset(samples, 0, sizeof(samples));
    file.close();
    s_playing = false;
    s_paused = false;

    Serial.printf("[story-audio] scene audio %s bytes=%u/%u %s\n", path,
                  (unsigned)played, (unsigned)info.data_length,
                  command_cancelled(version) ? "cancelled" : "done");
}

void play_pcm(const int16_t* samples, size_t sample_count, uint32_t sample_rate,
              uint32_t version)
{
    if (!samples || sample_count == 0 || sample_rate != AUDIO_SAMPLE_RATE) {
        Serial.println("[story-audio] unsupported ROM PCM format");
        return;
    }

    int16_t block[AUDIO_BLOCK_SAMPLES] = {};
    size_t offset = 0;
    s_playing = true;
    s_paused = false;
    bsp_audio_amp(true);
    while (offset < sample_count && !command_cancelled(version)) {
        wait_while_paused(version);
        if (command_cancelled(version)) break;
        size_t count = sample_count - offset;
        if (count > AUDIO_BLOCK_SAMPLES) count = AUDIO_BLOCK_SAMPLES;
        memcpy(block, samples + offset, count * sizeof(int16_t));
        const size_t written = bsp_audio_write_mono(block, count);
        if (written == 0) break;
        offset += written;
    }
    bsp_audio_amp(false);
    memset(block, 0, sizeof(block));
    s_playing = false;
    s_paused = false;
    Serial.printf("[story-audio] ROM PCM samples=%u/%u %s\n", (unsigned)offset,
                  (unsigned)sample_count,
                  command_cancelled(version) ? "cancelled" : "done");
}

int16_t ima_decode_nibble(int16_t predictor, int& index, uint8_t code)
{
    const int step = IMA_STEP_TABLE[index];
    int difference = step >> 3;
    if (code & 1) difference += step >> 2;
    if (code & 2) difference += step >> 1;
    if (code & 4) difference += step;
    int next = (int)predictor + ((code & 8) ? -difference : difference);
    if (next < -32768) next = -32768;
    if (next > 32767) next = 32767;
    index += IMA_INDEX_TABLE[code & 7];
    if (index < 0) index = 0;
    if (index > 88) index = 88;
    return (int16_t)next;
}

void play_adpcm(const uint8_t* data, size_t data_length, size_t sample_count,
                uint32_t sample_rate, uint32_t version)
{
    if (!data || data_length < 4 || sample_count == 0 || sample_rate != AUDIO_SAMPLE_RATE) {
        Serial.println("[story-audio] unsupported ROM ADPCM format");
        return;
    }

    /* tools/build_story_demo_assets.py encodes one continuous IMA ADPCM
     * stream per scene: a single 4-byte header (predictor, index) followed
     * by nibble codes for every remaining sample, with predictor/index
     * carried through for the whole scene rather than reset every
     * ADPCM_BLOCK_SAMPLES. (An earlier per-block-reset scheme forced the
     * adaptive step back to its minimum every 16ms, which measured ~17dB
     * round-trip SNR and was audible as a persistent grainy noise texture;
     * a continuous stream measures ~27dB SNR on the same source.) The loop
     * below still streams decoded samples out in ADPCM_BLOCK_SAMPLES
     * chunks for I2S/pause responsiveness -- that chunking is purely a
     * playback detail now and must not reset predictor/index or realign
     * the nibble stream between chunks.
     *
     * Each byte packs two codes as (earlier_sample | later_sample << 4),
     * i.e. the low nibble is the earlier sample; nibble codes start right
     * after the 4-byte header and cover global sample positions 1..N-1
     * (position 0 is the header's predictor itself, not nibble-coded).
     */
    const size_t nibble_bytes = sample_count > 0 ? sample_count / 2 : 0;
    if (data_length < 4 + nibble_bytes) {
        Serial.println("[story-audio] ROM ADPCM payload truncated");
        return;
    }
    int16_t predictor = (int16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
    int index = data[2];
    if (index > 88) {
        Serial.println("[story-audio] ROM ADPCM header invalid");
        return;
    }

    int16_t block[ADPCM_BLOCK_SAMPLES] = {};
    size_t sample_offset = 0;
    s_playing = true;
    s_paused = false;
    bsp_audio_amp(true);

    while (sample_offset < sample_count && !command_cancelled(version)) {
        size_t block_count = sample_count - sample_offset;
        if (block_count > ADPCM_BLOCK_SAMPLES) block_count = ADPCM_BLOCK_SAMPLES;

        size_t i = 0;
        if (sample_offset == 0) {
            block[0] = predictor; /* global sample 0: the raw header value */
            i = 1;
        }
        for (; i < block_count; i++) {
            const size_t global_pos = sample_offset + i; /* 1-based nibble-stream position */
            const uint8_t packed = data[4 + (global_pos - 1) / 2];
            const uint8_t code = (global_pos & 1) ? (packed & 0x0F) : (packed >> 4);
            predictor = ima_decode_nibble(predictor, index, code);
            block[i] = predictor;
        }

        wait_while_paused(version);
        if (command_cancelled(version)) break;
        size_t block_offset = 0;
        while (block_offset < block_count && !command_cancelled(version)) {
            const size_t written = bsp_audio_write_mono(block + block_offset,
                                                         block_count - block_offset);
            if (written == 0) break;
            block_offset += written;
        }
        sample_offset += block_offset;
        if (block_offset < block_count) break;
    }

    bsp_audio_amp(false);
    memset(block, 0, sizeof(block));
    s_playing = false;
    s_paused = false;
    Serial.printf("[story-audio] ROM ADPCM samples=%u/%u bytes=%u/%u %s\n",
                  (unsigned)sample_offset, (unsigned)sample_count,
                  (unsigned)(4 + nibble_bytes), (unsigned)data_length,
                  command_cancelled(version) ? "cancelled" :
                  (sample_offset == sample_count ? "done" : "short"));
}

void story_audio_task(void*)
{
    uint32_t handled_version = s_command_version;
    for (;;) {
        const uint32_t version = s_command_version;
        if (version != handled_version) {
            char path[COMMAND_PATH_LEN] = {};
            audio_source_t source;
            const int16_t* samples;
            size_t sample_count;
            uint32_t sample_rate;
            const uint8_t* data;
            size_t data_length;
            portENTER_CRITICAL(&s_command_mux);
            snprintf(path, sizeof(path), "%s", s_command_path);
            source = s_command_source;
            samples = s_command_samples;
            sample_count = s_command_sample_count;
            sample_rate = s_command_sample_rate;
            data = s_command_data;
            data_length = s_command_data_length;
            portEXIT_CRITICAL(&s_command_mux);
            handled_version = version;
            if (bsp_audio_ready()) {
                if (source == AUDIO_SOURCE_SD_WAV && path[0] != '\0') {
                    play_wav(path, version);
                } else if (source == AUDIO_SOURCE_ROM_PCM) {
                    play_pcm(samples, sample_count, sample_rate, version);
                } else if (source == AUDIO_SOURCE_ROM_ADPCM) {
                    play_adpcm(data, data_length, sample_count, sample_rate, version);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void request_command(audio_source_t source, const char* path,
                     const int16_t* samples, const uint8_t* data, size_t data_length,
                     size_t sample_count, uint32_t sample_rate)
{
    portENTER_CRITICAL(&s_command_mux);
    if (path) snprintf(s_command_path, sizeof(s_command_path), "%s", path);
    else s_command_path[0] = '\0';
    s_command_source = source;
    s_command_samples = samples;
    s_command_data = data;
    s_command_data_length = data_length;
    s_command_sample_count = sample_count;
    s_command_sample_rate = sample_rate;
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

    char base[COMMAND_PATH_LEN] = {};
    snprintf(base, sizeof(base), "%s", story_path);
    char* extension = strrchr(base, '.');
    if (extension && strcmp(extension, ".fvid") == 0) *extension = '\0';

    char path[COMMAND_PATH_LEN] = {};
    snprintf(path, sizeof(path), "%s/audio/%03d.wav", base, scene_index);
    request_command(AUDIO_SOURCE_SD_WAV, path, nullptr, nullptr, 0, 0, 0);
    Serial.printf("[story-audio] queue scene=%d path=%s\n", scene_index, path);
    return true;
}

bool story_audio_play_rom(const int16_t* samples, size_t sample_count, uint32_t sample_rate)
{
    story_audio_init();
    if (!s_task || !bsp_audio_ready() || !samples || sample_count == 0) return false;
    request_command(AUDIO_SOURCE_ROM_PCM, nullptr, samples, nullptr, 0,
                    sample_count, sample_rate);
    Serial.printf("[story-audio] queue ROM PCM samples=%u rate=%lu\n",
                  (unsigned)sample_count, (unsigned long)sample_rate);
    return true;
}

bool story_audio_play_rom_adpcm(const uint8_t* data, size_t data_length,
                                size_t sample_count, uint32_t sample_rate)
{
    story_audio_init();
    if (!s_task || !bsp_audio_ready() || !data || data_length < 4 || sample_count == 0) {
        return false;
    }
    request_command(AUDIO_SOURCE_ROM_ADPCM, nullptr, nullptr, data, data_length,
                    sample_count, sample_rate);
    Serial.printf("[story-audio] queue ROM ADPCM samples=%u bytes=%u rate=%lu\n",
                  (unsigned)sample_count, (unsigned)data_length,
                  (unsigned long)sample_rate);
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
