#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    STEP_AXIS_MAG = 0,
    STEP_AXIS_X   = 1,
    STEP_AXIS_Y   = 2,
    STEP_AXIS_Z   = 3,
} step_axis_t;

typedef struct {
    uint32_t t_ms;
    int16_t  amplitude_mg;
    uint16_t interval_ms;
} step_event_t;

void     steps_init(void);
void     steps_add_sample(int16_t x_mg, int16_t y_mg, int16_t z_mg);
uint32_t steps_get_count(void);
void     steps_reset(void);

/* v1 tunables. */
void     steps_set_thresh_mg(uint16_t mg);
void     steps_set_intervals_ms(uint16_t min_ms, uint16_t max_ms);

/* v1 helpers. */
uint32_t steps_get_cadence_ms(void);
uint16_t steps_get_per_min(void);

/* v2 tunables. */
void        steps_set_adaptive(bool en);
bool        steps_get_adaptive(void);
void        steps_set_bandpass(bool en);
bool        steps_get_bandpass(void);
void        steps_set_axis(step_axis_t axis);
step_axis_t steps_get_axis(void);
void        steps_set_amp_window_ms(uint16_t ms);
uint16_t    steps_get_amp_window_ms(void);

/* v2 live signals (for plotting + UI). */
int16_t  steps_current_signal(void);     /* post-axis-pick + post-bandpass + post-baseline */
int16_t  steps_current_baseline(void);
int16_t  steps_current_threshold(void);  /* effective threshold (adaptive applied) */

/* v2 events. */
uint32_t steps_total_events(void);
size_t   steps_get_events_since(uint32_t since_ms, step_event_t *out, size_t max);

/* v2 ground truth. */
void     steps_groundtruth_tap(uint32_t t_ms);
void     steps_groundtruth_clear(void);
size_t   steps_get_groundtruth_count(void);
void     steps_eval(uint16_t window_sec, uint16_t tol_ms,
                    uint32_t *out_detected, uint32_t *out_groundtruth,
                    uint32_t *out_matched,
                    uint16_t *out_prec_pct, uint16_t *out_recall_pct);
