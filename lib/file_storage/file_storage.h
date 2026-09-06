#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>

namespace file_storage {

/* SD_MMC is mounted by the board adapter at this fixed VFS mount point. */
constexpr const char* ROOT_PATH = "/sdcard";
constexpr size_t PATH_MAX_LENGTH = 192;
constexpr size_t SHA256_HEX_LENGTH = 64;

enum class Status : uint8_t {
    Ok = 0,
    NotReady,
    InvalidArgument,
    InvalidPath,
    PathTooLong,
    NotFound,
    AlreadyExists,
    IoError,
    NoSpace,
    Busy,
    PermissionDenied,
    HashMismatch,
    InvalidTransaction,
    Incomplete,
};

struct FileInfo {
    char path[PATH_MAX_LENGTH] = {};
    uint64_t size = 0;
    time_t modified = 0;
    bool is_directory = false;
};

struct SpaceInfo {
    uint64_t total_bytes = 0;
    uint64_t used_bytes = 0;
    uint64_t free_bytes = 0;
    bool ready = false;
};

const char* status_name(Status status);

/* Accepts /sdcard/... and SD-relative /... paths. Always returns a canonical
 * absolute path below ROOT_PATH. Dot-dot components, backslashes, control
 * characters and path prefixes outside ROOT_PATH are rejected. */
Status normalize_path(const char* requested, char* normalized, size_t capacity);

bool init();
bool ready();
SpaceInfo space();

Status stat(const char* path, FileInfo* out);
Status list(const char* path, FileInfo* out, size_t capacity, size_t* count);
Status make_directory(const char* path, bool parents = true);

Status read_at(const char* path, uint64_t offset, uint8_t* buffer,
               size_t capacity, size_t* bytes_read);
Status write_at(const char* path, uint64_t offset, const uint8_t* data,
                size_t length, bool truncate, size_t* bytes_written);
Status copy_file(const char* source, const char* destination);
Status rename(const char* source, const char* destination);
/* Replace destination only after source is complete. If the second rename
 * fails, the previous destination is restored. */
Status replace_file(const char* source, const char* destination);

/* A directory is never removed accidentally. Recursive removal requires the
 * explicit confirmation flag, and the SD root itself can never be removed. */
Status remove(const char* path, bool recursive = false, bool confirmed = false);

/* Playback is a logical read lease. Destructive operations and writes to a
 * path outside the staging area return Busy while a lease is active. */
Status playback_begin(const char* path);
void playback_end();
bool playback_active();

/* Hash a file into a lower-case, null-terminated SHA-256 string. */
Status sha256_file(const char* path, char hex[SHA256_HEX_LENGTH + 1]);
Status verify_sha256(const char* path, const char* expected_hex);

}  // namespace file_storage
