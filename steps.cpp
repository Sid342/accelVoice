#include "steps.h"
#include "app_config.h"
#include <Arduino.h>
#include <math.h>

static uint32_t s_count        = 0;
static int16_t  s_baseline_mg  = 1000;     /* ~1 g initial */
static uint32_t s_last_peak_ms = 0;
static bool     s_above        = false;

static int16_t mag_mg(int16_t x, int16_t y, int16_t z)
{
    int32_t sq = (int32_t)x * x + (int32_t)y * y + (int32_t)z * z;
    return (int16_t)sqrt((double)sq);
}

void steps_init(void)
{
    s_count        = 0;
    s_baseline_mg  = 1000;
    s_last_peak_ms = 0;
    s_above        = false;
}

void steps_reset(void) { s_count = 0; }
uint32_t steps_get_count(void) { return s_count; }

void steps_add_sample(int16_t x_mg, int16_t y_mg, int16_t z_mg)
{
    int16_t m = mag_mg(x_mg, y_mg, z_mg);

    /* EMA baseline, alpha = 1/16 → tracks gravity orientation slowly. */
    s_baseline_mg = s_baseline_mg + (m - s_baseline_mg) / 16;

    int16_t  delta = m - s_baseline_mg;
    uint32_t now   = millis();

    if (!s_above && delta > STEP_PEAK_THRESH_MG) {
        uint32_t gap = now - s_last_peak_ms;
        if (gap >= STEP_MIN_INTERVAL_MS && gap <= STEP_MAX_INTERVAL_MS) {
            s_count++;
        }
        s_last_peak_ms = now;
        s_above        = true;
    } else if (s_above && delta < (STEP_PEAK_THRESH_MG / 2)) {
        s_above = false;
    }
}
