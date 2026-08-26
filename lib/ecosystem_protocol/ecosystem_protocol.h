#pragma once

#include <stddef.h>
#include <stdint.h>

// BLE ecosystem protocol v0.3: shared, board-independent public contract.
namespace ecp {
constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr const char* SERVICE_UUID  = "7e0d0001-7b83-4b74-9d5c-6b5851120001";
constexpr const char* INFO_UUID     = "7e0d0002-7b83-4b74-9d5c-6b5851120001";
constexpr const char* COMMAND_UUID  = "7e0d0003-7b83-4b74-9d5c-6b5851120001";
constexpr const char* RESPONSE_UUID = "7e0d0004-7b83-4b74-9d5c-6b5851120001";
constexpr const char* STATE_UUID    = "7e0d0005-7b83-4b74-9d5c-6b5851120001";
// Keep application messages below the conservative central receive budget.
// This remains in force after the NimBLE mbuf copy fix because notifications
// still have an MTU-dependent ceiling.
constexpr size_t MAX_MESSAGE_BYTES = 256;
constexpr const char* OP_PING = "sys.ping";
constexpr const char* OP_INFO = "sys.info";
constexpr const char* OP_STATE_GET = "state.get";
constexpr const char* OP_UI_MESSAGE = "ui.message";
constexpr const char* OP_DISPLAY_CLEAR = "display.clear";
constexpr const char* OP_LED_SET = "led.set";
constexpr const char* OP_DEVICE_REBOOT = "device.reboot";
constexpr const char* OP_DEVICE_POWEROFF = "device.poweroff";
constexpr const char* OP_OTA_VERSION_CHECK = "ota.version_check";
// BLE v0.4 extensions
constexpr const char* OP_INPUT_KEY = "input.key";
constexpr const char* OP_INPUT_TEXT = "input.text";
constexpr const char* OP_INPUT_IMU = "input.imu";
constexpr const char* OP_CONFIG_WIFI_SET = "config.wifi.set";
constexpr const char* OP_CONFIG_WIFI_CLEAR = "config.wifi.clear";
constexpr const char* OP_CONFIG_VOLUME_SET = "config.volume.set";
constexpr const char* OP_CONFIG_BRIGHTNESS_SET = "config.brightness.set";

constexpr const char* CODE_OK = "OK";
constexpr const char* CODE_BAD_REQUEST = "BAD_REQUEST";
constexpr const char* CODE_UNKNOWN_COMMAND = "UNKNOWN_COMMAND";
constexpr const char* CODE_UNSUPPORTED = "UNSUPPORTED";

// Canonical capability names. Board profiles expose these names; clients
// should treat unknown names as forward-compatible extensions.
constexpr const char* CAP_DISPLAY   = "display";
constexpr const char* CAP_TOUCH     = "touch";
constexpr const char* CAP_AUDIO_OUT = "audio.out";
constexpr const char* CAP_AUDIO_IN  = "audio.in";
constexpr const char* CAP_RGB_LED   = "led.rgb";
constexpr const char* CAP_SDCARD    = "storage.sd";
constexpr const char* CAP_BATTERY   = "battery";
constexpr const char* CAP_IMU       = "imu";
constexpr const char* CAP_WIFI      = "wifi";
// BLE v0.4 capability extensions
constexpr const char* CAP_INPUT_REMOTE_KEY  = "input.remote.key";
constexpr const char* CAP_INPUT_REMOTE_TEXT = "input.remote.text";
constexpr const char* CAP_INPUT_REMOTE_IMU  = "input.remote.imu";
constexpr const char* CAP_CONFIG_WIFI       = "config.wifi";
constexpr const char* CAP_CONFIG_VOLUME     = "config.volume";
constexpr const char* CAP_CONFIG_BRIGHTNESS = "config.brightness";
constexpr const char* CAP_UI_NAV            = "ui.nav";
constexpr const char* CAP_APP_DEMO          = "app.demo";
}  // namespace ecp
