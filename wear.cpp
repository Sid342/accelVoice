#include "wear.h"
#include "app_config.h"
#include <stdint.h>

static wear_state_t s_state       = WEAR_STATE_ON_BODY;
static uint16_t     s_still_sec   = 0;
static uint16_t     s_offbody_sec = 0;
static bool         s_changed     = false;

void wear_init(void)
{
    s_state       = WEAR_STATE_ON_BODY;
    s_still_sec   = 0;
    s_offbody_sec = 0;
    s_changed     = true;   /* announce initial state on first poll */
}

void wear_notify_motion(void)
{
    s_still_sec   = 0;
    s_offbody_sec = 0;
    if (s_state == WEAR_STATE_OFF_BODY) {
        s_state   = WEAR_STATE_ON_BODY;
        s_changed = true;
    }
}

void wear_tick_sec(void)
{
    if (s_state != WEAR_STATE_ON_BODY) return;

    if (++s_still_sec >= ACCEL_STILL_SEC) {
        if (++s_offbody_sec >= ACCEL_OFFBODY_SEC) {
            s_state   = WEAR_STATE_OFF_BODY;
            s_changed = true;
        }
    }
}

wear_state_t wear_get_state(void) { return s_state; }

bool wear_state_changed(void)
{
    bool c = s_changed;
    s_changed = false;
    return c;
}
