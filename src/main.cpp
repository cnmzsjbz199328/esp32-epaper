#include <Arduino.h>

#include "app_diagnostics.h"
#include "app_family_video.h"
#include "bsp.h"
#include "touch.h"

#define APP_MODE_M0       1
#define APP_MODE_M1       2
#define APP_MODE_VIDEO    3

#ifndef APP_MODE
#define APP_MODE APP_MODE_VIDEO
#endif

void setup()
{
    bsp_board_init();
    bsp_ui_init();

    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 2000) delay(10);

    Serial.println(bsp_version_string());

    if (app_boot_held_for_diagnostics()) {
        bsp_touch_init();
        app_run_boot_diagnostics();
        return;
    }

    if (!bsp_touch_init()) {
        Serial.println("[touch] init failed; touch navigation disabled for this boot");
    }

#if APP_MODE == APP_MODE_VIDEO
    app_family_video_setup();
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
#if APP_MODE == APP_MODE_VIDEO
    app_family_video_loop();
#else
    delay(1000);
#endif
}
