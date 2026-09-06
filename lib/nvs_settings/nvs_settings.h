#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace ecp {
namespace settings {

struct WifiCredentials {
    String ssid;
    String password;
    bool valid;
};

void init(void);

// WiFi provisioning
bool get_wifi_credentials(WifiCredentials& creds);
bool set_wifi_credentials(const char* ssid, const char* password);
bool clear_wifi_credentials(void);

// File sync settings
String get_device_name(const char* default_name = "EPAPER-154");
bool set_device_name(const char* name);
String get_sync_token(void);       // Generated once on first access.
bool has_sync_token(void);
bool set_sync_token(const char* token);
bool rotate_sync_token(void);
bool get_auto_sync(bool default_val = false);
bool set_auto_sync(bool enabled);
bool get_allow_delete(bool default_val = false);
bool set_allow_delete(bool enabled);
bool get_auto_story_scan(bool default_val = true);
bool set_auto_story_scan(bool enabled);

// Volume setting (0..100)
uint8_t get_volume(uint8_t default_val = 80);
bool set_volume(uint8_t volume);

// Brightness setting (0..100)
uint8_t get_brightness(uint8_t default_val = 100);
bool set_brightness(uint8_t brightness);

}  // namespace settings
}  // namespace ecp
