#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    WEAR_STATE_ON_BODY  = 0,
    WEAR_STATE_OFF_BODY = 1,
} wear_state_t;

typedef enum {
    WEAR_FORCE_AUTO = 0,
    WEAR_FORCE_ON   = 1,
    WEAR_FORCE_OFF  = 2,
} wear_force_t;

void         wear_init(void);

/* v1 motion-IRQ path — kept as a third sign-of-life signal. */
void         wear_notify_motion(void);

/* v2 per-sample feed (call every accel read, regardless of state). Drives
 * the 1-second variance and the 5-second gravity-vector trackers. */
void         wear_feed_sample(int16_t x_mg, int16_t y_mg, int16_t z_mg, int16_t mag_mg);

/* Run once per second from main loop. Updates the state machine using
 * (variance > thr) OR (gravity-diff > thr) OR (motion IRQ within window). */
void         wear_tick_sec(void);

wear_state_t wear_get_state(void);
bool         wear_state_changed(void);

/* Runtime tunables. still_sec is retained for cfg compatibility but ignored
 * by the v2 logic (the still→offbody compound timer was the wrong gate). */
void         wear_set_timings(uint16_t still_sec, uint16_t offbody_sec);
void         wear_set_var_thresh_mg(uint16_t mg);
void         wear_set_grav_diff_thresh_mg(uint16_t mg);
uint16_t     wear_get_var_thresh_mg(void);
uint16_t     wear_get_grav_diff_thresh_mg(void);

/* Force override — bypass SM. */
void         wear_set_force(wear_force_t f);
wear_force_t wear_get_force(void);

/* Live signals for UI / diagnostics. */
int16_t      wear_current_var_mg(void);
int16_t      wear_current_grav_diff_mg(void);
const char  *wear_last_signal(void);          /* "var" / "grav" / "irq" / "none" */

/* Live countdown for UI: seconds until off-body. 0 if not currently
 * counting down. */
uint16_t     wear_remaining_sec(void);
