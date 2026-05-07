#include "battsim.h"

static uint8_t s_pct      = 80;
static bool    s_charging = false;
static bool    s_fault    = false;

void battsim_set(uint8_t pct, bool charging, bool fault)
{
    if (pct > 100) pct = 100;
    s_pct      = pct;
    s_charging = charging;
    s_fault    = fault;
}

uint8_t battsim_get_pct(void)      { return s_pct;      }
bool    battsim_is_charging(void)  { return s_charging; }
bool    battsim_is_fault(void)     { return s_fault;    }

batt_state_t battsim_get_state(void)
{
    if (s_fault)       return BATT_STATE_FAULT;
    if (s_charging)    return BATT_STATE_CHARGING;
    if (s_pct <= 5)    return BATT_STATE_CRITICAL;
    if (s_pct <= 15)   return BATT_STATE_LOW_BAT;
    return BATT_STATE_OK;
}

const char *battsim_state_str(batt_state_t s)
{
    switch (s) {
        case BATT_STATE_OK:       return "ok";
        case BATT_STATE_LOW_BAT:  return "low_bat";
        case BATT_STATE_CRITICAL: return "critical";
        case BATT_STATE_CHARGING: return "charging";
        case BATT_STATE_FAULT:    return "fault";
    }
    return "?";
}

bool battsim_overrides_ionizer(void)
{
    batt_state_t st = battsim_get_state();
    return st != BATT_STATE_OK;   /* anything other than OK forces ionizer off */
}
