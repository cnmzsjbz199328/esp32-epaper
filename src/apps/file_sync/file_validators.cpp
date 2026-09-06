#include "file_validators.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ctype.h>
#include <memory>
#include <string.h>

#include "bsp_pins.h"

namespace {

constexpr size_t FVID_HEADER_LENGTH = 16;
constexpr size_t FVID_RECORD_LENGTH = 5004;
constexpr uint8_t FVID_VERSION = 1;
constexpr size_t VALIDATOR_MAX_ENTRIES = 64;
constexpr size_t STORY_JSON_MAX = 16384;

uint16_t le16(const uint8_t* p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

uint32_t le32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

const char* extension_of(const char* path)
{
    const char* slash = strrchr(path ? path : "", '/');
    const char* dot = strrchr(path ? path : "", '.');
    return dot && (!slash || dot > slash) ? dot : "";
}

bool is_extension(const char* path, const char* extension)
{
    return strcasecmp(extension_of(path), extension) == 0;
}

void fail(file_validation_report_t* report, const char* detail)
{
    if (!report) return;
    report->ok = false;
    snprintf(report->detail, sizeof(report->detail), "%s", detail ? detail : "invalid");
}

bool numeric_stem(const char* path, uint16_t* value)
{
    if (!path || !value) return false;
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    const char* dot = strrchr(base, '.');
    if (!dot || dot == base) return false;
    uint32_t number = 0;
    for (const char* p = base; p < dot; p++) {
        if (!isdigit((unsigned char)*p)) return false;
        number = number * 10U + (uint32_t)(*p - '0');
        if (number > 65535U) return false;
    }
    *value = (uint16_t)number;
    return true;
}

bool validate_png_dimensions(const char* path)
{
    uint8_t header[24] = {};
    size_t got = 0;
    if (file_storage::read_at(path, 0, header, sizeof(header), &got) != file_storage::Status::Ok ||
        got != sizeof(header) || memcmp(header, "\x89PNG\r\n\x1a\n", 8) != 0 ||
        memcmp(header + 12, "IHDR", 4) != 0) return false;
    return ((uint32_t)header[16] << 24 | (uint32_t)header[17] << 16 |
            (uint32_t)header[18] << 8 | header[19]) == BSP_EPD_W &&
           ((uint32_t)header[20] << 24 | (uint32_t)header[21] << 16 |
            (uint32_t)header[22] << 8 | header[23]) == BSP_EPD_H;
}

bool safe_package_reference(const char* reference)
{
    if (!reference || !reference[0] || reference[0] == '/' || strchr(reference, '\\')) return false;
    for (const char* p = reference; *p; p++) {
        if ((unsigned char)*p < 0x20) return false;
    }
    const char* start = reference;
    while (*start) {
        const char* end = strchr(start, '/');
        const size_t length = end ? (size_t)(end - start) : strlen(start);
        if (length == 0 || (length == 2 && start[0] == '.' && start[1] == '.')) return false;
        start = end ? end + 1 : start + length;
    }
    return true;
}

bool package_path(const char* root, const char* reference, char* path, size_t capacity)
{
    if (!safe_package_reference(reference) || !path) return false;
    return snprintf(path, capacity, "%s/%s", root, reference) < (int)capacity;
}

bool validate_story_reference(const char* root, JsonDocument& document)
{
    const char* reference = document["fvid"] | "";
    if (!safe_package_reference(reference)) return false;
    char path[file_storage::PATH_MAX_LENGTH] = {};
    if (!package_path(root, reference, path, sizeof(path))) return false;
    file_storage::FileInfo info;
    if (file_storage::stat(path, &info) != file_storage::Status::Ok || info.is_directory) return false;

    JsonArray scenes = document["scenes"].as<JsonArray>();
    if (!scenes.isNull()) {
        for (JsonObject scene : scenes) {
            const char* image = scene["image"] | "";
            const char* audio = scene["audio"] | "";
            if (image[0]) {
                if (!package_path(root, image, path, sizeof(path)) ||
                    file_storage::stat(path, &info) != file_storage::Status::Ok || info.is_directory ||
                    !validate_png_dimensions(path)) return false;
            }
            /* Audio is optional at runtime. An absent or invalid audio path is
             * reported by the story scanner while the visual story remains
             * playable, but path traversal is never accepted. */
            if (audio[0]) {
                if (!safe_package_reference(audio) ||
                    !package_path(root, audio, path, sizeof(path))) return false;
                if (file_storage::stat(path, &info) == file_storage::Status::Ok) {
                    if (info.is_directory) return false;
                    file_validation_report_t audio_report;
                    if (file_validate_audio(path, &audio_report) != file_storage::Status::Ok) return false;
                }
            }
        }
    }
    const char* opening = document["opening_audio"] | "";
    return !opening[0] || safe_package_reference(opening);
}

}  // namespace

file_storage::Status file_validate_fvid(const char* path, file_validation_report_t* report)
{
    if (report) {
        *report = {};
        report->kind = file_validator_kind_t::Fvid;
    }
    file_storage::FileInfo info;
    file_storage::Status status = file_storage::stat(path, &info);
    if (status != file_storage::Status::Ok) return status;
    if (info.is_directory || info.size < FVID_HEADER_LENGTH) {
        fail(report, "fvid missing or too small");
        return file_storage::Status::InvalidArgument;
    }
    uint8_t header[FVID_HEADER_LENGTH] = {};
    size_t got = 0;
    status = file_storage::read_at(path, 0, header, sizeof(header), &got);
    const uint16_t frames = got == sizeof(header) ? le16(header + 10) : 0;
    if (status != file_storage::Status::Ok || got != sizeof(header) ||
        memcmp(header, "FVID", 4) != 0 || header[4] != FVID_VERSION ||
        le16(header + 6) != BSP_EPD_W || le16(header + 8) != BSP_EPD_H || frames == 0 ||
        info.size != FVID_HEADER_LENGTH + (uint64_t)frames * FVID_RECORD_LENGTH) {
        fail(report, "fvid header, dimensions, or frame count invalid");
        return file_storage::Status::InvalidArgument;
    }
    if (report) {
        report->ok = true;
        report->files_checked = 1;
        report->fvid_checked = 1;
        snprintf(report->detail, sizeof(report->detail), "FVID %u frames", frames);
    }
    return file_storage::Status::Ok;
}

file_storage::Status file_validate_audio(const char* path, file_validation_report_t* report)
{
    if (report) {
        *report = {};
        report->kind = file_validator_kind_t::Audio;
    }
    uint8_t header[44] = {};
    size_t got = 0;
    const file_storage::Status status = file_storage::read_at(path, 0, header, sizeof(header), &got);
    if (status != file_storage::Status::Ok) return status;
    if (got < sizeof(header) || memcmp(header, "RIFF", 4) != 0 ||
        memcmp(header + 8, "WAVE", 4) != 0 || memcmp(header + 12, "fmt ", 4) != 0 ||
        le16(header + 20) != 1 || le16(header + 22) != 1 || le32(header + 24) != 16000 ||
        le16(header + 34) != 16) {
        fail(report, "audio must be mono 16kHz PCM 16-bit WAV");
        return file_storage::Status::InvalidArgument;
    }
    if (report) {
        report->ok = true;
        report->files_checked = 1;
        report->audio_checked = 1;
        snprintf(report->detail, sizeof(report->detail), "mono 16kHz PCM WAV");
    }
    return file_storage::Status::Ok;
}

file_storage::Status file_validate_story_package(const char* root,
                                                  file_validation_report_t* report)
{
    if (report) {
        *report = {};
        report->kind = file_validator_kind_t::StoryPackage;
    }
    char normalized_root[file_storage::PATH_MAX_LENGTH] = {};
    file_storage::Status status = file_storage::normalize_path(
        root, normalized_root, sizeof(normalized_root));
    if (status != file_storage::Status::Ok) return status;

    char story_path[file_storage::PATH_MAX_LENGTH] = {};
    snprintf(story_path, sizeof(story_path), "%s/story.json", normalized_root);
    file_storage::FileInfo story_info;
    if (file_storage::stat(story_path, &story_info) != file_storage::Status::Ok || story_info.is_directory ||
        story_info.size == 0 || story_info.size > STORY_JSON_MAX) {
        fail(report, "story.json missing or too large");
        return file_storage::Status::InvalidArgument;
    }
    std::unique_ptr<uint8_t[]> story_data(new (std::nothrow) uint8_t[STORY_JSON_MAX + 1]());
    if (!story_data) {
        fail(report, "not enough memory for story.json");
        return file_storage::Status::IoError;
    }
    size_t story_length = 0;
    status = file_storage::read_at(story_path, 0, story_data.get(), (size_t)story_info.size, &story_length);
    if (status != file_storage::Status::Ok || story_length != story_info.size) return file_storage::Status::IoError;
    JsonDocument document;
    if (deserializeJson(document, story_data.get(), story_length) || !validate_story_reference(normalized_root, document)) {
        fail(report, "story.json is invalid or references a missing FVID");
        return file_storage::Status::InvalidArgument;
    }

    std::unique_ptr<file_storage::FileInfo[]> entries(
        new (std::nothrow) file_storage::FileInfo[VALIDATOR_MAX_ENTRIES]());
    std::unique_ptr<char[][file_storage::PATH_MAX_LENGTH]>
        directories(new (std::nothrow) char[16][file_storage::PATH_MAX_LENGTH]());
    if (!entries || !directories) {
        fail(report, "not enough memory for story scan");
        return file_storage::Status::IoError;
    }
    size_t directory_count = 1;
    snprintf(directories[0], sizeof(directories[0]), "%s", normalized_root);
    bool seen_number[128] = {};
    uint16_t max_number = 0;
    bool have_number = false;
    while (directory_count > 0) {
        char directory[file_storage::PATH_MAX_LENGTH] = {};
        snprintf(directory, sizeof(directory), "%s", directories[--directory_count]);
        size_t count = 0;
        status = file_storage::list(directory, entries.get(), VALIDATOR_MAX_ENTRIES, &count);
        if (status != file_storage::Status::Ok) return status;
        for (size_t i = 0; i < count; i++) {
            if (report && report->files_checked < UINT16_MAX) report->files_checked++;
            if (entries[i].is_directory) {
                if (directory_count < 16) snprintf(directories[directory_count++],
                                                    sizeof(directories[0]), "%s", entries[i].path);
                continue;
            }
            if (is_extension(entries[i].path, ".fvid")) {
                file_validation_report_t child;
                status = file_validate_fvid(entries[i].path, &child);
                if (status != file_storage::Status::Ok) { fail(report, child.detail); return status; }
                if (report) report->fvid_checked++;
            }
            /* Referenced PNG/WAV resources are checked while parsing
             * story.json. Unreferenced previews, source audio, and ordinary
             * files may coexist in a package without blocking its commit. */
            uint16_t number = 0;
            if ((is_extension(entries[i].path, ".wav") || is_extension(entries[i].path, ".png")) &&
                numeric_stem(entries[i].path, &number) && number < 128) {
                seen_number[number] = true;
                if (!have_number || number > max_number) max_number = number;
                have_number = true;
            }
        }
    }
    if (have_number) {
        for (uint16_t i = 0; i <= max_number; i++) {
            if (!seen_number[i]) {
                fail(report, "numbered assets are not continuous");
                return file_storage::Status::InvalidArgument;
            }
        }
    }
    if (report) {
        report->ok = true;
        snprintf(report->detail, sizeof(report->detail), "story valid: %u files, %u FVID, %u audio",
                 report->files_checked, report->fvid_checked, report->audio_checked);
    }
    return file_storage::Status::Ok;
}
