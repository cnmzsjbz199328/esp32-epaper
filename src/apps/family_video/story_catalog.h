#pragma once

#include "frame_source.h"

/* The catalog is deliberately TF-only. A package is visible only when its
 * /video/<id>/story.json and referenced FVID are valid. */
int story_catalog_scan(fvid_entry_t* out, int max);
const frame_source_t* story_catalog_frame_source(const fvid_entry_t& entry);
bool story_catalog_play_audio(const fvid_entry_t& entry, int scene_index);
const char* story_catalog_storage_name(const fvid_entry_t& entry);
