#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    uint32_t t_ms;
    uint8_t  peak_count;
    uint16_t duration_ms;
    uint16_t peak_amplitude_mg;
} cough_event_t;

void     cough_init(void);
void     cough_feed_sample(int16_t x_mg, int16_t y_mg, int16_t z_mg);

uint32_t cough_total(void);
uint16_t cough_rate_per_min(void);
uint32_t cough_last_event_ms(void);

/* Live envelope for /cough/wave (mg). */
size_t   cough_get_wave(int16_t *out, size_t max);
uint16_t cough_wave_period_ms(void);
int16_t  cough_current_envelope_mg(void);
uint8_t  cough_current_cluster_count(void);

/* Detected events ring (most-recent-first iteration via since_ms). */
size_t   cough_get_events_since(uint32_t since_ms, cough_event_t *out, size_t max);

/* Ground-truth taps + greedy nearest-match precision/recall. */
size_t   cough_get_gt_since(uint32_t since_ms, uint32_t *out, size_t max);
void     cough_add_gt(uint32_t t_ms);
void     cough_clear_gt(void);
size_t   cough_get_gt_count(void);
void     cough_eval(uint16_t window_ms, uint16_t *tp, uint16_t *fp, uint16_t *fn);

/* Manual fire — pushes a synthetic event for end-to-end UI validation
 * on bench rigs where the 5–15 Hz band-pass kills desk-tap signals. */
void     cough_simulate(void);

/* Tunables. */
void     cough_set_thresh_mg(uint16_t mg);
uint16_t cough_get_thresh_mg(void);
void     cough_set_cluster_window_ms(uint16_t ms);
uint16_t cough_get_cluster_window_ms(void);
void     cough_set_min_peaks(uint8_t n);
uint8_t  cough_get_min_peaks(void);
