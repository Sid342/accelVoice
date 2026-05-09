#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    VOICE_IDLE      = 0,
    VOICE_STREAMING = 1,
    VOICE_STOPPING  = 2,
} voice_state_t;

void          voice_init(void);
void          voice_loop_tick(void);

bool          voice_start(uint32_t timeout_ms, bool vad_enabled);
void          voice_stop(const char *reason);

voice_state_t voice_state(void);
const char   *voice_state_str(void);
const char   *voice_last_stop_reason(void);
uint32_t      voice_elapsed_ms(void);
uint32_t      voice_bytes_captured(void);
int16_t       voice_current_rms(void);
size_t        voice_last_wav_size(void);

/* Runtime tunables */
void          voice_set_vad_threshold(int16_t threshold);
void          voice_set_silence_ms(uint32_t ms);
void          voice_set_timeout_ms(uint32_t ms);
void          voice_set_gain_shift(uint8_t shift);
void          voice_set_dc_blocker(bool en);
void          voice_set_noise_gate(int16_t threshold);
int16_t       voice_get_vad_threshold(void);
uint32_t      voice_get_silence_ms(void);
uint32_t      voice_get_timeout_ms(void);
uint8_t       voice_get_gain_shift(void);
bool          voice_get_dc_blocker(void);
int16_t       voice_get_noise_gate(void);
