#include "frame_source.h"

#include <Arduino.h>
#include <SD_MMC.h>
#include <ctype.h>
#include <string.h>

#include "bsp.h"
#include "bsp_pins.h"
#include "family_video_assets.h"
#include "file_storage.h"
#include "sdcard.h"

namespace {

constexpr size_t FVID_HEADER_LEN = 16;
constexpr size_t FVID_RECORD_LEN = 5004;
constexpr uint8_t FVID_VERSION = 1;
constexpr int SCAN_PATH_MAX = SD_STORY_MAX + 1;

static File s_file;
static int s_count = 0;
static bool s_valid = false;
static const frame_source_t* s_current = nullptr;

uint16_t read_le16(const uint8_t* p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

bool read_at(size_t offset, uint8_t* out, size_t len)
{
    if (!s_file || !out || !s_file.seek(offset)) return false;
    return s_file.read(out, len) == (int)len;
}

bool valid_header(File& file, uint16_t* count)
{
    const size_t size = file.size();
    uint8_t header[FVID_HEADER_LEN] = {0};
    if (size < FVID_HEADER_LEN || !file.seek(0) ||
        file.read(header, sizeof(header)) != (int)sizeof(header) ||
        memcmp(header, "FVID", 4) != 0 || header[4] != FVID_VERSION ||
        read_le16(header + 6) != BSP_EPD_W || read_le16(header + 8) != BSP_EPD_H ||
        read_le16(header + 10) == 0 ||
        size != FVID_HEADER_LEN + (size_t)read_le16(header + 10) * FVID_RECORD_LEN) {
        return false;
    }
    if (count) *count = read_le16(header + 10);
    return true;
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
    uint8_t record_header[4] = {0};
    if (!read_at(offset, record_header, sizeof(record_header)) ||
        !read_at(offset + 4, framebuffer, len)) {
        Serial.printf("[video] SD short read frame=%d\n", index);
        return false;
    }
    return record_header[0] <= APP_REFRESH_FINAL_FULL;
}

const frame_source_t s_sd = { "SD", sd_count, sd_hint, sd_load };

bool has_fvid_suffix(const char* name)
{
    if (!name) return false;
    const size_t len = strlen(name);
    if (len <= 5 || strcmp(name + len - 5, ".fvid") != 0) return false;
    for (size_t i = 0; i < len; i++) {
        const char c = name[i];
        if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '-' &&
            !(i >= len - 5 && c == '.')) return false;
    }
    return true;
}

void sort_paths(char paths[][64], int count)
{
    for (int i = 1; i < count; i++) {
        char value[64];
        snprintf(value, sizeof(value), "%s", paths[i]);
        int j = i;
        while (j > 0 && strcmp(paths[j - 1], value) > 0) {
            snprintf(paths[j], sizeof(paths[j]), "%s", paths[j - 1]);
            j--;
        }
        snprintf(paths[j], sizeof(paths[j]), "%s", value);
    }
}

void fill_entry(fvid_entry_t* entry, const char* path, uint16_t frames)
{
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->path, sizeof(entry->path), "%s", path);
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    char name[24] = {};
    snprintf(name, sizeof(name), "%s", base);
    char* extension = strrchr(name, '.');
    if (extension) *extension = '\0';
    snprintf(entry->id, sizeof(entry->id), "%s", name);
    for (char* p = name; *p; p++) *p = (char)toupper((unsigned char)*p);
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    entry->frames = frames;
    entry->audio_scene_count = frames;
    entry->builtin_index = 0;
    entry->kind = FVID_ENTRY_SD;
}

}

const frame_source_t* frame_source_sd(void) { return s_valid ? &s_sd : nullptr; }

int frame_source_sd_scan(fvid_entry_t* out, int max)
{
    if (!out || max <= 0) return 0;
    if (!bsp_sd_init()) {
        Serial.println("[video] SD scan: card unavailable");
        return 0;
    }

    if (s_file) s_file.close();
    s_count = 0;
    s_valid = false;
    s_current = nullptr;

    File directory = SD_MMC.open("/sdcard/video");
    if (!directory || !directory.isDirectory()) {
        Serial.println("[video] SD scan: /sdcard/video unavailable");
        if (directory) directory.close();
        return 0;
    }

    char paths[SCAN_PATH_MAX][64] = {};
    int path_count = 0;
    while (true) {
        File entry = directory.openNextFile();
        if (!entry) break;
        const char* name = entry.name();
        const char* base = name ? strrchr(name, '/') : nullptr;
        base = base ? base + 1 : name;
        if (!entry.isDirectory() && has_fvid_suffix(base)) {
            if (path_count < SCAN_PATH_MAX) {
                snprintf(paths[path_count], sizeof(paths[0]), "/sdcard/video/%s", base);
                path_count++;
            }
        }
        entry.close();
    }
    directory.close();
    sort_paths(paths, path_count);

    int found = 0;
    bool ignored = false;
    for (int i = 0; i < path_count; i++) {
        File file = SD_MMC.open(paths[i], FILE_READ);
        uint16_t frames = 0;
        const bool valid = file && valid_header(file, &frames);
        if (file) file.close();
        if (!valid) {
            Serial.printf("[video] SD scan invalid %s\n", paths[i]);
            continue;
        }
        if (found >= max || found >= SD_STORY_MAX) {
            ignored = true;
            continue;
        }
        fill_entry(&out[found], paths[i], frames);
        found++;
    }
    if (path_count == SCAN_PATH_MAX) ignored = true;

    Serial.printf("[video] scan found %d:", found);
    for (int i = 0; i < found; i++) {
        Serial.printf(" %s(%u)", out[i].name, out[i].frames);
    }
    Serial.println();
    if (ignored) Serial.printf("[video] SD scan: more than %d stories; extras ignored\n", SD_STORY_MAX);
    return found;
}

bool frame_source_sd_open(const char* path)
{
    if (!path || !bsp_sd_init()) return false;
    file_storage::playback_end();
    if (s_file) s_file.close();
    s_count = 0;
    s_valid = false;
    s_current = nullptr;

    s_file = SD_MMC.open(path, FILE_READ);
    uint16_t count = 0;
    if (!s_file || !valid_header(s_file, &count)) {
        Serial.printf("[video] SD open invalid %s\n", path);
        if (s_file) s_file.close();
        return false;
    }
    const file_storage::Status playback_status = file_storage::playback_begin(path);
    if (playback_status != file_storage::Status::Ok) {
        Serial.printf("[video] SD playback lock failed: %s\n",
                      file_storage::status_name(playback_status));
        s_file.close();
        return false;
    }
    s_count = count;
    s_valid = true;
    s_current = &s_sd;
    Serial.printf("[video] frame source SD %s %d frames\n", path, s_count);
    return true;
}

const frame_source_t* frame_source_init(void)
{
    return s_current ? s_current : frame_source_rom();
}

const frame_source_t* frame_source_current(void)
{
    return s_current ? s_current : frame_source_rom();
}

const char* frame_source_name(void) { return frame_source_current()->name; }

void frame_source_use_rom(void)
{
    file_storage::playback_end();
    if (s_file) s_file.close();
    s_count = 0;
    s_valid = false;
    s_current = frame_source_rom();
    Serial.println("[video] frame source switched to ROM");
}
