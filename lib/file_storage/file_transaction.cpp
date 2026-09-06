#include "file_transaction.h"
#include "file_storage_internal.h"

#include <SD_MMC.h>
#include <string.h>

namespace {

constexpr const char* STAGING_ROOT = "/sdcard/.sync_staging";
constexpr const char* BACKUP_ROOT = "/sdcard/.sync_backup";
constexpr const char* STATE_ROOT = "/sdcard/.sync_transactions";
constexpr const char* VERSION_ROOT = "/sdcard/.sync_versions";

bool valid_id(const char* id)
{
    if (!id || id[0] == '\0') return false;
    for (size_t i = 0; id[i]; i++) {
        const char c = id[i];
        if (i >= file_storage::TRANSACTION_ID_MAX_LENGTH ||
            !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return true;
}

bool valid_hex(const char* value)
{
    if (!value || strlen(value) != file_storage::SHA256_HEX_LENGTH) return false;
    for (size_t i = 0; i < file_storage::SHA256_HEX_LENGTH; i++) {
        const char c = value[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

bool valid_relative_path(const char* path)
{
    if (!path || path[0] == '\0' || path[0] == '/') return false;
    for (size_t i = 0; path[i]; i++) {
        const unsigned char c = (unsigned char)path[i];
        if (c < 0x20 || c == '\\' || c == ':') return false;
    }
    char normalized[file_storage::PATH_MAX_LENGTH] = {};
    char prefixed[file_storage::PATH_MAX_LENGTH] = {};
    if (snprintf(prefixed, sizeof(prefixed), "/%s", path) >= (int)sizeof(prefixed)) return false;
    return file_storage::normalize_path(prefixed, normalized, sizeof(normalized)) ==
           file_storage::Status::Ok && strcmp(normalized, file_storage::ROOT_PATH) != 0;
}

void lowercase_hex(char* value)
{
    for (size_t i = 0; value && value[i]; i++) {
        if (value[i] >= 'A' && value[i] <= 'F') value[i] = (char)(value[i] - 'A' + 'a');
    }
}

}  // namespace

namespace file_storage {

FileTransaction::FileTransaction() = default;

FileTransaction::~FileTransaction()
{
    if (active_) abort();
}

int FileTransaction::find_file(const char* relative_path) const
{
    if (!relative_path) return -1;
    for (size_t i = 0; i < file_count_; i++) {
        if (strcmp(files_[i].path, relative_path) == 0) return (int)i;
    }
    return -1;
}

Status FileTransaction::build_path(const char* relative_path, char* out, size_t capacity) const
{
    if (!relative_path || !out || capacity == 0 || !valid_relative_path(relative_path)) {
        return Status::InvalidPath;
    }
    if (snprintf(out, capacity, "%s/%s", payload_root_, relative_path) >= (int)capacity) {
        return Status::PathTooLong;
    }
    return Status::Ok;
}

Status FileTransaction::write_state(const char* state)
{
    if (!state) return Status::InvalidArgument;
    char content[PATH_MAX_LENGTH + 32] = {};
    const int content_length = snprintf(content, sizeof(content), "%s\ntarget=%s\n",
                                        state, target_root_);
    if (content_length < 0 || content_length >= (int)sizeof(content)) return Status::PathTooLong;
    size_t written = 0;
    return internal::write_at_locked(state_path_, 0,
                                     reinterpret_cast<const uint8_t*>(content),
                                     (size_t)content_length, true, &written) == Status::Ok &&
                   written == (size_t)content_length ? Status::Ok : Status::IoError;
}

Status FileTransaction::begin(const char* transaction_id, const char* target_root,
                              const TransactionFileSpec* files, size_t file_count,
                              uint64_t required_bytes)
{
    if (active_ || !valid_id(transaction_id) || !files || file_count == 0 ||
        file_count > TRANSACTION_FILE_MAX) return Status::InvalidArgument;
    if (file_count > 0 && required_bytes == 0) {
        for (size_t i = 0; i < file_count; i++) {
            if (UINT64_MAX - required_bytes < files[i].size) return Status::InvalidArgument;
            required_bytes += files[i].size;
        }
    }
    if (!valid_relative_path(files[0].path)) return Status::InvalidPath;

    Status status = normalize_path(target_root, target_root_, sizeof(target_root_));
    if (status != Status::Ok || strcmp(target_root_, ROOT_PATH) == 0 ||
        strncmp(target_root_, "/sdcard/.sync_", strlen("/sdcard/.sync_")) == 0) {
        return status == Status::Ok ? Status::PermissionDenied : status;
    }
    snprintf(transaction_id_, sizeof(transaction_id_), "%s", transaction_id);
    const int staging_length = snprintf(staging_root_, sizeof(staging_root_), "%s/%s",
                                        STAGING_ROOT, transaction_id_);
    const int payload_length = snprintf(payload_root_, sizeof(payload_root_), "%s/payload",
                                        staging_root_);
    const int backup_length = snprintf(backup_root_, sizeof(backup_root_), "%s/%s",
                                       BACKUP_ROOT, transaction_id_);
    const int state_length = snprintf(state_path_, sizeof(state_path_), "%s/%s.state",
                                       STATE_ROOT, transaction_id_);
    const int version_length = snprintf(version_path_, sizeof(version_path_), "%s/%s.commit",
                                        VERSION_ROOT, transaction_id_);
    if (staging_length < 0 || payload_length < 0 || backup_length < 0 ||
        state_length < 0 || version_length < 0 ||
        staging_length >= (int)sizeof(staging_root_) ||
        payload_length >= (int)sizeof(payload_root_) ||
        backup_length >= (int)sizeof(backup_root_) ||
        state_length >= (int)sizeof(state_path_) ||
        version_length >= (int)sizeof(version_path_)) return Status::PathTooLong;

    if (!init()) return Status::NotReady;
    if (!internal::lock(portMAX_DELAY)) return Status::Busy;

    FileInfo existing;
    if (internal::stat_locked(staging_root_, &existing) == Status::Ok ||
        internal::stat_locked(version_path_, &existing) == Status::Ok) {
        internal::unlock();
        return Status::AlreadyExists;
    }
    status = internal::make_directory_locked(STAGING_ROOT, true);
    if (status == Status::Ok) status = internal::make_directory_locked(BACKUP_ROOT, true);
    if (status == Status::Ok) status = internal::make_directory_locked(STATE_ROOT, true);
    if (status == Status::Ok) status = internal::make_directory_locked(VERSION_ROOT, true);
    if (status == Status::Ok) status = internal::make_directory_locked(payload_root_, true);
    if (status != Status::Ok) {
        internal::unlock();
        return status;
    }

    const uint64_t total_bytes = SD_MMC.totalBytes();
    const uint64_t used_bytes = SD_MMC.usedBytes();
    const uint64_t free_bytes = total_bytes > used_bytes ? total_bytes - used_bytes : 0;
    if (total_bytes == 0 || free_bytes < required_bytes) {
        internal::remove_locked(staging_root_, true, true);
        internal::unlock();
        return Status::NoSpace;
    }

    file_count_ = 0;
    for (size_t i = 0; i < file_count; i++) {
        if (!valid_relative_path(files[i].path) || !valid_hex(files[i].sha256) ||
            find_file(files[i].path) >= 0 ||
            strlen(files[i].path) >= TRANSACTION_PATH_MAX_LENGTH) {
            internal::remove_locked(staging_root_, true, true);
            internal::unlock();
            return Status::InvalidArgument;
        }
        snprintf(files_[file_count_].path, sizeof(files_[file_count_].path), "%s", files[i].path);
        files_[file_count_].size = files[i].size;
        snprintf(files_[file_count_].sha256, sizeof(files_[file_count_].sha256), "%s", files[i].sha256);
        lowercase_hex(files_[file_count_].sha256);
        file_count_++;
    }
    for (size_t i = 0; i < file_count_; i++) {
        if (files_[i].size != 0) continue;
        char empty_path[PATH_MAX_LENGTH] = {};
        status = build_path(files_[i].path, empty_path, sizeof(empty_path));
        size_t written = 0;
        if (status == Status::Ok) {
            status = internal::write_at_locked(empty_path, 0, nullptr, 0, true, &written);
            if (status == Status::Ok && written != 0) status = Status::IoError;
        }
        if (status != Status::Ok) {
            internal::remove_locked(staging_root_, true, true);
            internal::unlock();
            file_count_ = 0;
            return status;
        }
    }
    status = write_state("PREPARED");
    internal::unlock();
    if (status != Status::Ok) {
        file_count_ = 0;
        return status;
    }
    active_ = true;
    return Status::Ok;
}

Status FileTransaction::write_chunk(const char* relative_path, uint64_t offset,
                                    const uint8_t* data, size_t length,
                                    size_t* bytes_written)
{
    if (bytes_written) *bytes_written = 0;
    if (!active_ || (!data && length != 0)) return Status::InvalidArgument;
    const int index = find_file(relative_path);
    if (index < 0) return Status::InvalidPath;
    const FileState& spec = files_[index];
    if (offset > spec.size || length > spec.size - offset) return Status::InvalidArgument;

    char path[PATH_MAX_LENGTH] = {};
    Status status = build_path(relative_path, path, sizeof(path));
    if (status != Status::Ok) return status;
    if (!internal::lock(portMAX_DELAY)) return Status::Busy;
    if (offset != 0) {
        FileInfo existing;
        status = internal::stat_locked(path, &existing);
        if (status != Status::Ok || existing.is_directory || existing.size != offset) {
            internal::unlock();
            return Status::Incomplete;
        }
    }
    status = internal::write_at_locked(path, offset, data, length, offset == 0, bytes_written);
    internal::unlock();
    return status;
}

Status FileTransaction::staged_size(const char* relative_path, uint64_t* size)
{
    if (size) *size = 0;
    if (!active_ || !size) return Status::InvalidTransaction;
    char path[PATH_MAX_LENGTH] = {};
    Status status = build_path(relative_path, path, sizeof(path));
    if (status != Status::Ok) return status;
    if (!internal::lock(portMAX_DELAY)) return Status::Busy;
    FileInfo info;
    status = internal::stat_locked(path, &info);
    if (status == Status::Ok) {
        if (info.is_directory) status = Status::InvalidArgument;
        else *size = info.size;
    }
    internal::unlock();
    return status;
}

Status FileTransaction::verify_locked()
{
    for (size_t i = 0; i < file_count_; i++) {
        char path[PATH_MAX_LENGTH] = {};
        Status status = build_path(files_[i].path, path, sizeof(path));
        if (status != Status::Ok) return status;
        FileInfo info;
        status = internal::stat_locked(path, &info);
        if (status != Status::Ok || info.is_directory) return Status::Incomplete;
        if (info.size != files_[i].size) return Status::Incomplete;
        char actual[SHA256_HEX_LENGTH + 1] = {};
        status = internal::sha256_file_locked(path, actual);
        if (status != Status::Ok) return status;
        if (strcmp(actual, files_[i].sha256) != 0) return Status::HashMismatch;
    }
    return Status::Ok;
}

Status FileTransaction::verify()
{
    if (!active_) return Status::InvalidTransaction;
    if (!internal::lock(portMAX_DELAY)) return Status::Busy;
    const Status status = verify_locked();
    internal::unlock();
    return status;
}

Status FileTransaction::commit()
{
    if (!active_) return Status::InvalidTransaction;
    if (!internal::lock(portMAX_DELAY)) return Status::Busy;
    if (playback_active()) {
        internal::unlock();
        return Status::Busy;
    }
    Status status = verify_locked();
    if (status != Status::Ok) {
        internal::unlock();
        return status;
    }
    status = write_state("COMMITTING");
    bool moved_old = false;
    bool moved_new = false;
    if (status == Status::Ok) {
        FileInfo target;
        if (internal::stat_locked(target_root_, &target) == Status::Ok) {
            if (!target.is_directory) status = Status::AlreadyExists;
            else status = internal::rename_locked(target_root_, backup_root_);
            moved_old = status == Status::Ok;
        }
    }
    if (status == Status::Ok) {
        status = internal::rename_locked(payload_root_, target_root_);
        moved_new = status == Status::Ok;
    }
    if (status == Status::Ok) {
        const char marker[] = "COMMITTED\n";
        size_t written = 0;
        status = internal::write_at_locked(version_path_, 0,
                                            reinterpret_cast<const uint8_t*>(marker),
                                            sizeof(marker) - 1, true, &written);
        if (status == Status::Ok && written != sizeof(marker) - 1) status = Status::IoError;
    }
    if (status != Status::Ok) {
        if (moved_new) internal::remove_locked(target_root_, true, true);
        if (moved_old) internal::rename_locked(backup_root_, target_root_);
        internal::remove_locked(staging_root_, true, true);
        write_state("ABORTED");
        internal::unlock();
        active_ = false;
        return status;
    }

    write_state("COMMITTED");
    internal::remove_locked(staging_root_, true, true);
    if (moved_old) internal::remove_locked(backup_root_, true, true);
    internal::unlock();
    active_ = false;
    return Status::Ok;
}

Status FileTransaction::abort()
{
    if (!active_) return Status::InvalidTransaction;
    if (!internal::lock(portMAX_DELAY)) return Status::Busy;
    const Status status = internal::remove_locked(staging_root_, true, true);
    if (status == Status::Ok || status == Status::NotFound) {
        write_state("ABORTED");
        active_ = false;
    }
    internal::unlock();
    return status == Status::NotFound ? Status::Ok : status;
}

Status recover_transactions(void)
{
    if (!init()) return Status::NotReady;
    if (!internal::lock(portMAX_DELAY)) return Status::Busy;
    char transactions_path[PATH_MAX_LENGTH] = {};
    if (!internal::to_sd_path("/sdcard/.sync_transactions",
                              transactions_path, sizeof(transactions_path))) {
        internal::unlock();
        return Status::PathTooLong;
    }
    File directory = SD_MMC.open(transactions_path, FILE_READ);
    if (!directory) {
        internal::unlock();
        return Status::Ok;
    }
    if (!directory.isDirectory()) {
        directory.close();
        internal::unlock();
        return Status::InvalidArgument;
    }

    while (true) {
        File state_file = directory.openNextFile();
        if (!state_file) break;
        char state_path[PATH_MAX_LENGTH] = {};
        snprintf(state_path, sizeof(state_path), "%s", state_file.path());
        const char* name = strrchr(state_path, '/');
        name = name ? name + 1 : state_path;
        const size_t name_length = strlen(name);
        if (name_length <= 6 || strcmp(name + name_length - 6, ".state") != 0) {
            state_file.close();
            continue;
        }
        char transaction_id[TRANSACTION_ID_MAX_LENGTH + 1] = {};
        const size_t id_length = name_length - 6;
        if (id_length > TRANSACTION_ID_MAX_LENGTH) {
            state_file.close();
            continue;
        }
        memcpy(transaction_id, name, id_length);
        transaction_id[id_length] = '\0';
        if (!valid_id(transaction_id)) {
            state_file.close();
            continue;
        }
        char content[PATH_MAX_LENGTH + 64] = {};
        const size_t content_size = state_file.read((uint8_t*)content, sizeof(content) - 1);
        content[content_size] = '\0';
        state_file.close();

        char target[PATH_MAX_LENGTH] = {};
        char* target_line = strstr(content, "target=");
        if (target_line) {
            target_line += strlen("target=");
            char* end = strchr(target_line, '\n');
            if (end) *end = '\0';
            snprintf(target, sizeof(target), "%s", target_line);
        }
        char normalized_target[PATH_MAX_LENGTH] = {};
        if (target[0] && (normalize_path(target, normalized_target, sizeof(normalized_target)) != Status::Ok ||
                          strcmp(normalized_target, target) != 0 ||
                          strcmp(normalized_target, ROOT_PATH) == 0 ||
                          strncmp(normalized_target, "/sdcard/.sync_", strlen("/sdcard/.sync_")) == 0)) {
            target[0] = '\0';
        } else if (target[0]) {
            snprintf(target, sizeof(target), "%s", normalized_target);
        }
        char staging[PATH_MAX_LENGTH] = {};
        char backup[PATH_MAX_LENGTH] = {};
        snprintf(staging, sizeof(staging), "/sdcard/.sync_staging/%s", transaction_id);
        snprintf(backup, sizeof(backup), "/sdcard/.sync_backup/%s", transaction_id);

        if (strncmp(content, "COMMITTING\n", 11) == 0 && target[0]) {
            FileInfo target_info;
            FileInfo backup_info;
            FileInfo staging_info;
            const bool have_target = internal::stat_locked(target, &target_info) == Status::Ok;
            const bool have_backup = internal::stat_locked(backup, &backup_info) == Status::Ok;
            const bool have_staging = internal::stat_locked(staging, &staging_info) == Status::Ok;
            if (have_target && have_backup) {
                /* New directory was installed before the reset. */
                internal::remove_locked(backup, true, true);
                internal::remove_locked(staging, true, true);
                char marker[PATH_MAX_LENGTH + 32] = {};
                const int marker_length = snprintf(marker, sizeof(marker), "COMMITTED\ntarget=%s\n", target);
                size_t written = 0;
                internal::write_at_locked(state_path, 0, (const uint8_t*)marker,
                                          marker_length > 0 ? (size_t)marker_length : 0,
                                          true, &written);
            } else if (!have_target && have_backup) {
                /* Old directory was moved but the new one was not installed. */
                internal::rename_locked(backup, target);
                internal::remove_locked(staging, true, true);
                char marker[PATH_MAX_LENGTH + 32] = {};
                const int marker_length = snprintf(marker, sizeof(marker), "ABORTED\ntarget=%s\n", target);
                size_t written = 0;
                internal::write_at_locked(state_path, 0, (const uint8_t*)marker,
                                          marker_length > 0 ? (size_t)marker_length : 0,
                                          true, &written);
            } else if (have_target && !have_backup && !have_staging) {
                /* No previous target existed and the staged directory has
                 * already been installed. A missing marker is recoverable. */
                internal::remove_locked(backup, true, true);
                char marker[PATH_MAX_LENGTH + 32] = {};
                const int marker_length = snprintf(marker, sizeof(marker),
                                                   "COMMITTED\ntarget=%s\n", target);
                size_t written = 0;
                internal::write_at_locked(state_path, 0, (const uint8_t*)marker,
                                          marker_length > 0 ? (size_t)marker_length : 0,
                                          true, &written);
            } else {
                /* The old target is still authoritative, or no version existed. */
                internal::remove_locked(staging, true, true);
                char marker[PATH_MAX_LENGTH + 32] = {};
                const int marker_length = snprintf(marker, sizeof(marker), "ABORTED\ntarget=%s\n", target);
                size_t written = 0;
                internal::write_at_locked(state_path, 0, (const uint8_t*)marker,
                                          marker_length > 0 ? (size_t)marker_length : 0,
                                          true, &written);
            }
        } else if (strncmp(content, "PREPARED\n", 9) == 0 ||
                   strncmp(content, "ABORTED\n", 8) == 0 ||
                   strncmp(content, "COMMITTED\n", 10) == 0) {
            /* A host can retry a prepared transaction; do not leave an old
             * staging directory that would make the transaction ID conflict. */
            internal::remove_locked(staging, true, true);
            if (strncmp(content, "COMMITTED\n", 10) == 0) {
                internal::remove_locked(backup, true, true);
            }
        }
    }
    directory.close();
    /* Anything left in staging after the state records have been replayed is
     * an orphan from a failed prepare or a raw upload. It is never visible
     * content, so discard it before accepting new transactions. */
    internal::remove_locked(STAGING_ROOT, true, true);
    internal::unlock();
    return Status::Ok;
}

}  // namespace file_storage
