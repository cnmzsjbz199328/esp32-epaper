#pragma once

#include <stdbool.h>

bool app_boot_held_for_diagnostics(void);
void app_run_boot_diagnostics(void);
void app_run_m0_diagnostics(void);
void app_run_m1_diagnostics(void);
