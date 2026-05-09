#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    SLOUCH_UNKNOWN    = 0,    /* not calibrated yet */
    SLOUCH_UPRIGHT    = 1,
    SLOUCH_SLOUCHING  = 2,
} slouch_state_t;

typedef struct {
    uint32_t start_t_ms;
    uint32_t duration_sec;
    int16_t  max_dev_deg_x10;
} slouch_event_t;

void           slouch_init(void);
/* Feed at 25 Hz (decimated path). Caller must already have wear_get_state()
 * == ON_BODY before calling. */
void           slouch_feed_sample(int16_t x_mg, int16_t y_mg, int16_t z_mg);

slouch_state_t slouch_get_state(void);

int16_t        slouch_current_pitch_deg_x10(void);
int16_t        slouch_baseline_deg_x10(void);
int16_t        slouch_current_deviation_deg_x10(void);
bool           slouch_is_calibrated(void);

uint32_t       slouch_session_count(void);
uint32_t       slouch_total_time_sec(void);
uint32_t       slouch_longest_session_sec(void);
uint32_t       slouch_time_since_last_upright_sec(void);

void           slouch_calibrate_now(void);
void           slouch_set_baseline_deg_x10(int16_t baseline);
void           slouch_reset_stats(void);

size_t         slouch_get_events_since(uint32_t since_ms,
                                       slouch_event_t *out, size_t max);

/* Tunables. */
void           slouch_set_thresh_deg(uint8_t deg);
uint8_t        slouch_get_thresh_deg(void);
void           slouch_set_sustain_sec(uint8_t sec);
uint8_t        slouch_get_sustain_sec(void);

/* Run-once-per-second from main loop, drives "time since last upright" and
 * session-duration counters. */
void           slouch_tick_sec(void);
