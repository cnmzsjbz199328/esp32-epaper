#include "nvs_settings.h"

#include <Preferences.h>
#include <esp_system.h>
#include <string.h>

namespace ecp {
namespace settings {

namespace {
constexpr const char* NVS_NAMESPACE = "ecp_cfg";
constexpr const char* KEY_WIFI_SSID = "wifi_ssid";
constexpr const char* KEY_WIFI_PASS = "wifi_pass";
constexpr const char* KEY_VOLUME    = "vol";
constexpr const char* KEY_BRIGHTNESS = "bright";
constexpr const char* KEY_DEVICE_NAME = "device_name";
constexpr const char* KEY_SYNC_TOKEN = "sync_token";
constexpr const char* KEY_AUTO_SYNC = "auto_sync";
constexpr const char* KEY_ALLOW_DELETE = "allow_del";
constexpr const char* KEY_AUTO_STORY_SCAN = "story_scan";

Preferences s_prefs;
bool s_inited = false;
}  // namespace

void init(void)
{
    if (s_inited) return;
    s_prefs.begin(NVS_NAMESPACE, false);
    s_inited = true;
}

bool get_wifi_credentials(WifiCredentials& creds)
{
    init();
    creds.ssid = s_prefs.isKey(KEY_WIFI_SSID) ? s_prefs.getString(KEY_WIFI_SSID) : "";
    creds.password = s_prefs.isKey(KEY_WIFI_PASS) ? s_prefs.getString(KEY_WIFI_PASS) : "";
    creds.valid = (creds.ssid.length() > 0);
    return creds.valid;
}

bool set_wifi_credentials(const char* ssid, const char* password)
{
    if (!ssid || strlen(ssid) == 0) return false;
    init();
    s_prefs.putString(KEY_WIFI_SSID, ssid);
    s_prefs.putString(KEY_WIFI_PASS, password ? password : "");
    return true;
}

bool clear_wifi_credentials(void)
{
    init();
    s_prefs.remove(KEY_WIFI_SSID);
    s_prefs.remove(KEY_WIFI_PASS);
    return true;
}

String get_device_name(const char* default_name)
{
    init();
    if (s_prefs.isKey(KEY_DEVICE_NAME)) return s_prefs.getString(KEY_DEVICE_NAME);
    return String(default_name ? default_name : "EPAPER-154");
}

bool set_device_name(const char* name)
{
    if (!name || strlen(name) == 0 || strlen(name) > 32) return false;
    init();
    return s_prefs.putString(KEY_DEVICE_NAME, name) > 0;
}

String get_sync_token(void)
{
    init();
    if (!s_prefs.isKey(KEY_SYNC_TOKEN)) rotate_sync_token();
    return s_prefs.getString(KEY_SYNC_TOKEN);
}

bool has_sync_token(void)
{
    init();
    return s_prefs.isKey(KEY_SYNC_TOKEN) && s_prefs.getString(KEY_SYNC_TOKEN).length() == 32;
}

bool set_sync_token(const char* token)
{
    if (!token || strlen(token) != 32) return false;
    init();
    return s_prefs.putString(KEY_SYNC_TOKEN, token) > 0;
}

bool rotate_sync_token(void)
{
    init();
    char token[33] = {};
    for (int i = 0; i < 4; i++) {
        snprintf(token + i * 8, sizeof(token) - i * 8, "%08lx",
                 (unsigned long)esp_random());
    }
    return s_prefs.putString(KEY_SYNC_TOKEN, token) > 0;
}

bool get_auto_sync(bool default_val)
{
    init();
    return s_prefs.getBool(KEY_AUTO_SYNC, default_val);
}

bool set_auto_sync(bool enabled)
{
    init();
    return s_prefs.putBool(KEY_AUTO_SYNC, enabled);
}

bool get_allow_delete(bool default_val)
{
    init();
    return s_prefs.getBool(KEY_ALLOW_DELETE, default_val);
}

bool set_allow_delete(bool enabled)
{
    init();
    return s_prefs.putBool(KEY_ALLOW_DELETE, enabled);
}

bool get_auto_story_scan(bool default_val)
{
    init();
    return s_prefs.getBool(KEY_AUTO_STORY_SCAN, default_val);
}

bool set_auto_story_scan(bool enabled)
{
    init();
    return s_prefs.putBool(KEY_AUTO_STORY_SCAN, enabled);
}

uint8_t get_volume(uint8_t default_val)
{
    init();
    return static_cast<uint8_t>(s_prefs.getUChar(KEY_VOLUME, default_val));
}

bool set_volume(uint8_t volume)
{
    if (volume > 100) return false;
    init();
    s_prefs.putUChar(KEY_VOLUME, volume);
    return true;
}

uint8_t get_brightness(uint8_t default_val)
{
    init();
    return static_cast<uint8_t>(s_prefs.getUChar(KEY_BRIGHTNESS, default_val));
}

bool set_brightness(uint8_t brightness)
{
    if (brightness > 100) return false;
    init();
    s_prefs.putUChar(KEY_BRIGHTNESS, brightness);
    return true;
}

}  // namespace settings
}  // namespace ecp
