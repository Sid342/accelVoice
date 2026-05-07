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
void         wear_notify_motion(void);
void         wear_tick_sec(void);
wear_state_t wear_get_state(void);
bool         wear_state_changed(void);

/* Runtime tunables (cfg-applied). */
void         wear_set_timings(uint16_t still_sec, uint16_t offbody_sec);

/* Force override — bypass SM. */
void         wear_set_force(wear_force_t f);
wear_force_t wear_get_force(void);

/* Live countdown for UI: seconds until off-body if still in still period.
 * Returns 0 if not counting down (off-body, or motion just happened).        */
uint16_t     wear_remaining_sec(void);
