#include "file_storage.h"
#include "file_storage_internal.h"

#include <SD_MMC.h>
#include <mbedtls/sha256.h>
#include <string.h>

namespace file_storage {

namespace internal {

Status sha256_file_locked(const char* normalized, char hex[SHA256_HEX_LENGTH + 1])
{
    if (!hex) return Status::InvalidArgument;
    hex[0] = '\0';
    char sd_path[PATH_MAX_LENGTH] = {};
    if (!internal::to_sd_path(normalized, sd_path, sizeof(sd_path))) return Status::PathTooLong;
    File file = SD_MMC.open(sd_path, FILE_READ);
    if (!file || file.isDirectory()) {
        if (file) file.close();
        return Status::NotFound;
    }

    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    mbedtls_sha256_starts(&context, 0);
    /* This function is also called from the HTTP task; keep its stack usage
     * small because WebServer and the SD driver already have deep call frames. */
    uint8_t buffer[1024];
    Status status = Status::Ok;
    while (file.available()) {
        const size_t got = file.read(buffer, sizeof(buffer));
        if (got == 0) {
            status = Status::IoError;
            break;
        }
        mbedtls_sha256_update(&context, buffer, got);
    }
    uint8_t digest[32] = {};
    if (status == Status::Ok) mbedtls_sha256_finish(&context, digest);
    mbedtls_sha256_free(&context);
    file.close();
    if (status != Status::Ok) return status;

    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(digest); i++) {
        hex[i * 2] = digits[digest[i] >> 4];
        hex[i * 2 + 1] = digits[digest[i] & 0x0F];
    }
    hex[SHA256_HEX_LENGTH] = '\0';
    return Status::Ok;
}

}  // namespace internal

Status sha256_file(const char* path, char hex[SHA256_HEX_LENGTH + 1])
{
    char normalized[PATH_MAX_LENGTH] = {};
    Status result = normalize_path(path, normalized, sizeof(normalized));
    if (result != Status::Ok) return result;
    if (!init()) return Status::NotReady;
    if (!internal::lock(portMAX_DELAY)) return Status::Busy;
    result = internal::sha256_file_locked(normalized, hex);
    internal::unlock();
    return result;
}

Status verify_sha256(const char* path, const char* expected_hex)
{
    if (!expected_hex || strlen(expected_hex) != SHA256_HEX_LENGTH) {
        return Status::InvalidArgument;
    }
    char actual[SHA256_HEX_LENGTH + 1] = {};
    const Status status = sha256_file(path, actual);
    if (status != Status::Ok) return status;
    for (size_t i = 0; i < SHA256_HEX_LENGTH; i++) {
        char expected = expected_hex[i];
        if (expected >= 'A' && expected <= 'F') expected = (char)(expected - 'A' + 'a');
        if (actual[i] != expected) return Status::HashMismatch;
    }
    return Status::Ok;
}

}  // namespace file_storage
