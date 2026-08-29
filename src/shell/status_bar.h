#pragma once

#include <stdint.h>

void status_bar_render_into_fb(void);
bool status_bar_refresh_if_due(uint32_t now_ms);
