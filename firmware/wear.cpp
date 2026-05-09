#include "wear.h"
#include "app_config.h"
#include <stdint.h>

static wear_state_t s_state         = WEAR_STATE_ON_BODY;
static uint16_t     s_still_sec     = 0;
static uint16_t     s_offbody_sec   = 0;
static bool         s_changed       = false;
static uint16_t     s_t_still_sec   = ACCEL_STILL_SEC;
static uint16_t     s_t_offbody_sec = ACCEL_OFFBODY_SEC;
static wear_force_t s_force         = WEAR_FORCE_AUTO;

static wear_state_t resolve_with_force(wear_state_t auto_state)
{
    switch (s_force) {
        case WEAR_FORCE_ON:  return WEAR_STATE_ON_BODY;
        case WEAR_FORCE_OFF: return WEAR_STATE_OFF_BODY;
        default:             return auto_state;
    }
}

void wear_init(void)
{
    s_state       = WEAR_STATE_ON_BODY;
    s_still_sec   = 0;
    s_offbody_sec = 0;
    s_changed     = true;
}

void wear_set_timings(uint16_t still_sec, uint16_t offbody_sec)
{
    if (still_sec   == 0) still_sec   = 1;
    if (offbody_sec == 0) offbody_sec = 1;
    s_t_still_sec   = still_sec;
    s_t_offbody_sec = offbody_sec;
}

void wear_set_force(wear_force_t f)
{
    if (s_force != f) {
        s_force   = f;
        s_changed = true;
    }
}

wear_force_t wear_get_force(void) { return s_force; }

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

    if (++s_still_sec >= s_t_still_sec) {
        if (++s_offbody_sec >= s_t_offbody_sec) {
            s_state   = WEAR_STATE_OFF_BODY;
            s_changed = true;
        }
    }
}

wear_state_t wear_get_state(void)
{
    return resolve_with_force(s_state);
}

bool wear_state_changed(void)
{
    bool c = s_changed;
    s_changed = false;
    return c;
}

uint16_t wear_remaining_sec(void)
{
    if (s_force != WEAR_FORCE_AUTO)            return 0;
    if (s_state != WEAR_STATE_ON_BODY)         return 0;
    if (s_still_sec < s_t_still_sec)           return 0;
    uint16_t left = s_t_offbody_sec - s_offbody_sec;
    return left;
}
