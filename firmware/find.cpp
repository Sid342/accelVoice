#include "find.h"
#include <Arduino.h>

#define DEFAULT_MS         30000
#define STROBE_HALF_PERIOD 50      /* 50 ms on, 50 ms off → 10 Hz */

static uint32_t s_until_ms = 0;
static bool     s_armed    = false;

void find_init(void)
{
    s_until_ms = 0;
    s_armed    = false;
}

void find_start(uint32_t duration_ms)
{
    if (duration_ms == 0) duration_ms = DEFAULT_MS;
    s_until_ms = millis() + duration_ms;
    s_armed    = true;
}

void find_stop(void)
{
    s_until_ms = 0;
    s_armed    = false;
}

bool find_active(uint32_t now_ms)
{
    if (!s_armed) return false;
    if ((int32_t)(now_ms - s_until_ms) >= 0) {
        s_armed = false;
        return false;
    }
    return true;
}

bool find_led_high(uint32_t now_ms)
{
    return (((now_ms / STROBE_HALF_PERIOD) & 1u) == 0);
}

uint32_t find_remaining_ms(uint32_t now_ms)
{
    if (!s_armed) return 0;
    if ((int32_t)(now_ms - s_until_ms) >= 0) return 0;
    return s_until_ms - now_ms;
}
