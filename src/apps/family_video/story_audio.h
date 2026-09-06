#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Scene audio is deliberately a separate, optional layer over FVID.  The
 * image player remains usable when a story has no audio directory or when a
 * WAV file is malformed.
 */
void story_audio_init(void);
void story_audio_stop(void);
bool story_audio_play_scene(const char* story_path, int scene_index);
bool story_audio_play_opening(const char* story_path);
bool story_audio_play_rom(const int16_t* samples, size_t sample_count, uint32_t sample_rate);
bool story_audio_play_rom_adpcm(const uint8_t* data, size_t data_length,
                                size_t sample_count, uint32_t sample_rate);
void story_audio_toggle_pause(void);
bool story_audio_is_playing(void);
bool story_audio_is_paused(void);
uint32_t story_audio_remaining_ms(void);
