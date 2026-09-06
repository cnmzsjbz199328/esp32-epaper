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

typedef enum {
    FVID_ENTRY_SD = 0,
    FVID_ENTRY_ROM_PHOTOS = 1,
    FVID_ENTRY_ROM_STORY = 2,
} fvid_entry_kind_t;

typedef struct {
    char path[64];
    char id[32];
    char name[24];
    uint16_t frames;
    uint16_t audio_scene_count;
    uint8_t builtin_index;
    fvid_entry_kind_t kind;
} fvid_entry_t;

const frame_source_t* frame_source_init(void);
const frame_source_t* frame_source_current(void);
const frame_source_t* frame_source_rom(void);
const frame_source_t* frame_source_story_rom(void);
const frame_source_t* frame_source_sd(void);
const char* frame_source_name(void);
void frame_source_use_rom(void);
int frame_source_sd_scan(fvid_entry_t* out, int max);
bool frame_source_sd_open(const char* path);
