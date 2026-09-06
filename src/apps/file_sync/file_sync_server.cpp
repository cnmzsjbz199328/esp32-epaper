#include "file_sync_server.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "file_storage.h"
#include "file_transaction.h"
#include "file_validators.h"
#include "../../../lib/nvs_settings/nvs_settings.h"

namespace {

constexpr uint16_t SERVER_PORT = 80;
constexpr size_t LIST_CAPACITY = 64;
constexpr size_t RAW_PATH_LENGTH = file_storage::PATH_MAX_LENGTH;
/* Coalesce one host request before the slow TF flush. The buffer is allocated
 * from PSRAM at first use so Wi-Fi and HTTP task internal RAM stay available. */
constexpr size_t RAW_UPLOAD_BUFFER_SIZE = 524288;
constexpr const char* HTTP_STAGING_PATH = "/sdcard/.sync_staging/.http_upload";

WebServer s_server(SERVER_PORT);
WiFiUDP s_discovery;
TaskHandle_t s_task = nullptr;
bool s_routes_registered = false;
bool s_server_started = false;
bool s_discovery_started = false;
volatile uint32_t s_commit_generation = 0;
uint32_t s_last_wifi_attempt_ms = 0;
uint32_t s_last_state_update_ms = 0;
bool s_state_update_initialized = false;
portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;
file_sync_server_state_t s_state = {};
file_storage::FileTransaction s_transaction;
file_storage::FileInfo s_list_entries[LIST_CAPACITY] = {};

constexpr uint16_t DISCOVERY_PORT = 4210;
constexpr const char* DISCOVERY_REQUEST = "FILE_SYNC_DISCOVER\n";

enum raw_mode_t { RAW_NONE, RAW_DIRECT_FILE, RAW_TRANSACTION_CHUNK };
struct raw_upload_t {
    raw_mode_t mode = RAW_NONE;
    bool authorized = false;
    bool failed = false;
    file_storage::Status error = file_storage::Status::Ok;
    char target[RAW_PATH_LENGTH] = {};
    char transaction[file_storage::TRANSACTION_ID_MAX_LENGTH + 1] = {};
    char relative_path[file_storage::TRANSACTION_PATH_MAX_LENGTH] = {};
    uint64_t offset = 0;
    uint64_t expected_total = UINT64_MAX;
    uint64_t declared_length = UINT64_MAX;
    bool final_chunk = true;
    size_t received = 0;
    char expected_sha256[file_storage::SHA256_HEX_LENGTH + 1] = {};
};
raw_upload_t s_raw;
uint8_t* s_raw_body = nullptr;
size_t s_raw_body_size = 0;

void update_state(const char* status, const char* error = nullptr)
{
    portENTER_CRITICAL(&s_state_mux);
    if (status) snprintf(s_state.last_status, sizeof(s_state.last_status), "%s", status);
    if (error) snprintf(s_state.last_error, sizeof(s_state.last_error), "%s", error);
    else if (status) s_state.last_error[0] = '\0';
    portEXIT_CRITICAL(&s_state_mux);
}

void update_network_state()
{
    const uint32_t now = millis();
    if (s_state_update_initialized && now - s_last_state_update_ms < 500) return;
    s_last_state_update_ms = now;
    s_state_update_initialized = true;
    const bool connected = WiFi.status() == WL_CONNECTED;
    String ip = connected ? WiFi.localIP().toString() : "-";
    portENTER_CRITICAL(&s_state_mux);
    s_state.wifi_connected = connected;
    s_state.running = s_server_started;
    snprintf(s_state.ip, sizeof(s_state.ip), "%s", ip.c_str());
    portEXIT_CRITICAL(&s_state_mux);
}

int http_status(file_storage::Status status)
{
    using file_storage::Status;
    switch (status) {
        case Status::Ok: return 200;
        case Status::InvalidArgument:
        case Status::InvalidPath:
        case Status::PathTooLong: return 400;
        case Status::NotFound: return 404;
        case Status::AlreadyExists:
        case Status::Incomplete: return 409;
        case Status::Busy: return 409;
        case Status::PermissionDenied: return 403;
        case Status::NoSpace: return 507;
        case Status::HashMismatch: return 422;
        case Status::NotReady: return 503;
        case Status::InvalidTransaction: return 409;
        default: return 500;
    }
}

void send_json(int code, JsonDocument& document)
{
    String body;
    serializeJson(document, body);
    s_server.send(code, "application/json", body);
}

void send_error(int code, const char* error, const char* detail = nullptr)
{
    JsonDocument document;
    document["ok"] = false;
    document["error"] = error ? error : "error";
    if (detail) document["detail"] = detail;
    send_json(code, document);
}

void send_status(file_storage::Status status, const char* operation)
{
    if (status == file_storage::Status::Ok) {
        JsonDocument document;
        document["ok"] = true;
        document["operation"] = operation;
        send_json(200, document);
        return;
    }
    update_state(nullptr, file_storage::status_name(status));
    send_error(http_status(status), file_storage::status_name(status), operation);
}

bool authorized()
{
    const String expected = ecp::settings::get_sync_token();
    if (expected.length() != 32 || !s_server.hasHeader("X-File-Sync-Token") ||
        s_server.header("X-File-Sync-Token") != expected) {
        send_error(401, "unauthorized", "X-File-Sync-Token required");
        return false;
    }
    return true;
}

bool authorized_without_response()
{
    const String expected = ecp::settings::get_sync_token();
    return expected.length() == 32 && s_server.hasHeader("X-File-Sync-Token") &&
           s_server.header("X-File-Sync-Token") == expected;
}

String public_path(const char* path)
{
    if (!path) return "/";
    const size_t root_length = strlen(file_storage::ROOT_PATH);
    if (strncmp(path, file_storage::ROOT_PATH, root_length) == 0) {
        const char* relative = path + root_length;
        return relative[0] ? String(relative) : String("/");
    }
    return String(path);
}

bool internal_entry(const file_storage::FileInfo& info)
{
    const char* base = strrchr(info.path, '/');
    base = base ? base + 1 : info.path;
    return strncmp(base, ".sync_", 6) == 0;
}

const char* content_type(const char* path)
{
    if (!path) return "application/octet-stream";
    const char* extension = strrchr(path, '.');
    if (!extension) return "application/octet-stream";
    if (strcasecmp(extension, ".json") == 0) return "application/json";
    if (strcasecmp(extension, ".txt") == 0 || strcasecmp(extension, ".md") == 0) return "text/plain";
    if (strcasecmp(extension, ".png") == 0) return "image/png";
    if (strcasecmp(extension, ".jpg") == 0 || strcasecmp(extension, ".jpeg") == 0) return "image/jpeg";
    if (strcasecmp(extension, ".wav") == 0) return "audio/wav";
    return "application/octet-stream";
}

void handle_device()
{
    JsonDocument document;
    document["ok"] = true;
    document["protocol"] = "file-sync/1";
    char id[20] = {};
    snprintf(id, sizeof(id), "s3-%06llX", (unsigned long long)(ESP.getEfuseMac() & 0xFFFFFFULL));
    document["id"] = id;
    document["name"] = ecp::settings::get_device_name("EPAPER-154");
    document["ip"] = WiFi.localIP().toString();
    document["port"] = SERVER_PORT;
    document["auth"] = "X-File-Sync-Token";
    const file_storage::SpaceInfo storage = file_storage::space();
    document["storage"]["ready"] = storage.ready;
    document["storage"]["total_bytes"] = storage.total_bytes;
    document["storage"]["free_bytes"] = storage.free_bytes;
    send_json(200, document);
}

void handle_settings()
{
    if (!authorized()) return;
    if (s_server.method() == HTTP_GET) {
        JsonDocument document;
        document["ok"] = true;
        document["name"] = ecp::settings::get_device_name("EPAPER-154");
        document["auto_sync"] = ecp::settings::get_auto_sync(false);
        document["allow_delete"] = ecp::settings::get_allow_delete(false);
        document["auto_story_scan"] = ecp::settings::get_auto_story_scan(true);
        send_json(200, document);
        return;
    }
    JsonDocument request;
    if (deserializeJson(request, s_server.arg("plain"))) {
        send_error(400, "invalid_json");
        return;
    }
    if (request["name"].is<const char*>() &&
        !ecp::settings::set_device_name(request["name"].as<const char*>())) {
        send_error(400, "invalid_device_name");
        return;
    }
    if (request["auto_sync"].is<bool>()) ecp::settings::set_auto_sync(request["auto_sync"]);
    if (request["allow_delete"].is<bool>()) ecp::settings::set_allow_delete(request["allow_delete"]);
    if (request["auto_story_scan"].is<bool>()) ecp::settings::set_auto_story_scan(request["auto_story_scan"]);
    JsonDocument response;
    response["ok"] = true;
    if (request["rotate_token"] | false) {
        if (!ecp::settings::rotate_sync_token()) {
            send_error(500, "token_rotation_failed");
            return;
        }
        response["token"] = ecp::settings::get_sync_token();
    }
    send_json(200, response);
}

void handle_files()
{
    if (!authorized()) return;
    const String requested = s_server.hasArg("path") ? s_server.arg("path") : "/";
    size_t count = 0;
    const file_storage::Status status = file_storage::list(requested.c_str(), s_list_entries,
                                                            LIST_CAPACITY, &count);
    if (status != file_storage::Status::Ok) {
        send_status(status, "list");
        return;
    }
    JsonDocument document;
    document["ok"] = true;
    document["path"] = requested;
    JsonArray array = document["files"].to<JsonArray>();
    for (size_t i = 0; i < count; i++) {
        if (internal_entry(s_list_entries[i])) continue;
        JsonObject item = array.add<JsonObject>();
        item["path"] = public_path(s_list_entries[i].path);
        item["directory"] = s_list_entries[i].is_directory;
        item["size"] = s_list_entries[i].size;
        item["modified"] = (uint32_t)s_list_entries[i].modified;
    }
    document["truncated"] = count == LIST_CAPACITY;
    send_json(200, document);
}

void handle_file_download()
{
    if (!authorized()) return;
    const String requested = s_server.arg("path");
    file_storage::FileInfo info;
    const file_storage::Status status = file_storage::stat(requested.c_str(), &info);
    if (status != file_storage::Status::Ok || info.is_directory) {
        send_status(status == file_storage::Status::Ok ? file_storage::Status::InvalidArgument : status,
                    "download");
        return;
    }
    uint64_t offset = 0;
    uint64_t end = info.size ? info.size - 1 : 0;
    int response_code = 200;
    const String range = s_server.header("Range");
    int range_parts = 0;
    if (range.startsWith("bytes=")) {
        range_parts = sscanf(range.c_str() + 6, "%llu-%llu", &offset, &end);
    }
    if (range_parts >= 1) {
        if (offset >= info.size) {
            send_error(416, "range_not_satisfiable");
            return;
        }
        if (range_parts == 1) end = info.size - 1;
        if (end >= info.size) end = info.size - 1;
        if (end < offset) {
            send_error(416, "range_not_satisfiable");
            return;
        }
        response_code = 206;
        char content_range[96];
        snprintf(content_range, sizeof(content_range), "bytes %llu-%llu/%llu",
                 (unsigned long long)offset, (unsigned long long)end,
                 (unsigned long long)info.size);
        s_server.sendHeader("Content-Range", content_range);
    }
    const uint64_t length = info.size == 0 ? 0 : end - offset + 1;
    s_server.sendHeader("Accept-Ranges", "bytes");
    s_server.setContentLength((size_t)length);
    s_server.send(response_code, content_type(info.path), "");
    uint8_t buffer[1436];
    uint64_t position = offset;
    while (position < offset + length) {
        size_t request_length = sizeof(buffer);
        const uint64_t remaining = offset + length - position;
        if (remaining < request_length) request_length = (size_t)remaining;
        size_t got = 0;
        if (file_storage::read_at(info.path, position, buffer, request_length, &got) != file_storage::Status::Ok ||
            got == 0) break;
        s_server.sendContent(reinterpret_cast<const char*>(buffer), got);
        position += got;
    }
}

bool parse_content_range(uint64_t* offset, uint64_t* total, uint64_t* length)
{
    if (!offset || !total || !length) return false;
    *offset = 0;
    *total = UINT64_MAX;
    *length = UINT64_MAX;
    if (!s_server.hasHeader("Content-Range")) return true;
    unsigned long long start = 0;
    unsigned long long end = 0;
    unsigned long long size = 0;
    const String range = s_server.header("Content-Range");
    if (sscanf(range.c_str(), "bytes %llu-%llu/%llu", &start, &end, &size) != 3 ||
        end < start || size == 0 || start >= size || end >= size ||
        end - start == UINT64_MAX) {
        return false;
    }
    *offset = (uint64_t)start;
    *total = (uint64_t)size;
    *length = (uint64_t)(end - start + 1);
    return true;
}

void raw_fail(file_storage::Status status)
{
    if (!s_raw.failed) {
        s_raw.failed = true;
        s_raw.error = status;
    }
}

void handle_raw_upload()
{
    HTTPRaw& raw = s_server.raw();
    if (raw.status == RAW_START) {
        s_raw = {};
        s_raw_body_size = 0;
        if (!s_raw_body) s_raw_body = static_cast<uint8_t*>(ps_malloc(RAW_UPLOAD_BUFFER_SIZE));
        if (!s_raw_body) {
            raw_fail(file_storage::Status::IoError);
            return;
        }
        s_raw.authorized = authorized_without_response();
        if (!s_raw.authorized) {
            raw_fail(file_storage::Status::PermissionDenied);
            return;
        }
        uint64_t offset = 0;
        uint64_t total = UINT64_MAX;
        uint64_t length = UINT64_MAX;
        if (!parse_content_range(&offset, &total, &length)) {
            raw_fail(file_storage::Status::InvalidArgument);
            return;
        }
        s_raw.offset = offset;
        s_raw.expected_total = total;
        s_raw.declared_length = length;
        s_raw.final_chunk = total == UINT64_MAX || offset + length == total;
        if (s_server.uri() == "/api/v1/file" && s_server.method() == HTTP_PUT) {
            s_raw.mode = RAW_DIRECT_FILE;
            const String header_path = s_server.header("X-File-Path");
            const String requested = header_path.length() > 0
                ? header_path : s_server.arg("path");
            if (file_storage::normalize_path(requested.c_str(), s_raw.target, sizeof(s_raw.target)) !=
                    file_storage::Status::Ok || strcmp(s_raw.target, file_storage::ROOT_PATH) == 0 ||
                strncmp(s_raw.target, "/sdcard/.sync_", 14) == 0) {
                raw_fail(file_storage::Status::InvalidPath);
                return;
            }
            snprintf(s_raw.expected_sha256, sizeof(s_raw.expected_sha256), "%s",
                     s_server.header("X-SHA256").c_str());
            if (s_raw.expected_sha256[0] && strlen(s_raw.expected_sha256) != file_storage::SHA256_HEX_LENGTH) {
                raw_fail(file_storage::Status::InvalidArgument);
                return;
            }
            if (offset == 0) {
                size_t written = 0;
                const file_storage::Status status = file_storage::write_at(
                    HTTP_STAGING_PATH, 0, nullptr, 0, true, &written);
                if (status != file_storage::Status::Ok) raw_fail(status);
            } else {
                file_storage::FileInfo info;
                if (file_storage::stat(HTTP_STAGING_PATH, &info) != file_storage::Status::Ok ||
                    info.is_directory || info.size != offset) raw_fail(file_storage::Status::Incomplete);
            }
            return;
        }
        if (s_server.uri() == "/api/v1/sync/chunk" && s_server.method() == HTTP_PUT) {
            s_raw.mode = RAW_TRANSACTION_CHUNK;
            const String header_transaction = s_server.header("X-Sync-Transaction");
            const String header_path = s_server.header("X-Sync-Path");
            const String transaction = header_transaction.length() > 0
                ? header_transaction : s_server.arg("transaction");
            const String relative_path = header_path.length() > 0
                ? header_path : s_server.arg("path");
            snprintf(s_raw.transaction, sizeof(s_raw.transaction), "%s", transaction.c_str());
            snprintf(s_raw.relative_path, sizeof(s_raw.relative_path), "%s", relative_path.c_str());
            if (!s_transaction.active() || transaction != s_transaction.id()) {
                raw_fail(file_storage::Status::InvalidTransaction);
            }
            return;
        }
        raw_fail(file_storage::Status::InvalidArgument);
        return;
    }
    if (raw.status == RAW_WRITE) {
        if (s_raw.failed) return;
        if (raw.currentSize > RAW_UPLOAD_BUFFER_SIZE ||
            s_raw_body_size > RAW_UPLOAD_BUFFER_SIZE - raw.currentSize) {
            raw_fail(file_storage::Status::InvalidArgument);
            return;
        }
        memcpy(s_raw_body + s_raw_body_size, raw.buf, raw.currentSize);
        s_raw_body_size += raw.currentSize;
        return;
    }
    if (raw.status == RAW_ABORTED) {
        update_state(nullptr, "http body aborted");
        s_raw.failed = true;
        return;
    }
    if (raw.status != RAW_END) return;

    if (!s_raw.failed && s_raw.declared_length != UINT64_MAX &&
        raw.totalSize != s_raw.declared_length) {
        raw_fail(file_storage::Status::Incomplete);
    }
    if (!s_raw.failed && s_raw.final_chunk && s_raw.expected_total != UINT64_MAX &&
        (s_raw_body_size > UINT64_MAX - s_raw.offset ||
         s_raw.offset + s_raw_body_size != s_raw.expected_total)) {
        raw_fail(file_storage::Status::Incomplete);
    }
    if (!s_raw.failed && s_raw.mode == RAW_DIRECT_FILE) {
        size_t written = 0;
        const file_storage::Status status = file_storage::write_at(
            HTTP_STAGING_PATH, s_raw.offset, s_raw_body, s_raw_body_size, false, &written);
        if (status != file_storage::Status::Ok) raw_fail(status);
        else if (written != s_raw_body_size) raw_fail(file_storage::Status::IoError);
        else {
            s_raw.offset += written;
            s_raw.received = written;
        }
    } else if (!s_raw.failed && s_raw.mode == RAW_TRANSACTION_CHUNK) {
        size_t written = 0;
        const file_storage::Status status = s_transaction.write_chunk(
            s_raw.relative_path, s_raw.offset, s_raw_body, s_raw_body_size, &written);
        if (status != file_storage::Status::Ok) raw_fail(status);
        else if (written != s_raw_body_size) raw_fail(file_storage::Status::IoError);
        else {
            s_raw.offset += written;
            s_raw.received = written;
        }
    }
    const bool direct_complete = s_raw.final_chunk;
    if (!s_raw.failed && direct_complete && s_raw.mode == RAW_DIRECT_FILE && s_raw.expected_sha256[0]) {
        const file_storage::Status status = file_storage::verify_sha256(
            HTTP_STAGING_PATH, s_raw.expected_sha256);
        if (status != file_storage::Status::Ok) raw_fail(status);
    }
    if (!s_raw.failed && direct_complete && s_raw.mode == RAW_DIRECT_FILE) {
        const file_storage::Status status = file_storage::replace_file(HTTP_STAGING_PATH, s_raw.target);
        if (status != file_storage::Status::Ok) raw_fail(status);
        else update_state("file uploaded");
    } else if (!s_raw.failed && s_raw.mode == RAW_TRANSACTION_CHUNK) {
        update_state("chunk received");
    }
    if (s_raw.failed) {
        update_state(nullptr, file_storage::status_name(s_raw.error));
        send_error(http_status(s_raw.error), file_storage::status_name(s_raw.error));
    } else {
        JsonDocument document;
        document["ok"] = true;
        document["offset"] = s_raw.offset;
        document["received"] = s_raw.received;
        send_json(200, document);
    }
}

void handle_file_delete()
{
    if (!authorized()) return;
    if (!ecp::settings::get_allow_delete(false)) {
        send_error(403, "delete_disabled");
        return;
    }
    const bool confirmed = s_server.arg("confirm") == "true";
    const file_storage::Status status = file_storage::remove(s_server.arg("path").c_str(),
                                                               s_server.arg("recursive") == "true",
                                                               confirmed);
    send_status(status, "delete");
}

void handle_rename()
{
    if (!authorized()) return;
    JsonDocument document;
    const DeserializationError error = deserializeJson(document, s_server.arg("plain"));
    if (error || !document["from"].is<const char*>() || !document["to"].is<const char*>()) {
        send_error(400, "invalid_json", "from and to required");
        return;
    }
    send_status(file_storage::rename(document["from"], document["to"]), "rename");
}

void handle_prepare()
{
    if (!authorized()) return;
    JsonDocument document;
    if (deserializeJson(document, s_server.arg("plain"))) {
        send_error(400, "invalid_json");
        return;
    }
    const char* transaction = document["transaction"] | "";
    const char* root = document["root"] | "";
    JsonArray files_json = document["files"].as<JsonArray>();
    if (!transaction[0] || !root[0] || files_json.isNull() || files_json.size() == 0 ||
        files_json.size() > file_storage::TRANSACTION_FILE_MAX) {
        send_error(400, "invalid_manifest");
        return;
    }
    if (s_transaction.active()) {
        char normalized_root[file_storage::PATH_MAX_LENGTH] = {};
        if (file_storage::normalize_path(root, normalized_root, sizeof(normalized_root)) != file_storage::Status::Ok ||
            strcmp(transaction, s_transaction.id()) != 0 || strcmp(normalized_root, s_transaction.target_root()) != 0) {
            send_error(409, "transaction_active");
            return;
        }
        JsonDocument response;
        response["ok"] = true;
        response["transaction"] = transaction;
        response["root"] = root;
        response["resumed"] = true;
        send_json(200, response);
        return;
    }
    /* The HTTP task has a deliberately bounded stack. A full story package
     * can contain many audio and metadata files, so keep the manifest in
     * static storage rather than consuming several KB of task stack. */
    static file_storage::TransactionFileSpec specs[file_storage::TRANSACTION_FILE_MAX] = {};
    size_t index = 0;
    for (JsonObject item : files_json) {
        specs[index].path = item["path"] | "";
        specs[index].size = item["size"] | (uint64_t)0;
        specs[index].sha256 = item["sha256"] | "";
        if (!specs[index].path[0] || !specs[index].sha256[0]) {
            send_error(400, "invalid_manifest_file");
            return;
        }
        index++;
    }
    Serial.printf("[file-sync] prepare begin tx=%s files=%u root=%s\n",
                  transaction, (unsigned)index, root);
    const uint32_t prepare_started = millis();
    const file_storage::Status status = s_transaction.begin(transaction, root, specs, index);
    Serial.printf("[file-sync] prepare end status=%s elapsed=%lu ms\n",
                  file_storage::status_name(status), (unsigned long)(millis() - prepare_started));
    if (status != file_storage::Status::Ok) {
        send_status(status, "prepare");
        return;
    }
    update_state("transaction prepared");
    JsonDocument response;
    response["ok"] = true;
    response["transaction"] = transaction;
    response["root"] = root;
    send_json(200, response);
}

void handle_commit()
{
    if (!authorized()) return;
    if (ecp::settings::get_auto_story_scan(true) &&
        strncmp(s_transaction.target_root(), "/sdcard/video/", 14) == 0) {
        char story_path[file_storage::PATH_MAX_LENGTH] = {};
        snprintf(story_path, sizeof(story_path), "%s/story.json", s_transaction.staging_root());
        file_storage::FileInfo story;
        if (file_storage::stat(story_path, &story) == file_storage::Status::Ok) {
            file_validation_report_t report;
            const file_storage::Status validation = file_validate_story_package(
                s_transaction.staging_root(), &report);
            if (validation != file_storage::Status::Ok) {
                update_state(nullptr, report.detail);
                send_error(422, "story_validation_failed", report.detail);
                return;
            }
        }
    }
    const file_storage::Status status = s_transaction.commit();
    send_status(status, "commit");
    if (status == file_storage::Status::Ok) {
        s_commit_generation++;
        update_state("transaction committed");
    }
}

void handle_transaction_status()
{
    if (!authorized()) return;
    if (!s_transaction.active() || s_server.arg("transaction") != s_transaction.id()) {
        send_error(409, "invalid_transaction");
        return;
    }
    const String path = s_server.arg("path");
    uint64_t size = 0;
    const file_storage::Status status = s_transaction.staged_size(path.c_str(), &size);
    if (status != file_storage::Status::Ok && status != file_storage::Status::NotFound) {
        send_status(status, "sync_status");
        return;
    }
    JsonDocument document;
    document["ok"] = true;
    document["transaction"] = s_transaction.id();
    document["path"] = path;
    document["received"] = status == file_storage::Status::Ok ? size : 0;
    send_json(200, document);
}

void handle_abort()
{
    if (!authorized()) return;
    const file_storage::Status status = s_transaction.abort();
    send_status(status, "abort");
    if (status == file_storage::Status::Ok) update_state("transaction aborted");
}

void register_routes()
{
    if (s_routes_registered) return;
    const char* headers[] = {"X-File-Sync-Token", "Content-Range", "X-SHA256", "Range",
                             "X-File-Path", "X-Sync-Transaction", "X-Sync-Path"};
    s_server.collectHeaders(headers, sizeof(headers) / sizeof(headers[0]));
    s_server.enableCORS(true);
    s_server.on("/api/v1/device", HTTP_GET, handle_device);
    s_server.on("/api/v1/settings", HTTP_ANY, handle_settings);
    s_server.on("/api/v1/files", HTTP_GET, handle_files);
    s_server.on("/api/v1/file", HTTP_GET, handle_file_download);
    s_server.on("/api/v1/file", HTTP_PUT, []() {}, handle_raw_upload);
    s_server.on("/api/v1/file", HTTP_DELETE, handle_file_delete);
    s_server.on("/api/v1/rename", HTTP_POST, handle_rename);
    s_server.on("/api/v1/sync/prepare", HTTP_POST, handle_prepare);
    s_server.on("/api/v1/sync/chunk", HTTP_PUT, []() {}, handle_raw_upload);
    s_server.on("/api/v1/sync/status", HTTP_GET, handle_transaction_status);
    s_server.on("/api/v1/sync/commit", HTTP_POST, handle_commit);
    s_server.on("/api/v1/sync/abort", HTTP_POST, handle_abort);
    s_server.onNotFound([]() { send_error(404, "not_found"); });
    s_routes_registered = true;
}

void file_sync_task(void*)
{
    for (;;) {
        ecp::settings::WifiCredentials credentials;
        const bool configured = ecp::settings::get_wifi_credentials(credentials);
        if (configured && WiFi.status() != WL_CONNECTED && millis() - s_last_wifi_attempt_ms > 10000) {
            WiFi.mode(WIFI_STA);
            WiFi.begin(credentials.ssid.c_str(), credentials.password.c_str());
            s_last_wifi_attempt_ms = millis();
        }
        if (WiFi.status() == WL_CONNECTED) {
            if (!s_discovery_started) {
                s_discovery_started = s_discovery.begin(DISCOVERY_PORT) == 1;
            }
            if (s_discovery_started && s_discovery.parsePacket() > 0) {
                char request[32] = {};
                const int length = s_discovery.read((uint8_t*)request, sizeof(request) - 1);
                request[max(0, length)] = '\0';
                if (strcmp(request, DISCOVERY_REQUEST) == 0 || strcmp(request, "FILE_SYNC_DISCOVER") == 0) {
                    JsonDocument document;
                    document["protocol"] = "file-sync/1";
                    char id[20] = {};
                    snprintf(id, sizeof(id), "s3-%06llX", (unsigned long long)(ESP.getEfuseMac() & 0xFFFFFFULL));
                    document["id"] = id;
                    document["name"] = ecp::settings::get_device_name("EPAPER-154");
                    document["ip"] = WiFi.localIP().toString();
                    document["port"] = SERVER_PORT;
                    String response;
                    serializeJson(document, response);
                    s_discovery.beginPacket(s_discovery.remoteIP(), s_discovery.remotePort());
                    s_discovery.write((const uint8_t*)response.c_str(), response.length());
                    s_discovery.endPacket();
                }
            }
            if (!s_server_started) {
                s_server.begin();
                s_server_started = true;
                update_state("server ready");
            }
            s_server.handleClient();
        } else if (s_server_started) {
            s_server.stop();
            s_server_started = false;
            if (s_discovery_started) {
                s_discovery.stop();
                s_discovery_started = false;
            }
            update_state("wifi disconnected");
        }
        update_network_state();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

}  // namespace

void file_sync_server_begin(void)
{
    if (s_task) return;
    register_routes();
    xTaskCreatePinnedToCore(file_sync_task, "file_sync_http", 8192, nullptr, 2, &s_task, 1);
}

void file_sync_server_tick(void)
{
    /* HTTP runs on its own task so shell apps may wait for input without
     * making the file service unavailable. Kept as a public no-op hook for
     * boards that later choose a cooperative server task. */
    update_network_state();
}

void file_sync_server_stop(void)
{
    /* The service is intentionally lifetime-scoped for this firmware. */
}

bool file_sync_server_get_state(file_sync_server_state_t* out)
{
    if (!out) return false;
    portENTER_CRITICAL(&s_state_mux);
    *out = s_state;
    portEXIT_CRITICAL(&s_state_mux);
    return true;
}

uint32_t file_sync_server_commit_generation(void)
{
    return s_commit_generation;
}
