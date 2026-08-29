#include "frame_source.h"

#include <string.h>

#include "bsp.h"
#include "family_video_assets.h"

namespace {

int rom_count(void) { return APP_VIDEO_FRAME_COUNT; }

int rom_hint(int index)
{
    if (index < 0 || index >= APP_VIDEO_FRAME_COUNT) return APP_REFRESH_FULL;
    return (int)APP_VIDEO_REFRESH_HINTS[index];
}

bool rom_load(int index, uint8_t* framebuffer, size_t len)
{
    if (!framebuffer || len != APP_VIDEO_FRAME_LEN ||
        index < 0 || index >= APP_VIDEO_FRAME_COUNT) return false;
    memcpy(framebuffer, APP_VIDEO_FRAMES[index], len);
    return true;
}

const frame_source_t s_rom = { "ROM", rom_count, rom_hint, rom_load };

}

const frame_source_t* frame_source_rom(void) { return &s_rom; }
