#include <Arduino.h>

#include "app_diagnostics.h"
#include "bsp.h"
#include "ecosystem_ble.h"
#include "nvs_settings.h"
#include "shell/app_registry.h"
#include "shell/shell.h"
#include "touch.h"

#if __has_include("wifi_secrets.local.h")
#include "wifi_secrets.local.h"
#define EPAPER_HAS_LOCAL_WIFI_SECRETS 1
#else
#define EPAPER_HAS_LOCAL_WIFI_SECRETS 0
#endif

#define APP_MODE_M0       1
#define APP_MODE_M1       2
#define APP_MODE_SHELL    3

#ifndef APP_MODE
#define APP_MODE APP_MODE_SHELL
#endif

static void provision_local_wifi_credentials()
{
#if EPAPER_HAS_LOCAL_WIFI_SECRETS
    ecp::settings::WifiCredentials current;
    const bool has_current = ecp::settings::get_wifi_credentials(current);
    if (!has_current || current.ssid != EPAPER_WIFI_SSID || current.password != EPAPER_WIFI_PASSWORD) {
        ecp::settings::set_wifi_credentials(EPAPER_WIFI_SSID, EPAPER_WIFI_PASSWORD);
        Serial.println("[wifi] local credentials provisioned");
    }
#endif
}

void setup()
{
    bsp_board_init();
    bsp_ui_init();

    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 2000) delay(10);

    Serial.println(bsp_version_string());
    provision_local_wifi_credentials();

    if (app_boot_held_for_diagnostics()) {
        bsp_touch_init();
        app_run_boot_diagnostics();
        return;
    }

    if (!bsp_touch_init()) {
        Serial.println("[touch] init failed; touch navigation disabled for this boot");
    }

#if APP_MODE == APP_MODE_SHELL
    shell_ble_begin_with_retry();
    shell_begin();
#if defined(APP_AUTOSTART_PHOTOS)
    const app_entry_t* apps = app_registry_entries();
    if (app_registry_count() > 0) shell_open_app(&apps[0]);
#endif
#elif APP_MODE == APP_MODE_M0
    app_run_m0_diagnostics();
#elif APP_MODE == APP_MODE_M1
    app_run_m1_diagnostics();
#else
    Serial.printf("[app] unknown APP_MODE=%d\n", APP_MODE);
#endif
}

void loop()
{
#if APP_MODE == APP_MODE_SHELL
    shell_tick();
#else
    delay(1000);
#endif
}
