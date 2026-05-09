#include "steps.h"
#include "app_config.h"
#include <Arduino.h>
#include <math.h>

static uint32_t s_count           = 0;
static int16_t  s_baseline_mg     = 1000;
static uint32_t s_last_peak_ms    = 0;
static uint32_t s_prev_step_ms    = 0;
static uint32_t s_last_cadence_ms = 0;
static bool     s_above           = false;
static uint16_t s_thresh_mg       = STEP_PEAK_THRESH_MG;
static uint16_t s_min_ms          = STEP_MIN_INTERVAL_MS;
static uint16_t s_max_ms          = STEP_MAX_INTERVAL_MS;

static int16_t mag_mg(int16_t x, int16_t y, int16_t z)
{
    int32_t sq = (int32_t)x * x + (int32_t)y * y + (int32_t)z * z;
    return (int16_t)sqrt((double)sq);
}

void steps_init(void)
{
    s_count           = 0;
    s_baseline_mg     = 1000;
    s_last_peak_ms    = 0;
    s_prev_step_ms    = 0;
    s_last_cadence_ms = 0;
    s_above           = false;
}

void steps_reset(void)
{
    s_count           = 0;
    s_prev_step_ms    = 0;
    s_last_cadence_ms = 0;
}

uint32_t steps_get_count(void) { return s_count; }

void steps_set_thresh_mg(uint16_t mg)
{
    if (mg < 10)   mg = 10;
    if (mg > 2000) mg = 2000;
    s_thresh_mg = mg;
}

void steps_set_intervals_ms(uint16_t min_ms, uint16_t max_ms)
{
    if (min_ms < 50)        min_ms = 50;
    if (max_ms > 5000)      max_ms = 5000;
    if (min_ms >= max_ms)   max_ms = min_ms + 100;
    s_min_ms = min_ms;
    s_max_ms = max_ms;
}

uint32_t steps_get_cadence_ms(void)
{
    /* Stale if last step was > 3 s ago. */
    if (s_last_cadence_ms == 0) return 0;
    if (millis() - s_prev_step_ms > 3000) return 0;
    return s_last_cadence_ms;
}

uint16_t steps_get_per_min(void)
{
    uint32_t c = steps_get_cadence_ms();
    if (c == 0) return 0;
    return (uint16_t)(60000UL / c);
}

void steps_add_sample(int16_t x_mg, int16_t y_mg, int16_t z_mg)
{
    int16_t m = mag_mg(x_mg, y_mg, z_mg);

    s_baseline_mg = s_baseline_mg + (m - s_baseline_mg) / 16;

    int16_t  delta = m - s_baseline_mg;
    uint32_t now   = millis();

    if (!s_above && delta > (int16_t)s_thresh_mg) {
        uint32_t gap = now - s_last_peak_ms;
        if (gap >= s_min_ms && gap <= s_max_ms) {
            s_count++;
            if (s_prev_step_ms != 0) {
                s_last_cadence_ms = now - s_prev_step_ms;
            }
            s_prev_step_ms = now;
        }
        s_last_peak_ms = now;
        s_above        = true;
    } else if (s_above && delta < (int16_t)(s_thresh_mg / 2)) {
        s_above = false;
    }
}
