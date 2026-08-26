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

// Volume setting (0..100)
uint8_t get_volume(uint8_t default_val = 80);
bool set_volume(uint8_t volume);

// Brightness setting (0..100)
uint8_t get_brightness(uint8_t default_val = 100);
bool set_brightness(uint8_t brightness);

}  // namespace settings
}  // namespace ecp

