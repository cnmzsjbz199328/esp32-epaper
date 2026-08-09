#pragma once
#include <Arduino.h>

typedef struct {
    bool down;
    uint16_t x;
    uint16_t y;
    uint8_t gesture;
    uint8_t points;
} bsp_touch_point_t;

bool bsp_touch_init(void);
bool bsp_touch_ready(void);
bool bsp_touch_read(bsp_touch_point_t* p);
bool bsp_touch_raw(uint8_t out[7]);
void bsp_touch_monitor(void);

