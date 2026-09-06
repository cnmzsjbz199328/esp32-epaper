#pragma once

#include "file_storage.h"

namespace file_storage::internal {

/* SD_MMC::open() receives paths relative to the /sdcard mount. */
bool to_sd_path(const char* normalized_path, char* out, size_t capacity);
bool lock(uint32_t timeout_ms);
void unlock();

bool is_staging_path(const char* normalized_path);
bool mutation_allowed(const char* normalized_path, bool staging_write);

Status stat_locked(const char* normalized_path, FileInfo* out);
Status make_directory_locked(const char* normalized_path, bool parents);
Status rename_locked(const char* source, const char* destination);
Status remove_locked(const char* normalized_path, bool recursive, bool confirmed);
Status write_at_locked(const char* normalized_path, uint64_t offset,
                       const uint8_t* data, size_t length, bool truncate,
                       size_t* bytes_written);
Status sha256_file_locked(const char* normalized_path,
                          char hex[SHA256_HEX_LENGTH + 1]);

}  // namespace file_storage::internal
