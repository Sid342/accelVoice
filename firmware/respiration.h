#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    RESP_AXIS_Z = 0,
    RESP_AXIS_X = 1,
    RESP_AXIS_Y = 2,
} resp_axis_t;

typedef enum {
    RESP_PHASE_FLAT   = 0,
    RESP_PHASE_INHALE = 1,    /* current sample above running mean */
    RESP_PHASE_EXHALE = 2,    /* current sample below running mean */
} resp_phase_t;

void     resp_init(void);
void     resp_reset(void);

/* Feed every sample (25 Hz). Internally picks the configured axis. */
void     resp_add_sample(int16_t x_mg, int16_t y_mg, int16_t z_mg);

/* Existing windowed-BPM API. */
bool     resp_result_ready(void);
uint8_t  resp_get_bpm(void);
uint8_t  resp_get_bpm_raw(void);
uint16_t resp_window_progress(void);
uint16_t resp_window_len(void);
void     resp_set_filter(uint8_t bpm_min, uint8_t bpm_max);
void     resp_set_window_sec(uint8_t sec);

/* v2 events. */
typedef struct {
    uint32_t t_ms;
    int16_t  amplitude_mg;     /* peak-to-peak of the just-completed cycle */
    uint16_t interval_ms;
} resp_event_t;

void         resp_set_min_interval_ms(uint16_t ms);
uint16_t     resp_get_min_interval_ms(void);
void         resp_set_iir_alpha_q15(uint16_t a);
uint16_t     resp_get_iir_alpha_q15(void);
void         resp_set_min_amplitude_mg(int16_t mg);
int16_t      resp_get_min_amplitude_mg(void);
void         resp_set_axis(resp_axis_t a);
resp_axis_t  resp_get_axis(void);

int16_t      resp_current_mean_mg(void);
int16_t      resp_current_amplitude_mg(void);   /* depth of last breath */
resp_phase_t resp_current_phase(void);
uint16_t     resp_instant_bpm(void);
uint16_t     resp_last_interval_ms(void);
uint32_t     resp_last_peak_ms(void);
uint32_t     resp_total_events(void);

size_t       resp_get_events_since(uint32_t since_ms, resp_event_t *out, size_t max);
size_t       resp_get_wave(int16_t *out_axis, int16_t *out_mean, size_t max);
