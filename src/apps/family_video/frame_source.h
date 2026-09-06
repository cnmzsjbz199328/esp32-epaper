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
#define STORY_ID_MAX_LENGTH 32
#define STORY_TITLE_MAX_LENGTH 48
#define STORY_AUTHOR_MAX_LENGTH 40
#define STORY_PATH_MAX_LENGTH 96

typedef enum {
    FVID_REFRESH_PARTIAL = 0,
    FVID_REFRESH_FULL = 1,
    FVID_REFRESH_FINAL_FULL = 2,
} fvid_refresh_hint_t;

typedef enum {
    FVID_ENTRY_SD = 0,
} fvid_entry_kind_t;

typedef struct {
    char path[STORY_PATH_MAX_LENGTH];       /* Story package root. */
    char fvid_path[STORY_PATH_MAX_LENGTH];  /* Explicit story.json reference. */
    char id[STORY_ID_MAX_LENGTH];
    char name[STORY_TITLE_MAX_LENGTH];
    char author[STORY_AUTHOR_MAX_LENGTH];
    char opening_audio[STORY_PATH_MAX_LENGTH];
    uint16_t frames;
    uint16_t audio_scene_count;
    bool audio_missing;
    bool closing_overlay;
    fvid_entry_kind_t kind;
} fvid_entry_t;

const frame_source_t* frame_source_init(void);
const frame_source_t* frame_source_current(void);
/* ROM sources are intentionally unavailable in production firmware. */
const frame_source_t* frame_source_rom(void);
const frame_source_t* frame_source_story_rom(void);
const frame_source_t* frame_source_sd(void);
const char* frame_source_name(void);
void frame_source_use_rom(void);
int frame_source_sd_scan(fvid_entry_t* out, int max);
bool frame_source_sd_open(const char* path);
