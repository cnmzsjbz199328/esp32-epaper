#include "frame_source.h"

#include <string.h>

#include "bsp.h"
#include "story_demo_assets.h"
#include "family_video_assets.h"

namespace {

int photos_count(void)
{
    return APP_VIDEO_FRAME_COUNT;
}

int photos_hint(int index)
{
    if (index < 0 || index >= photos_count()) return 1; /* APP_REFRESH_FULL */
    return (int)APP_VIDEO_REFRESH_HINTS[index];
}

bool photos_load(int index, uint8_t* framebuffer, size_t len)
{
    if (!framebuffer || len != bsp_ui_fb_len() ||
        index < 0 || index >= photos_count()) return false;
    memcpy(framebuffer, APP_VIDEO_FRAMES[index], len);
    return true;
}

int story_count(void) { return STORY_DEMO_FRAME_COUNT; }

int story_hint(int index)
{
    if (index < 0 || index >= story_count()) return 1; /* APP_REFRESH_FULL */
    return (int)STORY_DEMO_REFRESH_HINTS[index];
}

bool story_load(int index, uint8_t* framebuffer, size_t len)
{
    if (!framebuffer || len != bsp_ui_fb_len() ||
        index < 0 || index >= story_count()) return false;
    memcpy(framebuffer, STORY_DEMO_FRAMES[index], len);
    return true;
}

const frame_source_t s_rom = { "PHOTOS", photos_count, photos_hint, photos_load };
const frame_source_t s_story_rom = { "FOX_FOREST", story_count, story_hint, story_load };

}

const frame_source_t* frame_source_rom(void) { return &s_rom; }
const frame_source_t* frame_source_story_rom(void) { return &s_story_rom; }
