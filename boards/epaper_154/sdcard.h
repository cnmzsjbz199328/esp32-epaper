#pragma once
#include <Arduino.h>

bool        bsp_sd_init(void);
void        bsp_sd_deinit(void);
bool        bsp_sd_ready(void);
uint64_t    bsp_sd_size_mb(void);
const char* bsp_sd_type(void);
bool        bsp_sd_rw_check(void);

