#pragma once

#include "frame_source.h"

/* The catalog owns story identity and the mapping from a library entry to
 * its frame/audio source. SD entries are discovered separately by the FVID
 * scanner and use their own path-based audio mapping. */
int story_catalog_append_builtins(fvid_entry_t* out, int max);
const frame_source_t* story_catalog_frame_source(const fvid_entry_t& entry);
bool story_catalog_play_audio(const fvid_entry_t& entry, int scene_index);
const char* story_catalog_storage_name(const fvid_entry_t& entry);
