#include "frame_source.h"

#include <Arduino.h>
#include <SD_MMC.h>
#include <string.h>

#include "bsp.h"
#include "bsp_pins.h"
#include "family_video_assets.h"
#include "sdcard.h"

namespace {

constexpr size_t FVID_HEADER_LEN = 16;
constexpr size_t FVID_RECORD_LEN = 5004;
constexpr uint16_t FVID_VERSION = 1;
static File s_file;
static int s_count = 0;
static bool s_valid = false;

uint16_t read_le16(const uint8_t* p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

bool read_at(size_t offset, uint8_t* out, size_t len)
{
    if (!s_file || !out || !s_file.seek(offset)) return false;
    return s_file.read(out, len) == (int)len;
}

int sd_count(void) { return s_count; }

int sd_hint(int index)
{
    if (index < 0 || index >= s_count) return APP_REFRESH_FULL;
    uint8_t hint = 0;
    return read_at(FVID_HEADER_LEN + (size_t)index * FVID_RECORD_LEN, &hint, 1)
        ? (int)hint : APP_REFRESH_FULL;
}

bool sd_load(int index, uint8_t* framebuffer, size_t len)
{
    if (!s_valid || !framebuffer || len != bsp_ui_fb_len() || index < 0 || index >= s_count) {
        return false;
    }
    const size_t offset = FVID_HEADER_LEN + (size_t)index * FVID_RECORD_LEN;
    uint8_t header[4] = {0};
    if (!read_at(offset, header, sizeof(header)) ||
        !read_at(offset + 4, framebuffer, len)) {
        Serial.printf("[video] SD short read frame=%d\n", index);
        return false;
    }
    return header[0] <= APP_REFRESH_FINAL_FULL;
}

const frame_source_t s_sd = { "SD", sd_count, sd_hint, sd_load };

bool open_fvid(void)
{
    if (!bsp_sd_init()) return false;
    s_file = SD_MMC.open("/sdcard/video/default.fvid", FILE_READ);
    if (!s_file) {
        s_file = SD_MMC.open("/sdcard/video/family_video.fvid", FILE_READ);
    }
    if (!s_file) {
        Serial.println("[video] SD no /sdcard/video/default.fvid or family_video.fvid");
        return false;
    }

    const size_t size = s_file.size();
    uint8_t header[FVID_HEADER_LEN] = {0};
    if (size < FVID_HEADER_LEN || s_file.read(header, sizeof(header)) != (int)sizeof(header) ||
        memcmp(header, "FVID", 4) != 0 || header[4] != FVID_VERSION ||
        read_le16(header + 6) != BSP_EPD_W || read_le16(header + 8) != BSP_EPD_H ||
        read_le16(header + 10) == 0 ||
        size != FVID_HEADER_LEN + (size_t)read_le16(header + 10) * FVID_RECORD_LEN) {
        Serial.printf("[video] SD invalid FVID size=%lu\n", (unsigned long)size);
        s_file.close();
        return false;
    }
    s_count = read_le16(header + 10);
    s_valid = true;
    Serial.printf("[video] frame source SD %d frames\n", s_count);
    return true;
}

}

const frame_source_t* frame_source_sd(void) { return s_valid ? &s_sd : nullptr; }

static const frame_source_t* s_current = nullptr;

const frame_source_t* frame_source_init(void)
{
    if (s_current) return s_current;
    if (open_fvid()) s_current = &s_sd;
    else {
        s_current = frame_source_rom();
        Serial.printf("[video] frame source %s (SD fallback)\n", s_current->name);
    }
    return s_current;
}

const frame_source_t* frame_source_current(void) { return s_current ? s_current : frame_source_init(); }
const char* frame_source_name(void) { return frame_source_current()->name; }

void frame_source_use_rom(void)
{
    if (s_file) s_file.close();
    s_valid = false;
    s_current = frame_source_rom();
    Serial.println("[video] frame source switched to ROM");
}
