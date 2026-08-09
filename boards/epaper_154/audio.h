#pragma once
#include <Arduino.h>

bool   bsp_audio_init(uint32_t sample_rate = 16000, int volume = 75);
void   bsp_audio_deinit(void);
void   bsp_audio_set_volume(int volume);
void   bsp_audio_amp(bool on);
size_t bsp_audio_write_mono(const int16_t* samples, size_t count);
void   bsp_audio_tone(uint32_t freq_hz, uint32_t duration_ms, uint8_t amplitude_pct = 40);
void   bsp_audio_beep_startup(void);
bool   bsp_audio_ready(void);
void   bsp_audio_chip_id(uint8_t* id1, uint8_t* id2);

bool     bsp_mic_init(uint8_t gain_db = 24);
bool     bsp_mic_ready(void);
size_t   bsp_mic_read(int16_t* out, size_t max_samples, uint32_t timeout_ms = 100);
uint16_t bsp_mic_peak_level(void);

