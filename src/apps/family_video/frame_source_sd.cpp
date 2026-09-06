#include "frame_source.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD_MMC.h>
#include <ctype.h>
#include <memory>
#include <string.h>

#include "bsp.h"
#include "bsp_pins.h"
#include "file_storage.h"
#include "../../../lib/file_storage/file_storage_internal.h"
#include "../file_sync/file_validators.h"
#include "sdcard.h"

namespace {

constexpr size_t FVID_HEADER_LEN = 16;
constexpr size_t FVID_RECORD_LEN = 5004;
constexpr uint8_t FVID_VERSION = 1;
constexpr size_t STORY_JSON_MAX = 16384;
constexpr int SCAN_PATH_MAX = SD_STORY_MAX + 1;

static File s_file;
static int s_count = 0;
static bool s_valid = false;
static const frame_source_t* s_current = nullptr;

File open_sd_file(const char* normalized_path, const char* mode)
{
    char sd_path[STORY_PATH_MAX_LENGTH] = {};
    if (!file_storage::internal::to_sd_path(normalized_path, sd_path, sizeof(sd_path))) return {};
    return SD_MMC.open(sd_path, mode);
}

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
    if (index < 0 || index >= s_count) return FVID_REFRESH_FULL;
    uint8_t hint = 0;
    return read_at(FVID_HEADER_LEN + (size_t)index * FVID_RECORD_LEN, &hint, 1)
        ? (int)hint : FVID_REFRESH_FULL;
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
    return record_header[0] <= FVID_REFRESH_FINAL_FULL;
}

const frame_source_t s_sd = { "SD", sd_count, sd_hint, sd_load };

bool valid_story_id(const char* name)
{
    if (!name || !name[0]) return false;
    const size_t len = strlen(name);
    if (len >= STORY_ID_MAX_LENGTH) return false;
    for (size_t i = 0; i < len; i++) {
        const char c = name[i];
        if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '-' && c != '_') return false;
    }
    return true;
}

bool file_exists(const char* path)
{
    if (!path) return false;
    File file = open_sd_file(path, FILE_READ);
    if (!file || file.isDirectory()) {
        if (file) file.close();
        return false;
    }
    file.close();
    return true;
}

void sort_paths(char paths[][STORY_PATH_MAX_LENGTH], int count)
{
    for (int i = 1; i < count; i++) {
        char value[STORY_PATH_MAX_LENGTH];
        snprintf(value, sizeof(value), "%s", paths[i]);
        int j = i;
        while (j > 0 && strcmp(paths[j - 1], value) > 0) {
            snprintf(paths[j], sizeof(paths[j]), "%s", paths[j - 1]);
            j--;
        }
        snprintf(paths[j], sizeof(paths[j]), "%s", value);
    }
}

bool read_story_entry(const char* root, const char* directory_id, fvid_entry_t* entry)
{
    if (!root || !directory_id || !entry || !valid_story_id(directory_id)) return false;

    file_validation_report_t report;
    if (file_validate_story_package(root, &report) != file_storage::Status::Ok) {
        Serial.printf("[video] story rejected %s: %s\n", root, report.detail);
        return false;
    }

    char story_path[STORY_PATH_MAX_LENGTH] = {};
    if (snprintf(story_path, sizeof(story_path), "%s/story.json", root) >= (int)sizeof(story_path)) return false;
    file_storage::FileInfo story_info;
    if (file_storage::stat(story_path, &story_info) != file_storage::Status::Ok ||
        story_info.size == 0 || story_info.size > STORY_JSON_MAX) return false;
    std::unique_ptr<uint8_t[]> data(new (std::nothrow) uint8_t[(size_t)story_info.size + 1]());
    if (!data) return false;
    size_t length = 0;
    if (file_storage::read_at(story_path, 0, data.get(), (size_t)story_info.size, &length) != file_storage::Status::Ok ||
        length != story_info.size) return false;

    JsonDocument document;
    if (deserializeJson(document, data.get(), length)) return false;
    const char* id = document["id"] | "";
    const char* fvid = document["fvid"] | "";
    if (strcmp(id, directory_id) != 0 || !fvid[0] || fvid[0] == '/' || strstr(fvid, "..")) return false;

    memset(entry, 0, sizeof(*entry));
    snprintf(entry->path, sizeof(entry->path), "%s", root);
    if (snprintf(entry->fvid_path, sizeof(entry->fvid_path), "%s/%s", root, fvid) >=
        (int)sizeof(entry->fvid_path)) return false;
    snprintf(entry->id, sizeof(entry->id), "%s", id);
    snprintf(entry->name, sizeof(entry->name), "%s", document["title"] | id);
    snprintf(entry->author, sizeof(entry->author), "%s", document["author"] | "");
    snprintf(entry->opening_audio, sizeof(entry->opening_audio), "%s", document["opening_audio"] | "");
    entry->closing_overlay = !document["closing_overlay"].isNull();
    entry->kind = FVID_ENTRY_SD;

    File fvid_file = open_sd_file(entry->fvid_path, FILE_READ);
    uint16_t frame_count = 0;
    if (!fvid_file || !valid_header(fvid_file, &frame_count)) {
        if (fvid_file) fvid_file.close();
        return false;
    }
    fvid_file.close();
    const char* expected_sha256 = document["fvid_sha256"] | "";
    if (expected_sha256[0] &&
        file_storage::verify_sha256(entry->fvid_path, expected_sha256) != file_storage::Status::Ok) {
        Serial.printf("[video] story rejected %s: FVID hash mismatch\n", root);
        return false;
    }
    JsonArray scenes = document["scenes"].as<JsonArray>();
    if (!scenes.isNull() && scenes.size() > 0 && scenes.size() != frame_count) return false;
    entry->frames = frame_count;
    entry->audio_scene_count = scenes.isNull() ? 0 : (uint16_t)min((size_t)UINT16_MAX, scenes.size());
    entry->audio_missing = false;
    if (!scenes.isNull()) {
        for (JsonObject scene : scenes) {
            const char* audio = scene["audio"] | "";
            char audio_path[STORY_PATH_MAX_LENGTH] = {};
            if (!audio[0] || audio[0] == '/' || strstr(audio, "..") ||
                snprintf(audio_path, sizeof(audio_path), "%s/%s", root, audio) >= (int)sizeof(audio_path) ||
                !file_exists(audio_path)) {
                entry->audio_missing = true;
            } else {
                File audio_file = open_sd_file(audio_path, FILE_READ);
                if (!audio_file) entry->audio_missing = true;
                else audio_file.close();
            }
        }
    }
    return true;
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

    File directory = SD_MMC.open("/video");
    if (!directory || !directory.isDirectory()) {
        Serial.println("[video] SD scan: /video unavailable");
        if (directory) directory.close();
        return 0;
    }

    char paths[SCAN_PATH_MAX][STORY_PATH_MAX_LENGTH] = {};
    int path_count = 0;
    while (true) {
        File entry = directory.openNextFile();
        if (!entry) break;
        const char* name = entry.name();
        const char* base = name ? strrchr(name, '/') : nullptr;
        base = base ? base + 1 : name;
        if (entry.isDirectory() && valid_story_id(base)) {
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
        if (found >= max || found >= SD_STORY_MAX) {
            ignored = true;
            continue;
        }
        char directory_id[STORY_ID_MAX_LENGTH] = {};
        const char* base = strrchr(paths[i], '/');
        snprintf(directory_id, sizeof(directory_id), "%s", base ? base + 1 : paths[i]);
        if (read_story_entry(paths[i], directory_id, &out[found])) found++;
    }
    if (path_count >= SCAN_PATH_MAX) ignored = true;

    Serial.printf("[video] scan found %d:", found);
    for (int i = 0; i < found; i++) {
        Serial.printf(" %s(%u)%s", out[i].id, out[i].frames,
                      out[i].audio_missing ? ":no-audio" : "");
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

    s_file = open_sd_file(path, FILE_READ);
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
    return s_current;
}

const frame_source_t* frame_source_current(void)
{
    return s_current;
}

const char* frame_source_name(void)
{
    const frame_source_t* source = frame_source_current();
    return source ? source->name : "-";
}

void frame_source_use_rom(void)
{
    file_storage::playback_end();
    if (s_file) s_file.close();
    s_count = 0;
    s_valid = false;
    s_current = nullptr;
    Serial.println("[video] frame source cleared (ROM disabled)");
}
