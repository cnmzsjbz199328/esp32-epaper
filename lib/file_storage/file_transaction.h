#pragma once

#include "file_storage.h"

namespace file_storage {

constexpr size_t TRANSACTION_ID_MAX_LENGTH = 48;
constexpr size_t TRANSACTION_PATH_MAX_LENGTH = 160;
/* Story packages may include FVID, audio, images, and optional source or
 * metadata files. Keep one transaction atomic without forcing hosts to split
 * a package into independently replaceable pieces. */
constexpr size_t TRANSACTION_FILE_MAX = 128;

struct TransactionFileSpec {
    const char* path;       /* Relative to target_root, e.g. story.json. */
    uint64_t size;
    const char* sha256;     /* Lower- or upper-case 64-character hex. */
};

/* Repairs transactions left in COMMITTING state by a reset or power loss.
 * PREPARED transactions are discarded safely; the host can prepare them
 * again using its manifest. */
Status recover_transactions(void);

/* Stages a complete directory replacement. The target is changed only after
 * every declared file has the expected length and SHA-256. */
class FileTransaction {
public:
    FileTransaction();
    ~FileTransaction();

    FileTransaction(const FileTransaction&) = delete;
    FileTransaction& operator=(const FileTransaction&) = delete;

    Status begin(const char* transaction_id, const char* target_root,
                 const TransactionFileSpec* files, size_t file_count,
                 uint64_t required_bytes = 0);
    Status write_chunk(const char* relative_path, uint64_t offset,
                       const uint8_t* data, size_t length,
                       size_t* bytes_written = nullptr);
    Status staged_size(const char* relative_path, uint64_t* size);
    Status verify();
    Status commit();
    Status abort();

    bool active() const { return active_; }
    const char* id() const { return transaction_id_; }
    const char* target_root() const { return target_root_; }
    const char* staging_root() const { return payload_root_; }

private:
    struct FileState {
        char path[TRANSACTION_PATH_MAX_LENGTH] = {};
        uint64_t size = 0;
        char sha256[SHA256_HEX_LENGTH + 1] = {};
    };

    Status build_path(const char* relative_path, char* out, size_t capacity) const;
    int find_file(const char* relative_path) const;
    Status write_state(const char* state);
    Status verify_locked();

    char transaction_id_[TRANSACTION_ID_MAX_LENGTH + 1] = {};
    char target_root_[PATH_MAX_LENGTH] = {};
    char staging_root_[PATH_MAX_LENGTH] = {};
    char payload_root_[PATH_MAX_LENGTH] = {};
    char backup_root_[PATH_MAX_LENGTH] = {};
    char state_path_[PATH_MAX_LENGTH] = {};
    char version_path_[PATH_MAX_LENGTH] = {};
    FileState files_[TRANSACTION_FILE_MAX] = {};
    size_t file_count_ = 0;
    bool active_ = false;
};

}  // namespace file_storage
