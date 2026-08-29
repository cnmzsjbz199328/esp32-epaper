#include "diag_runner.h"

#include <Arduino.h>

#include "../../app_diagnostics.h"
#include "bsp.h"
#include "../../shell/shell.h"
#include "../../../boards/epaper_154/touch.h"

void diag_runner_run_full(void)
{
    Serial.println("[settings] hardware selftest begin (modal)");
    bsp_selftest_begin();
    bsp_selftest_board_specific();
    bsp_selftest_summary();

    /* selftest.cpp intentionally owns and deletes the IDF I2C driver while
     * probing. Reinstall the application-facing Wire/touch path before the
     * shell is resumed. The following full-refresh transition also rebuilds
     * the e-paper partial base image after the diagnostic driver touched EPD. */
    (void)bsp_touch_init();
    Serial.printf("[settings] hardware selftest done P%d W%d F%d records=%u\n",
                  bsp_selftest_count(BSP_SELFTEST_PASS),
                  bsp_selftest_count(BSP_SELFTEST_WARN),
                  bsp_selftest_count(BSP_SELFTEST_FAIL),
                  (unsigned)bsp_selftest_record_count());
}
