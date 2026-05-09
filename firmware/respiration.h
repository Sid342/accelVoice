#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

void     resp_init(void);
void     resp_reset(void);
void     resp_add_sample(int16_t z_mg);

/* Existing windowed-BPM API. */
bool     resp_result_ready(void);
uint8_t  resp_get_bpm(void);          /* 0 if outside filter */
uint8_t  resp_get_bpm_raw(void);      /* latest, unfiltered */
uint16_t resp_window_progress(void);
uint16_t resp_window_len(void);
void     resp_set_filter(uint8_t bpm_min, uint8_t bpm_max);
void     resp_set_window_sec(uint8_t sec);   /* 5..15 */

/* v2: online peak detector — runs in parallel with the windowed BPM. */
typedef struct {
    uint32_t t_ms;
    int16_t  amplitude_mg;
    uint16_t interval_ms;
} resp_event_t;

void     resp_set_min_interval_ms(uint16_t ms);
uint16_t resp_get_min_interval_ms(void);
void     resp_set_iir_alpha_q15(uint16_t a);
uint16_t resp_get_iir_alpha_q15(void);

int16_t  resp_current_mean_mg(void);
uint16_t resp_instant_bpm(void);
uint32_t resp_total_events(void);

/* Copy events with t_ms > since_ms into out[] up to max. Oldest-first.
 * Returns the number of events copied. */
size_t   resp_get_events_since(uint32_t since_ms, resp_event_t *out, size_t max);

/* Copy last min(count, max) wave samples (oldest-first) into parallel
 * arrays. Returns the number of samples copied. Either out array may
 * be NULL to skip that channel. */
size_t   resp_get_wave(int16_t *out_z, int16_t *out_mean, size_t max);
