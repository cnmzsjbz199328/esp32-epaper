#pragma once

#include "../apps/iapp.h"

void launcher_render_into_fb(void);
void launcher_tick(void);
void launcher_handle_key(const char* key, const char* event);
void launcher_handle_tap(uint16_t x, uint16_t y, bool long_press);
