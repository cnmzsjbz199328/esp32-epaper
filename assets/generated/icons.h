#pragma once

#include <stdint.h>

#define SHELL_ICON_W 80
#define SHELL_ICON_H 80
#define SHELL_ICON_LEN (SHELL_ICON_W * SHELL_ICON_H / 8)

extern const uint8_t SHELL_ICON_FRAMES[SHELL_ICON_LEN];
extern const uint8_t SHELL_ICON_SETTINGS[SHELL_ICON_LEN];

