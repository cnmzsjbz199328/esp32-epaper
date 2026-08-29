#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct frame_source {
    const char* name;
    int (*count)(void);
    int (*hint)(int index);
    bool (*load)(int index, uint8_t* framebuffer, size_t len);
} frame_source_t;

#define SD_STORY_MAX 16

typedef struct {
    char path[64];
    char name[24];
    uint16_t frames;
} fvid_entry_t;

const frame_source_t* frame_source_init(void);
const frame_source_t* frame_source_current(void);
const frame_source_t* frame_source_rom(void);
const frame_source_t* frame_source_sd(void);
const char* frame_source_name(void);
void frame_source_use_rom(void);
int frame_source_sd_scan(fvid_entry_t* out, int max);
bool frame_source_sd_open(const char* path);
