#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    BATT_STATE_OK       = 0,
    BATT_STATE_LOW_BAT  = 1,
    BATT_STATE_CRITICAL = 2,
    BATT_STATE_CHARGING = 3,
    BATT_STATE_FAULT    = 4,
} batt_state_t;

void         battsim_set(uint8_t pct, bool charging, bool fault);
uint8_t      battsim_get_pct(void);
bool         battsim_is_charging(void);
bool         battsim_is_fault(void);
batt_state_t battsim_get_state(void);
const char  *battsim_state_str(batt_state_t s);

/* True when production firmware would force ionizer off regardless of mode/wear.
 *   - LOW_BAT, CRITICAL, FAULT → override ON
 *   - CHARGING → override ON (charger overrides per nrfBLE plan)
 *   - OK → override OFF                                                       */
bool battsim_overrides_ionizer(void);
