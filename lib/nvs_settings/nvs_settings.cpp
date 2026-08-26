#include "nvs_settings.h"

#include <Preferences.h>

namespace ecp {
namespace settings {

namespace {
constexpr const char* NVS_NAMESPACE = "ecp_cfg";
constexpr const char* KEY_WIFI_SSID = "wifi_ssid";
constexpr const char* KEY_WIFI_PASS = "wifi_pass";
constexpr const char* KEY_VOLUME    = "vol";
constexpr const char* KEY_BRIGHTNESS = "bright";

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
    creds.ssid = s_prefs.getString(KEY_WIFI_SSID, "");
    creds.password = s_prefs.getString(KEY_WIFI_PASS, "");
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

