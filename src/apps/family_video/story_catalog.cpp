#include "story_catalog.h"

#include <Arduino.h>
#include <string.h>

#include "family_video_assets.h"
#include "story_audio.h"
#include "story_demo_assets.h"
#include "story_demo_audio.h"

namespace {

void fill_entry(fvid_entry_t* entry, const char* id, const char* name,
                uint16_t frames, uint16_t audio_scene_count,
                fvid_entry_kind_t kind, uint8_t builtin_index)
{
    if (!entry) return;
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->id, sizeof(entry->id), "%s", id);
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    entry->frames = frames;
    entry->audio_scene_count = audio_scene_count;
    entry->builtin_index = builtin_index;
    entry->kind = kind;
}

}

int story_catalog_append_builtins(fvid_entry_t* out, int max)
{
    if (!out || max <= 0) return 0;
    int count = 0;
    if (count < max) {
        fill_entry(&out[count++], "photos", "PHOTOS",
                   (uint16_t)APP_VIDEO_FRAME_COUNT, 0,
                   FVID_ENTRY_ROM_PHOTOS, 0);
    }
    if (count < max) {
        fill_entry(&out[count++], "fox_forest", "FOX_FOREST",
                   (uint16_t)STORY_DEMO_FRAME_COUNT,
                   (uint16_t)STORY_DEMO_AUDIO_SCENE_COUNT,
                   FVID_ENTRY_ROM_STORY, 1);
    }
    return count;
}

const frame_source_t* story_catalog_frame_source(const fvid_entry_t& entry)
{
    if (entry.kind == FVID_ENTRY_ROM_PHOTOS) return frame_source_rom();
    if (entry.kind == FVID_ENTRY_ROM_STORY) return frame_source_story_rom();
    return nullptr;
}

bool story_catalog_play_audio(const fvid_entry_t& entry, int scene_index)
{
    if (entry.kind != FVID_ENTRY_ROM_STORY ||
        scene_index < 0 || scene_index >= entry.audio_scene_count ||
        scene_index >= STORY_DEMO_AUDIO_SCENE_COUNT) {
        story_audio_stop();
        return false;
    }
    Serial.printf("[video] audio map id=%s scene=%d source=ROM\n",
                  entry.id, scene_index);
    return story_audio_play_rom_adpcm(
        STORY_DEMO_AUDIO_DATA[scene_index],
        STORY_DEMO_AUDIO_DATA_LENGTHS[scene_index],
        STORY_DEMO_AUDIO_SAMPLE_COUNTS[scene_index],
        STORY_DEMO_AUDIO_SAMPLE_RATE);
}

const char* story_catalog_storage_name(const fvid_entry_t& entry)
{
    switch (entry.kind) {
    case FVID_ENTRY_ROM_PHOTOS:
    case FVID_ENTRY_ROM_STORY:
        return "ROM";
    case FVID_ENTRY_SD:
    default:
        return "SD";
    }
}
