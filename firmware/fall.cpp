#include "fall.h"
#include "app_config.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>

/* 3-stage state machine consumed at 100 Hz:
 *   IDLE        → mag < freefall_mg for ≥ FALL_FREEFALL_MIN_MS  → FREEFALL
 *   FREEFALL    → mag > impact_mg within FALL_IMPACT_WINDOW_MS  → IMPACT
 *   IMPACT      → MAD-of-mag < FALL_STILL_MAD_MG sustained for
 *                 FALL_STILL_MIN_MS within FALL_STILL_WINDOW_MS → CONFIRMED
 *   CONFIRMED   → auto-clear back to IDLE after FALL_CONFIRMED_HOLD_MS
 *
 * Any stage exceeding its window without progression resets to IDLE.
 */

/* ── State ───────────────────────────────────────────────────────────────── */
static fall_state_t s_state = FALL_IDLE;
static uint32_t s_state_entered_ms = 0;

static uint16_t s_freefall_mg = FALL_FREEFALL_MG_DEFAULT;
static uint16_t s_impact_mg   = FALL_IMPACT_MG_DEFAULT;

/* Free-fall arming: mag has been below thr for this many consecutive ms */
static uint32_t s_below_since_ms = 0;
static uint16_t s_ff_min_mg      = 0xFFFF;

/* Impact tracking */
static uint32_t s_ff_arm_until_ms = 0;
static uint16_t s_impact_peak_mg  = 0;
static uint32_t s_impact_at_ms    = 0;

/* Stillness window — ring of last N mag samples to compute MAD. */
static int16_t  s_mad_ring[FALL_MAD_WIN_SAMPLES];
static uint16_t s_mad_idx   = 0;
static uint16_t s_mad_count = 0;
static uint32_t s_still_started_ms = 0;
static uint32_t s_still_until_ms   = 0;

static uint32_t s_total_falls   = 0;
static uint32_t s_last_fall_ms  = 0;

static fall_event_t s_events[FALL_EVENT_RING_SIZE];
static uint16_t     s_evt_head  = 0;
static uint16_t     s_evt_count = 0;

static void enter_state(fall_state_t s, uint32_t now)
{
    s_state = s;
    s_state_entered_ms = now;
}

void fall_init(void)
{
    s_state = FALL_IDLE;
    s_state_entered_ms = 0;
    s_below_since_ms = 0;
    s_ff_min_mg = 0xFFFF;
    s_ff_arm_until_ms = 0;
    s_impact_peak_mg = 0;
    s_impact_at_ms = 0;
    s_mad_idx = s_mad_count = 0;
    s_still_started_ms = 0;
    s_still_until_ms = 0;
    s_total_falls = 0;
    s_last_fall_ms = 0;
    s_evt_head = s_evt_count = 0;
}

fall_state_t fall_get_state(void)
{
    /* Auto-decay CONFIRMED after the hold time. */
    if (s_state == FALL_CONFIRMED &&
        millis() - s_state_entered_ms > FALL_CONFIRMED_HOLD_MS) {
        return FALL_IDLE;
    }
    return s_state;
}

const char *fall_state_str(fall_state_t s)
{
    switch (s) {
        case FALL_IDLE:       return "idle";
        case FALL_FREEFALL:   return "freefall";
        case FALL_IMPACT:     return "impact";
        case FALL_STILL_PEND: return "still_pend";
        case FALL_CONFIRMED:  return "confirmed";
    }
    return "?";
}

uint32_t fall_total_falls(void) { return s_total_falls; }
uint32_t fall_last_fall_ms(void) { return s_last_fall_ms; }

size_t fall_get_events_since(uint32_t since_ms,
                             fall_event_t *out, size_t max)
{
    if (max == 0 || s_evt_count == 0) return 0;
    uint16_t start = (s_evt_head + FALL_EVENT_RING_SIZE - s_evt_count)
                     % FALL_EVENT_RING_SIZE;
    size_t o = 0;
    for (uint16_t i = 0; i < s_evt_count && o < max; i++) {
        uint16_t pos = (start + i) % FALL_EVENT_RING_SIZE;
        if (s_events[pos].t_ms > since_ms) out[o++] = s_events[pos];
    }
    return o;
}

void fall_set_freefall_mg(uint16_t mg)
{
    if (mg < 100)  mg = 100;
    if (mg > 1000) mg = 1000;
    s_freefall_mg = mg;
}
uint16_t fall_get_freefall_mg(void) { return s_freefall_mg; }

void fall_set_impact_mg(uint16_t mg)
{
    if (mg < 1000) mg = 1000;
    if (mg > 8000) mg = 8000;
    s_impact_mg = mg;
}
uint16_t fall_get_impact_mg(void) { return s_impact_mg; }

static void event_push(uint32_t t_ms, uint16_t ff_min, uint16_t imp_max,
                       uint16_t mad, bool sim)
{
    s_events[s_evt_head].t_ms             = t_ms;
    s_events[s_evt_head].freefall_min_mg  = ff_min;
    s_events[s_evt_head].impact_max_mg    = imp_max;
    s_events[s_evt_head].still_mad_mg     = mad;
    s_events[s_evt_head].simulated        = sim;
    s_evt_head = (s_evt_head + 1) % FALL_EVENT_RING_SIZE;
    if (s_evt_count < FALL_EVENT_RING_SIZE) s_evt_count++;
}

void fall_simulate(void)
{
    uint32_t now = millis();
    enter_state(FALL_CONFIRMED, now);
    s_total_falls++;
    s_last_fall_ms = now;
    event_push(now, 0, 0, 0, true);
}

/* MAD-of-mag over the FALL_MAD_WIN_SAMPLES ring. Cheap O(N) but only
 * called during STILL_PEND so the cost is bounded. */
static uint16_t mad_of_ring(void)
{
    if (s_mad_count < 4) return 0xFFFF;
    /* mean */
    int32_t sum = 0;
    for (uint16_t i = 0; i < s_mad_count; i++) sum += s_mad_ring[i];
    int32_t mean = sum / s_mad_count;
    int32_t mad = 0;
    for (uint16_t i = 0; i < s_mad_count; i++) {
        int32_t d = (int32_t)s_mad_ring[i] - mean;
        if (d < 0) d = -d;
        mad += d;
    }
    mad /= s_mad_count;
    if (mad > 0xFFFF) mad = 0xFFFF;
    return (uint16_t)mad;
}

void fall_feed_sample(int16_t x_mg, int16_t y_mg, int16_t z_mg, int16_t mag_mg)
{
    (void)x_mg; (void)y_mg; (void)z_mg;
    uint32_t now = millis();
    uint16_t mag = (mag_mg < 0) ? 0 : (uint16_t)mag_mg;

    /* Always feed the MAD ring — cheap, lets stillness probe at any time. */
    s_mad_ring[s_mad_idx] = mag_mg;
    s_mad_idx = (s_mad_idx + 1) % FALL_MAD_WIN_SAMPLES;
    if (s_mad_count < FALL_MAD_WIN_SAMPLES) s_mad_count++;

    /* Auto-decay CONFIRMED back to IDLE after hold. */
    if (s_state == FALL_CONFIRMED &&
        now - s_state_entered_ms > FALL_CONFIRMED_HOLD_MS) {
        enter_state(FALL_IDLE, now);
    }

    switch (s_state) {
        case FALL_IDLE:
        case FALL_CONFIRMED: {
            if (mag < s_freefall_mg) {
                if (s_below_since_ms == 0) {
                    s_below_since_ms = now;
                    s_ff_min_mg      = mag;
                } else {
                    if (mag < s_ff_min_mg) s_ff_min_mg = mag;
                    if (now - s_below_since_ms >= FALL_FREEFALL_MIN_MS) {
                        /* Free-fall confirmed → arm impact watcher. */
                        enter_state(FALL_FREEFALL, now);
                        s_ff_arm_until_ms = now + FALL_IMPACT_WINDOW_MS;
                        s_impact_peak_mg  = 0;
                    }
                }
            } else {
                s_below_since_ms = 0;
                s_ff_min_mg = 0xFFFF;
            }
            break;
        }
        case FALL_FREEFALL: {
            /* Watch for impact. */
            if (now > s_ff_arm_until_ms) {
                /* Free-fall ended without impact → reset. */
                enter_state(FALL_IDLE, now);
                s_below_since_ms = 0;
                s_ff_min_mg = 0xFFFF;
                break;
            }
            if (mag > s_impact_peak_mg) s_impact_peak_mg = mag;
            if (mag > s_impact_mg) {
                enter_state(FALL_IMPACT, now);
                s_impact_at_ms = now;
                s_still_until_ms = now + FALL_STILL_WINDOW_MS;
                s_still_started_ms = 0;
            }
            break;
        }
        case FALL_IMPACT:
        case FALL_STILL_PEND: {
            if (now > s_still_until_ms) {
                /* No stillness within window → cancel. */
                enter_state(FALL_IDLE, now);
                s_below_since_ms = 0;
                s_ff_min_mg = 0xFFFF;
                break;
            }
            uint16_t mad = mad_of_ring();
            if (mad < FALL_STILL_MAD_MG) {
                if (s_still_started_ms == 0) {
                    s_still_started_ms = now;
                    if (s_state == FALL_IMPACT) enter_state(FALL_STILL_PEND, now);
                } else if (now - s_still_started_ms >= FALL_STILL_MIN_MS) {
                    /* CONFIRM */
                    enter_state(FALL_CONFIRMED, now);
                    s_total_falls++;
                    s_last_fall_ms = now;
                    event_push(now, s_ff_min_mg, s_impact_peak_mg, mad, false);
                    s_below_since_ms = 0;
                    s_ff_min_mg = 0xFFFF;
                }
            } else {
                /* Movement broke the stillness — reset stillness timer but
                 * stay in IMPACT until the still window expires. */
                s_still_started_ms = 0;
                if (s_state == FALL_STILL_PEND) enter_state(FALL_IMPACT, now);
            }
            break;
        }
    }
}
