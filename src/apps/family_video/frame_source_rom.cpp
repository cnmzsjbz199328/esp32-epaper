#include "frame_source.h"

/* Kept as link-compatible diagnostic hooks. Formal firmware has no ROM story
 * frame arrays or audio; both hooks intentionally return nullptr. */
const frame_source_t* frame_source_rom(void) { return nullptr; }
const frame_source_t* frame_source_story_rom(void) { return nullptr; }
