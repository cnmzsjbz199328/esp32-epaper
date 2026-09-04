#include "frame_source.h"

#include <string.h>

#include "bsp.h"
#if defined(APP_STORY_DEMO_ROM)
#include "story_demo_assets.h"
#else
#include "family_video_assets.h"
#endif

namespace {

int rom_count(void)
{
#if defined(APP_STORY_DEMO_ROM)
    return STORY_DEMO_FRAME_COUNT;
#else
    return APP_VIDEO_FRAME_COUNT;
#endif
}

int rom_hint(int index)
{
    if (index < 0 || index >= rom_count()) return 1; /* APP_REFRESH_FULL */
#if defined(APP_STORY_DEMO_ROM)
    return (int)STORY_DEMO_REFRESH_HINTS[index];
#else
    return (int)APP_VIDEO_REFRESH_HINTS[index];
#endif
}

bool rom_load(int index, uint8_t* framebuffer, size_t len)
{
    if (!framebuffer || len != bsp_ui_fb_len() ||
        index < 0 || index >= rom_count()) return false;
#if defined(APP_STORY_DEMO_ROM)
    memcpy(framebuffer, STORY_DEMO_FRAMES[index], len);
#else
    memcpy(framebuffer, APP_VIDEO_FRAMES[index], len);
#endif
    return true;
}

const frame_source_t s_rom = { "ROM", rom_count, rom_hint, rom_load };

}

const frame_source_t* frame_source_rom(void) { return &s_rom; }
