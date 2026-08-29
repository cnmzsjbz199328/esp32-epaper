#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct frame_source {
    const char* name;
    int (*count)(void);
    int (*hint)(int index);
    bool (*load)(int index, uint8_t* framebuffer, size_t len);
} frame_source_t;

const frame_source_t* frame_source_init(void);
const frame_source_t* frame_source_current(void);
const frame_source_t* frame_source_rom(void);
const frame_source_t* frame_source_sd(void);
const char* frame_source_name(void);
void frame_source_use_rom(void);
