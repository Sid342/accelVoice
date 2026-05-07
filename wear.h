#pragma once
#include <stdbool.h>

typedef enum {
    WEAR_STATE_ON_BODY  = 0,
    WEAR_STATE_OFF_BODY = 1,
} wear_state_t;

void         wear_init(void);
void         wear_notify_motion(void);  /* call from accel motion detection */
void         wear_tick_sec(void);       /* call once per second             */
wear_state_t wear_get_state(void);
bool         wear_state_changed(void);  /* one-shot edge flag               */
