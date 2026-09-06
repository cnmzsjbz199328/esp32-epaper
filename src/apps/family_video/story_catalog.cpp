#include "story_catalog.h"
#include "story_audio.h"

int story_catalog_scan(fvid_entry_t* out, int max)
{
    return frame_source_sd_scan(out, max);
}

const frame_source_t* story_catalog_frame_source(const fvid_entry_t& entry)
{
    return entry.kind == FVID_ENTRY_SD ? frame_source_sd() : nullptr;
}

bool story_catalog_play_audio(const fvid_entry_t& entry, int scene_index)
{
    return entry.kind == FVID_ENTRY_SD && scene_index >= 0 &&
           scene_index < entry.frames &&
           story_audio_play_scene(entry.path, scene_index);
}

const char* story_catalog_storage_name(const fvid_entry_t& entry)
{
    switch (entry.kind) {
    case FVID_ENTRY_SD:
    default:
        return "TF";
    }
}
