#include "file_storage.h"
#include "file_storage_internal.h"

#include <SD_MMC.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

#include "sdcard.h"

namespace {

SemaphoreHandle_t s_mutex = nullptr;
volatile bool s_playback_active = false;
char s_playback_path[file_storage::PATH_MAX_LENGTH] = {};

bool ensure_mutex()
{
    if (s_mutex) return true;
    static portMUX_TYPE create_mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&create_mux);
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    portEXIT_CRITICAL(&create_mux);
    return s_mutex != nullptr;
}

bool starts_with_path(const char* path, const char* prefix)
{
    const size_t prefix_length = strlen(prefix);
    return strncmp(path, prefix, prefix_length) == 0 &&
           (path[prefix_length] == '\0' || path[prefix_length] == '/');
}

bool protected_by_playback(const char* path)
{
    return s_playback_active && (starts_with_path(path, s_playback_path) ||
                                 starts_with_path(s_playback_path, path));
}

bool remove_tree(File& directory)
{
    while (true) {
        File child = directory.openNextFile();
        if (!child) break;
        char child_path[file_storage::PATH_MAX_LENGTH] = {};
        snprintf(child_path, sizeof(child_path), "%s", child.path());
        const bool is_directory = child.isDirectory();
        child.close();
        char child_sd_path[file_storage::PATH_MAX_LENGTH] = {};
        if (!file_storage::internal::to_sd_path(child_path, child_sd_path,
                                                 sizeof(child_sd_path))) return false;
        if (is_directory) {
            File nested = SD_MMC.open(child_sd_path, FILE_READ);
            if (!nested || !remove_tree(nested)) {
                if (nested) nested.close();
                return false;
            }
            nested.close();
            if (!SD_MMC.rmdir(child_sd_path)) return false;
        } else if (!SD_MMC.remove(child_sd_path)) {
            return false;
        }
    }
    return true;
}

bool mkdir_parents(const char* normalized)
{
    if (strcmp(normalized, file_storage::ROOT_PATH) == 0) return true;
    char path[file_storage::PATH_MAX_LENGTH] = {};
    snprintf(path, sizeof(path), "%s", file_storage::ROOT_PATH);
    const char* cursor = normalized + strlen(file_storage::ROOT_PATH);
    while (*cursor == '/') cursor++;
    while (*cursor) {
        const char* end = strchr(cursor, '/');
        const size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
        const size_t used = strlen(path);
        if (used + length + 2 > sizeof(path)) return false;
        path[used] = '/';
        memcpy(path + used + 1, cursor, length);
        path[used + 1 + length] = '\0';
        char sd_path[file_storage::PATH_MAX_LENGTH] = {};
        if (!file_storage::internal::to_sd_path(path, sd_path, sizeof(sd_path))) return false;
        if (!SD_MMC.exists(sd_path) && !SD_MMC.mkdir(sd_path)) return false;
        cursor = end ? end + 1 : cursor + length;
        while (*cursor == '/') cursor++;
    }
    return true;
}

}  // namespace

namespace file_storage::internal {

bool to_sd_path(const char* normalized_path, char* out, size_t capacity)
{
    if (!normalized_path || !out || capacity == 0) return false;
    const size_t root_length = strlen(file_storage::ROOT_PATH);
    const char* relative = normalized_path;
    if (strncmp(normalized_path, file_storage::ROOT_PATH, root_length) == 0 &&
        (normalized_path[root_length] == '\0' || normalized_path[root_length] == '/')) {
        relative = normalized_path + root_length;
    }
    if (relative[0] == '\0') relative = "/";
    return snprintf(out, capacity, "%s", relative) < (int)capacity;
}

bool lock(uint32_t timeout_ms)
{
    if (!ensure_mutex()) return false;
    const TickType_t ticks = timeout_ms == UINT32_MAX
        ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(s_mutex, ticks) == pdTRUE;
}

void unlock()
{
    if (s_mutex) xSemaphoreGive(s_mutex);
}

bool is_staging_path(const char* normalized_path)
{
    return starts_with_path(normalized_path, "/sdcard/.sync_staging");
}

bool mutation_allowed(const char* normalized_path, bool staging_write)
{
    return staging_write || !protected_by_playback(normalized_path);
}

Status stat_locked(const char* normalized_path, FileInfo* out)
{
    if (!out) return Status::InvalidArgument;
    char sd_path[file_storage::PATH_MAX_LENGTH] = {};
    if (!to_sd_path(normalized_path, sd_path, sizeof(sd_path))) return Status::PathTooLong;
    File file = SD_MMC.open(sd_path, FILE_READ);
    if (!file) return Status::NotFound;
    memset(out, 0, sizeof(*out));
    snprintf(out->path, sizeof(out->path), "%s", normalized_path);
    out->is_directory = file.isDirectory();
    out->size = out->is_directory ? 0 : file.size();
    out->modified = file.getLastWrite();
    file.close();
    return Status::Ok;
}

Status make_directory_locked(const char* normalized_path, bool parents)
{
    if (strcmp(normalized_path, file_storage::ROOT_PATH) == 0) return Status::Ok;
    char sd_path[file_storage::PATH_MAX_LENGTH] = {};
    if (!to_sd_path(normalized_path, sd_path, sizeof(sd_path))) return Status::PathTooLong;
    if (SD_MMC.exists(sd_path)) {
        FileInfo info;
        return stat_locked(normalized_path, &info) == Status::Ok && info.is_directory
            ? Status::Ok : Status::AlreadyExists;
    }
    if (parents ? mkdir_parents(normalized_path) : SD_MMC.mkdir(sd_path)) {
        return Status::Ok;
    }
    return Status::IoError;
}

Status rename_locked(const char* source, const char* destination)
{
    if (!mutation_allowed(source, false) || !mutation_allowed(destination, false)) {
        return Status::Busy;
    }
    char source_sd_path[file_storage::PATH_MAX_LENGTH] = {};
    char destination_sd_path[file_storage::PATH_MAX_LENGTH] = {};
    if (!to_sd_path(source, source_sd_path, sizeof(source_sd_path)) ||
        !to_sd_path(destination, destination_sd_path, sizeof(destination_sd_path))) {
        return Status::PathTooLong;
    }
    if (!SD_MMC.exists(source_sd_path)) return Status::NotFound;
    if (SD_MMC.exists(destination_sd_path)) return Status::AlreadyExists;
    char parent[file_storage::PATH_MAX_LENGTH] = {};
    snprintf(parent, sizeof(parent), "%s", destination);
    char* slash = strrchr(parent, '/');
    if (slash && slash != parent) {
        *slash = '\0';
        if (make_directory_locked(parent, true) != Status::Ok) return Status::IoError;
    }
    return SD_MMC.rename(source_sd_path, destination_sd_path) ? Status::Ok : Status::IoError;
}

Status remove_locked(const char* normalized_path, bool recursive, bool confirmed)
{
    if (strcmp(normalized_path, file_storage::ROOT_PATH) == 0) return Status::PermissionDenied;
    if (!mutation_allowed(normalized_path, false)) return Status::Busy;
    char sd_path[file_storage::PATH_MAX_LENGTH] = {};
    if (!to_sd_path(normalized_path, sd_path, sizeof(sd_path))) return Status::PathTooLong;
    File file = SD_MMC.open(sd_path, FILE_READ);
    if (!file) return Status::NotFound;
    const bool directory = file.isDirectory();
    if (directory && (!recursive || !confirmed)) {
        file.close();
        return Status::PermissionDenied;
    }
    bool ok = true;
    if (directory) ok = remove_tree(file) && SD_MMC.rmdir(sd_path);
    else ok = SD_MMC.remove(sd_path);
    file.close();
    return ok ? Status::Ok : Status::IoError;
}

Status write_at_locked(const char* normalized_path, uint64_t offset,
                       const uint8_t* data, size_t length, bool truncate,
                       size_t* bytes_written)
{
    if (bytes_written) *bytes_written = 0;
    if ((!data && length != 0) || offset > UINT32_MAX) return Status::InvalidArgument;
    const bool staging = is_staging_path(normalized_path);
    if (!mutation_allowed(normalized_path, staging)) return Status::Busy;
    if (length > UINT32_MAX - (uint32_t)offset) return Status::InvalidArgument;
    if (truncate && offset != 0) return Status::InvalidArgument;
    char sd_path[file_storage::PATH_MAX_LENGTH] = {};
    if (!to_sd_path(normalized_path, sd_path, sizeof(sd_path))) return Status::PathTooLong;
    if (!truncate && SD_MMC.exists(sd_path)) {
        FileInfo existing;
        if (stat_locked(normalized_path, &existing) != Status::Ok || existing.is_directory ||
            offset > existing.size) return Status::Incomplete;
    } else if (!truncate && offset != 0) {
        return Status::Incomplete;
    }
    char parent_path[file_storage::PATH_MAX_LENGTH] = {};
    snprintf(parent_path, sizeof(parent_path), "%s", normalized_path);
    char* parent_slash = strrchr(parent_path, '/');
    if (parent_slash && parent_slash != parent_path) *parent_slash = '\0';
    if (!mkdir_parents(parent_path)) return Status::IoError;
    if (truncate && SD_MMC.exists(sd_path)) {
        FileInfo existing;
        if (stat_locked(normalized_path, &existing) != Status::Ok) return Status::IoError;
        if (existing.is_directory) {
            if (!staging || remove_locked(normalized_path, true, true) != Status::Ok) {
                return Status::InvalidArgument;
            }
        } else if (!SD_MMC.remove(sd_path)) {
            return Status::IoError;
        }
    }
    const char* mode = SD_MMC.exists(sd_path) ? "r+" : FILE_WRITE;
    File file = SD_MMC.open(sd_path, mode);
    if (!file) return Status::IoError;
    if (!file.seek((uint32_t)offset)) {
        file.close();
        return Status::IoError;
    }
    const size_t written = length == 0 ? 0 : file.write(data, length);
    file.flush();
    file.close();
    if (bytes_written) *bytes_written = written;
    return written == length ? Status::Ok : Status::IoError;
}

}  // namespace file_storage::internal

namespace file_storage {

const char* status_name(Status status)
{
    switch (status) {
        case Status::Ok: return "ok";
        case Status::NotReady: return "not_ready";
        case Status::InvalidArgument: return "invalid_argument";
        case Status::InvalidPath: return "invalid_path";
        case Status::PathTooLong: return "path_too_long";
        case Status::NotFound: return "not_found";
        case Status::AlreadyExists: return "already_exists";
        case Status::IoError: return "io_error";
        case Status::NoSpace: return "no_space";
        case Status::Busy: return "busy";
        case Status::PermissionDenied: return "permission_denied";
        case Status::HashMismatch: return "hash_mismatch";
        case Status::InvalidTransaction: return "invalid_transaction";
        case Status::Incomplete: return "incomplete";
    }
    return "unknown";
}

bool init()
{
    return bsp_sd_init();
}

bool ready()
{
    return bsp_sd_ready();
}

SpaceInfo space()
{
    SpaceInfo result;
    if (!init() || !internal::lock(portMAX_DELAY)) return result;
    result.ready = true;
    result.total_bytes = SD_MMC.totalBytes();
    result.used_bytes = SD_MMC.usedBytes();
    result.free_bytes = result.total_bytes > result.used_bytes
        ? result.total_bytes - result.used_bytes : 0;
    internal::unlock();
    return result;
}

Status stat(const char* path, FileInfo* out)
{
    char normalized[PATH_MAX_LENGTH] = {};
    Status status = normalize_path(path, normalized, sizeof(normalized));
    if (status != Status::Ok) return status;
    if (!init()) return Status::NotReady;
    if (!internal::lock(portMAX_DELAY)) return Status::Busy;
    status = internal::stat_locked(normalized, out);
    internal::unlock();
    return status;
}

Status list(const char* path, FileInfo* out, size_t capacity, size_t* count)
{
    if (!out || !count) return Status::InvalidArgument;
    *count = 0;
    char normalized[PATH_MAX_LENGTH] = {};
    Status status = normalize_path(path, normalized, sizeof(normalized));
    if (status != Status::Ok) return status;
    if (!init()) return Status::NotReady;
    if (!internal::lock(portMAX_DELAY)) return Status::Busy;
    char sd_path[PATH_MAX_LENGTH] = {};
    if (!internal::to_sd_path(normalized, sd_path, sizeof(sd_path))) {
        internal::unlock();
        return Status::PathTooLong;
    }
    File directory = SD_MMC.open(sd_path, FILE_READ);
    if (!directory || !directory.isDirectory()) {
        if (directory) directory.close();
        internal::unlock();
        return Status::NotFound;
    }
    size_t scanned = 0;
    while (*count < capacity && scanned < capacity) {
        File entry = directory.openNextFile();
        if (!entry) break;
        scanned++;
        FileInfo& info = out[*count];
        memset(&info, 0, sizeof(info));
        Status child_status = normalize_path(entry.path(), info.path, sizeof(info.path));
        if (child_status == Status::Ok) {
            info.is_directory = entry.isDirectory();
            info.size = info.is_directory ? 0 : entry.size();
            info.modified = entry.getLastWrite();
            (*count)++;
        }
        entry.close();
    }
    directory.close();
    internal::unlock();
    return Status::Ok;
}

Status make_directory(const char* path, bool parents)
{
    char normalized[PATH_MAX_LENGTH] = {};
    Status status = normalize_path(path, normalized, sizeof(normalized));
    if (status != Status::Ok) return status;
    if (!init()) return Status::NotReady;
    if (!internal::lock(portMAX_DELAY)) return Status::Busy;
    status = internal::make_directory_locked(normalized, parents);
    internal::unlock();
    return status;
}

Status read_at(const char* path, uint64_t offset, uint8_t* buffer,
               size_t capacity, size_t* bytes_read)
{
    if (bytes_read) *bytes_read = 0;
    if (!buffer && capacity != 0) return Status::InvalidArgument;
    char normalized[PATH_MAX_LENGTH] = {};
    Status status = normalize_path(path, normalized, sizeof(normalized));
    if (status != Status::Ok) return status;
    if (!init()) return Status::NotReady;
    if (!internal::lock(portMAX_DELAY)) return Status::Busy;
    char sd_path[PATH_MAX_LENGTH] = {};
    if (!internal::to_sd_path(normalized, sd_path, sizeof(sd_path))) {
        internal::unlock();
        return Status::PathTooLong;
    }
    File file = SD_MMC.open(sd_path, FILE_READ);
    if (!file || file.isDirectory()) {
        if (file) file.close();
        internal::unlock();
        return Status::NotFound;
    }
    if (offset > file.size() || offset > UINT32_MAX || !file.seek((uint32_t)offset)) {
        file.close();
        internal::unlock();
        return Status::InvalidArgument;
    }
    const size_t got = capacity == 0 ? 0 : file.read(buffer, capacity);
    file.close();
    if (bytes_read) *bytes_read = got;
    internal::unlock();
    return Status::Ok;
}

Status write_at(const char* path, uint64_t offset, const uint8_t* data,
                size_t length, bool truncate, size_t* bytes_written)
{
    char normalized[PATH_MAX_LENGTH] = {};
    Status status = normalize_path(path, normalized, sizeof(normalized));
    if (status != Status::Ok) return status;
    if (!init()) return Status::NotReady;
    if (!internal::lock(portMAX_DELAY)) return Status::Busy;
    status = internal::write_at_locked(normalized, offset, data, length, truncate,
                                        bytes_written);
    internal::unlock();
    return status;
}

Status copy_file(const char* source, const char* destination)
{
    char source_path[PATH_MAX_LENGTH] = {};
    char destination_path[PATH_MAX_LENGTH] = {};
    Status status = normalize_path(source, source_path, sizeof(source_path));
    if (status != Status::Ok) return status;
    status = normalize_path(destination, destination_path, sizeof(destination_path));
    if (status != Status::Ok) return status;
    if (!init()) return Status::NotReady;
    if (!internal::lock(portMAX_DELAY)) return Status::Busy;
    if (!internal::mutation_allowed(destination_path, internal::is_staging_path(destination_path))) {
        internal::unlock();
        return Status::Busy;
    }
    char source_sd_path[PATH_MAX_LENGTH] = {};
    char destination_sd_path[PATH_MAX_LENGTH] = {};
    if (!internal::to_sd_path(source_path, source_sd_path, sizeof(source_sd_path)) ||
        !internal::to_sd_path(destination_path, destination_sd_path, sizeof(destination_sd_path))) {
        internal::unlock();
        return Status::PathTooLong;
    }
    File input = SD_MMC.open(source_sd_path, FILE_READ);
    if (!input || input.isDirectory()) {
        if (input) input.close();
        internal::unlock();
        return Status::NotFound;
    }
    char parent_path[PATH_MAX_LENGTH] = {};
    snprintf(parent_path, sizeof(parent_path), "%s", destination_path);
    char* parent_slash = strrchr(parent_path, '/');
    if (parent_slash && parent_slash != parent_path) *parent_slash = '\0';
    if (!mkdir_parents(parent_path)) {
        input.close();
        internal::unlock();
        return Status::IoError;
    }
    if (SD_MMC.exists(destination_sd_path) && !SD_MMC.remove(destination_sd_path)) {
        input.close();
        internal::unlock();
        return Status::IoError;
    }
    File output = SD_MMC.open(destination_sd_path, FILE_WRITE);
    uint8_t buffer[4096];
    bool ok = output && !output.isDirectory();
    while (ok && input.available()) {
        const size_t got = input.read(buffer, sizeof(buffer));
        ok = got > 0 && output.write(buffer, got) == got;
    }
    if (output) {
        output.flush();
        output.close();
    }
    input.close();
    internal::unlock();
    return ok ? Status::Ok : Status::IoError;
}

Status rename(const char* source, const char* destination)
{
    char source_path[PATH_MAX_LENGTH] = {};
    char destination_path[PATH_MAX_LENGTH] = {};
    Status status = normalize_path(source, source_path, sizeof(source_path));
    if (status != Status::Ok) return status;
    status = normalize_path(destination, destination_path, sizeof(destination_path));
    if (status != Status::Ok) return status;
    if (!init()) return Status::NotReady;
    if (!internal::lock(portMAX_DELAY)) return Status::Busy;
    status = internal::rename_locked(source_path, destination_path);
    internal::unlock();
    return status;
}

Status replace_file(const char* source, const char* destination)
{
    char source_path[PATH_MAX_LENGTH] = {};
    char destination_path[PATH_MAX_LENGTH] = {};
    Status status = normalize_path(source, source_path, sizeof(source_path));
    if (status != Status::Ok) return status;
    status = normalize_path(destination, destination_path, sizeof(destination_path));
    if (status != Status::Ok || strcmp(destination_path, ROOT_PATH) == 0) {
        return status == Status::Ok ? Status::PermissionDenied : status;
    }
    if (!init()) return Status::NotReady;
    if (!internal::lock(portMAX_DELAY)) return Status::Busy;
    FileInfo source_info;
    if (internal::stat_locked(source_path, &source_info) != Status::Ok || source_info.is_directory) {
        internal::unlock();
        return Status::NotFound;
    }
    FileInfo destination_info;
    if (internal::stat_locked(destination_path, &destination_info) == Status::Ok &&
        destination_info.is_directory) {
        internal::unlock();
        return Status::InvalidArgument;
    }
    if (!internal::mutation_allowed(destination_path, false)) {
        internal::unlock();
        return Status::Busy;
    }
    char destination_sd_path[PATH_MAX_LENGTH] = {};
    if (!internal::to_sd_path(destination_path, destination_sd_path,
                              sizeof(destination_sd_path))) {
        internal::unlock();
        return Status::PathTooLong;
    }
    constexpr const char* BACKUP = "/sdcard/.sync_backup/.replace.bak";
    internal::remove_locked(BACKUP, true, true);
    bool old_moved = false;
    if (SD_MMC.exists(destination_sd_path)) {
        status = internal::rename_locked(destination_path, BACKUP);
        old_moved = status == Status::Ok;
    } else {
        status = Status::Ok;
    }
    if (status == Status::Ok) status = internal::rename_locked(source_path, destination_path);
    if (status != Status::Ok) {
        if (old_moved) internal::rename_locked(BACKUP, destination_path);
        internal::unlock();
        return status;
    }
    if (old_moved) internal::remove_locked(BACKUP, true, true);
    internal::unlock();
    return Status::Ok;
}

Status remove(const char* path, bool recursive, bool confirmed)
{
    char normalized[PATH_MAX_LENGTH] = {};
    Status status = normalize_path(path, normalized, sizeof(normalized));
    if (status != Status::Ok) return status;
    if (!init()) return Status::NotReady;
    if (!internal::lock(portMAX_DELAY)) return Status::Busy;
    status = internal::remove_locked(normalized, recursive, confirmed);
    internal::unlock();
    return status;
}

Status playback_begin(const char* path)
{
    char normalized[PATH_MAX_LENGTH] = {};
    Status status = normalize_path(path, normalized, sizeof(normalized));
    if (status != Status::Ok) return status;
    if (!init()) return Status::NotReady;
    if (!internal::lock(portMAX_DELAY)) return Status::Busy;
    if (s_playback_active) {
        internal::unlock();
        return Status::Busy;
    }
    s_playback_active = true;
    snprintf(s_playback_path, sizeof(s_playback_path), "%s", normalized);
    internal::unlock();
    return Status::Ok;
}

void playback_end()
{
    if (!internal::lock(portMAX_DELAY)) return;
    s_playback_active = false;
    s_playback_path[0] = '\0';
    internal::unlock();
}

bool playback_active()
{
    return s_playback_active;
}

}  // namespace file_storage
