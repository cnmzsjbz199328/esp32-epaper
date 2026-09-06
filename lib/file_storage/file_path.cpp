#include "file_storage.h"

#include <string.h>

namespace {

bool valid_component(const char* begin, size_t length)
{
    if (length == 0 || (length == 1 && begin[0] == '.') ||
        (length == 2 && begin[0] == '.' && begin[1] == '.')) {
        return length == 1 && begin[0] == '.';
    }
    for (size_t i = 0; i < length; i++) {
        const unsigned char c = (unsigned char)begin[i];
        if (c < 0x20 || c == '\\' || c == ':') return false;
    }
    return true;
}

bool append_component(char* out, size_t capacity, size_t* length,
                      const char* begin, size_t component_length)
{
    if (!valid_component(begin, component_length)) return false;
    if (component_length == 1 && begin[0] == '.') return true;
    const size_t separator = *length >= strlen(file_storage::ROOT_PATH) ? 1 : 0;
    if (*length + separator + component_length + 1 > capacity) return false;
    if (separator) out[(*length)++] = '/';
    memcpy(out + *length, begin, component_length);
    *length += component_length;
    out[*length] = '\0';
    return true;
}

}  // namespace

namespace file_storage {

Status normalize_path(const char* requested, char* normalized, size_t capacity)
{
    if (!requested || !normalized || capacity == 0) return Status::InvalidArgument;
    const size_t input_length = strlen(requested);
    if (input_length == 0) return Status::InvalidPath;

    const size_t root_length = strlen(ROOT_PATH);
    size_t start = 0;
    if (input_length >= root_length && strncmp(requested, ROOT_PATH, root_length) == 0 &&
        (requested[root_length] == '\0' || requested[root_length] == '/')) {
        start = root_length;
    } else if (requested[0] == '/') {
        /* Protocol-facing paths such as /video/x are SD-relative. */
        start = 0;
    } else {
        return Status::InvalidPath;
    }

    if (root_length + 1 > capacity) return Status::PathTooLong;
    memcpy(normalized, ROOT_PATH, root_length + 1);
    size_t output_length = root_length;
    size_t component_start = start;
    while (component_start <= input_length) {
        while (component_start < input_length && requested[component_start] == '/') component_start++;
        if (component_start >= input_length) break;
        size_t component_end = component_start;
        while (component_end < input_length && requested[component_end] != '/') component_end++;
        const size_t component_length = component_end - component_start;
        if (component_length == 2 && requested[component_start] == '.' &&
            requested[component_start + 1] == '.') {
            return Status::InvalidPath;
        }
        if (!append_component(normalized, capacity, &output_length,
                              requested + component_start, component_length)) {
            return valid_component(requested + component_start, component_length)
                ? Status::PathTooLong : Status::InvalidPath;
        }
        component_start = component_end + 1;
    }
    return Status::Ok;
}

}  // namespace file_storage
