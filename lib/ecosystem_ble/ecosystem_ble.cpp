#include "ecosystem_ble.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <NimBLEDevice.h>

#include <WiFi.h>

#include "bsp.h"
#include "ecosystem_protocol.h"
#include "nvs_settings.h"
#include "logger.h"
#include "app_state.h"

#if BSP_HAS_BATTERY

uint32_t bsp_battery_mv(void);
int bsp_battery_level(void);
bool bsp_charge_raw(void);
#endif

extern "C" {
__attribute__((weak)) void bsp_remote_key_event(const char* key, const char* event, const char* char_val) {}
__attribute__((weak)) void bsp_remote_text_event(const char* session, const char* text, bool final) {}
__attribute__((weak)) void bsp_remote_imu_event(int16_t ax, int16_t ay, int16_t az) {}
__attribute__((weak)) void bsp_remote_volume_event(uint8_t volume) {}
__attribute__((weak)) void bsp_app_state_json(JsonObject& app) {}
__attribute__((weak)) void bsp_text_prompt_state(JsonObject& state) {}
}

namespace {
/* ── Command queue: fixed buffers to avoid String/heap in BLE callback ─
 * Lock-free SPSC ring: onWrite() (NimBLE stack task) is the sole producer,
 * process_pending_command() (Arduino loop task) is the sole consumer.
 * More than one slot matters because several handlers ack a command with
 * notify_empty_ok() *before* doing their own slow follow-up work (e.g. a
 * physical display flush) -- the client sees the ack and may already have
 * written the next command while that slot is technically still "in
 * flight" until the handler returns. A single-slot buffer treated that as
 * "queue full" and silently dropped the next command; boards with a fast
 * flush never noticed, epaper_154's ~1.755s EPD refresh made the window
 * wide enough to lose every follow-up command. */
static const size_t CMD_BUF_SIZE = ecp::MAX_MESSAGE_BYTES + 1;
static const size_t CMD_QUEUE_SLOTS = 3;
static char           s_cmd_buf[CMD_QUEUE_SLOTS][CMD_BUF_SIZE];
static size_t         s_cmd_len[CMD_QUEUE_SLOTS] = {0};
static volatile size_t s_cmd_head = 0;  // next slot to consume
static volatile size_t s_cmd_tail = 0;  // next slot to produce

NimBLECharacteristic* s_response = nullptr;
NimBLECharacteristic* s_state = nullptr;
constexpr uint32_t STATE_NOTIFY_INTERVAL_MS = 5000;
constexpr uint32_t SHUTDOWN_GRACE_DELAY_MS = 300;
constexpr size_t   MAX_UI_MESSAGE_LEN = 96;

String device_id()


{
    const uint64_t mac = ESP.getEfuseMac();
    char id[20];
    snprintf(id, sizeof(id), "s3-%06llX", (unsigned long long)(mac & 0xFFFFFFULL));
    return String(id);
}

String device_name() { return String("ECO-") + BSP_BOARD_ID + "-" + device_id().substring(3); }

void add_capabilities(JsonArray caps)
{
#if BSP_HAS_DISPLAY
    caps.add(ecp::CAP_DISPLAY);
#endif
#if BSP_HAS_TOUCH
    caps.add(ecp::CAP_TOUCH);
#endif
#if BSP_HAS_AUDIO_OUT
    caps.add(ecp::CAP_AUDIO_OUT);
#endif
#if BSP_HAS_AUDIO_IN
    caps.add(ecp::CAP_AUDIO_IN);
#endif
#if BSP_HAS_RGB_LED
    caps.add(ecp::CAP_RGB_LED);
#endif
#if BSP_HAS_SDCARD
    caps.add(ecp::CAP_SDCARD);
#endif
#if BSP_HAS_BATTERY
    caps.add(ecp::CAP_BATTERY);
#endif
#if BSP_HAS_IMU
    caps.add(ecp::CAP_IMU);
#endif
#if BSP_HAS_WIFI
    caps.add(ecp::CAP_WIFI);
#endif
// BLE v0.4 capability exports
#if defined(BSP_CAP_INPUT_REMOTE_KEY) && BSP_CAP_INPUT_REMOTE_KEY
    caps.add(ecp::CAP_INPUT_REMOTE_KEY);
#endif
#if defined(BSP_CAP_INPUT_REMOTE_TEXT) && BSP_CAP_INPUT_REMOTE_TEXT
    caps.add(ecp::CAP_INPUT_REMOTE_TEXT);
#endif
#if defined(BSP_CAP_INPUT_REMOTE_IMU) && BSP_CAP_INPUT_REMOTE_IMU
    caps.add(ecp::CAP_INPUT_REMOTE_IMU);
#endif
#if defined(BSP_CAP_CONFIG_WIFI) && BSP_CAP_CONFIG_WIFI
    caps.add(ecp::CAP_CONFIG_WIFI);
#endif
#if defined(BSP_CAP_CONFIG_VOLUME) && BSP_CAP_CONFIG_VOLUME
    caps.add(ecp::CAP_CONFIG_VOLUME);
#endif
#if defined(BSP_CAP_CONFIG_BRIGHTNESS) && BSP_CAP_CONFIG_BRIGHTNESS
    caps.add(ecp::CAP_CONFIG_BRIGHTNESS);
#endif
#if defined(BSP_CAP_UI_NAV) && BSP_CAP_UI_NAV
    caps.add(ecp::CAP_UI_NAV);
#endif
#if defined(BSP_CAP_APP_DEMO) && BSP_CAP_APP_DEMO
    caps.add(ecp::CAP_APP_DEMO);
#endif
}


// The banner form of the version string leads with the board id, which the
// identity object already carries in `type` right next to it.  Repeating it
// costs 14 bytes on the longest board name and pushes touch_lcd_154's
// sys.info over the response budget, so drop the prefix here only; the boot
// banner keeps its own format.
const char* firmware_without_board_prefix()
{
    const char* version = bsp_version_string();
    const size_t board_len = strlen(BSP_BOARD_ID);
    if (strncmp(version, BSP_BOARD_ID, board_len) == 0 && version[board_len] == ' ') {
        return version + board_len + 1;
    }
    return version;
}

const char* firmware_short_version()
{
    static char version[16];
    snprintf(version, sizeof(version), "v%d.%d.%d",
             BSP_BOARD_VER_MAJOR, BSP_BOARD_VER_MINOR, BSP_BOARD_VER_PATCH);
    return version;
}

void fill_device_info(JsonObject info)
{
    info["v"] = ecp::PROTOCOL_VERSION;
    info["id"] = device_id();
    info["type"] = BSP_BOARD_ID;
    info["firmware"] = firmware_without_board_prefix();
    add_capabilities(info["capabilities"].to<JsonArray>());
}

void fill_sys_info(JsonObject info)
{
    info["v"] = ecp::PROTOCOL_VERSION;
    info["id"] = device_id();
    info["type"] = BSP_BOARD_ID;
    info["firmware"] = firmware_short_version();
    // The full canonical capability list lives in INFO_UUID.  Keeping
    // sys.info compact prevents its response notification from exceeding
    // MTU-3 on capability-rich v0.4 boards such as amoled_206.
    info["capabilities_ref"] = "device_info";
}

String info_payload()
{
    JsonDocument doc;
    fill_device_info(doc.to<JsonObject>());
    String payload;
    serializeJson(doc, payload);
    return payload;
}

void fill_state(JsonObject state)
{
    state["uptime_ms"] = millis();
#if BSP_HAS_BATTERY
    JsonObject battery = state["battery"].to<JsonObject>();
    battery["mv"] = bsp_battery_mv();
    battery["level"] = bsp_battery_level();
    battery["charging"] = bsp_charge_raw();
#endif

#if BSP_HAS_WIFI
    ecp::settings::WifiCredentials creds;
    JsonObject wifi_st = state["wifi"].to<JsonObject>();
    if (ecp::settings::get_wifi_credentials(creds)) {
        wifi_st["configured"] = true;
        wifi_st["ssid"] = creds.ssid;
        bool connected = (WiFi.status() == WL_CONNECTED);
        wifi_st["connected"] = connected;
        if (connected) {
            wifi_st["ip"] = WiFi.localIP().toString();
            wifi_st["rssi"] = WiFi.RSSI();
        }
    } else {
        wifi_st["configured"] = false;
        wifi_st["connected"] = false;
    }
#endif


#if defined(BSP_CAP_CONFIG_VOLUME) && BSP_CAP_CONFIG_VOLUME
    JsonObject settings = state["settings"].to<JsonObject>();
    settings["volume"] = ecp::settings::get_volume();
#endif
#if defined(BSP_CAP_CONFIG_BRIGHTNESS) && BSP_CAP_CONFIG_BRIGHTNESS
    if (!state["settings"].is<JsonObjectConst>()) {
        state["settings"].to<JsonObject>();
    }
    state["settings"]["brightness"] = ecp::settings::get_brightness();
#endif


#if defined(BSP_CAP_APP_DEMO) && BSP_CAP_APP_DEMO
    JsonObject app = state["app"].to<JsonObject>();
    bsp_app_state_json(app);
#endif

#if defined(BSP_CAP_INPUT_REMOTE_TEXT) && BSP_CAP_INPUT_REMOTE_TEXT
    // Populated only while a board UI page has a text field focused; absent
    // otherwise. Controllers (e.g. Cardputer) watch this via STATE notify to
    // know when to open a local text-entry session and start sending input.text.
    bsp_text_prompt_state(state);
#endif

}


String state_payload()
{
    JsonDocument doc;
    fill_state(doc.to<JsonObject>());
    String payload;
    serializeJson(doc, payload);
    return payload;
}

String state_notify_payload()
{
    JsonDocument doc;
    doc["v"] = ecp::PROTOCOL_VERSION;
    doc["rid"] = 0;
    doc["ok"] = true;
    doc["code"] = "STATE";
    fill_state(doc["body"].to<JsonObject>());
    String payload;
    serializeJson(doc, payload);
    return payload;
}

// A notification carries at most ATT_MTU-3 bytes and, unlike a GATT read, it
// cannot be continued: the rest is dropped on the air with no error raised at
// either end.  The characteristic value stays complete, so a client that reads
// it back still gets everything, but a client that only listens sees a cut-off
// payload.  Say so rather than truncating in silence.
void warn_if_over_notification_limit(const String& payload)
{
    NimBLEServer* server = NimBLEDevice::getServer();
    if (!server || server->getConnectedCount() == 0) return;
    const uint16_t mtu = server->getPeerMTU(server->getPeerInfo(0).getConnHandle());
    if (mtu <= 3) return;
    const size_t limit = static_cast<size_t>(mtu) - 3;
    if (payload.length() > limit) {
        LOG_W("ECP", "notify payload %u B over limit %u (mtu %u); listeners see truncated",
                      (unsigned)payload.length(), (unsigned)limit, (unsigned)mtu);
    }
}

void emit_response(const String& payload, uint32_t rid, const char* code)
{
    warn_if_over_notification_limit(payload);
    s_response->setValue(payload.c_str());
    s_response->notify();
    LOG_D("ECP", "response rid=%lu code=%s len=%u", (unsigned long)rid, code,
                  (unsigned)payload.length());
}

bool publish_state()
{
    if (!s_state) return false;
    const String payload = state_notify_payload();
    warn_if_over_notification_limit(payload);
    s_state->setValue(payload.c_str());
    const bool sent = s_state->notify();
    LOG_D("ECP", "state notify sent=%d %s", sent ? 1 : 0, payload.c_str());
    return sent;
}

void notify_response(uint32_t rid, bool ok, const char* code, const char* message)
{
    JsonDocument doc;
    doc["v"] = ecp::PROTOCOL_VERSION;
    doc["rid"] = rid;
    doc["ok"] = ok;
    doc["code"] = code;
    doc["body"]["message"] = message;
    String payload;
    serializeJson(doc, payload);
    emit_response(payload, rid, code);
}

void notify_empty_ok(uint32_t rid)
{
    JsonDocument doc;
    doc["v"] = ecp::PROTOCOL_VERSION;
    doc["rid"] = rid;
    doc["ok"] = true;
    doc["code"] = ecp::CODE_OK;
    String payload;
    serializeJson(doc, payload);
    emit_response(payload, rid, ecp::CODE_OK);
}

void notify_info(uint32_t rid)
{
    JsonDocument doc;
    doc["v"] = ecp::PROTOCOL_VERSION;
    doc["rid"] = rid;
    doc["ok"] = true;
    doc["code"] = ecp::CODE_OK;
    fill_sys_info(doc["body"].to<JsonObject>());
    String payload;
    serializeJson(doc, payload);
    emit_response(payload, rid, ecp::CODE_OK);
}

void notify_state_response(uint32_t rid)
{
    // A state request also emits the snapshot on the state channel. This
    // makes the read/notify contract deterministic even while a board app is
    // busy in a long-running hardware test page.
    publish_state();
    JsonDocument doc;
    doc["v"] = ecp::PROTOCOL_VERSION;
    doc["rid"] = rid;
    doc["ok"] = true;
    doc["code"] = ecp::CODE_OK;
    fill_state(doc["body"].to<JsonObject>());
    String payload;
    serializeJson(doc, payload);
    emit_response(payload, rid, ecp::CODE_OK);
}

using CommandHandler = void (*)(uint32_t, JsonDocument&);
void handle_ping(uint32_t rid, JsonDocument&) { notify_response(rid, true, ecp::CODE_OK, "pong"); }
void handle_info(uint32_t rid, JsonDocument&) { notify_info(rid); }
void handle_state_get(uint32_t rid, JsonDocument&) { notify_state_response(rid); }

void handle_ui_message(uint32_t rid, JsonDocument& request)
{
    const char* message = request["message"] | "";
    if (!message || message[0] == '\0' || strlen(message) > MAX_UI_MESSAGE_LEN) {
        notify_response(rid, false, ecp::CODE_BAD_REQUEST, "message required");
        return;
    }
#if BSP_HAS_DISPLAY
    bsp_ui_printf("%s\n", message);
    notify_empty_ok(rid);
    bsp_ui_flush();
#else
    notify_response(rid, false, ecp::CODE_UNSUPPORTED, ecp::OP_UI_MESSAGE);
#endif
}

void handle_display_clear(uint32_t rid, JsonDocument&)
{
#if BSP_HAS_DISPLAY
    bsp_ui_clear();
    notify_empty_ok(rid);
    bsp_ui_flush();
#else
    notify_response(rid, false, ecp::CODE_UNSUPPORTED, ecp::OP_DISPLAY_CLEAR);
#endif
}

bool read_color(JsonDocument& request, const char* key, uint8_t& value)
{
    if (!request[key].is<int>()) return false;
    const int raw = request[key].as<int>();
    if (raw < 0 || raw > 255) return false;
    value = static_cast<uint8_t>(raw);
    return true;
}

void handle_led_set(uint32_t rid, JsonDocument& request)
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    if (!read_color(request, "r", r) || !read_color(request, "g", g) || !read_color(request, "b", b)) {
        notify_response(rid, false, ecp::CODE_BAD_REQUEST, "r/g/b 0..255 required");
        return;
    }
#if BSP_HAS_RGB_LED
    bsp_led_all(r, g, b);
    notify_empty_ok(rid);
#else
    notify_response(rid, false, ecp::CODE_UNSUPPORTED, ecp::OP_LED_SET);
#endif
}

/* Packs BSP_BOARD_VER_{MAJOR,MINOR,PATCH} (see bsp_version.h) into the
 * same 0xMMmmpp00 layout OTA clients compare fw_version against. */
#define FIRMWARE_VERSION \
    ((uint32_t)(BSP_BOARD_VER_MAJOR) << 24 | (uint32_t)(BSP_BOARD_VER_MINOR) << 16 | \
     (uint32_t)(BSP_BOARD_VER_PATCH) << 8)

void handle_ota_version_check(uint32_t rid, JsonDocument& request)
{
    /* OTA pre-flight check: verify the proposed firmware version
     * is newer than the currently running version to prevent
     * accidental downgrades. This is a minimum-viable security
     * check – full OTA with signature verification is planned. */
    if (!request["fw_version"].is<uint32_t>()) {
        notify_response(rid, false, ecp::CODE_BAD_REQUEST, "fw_version (uint32) required");
        return;
    }
    uint32_t proposed = request["fw_version"].as<uint32_t>();
    uint32_t current  = FIRMWARE_VERSION;
    if (proposed <= current) {
        char msg[64];
        snprintf(msg, sizeof(msg), "downgrade blocked: proposed 0x%08X <= current 0x%08X",
                 (unsigned)proposed, (unsigned)current);
        notify_response(rid, false, ecp::CODE_BAD_REQUEST, msg);
        Serial.printf("[OTA] %s\n", msg);
        return;
    }
    char msg[64];
    snprintf(msg, sizeof(msg), "version OK: 0x%08X -> 0x%08X",
             (unsigned)current, (unsigned)proposed);
    notify_response(rid, true, ecp::CODE_OK, msg);
    Serial.printf("[OTA] %s\n", msg);
}

void handle_device_reboot(uint32_t rid, JsonDocument& request)
{
    const bool confirm = request["confirm"] | false;
    if (!confirm) {
        notify_response(rid, false, ecp::CODE_BAD_REQUEST, "confirm:true required");
        return;
    }
    notify_empty_ok(rid);
    // Allow NimBLE stack time to transmit the OK notification before restarting hardware
    delay(SHUTDOWN_GRACE_DELAY_MS);
    esp_restart();
}

void handle_device_poweroff(uint32_t rid, JsonDocument& request)
{
    const bool confirm = request["confirm"] | false;
    if (!confirm) {
        notify_response(rid, false, ecp::CODE_BAD_REQUEST, "confirm:true required");
        return;
    }
    notify_empty_ok(rid);
    // Allow NimBLE stack time to transmit the OK notification before shutting down hardware
    delay(SHUTDOWN_GRACE_DELAY_MS);
    bsp_power_off();
}


void handle_input_key(uint32_t rid, JsonDocument& request)
{
    const char* key = request["key"] | "";
    const char* event = request["event"] | "";
    if (key[0] == '\0' || event[0] == '\0') {
        notify_response(rid, false, ecp::CODE_BAD_REQUEST, "key and event required");
        return;
    }
    bool valid_key = (strcmp(key, "up") == 0 || strcmp(key, "down") == 0 || strcmp(key, "left") == 0 ||
                      strcmp(key, "right") == 0 || strcmp(key, "enter") == 0 || strcmp(key, "back") == 0 ||
                      strcmp(key, "esc") == 0 || strcmp(key, "home") == 0 ||
                      strcmp(key, "space") == 0 || strcmp(key, "char") == 0 || strcmp(key, "backspace") == 0);
    bool valid_event = (strcmp(event, "down") == 0 || strcmp(event, "up") == 0 ||
                        strcmp(event, "repeat") == 0 || strcmp(event, "press") == 0);
    if (!valid_key || !valid_event) {
        notify_response(rid, false, ecp::CODE_BAD_REQUEST, "invalid key or event");
        return;
    }
    if (strcmp(key, "char") == 0) {
        const char* ch = request["char"] | "";
        if (ch[0] == '\0') {
            notify_response(rid, false, ecp::CODE_BAD_REQUEST, "char payload required");
            return;
        }
    }
#if defined(BSP_CAP_INPUT_REMOTE_KEY) && BSP_CAP_INPUT_REMOTE_KEY
    const char* ch_val = request["char"] | "";
    bsp_remote_key_event(key, event, ch_val);
    notify_empty_ok(rid);
#else
    notify_response(rid, false, ecp::CODE_UNSUPPORTED, ecp::OP_INPUT_KEY);
#endif
}

void handle_input_text(uint32_t rid, JsonDocument& request)
{
    const char* session = request["session"] | "";
    const char* text = request["text"] | "";
    if (session[0] == '\0') {
        notify_response(rid, false, ecp::CODE_BAD_REQUEST, "session required");
        return;
    }
    if (strlen(text) > MAX_UI_MESSAGE_LEN) {
        notify_response(rid, false, ecp::CODE_BAD_REQUEST, "text exceeds 96 bytes");
        return;
    }
#if defined(BSP_CAP_INPUT_REMOTE_TEXT) && BSP_CAP_INPUT_REMOTE_TEXT
    auto& appState = AppState::getInstance();
    const bool final = request["final"] | false;
    LOG_I("ECP", "input.text session=%s len=%u final=%u",
          session, (unsigned)strlen(text), final ? 1U : 0U);
    bsp_remote_text_event(session, text, final);
    if (strcmp(session, "wifi.ssid") == 0) {
        appState.temp_wifi_ssid = text;
    } else if (strcmp(session, "wifi.password") == 0) {
        appState.temp_wifi_pass = text;
    }
#if BSP_HAS_DISPLAY
    if (strcmp(session, "wifi.password") == 0) {
        bsp_ui_printf("[TEXT] %s: *****\n", session);
    } else {
        bsp_ui_printf("[TEXT] %s: %s\n", session, text);
    }
    bsp_ui_flush();
#endif

    if (final && session[0] != '\0') {
        if (strncmp(session, "wifi.", 5) == 0 && !appState.temp_wifi_ssid.isEmpty()) {
#if BSP_HAS_WIFI
            ecp::settings::set_wifi_credentials(appState.temp_wifi_ssid.c_str(), appState.temp_wifi_pass.c_str());
            WiFi.mode(WIFI_STA);
            WiFi.begin(appState.temp_wifi_ssid.c_str(), appState.temp_wifi_pass.c_str());
            LOG_I("ECP", "text session final, WiFi connect to %s", appState.temp_wifi_ssid.c_str());
#endif
            appState.temp_wifi_pass = "";
        }
    }
    notify_empty_ok(rid);
#else
    notify_response(rid, false, ecp::CODE_UNSUPPORTED, ecp::OP_INPUT_TEXT);
#endif
}

void handle_input_imu(uint32_t rid, JsonDocument& request)
{
    if (!request["seq"].is<uint32_t>() || !request["ax"].is<int>() ||
        !request["ay"].is<int>() || !request["az"].is<int>()) {
        notify_response(rid, false, ecp::CODE_BAD_REQUEST, "seq, ax, ay, az required");
        return;
    }
#if defined(BSP_CAP_INPUT_REMOTE_IMU) && BSP_CAP_INPUT_REMOTE_IMU
    const int16_t ax = (int16_t)(int)request["ax"];
    const int16_t ay = (int16_t)(int)request["ay"];
    const int16_t az = (int16_t)(int)request["az"];
    bsp_remote_imu_event(ax, ay, az);
    notify_empty_ok(rid);
#else
    notify_response(rid, false, ecp::CODE_UNSUPPORTED, ecp::OP_INPUT_IMU);
#endif
}

void handle_config_wifi_set(uint32_t rid, JsonDocument& request)
{
    const char* ssid = request["ssid"] | "";
    const char* pass = request["password"] | "";
    if (ssid[0] == '\0') {
        notify_response(rid, false, ecp::CODE_BAD_REQUEST, "ssid required");
        return;
    }
#if defined(BSP_CAP_CONFIG_WIFI) && BSP_CAP_CONFIG_WIFI
    ecp::settings::set_wifi_credentials(ssid, pass);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    LOG_I("ECP", "WiFi config set, connecting to SSID: %s", ssid);
#if BSP_HAS_DISPLAY
    bsp_ui_printf("[WIFI] Connecting to %s...\n", ssid);
    bsp_ui_flush();
#endif
    notify_empty_ok(rid);
#else
    notify_response(rid, false, ecp::CODE_UNSUPPORTED, ecp::OP_CONFIG_WIFI_SET);
#endif
}

void handle_config_wifi_clear(uint32_t rid, JsonDocument&)
{
#if defined(BSP_CAP_CONFIG_WIFI) && BSP_CAP_CONFIG_WIFI
    ecp::settings::clear_wifi_credentials();
    WiFi.disconnect(true, true);
#if BSP_HAS_DISPLAY
    bsp_ui_printf("[WIFI] Configuration cleared\n");
    bsp_ui_flush();
#endif
    notify_empty_ok(rid);
#else
    notify_response(rid, false, ecp::CODE_UNSUPPORTED, ecp::OP_CONFIG_WIFI_CLEAR);
#endif
}

void handle_config_volume_set(uint32_t rid, JsonDocument& request)
{
    if (!request["volume"].is<int>()) {
        notify_response(rid, false, ecp::CODE_BAD_REQUEST, "volume 0..100 required");
        return;
    }
    int vol = request["volume"].as<int>();
    if (vol < 0 || vol > 100) {
        notify_response(rid, false, ecp::CODE_BAD_REQUEST, "volume out of range");
        return;
    }
#if defined(BSP_CAP_CONFIG_VOLUME) && BSP_CAP_CONFIG_VOLUME
    ecp::settings::set_volume(static_cast<uint8_t>(vol));
    bsp_remote_volume_event(static_cast<uint8_t>(vol));
#if BSP_HAS_DISPLAY
    bsp_ui_printf("[AUDIO] Volume set: %d%%\n", vol);
    bsp_ui_flush();
#endif
    notify_empty_ok(rid);
#else
    notify_response(rid, false, ecp::CODE_UNSUPPORTED, ecp::OP_CONFIG_VOLUME_SET);
#endif
}

void handle_config_brightness_set(uint32_t rid, JsonDocument& request)
{
    if (!request["brightness"].is<int>()) {
        notify_response(rid, false, ecp::CODE_BAD_REQUEST, "brightness 0..100 required");
        return;
    }
    int bright = request["brightness"].as<int>();
    if (bright < 0 || bright > 100) {
        notify_response(rid, false, ecp::CODE_BAD_REQUEST, "brightness out of range");
        return;
    }
#if defined(BSP_CAP_CONFIG_BRIGHTNESS) && BSP_CAP_CONFIG_BRIGHTNESS
    ecp::settings::set_brightness(static_cast<uint8_t>(bright));
    bsp_display_set_brightness(static_cast<uint8_t>(bright));
#if BSP_HAS_DISPLAY
    bsp_ui_printf("[DISP] Brightness set: %d%%\n", bright);
    bsp_ui_flush();
#endif
    notify_empty_ok(rid);
#else
    notify_response(rid, false, ecp::CODE_UNSUPPORTED, ecp::OP_CONFIG_BRIGHTNESS_SET);
#endif
}



struct CommandEntry {
    const char* op;
    CommandHandler handler;
};
const CommandEntry COMMANDS[] = {
    {ecp::OP_PING, handle_ping},
    {ecp::OP_INFO, handle_info},
    {ecp::OP_STATE_GET, handle_state_get},
    {ecp::OP_UI_MESSAGE, handle_ui_message},
    {ecp::OP_DISPLAY_CLEAR, handle_display_clear},
    {ecp::OP_LED_SET, handle_led_set},
    {ecp::OP_OTA_VERSION_CHECK, handle_ota_version_check},
    {ecp::OP_DEVICE_REBOOT, handle_device_reboot},
    {ecp::OP_DEVICE_POWEROFF, handle_device_poweroff},
    {ecp::OP_INPUT_KEY, handle_input_key},

    {ecp::OP_INPUT_TEXT, handle_input_text},
    {ecp::OP_INPUT_IMU, handle_input_imu},
    {ecp::OP_CONFIG_WIFI_SET, handle_config_wifi_set},
    {ecp::OP_CONFIG_WIFI_CLEAR, handle_config_wifi_clear},
    {ecp::OP_CONFIG_VOLUME_SET, handle_config_volume_set},
    {ecp::OP_CONFIG_BRIGHTNESS_SET, handle_config_brightness_set},
};


const CommandEntry* find_command(const char* op)
{
    for (const CommandEntry& command : COMMANDS) {
        if (strcmp(op, command.op) == 0) return &command;
    }
    return nullptr;
}

static void process_pending_command()
{
    if (s_cmd_head == s_cmd_tail) return;  // queue empty

    const size_t slot = s_cmd_head;

    /* Parse and dispatch in main-loop context where heap alloc is safe */
    JsonDocument request;
    if (deserializeJson(request, s_cmd_buf[slot], s_cmd_len[slot])) {
        notify_response(0, false, ecp::CODE_BAD_REQUEST, "invalid json");
        s_cmd_head = (s_cmd_head + 1) % CMD_QUEUE_SLOTS;
        return;
    }
    const uint32_t rid = request["rid"] | 0;
    const uint8_t version = request["v"] | 0;
    const char* op = request["op"] | "";
    if (version != ecp::PROTOCOL_VERSION || rid == 0 || op[0] == '\0') {
        notify_response(rid, false, ecp::CODE_BAD_REQUEST, "v, rid and op required");
    } else if (const CommandEntry* command = find_command(op)) {
        command->handler(rid, request);
    } else {
        notify_response(rid, false, ecp::CODE_UNKNOWN_COMMAND, op);
    }
    // Advance past this slot only now that the handler (including any of its
    // own slow follow-up work, e.g. a display flush) has fully returned --
    // request[] may hold pointers into s_cmd_buf[slot] (ArduinoJson parses
    // in place), so the slot must stay untouched by the producer until here.
    s_cmd_head = (s_cmd_head + 1) % CMD_QUEUE_SLOTS;
}

class CommandCallbacks final : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo&) override
    {
        const std::string raw = characteristic->getValue();
        if (raw.empty() || raw.size() > ecp::MAX_MESSAGE_BYTES) {
            notify_response(0, false, ecp::CODE_BAD_REQUEST, "message size");
            return;
        }
        /* Copy to a fixed buffer slot -- no heap allocation in BLE callback
         * context. If every slot is still awaiting consumption, drop this
         * one rather than blocking the BLE stack. */
        const size_t next_tail = (s_cmd_tail + 1) % CMD_QUEUE_SLOTS;
        if (next_tail == s_cmd_head) {
            Serial.println("[ecp] cmd dropped (queue full)");
            return;
        }
        memcpy(s_cmd_buf[s_cmd_tail], raw.c_str(), raw.size());
        s_cmd_buf[s_cmd_tail][raw.size()] = '\0';
        s_cmd_len[s_cmd_tail] = raw.size();
        s_cmd_tail = next_tail;
    }
};

CommandCallbacks s_command_callbacks;

class ServerCallbacks final : public NimBLEServerCallbacks {
    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) override
    {
        // A central may leave at any time (including after a timeout).  NimBLE
        // does not resume advertising for us, so without this the node becomes
        // undiscoverable until the next reboot.
        NimBLEDevice::getAdvertising()->start();
        LOG_W("ECP", "disconnected reason=%d; advertising restarted", reason);
    }
};

ServerCallbacks s_server_callbacks;
}  // namespace

bool ecp_ble_begin(void)
{
#if defined(ECP_BLE_DISABLED) && ECP_BLE_DISABLED
    LOG_I("ECP", "BLE disabled by build flag");
    return false;
#else
    const String name = device_name();
    NimBLEDevice::init(name.c_str());
    NimBLEDevice::setMTU(ecp::MAX_MESSAGE_BYTES + 3);
    NimBLEServer* server = NimBLEDevice::createServer();
    server->setCallbacks(&s_server_callbacks);
    NimBLEService* service = server->createService(ecp::SERVICE_UUID);
    NimBLECharacteristic* info = service->createCharacteristic(ecp::INFO_UUID, NIMBLE_PROPERTY::READ);
    NimBLECharacteristic* command = service->createCharacteristic(ecp::COMMAND_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    s_response = service->createCharacteristic(ecp::RESPONSE_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    s_state = service->createCharacteristic(ecp::STATE_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    const String payload = info_payload();
    info->setValue(payload.c_str());
    s_response->setValue("{\"v\":1,\"rid\":0,\"ok\":true,\"code\":\"READY\"}");
    s_state->setValue(state_notify_payload().c_str());
    command->setCallbacks(&s_command_callbacks);
    service->start();
    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(ecp::SERVICE_UUID);
    // The 128-bit service UUID eats 16 of the 31 advertisement bytes, so the
    // local name only fits in the scan response.  Enabling scan response
    // without supplying the name leaves every node anonymous: a scanner sees
    // the ECO service but cannot tell which board answered.
    NimBLEAdvertisementData scan_response;
    scan_response.setName(name.c_str());
    advertising->setScanResponseData(scan_response);
    advertising->enableScanResponse(true);
    advertising->start();
    LOG_I("ECP", "advertising name=%s service=%s", name.c_str(), ecp::SERVICE_UUID);
    return true;
#endif
}

void ecp_ble_loop(void)
{
#if defined(ECP_BLE_DISABLED) && ECP_BLE_DISABLED
    return;
#else
    process_pending_command();  // Dispatch queued BLE commands in main loop
    auto& appState = AppState::getInstance();
    if (!s_state || !NimBLEDevice::getServer() || NimBLEDevice::getServer()->getConnectedCount() == 0) return;
    if (millis() - appState.last_state_notify < STATE_NOTIFY_INTERVAL_MS) return;
    appState.last_state_notify = millis();
    publish_state();
#endif
}

bool ecp_ble_connected(void)
{
#if defined(ECP_BLE_DISABLED) && ECP_BLE_DISABLED
    return false;
#else
    return s_state && NimBLEDevice::getServer() && NimBLEDevice::getServer()->getConnectedCount() > 0;
#endif
}

void ecp_ble_notify_state_now(void)
{
#if defined(ECP_BLE_DISABLED) && ECP_BLE_DISABLED
    return;
#else
    if (!s_state || !NimBLEDevice::getServer() || NimBLEDevice::getServer()->getConnectedCount() == 0) return;
    AppState::getInstance().last_state_notify = millis();
    publish_state();
#endif
}
