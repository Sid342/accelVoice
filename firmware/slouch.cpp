#include "slouch.h"
#include "app_config.h"
#include <Arduino.h>
#include <math.h>

/* Pipeline @ 25 Hz (decimated path):
 *   xyz → slow-IIR gravity vector → atan2 pitch (deg×10)
 *       → vs baseline → deviation
 *       → SLOUCH state machine with sustain + recovery dwell
 */

/* ── State ───────────────────────────────────────────────────────────────── */
static float    s_grav_x = 0.0f, s_grav_y = 0.0f, s_grav_z = 1000.0f;
static bool     s_grav_init = false;
static int16_t  s_pitch_x10  = 0;
static int16_t  s_baseline_x10 = SLOUCH_BASELINE_DEG_X10_DEFAULT;
static bool     s_calibrated   = false;
static uint8_t  s_thresh_deg   = SLOUCH_THRESH_DEG_DEFAULT;
static uint8_t  s_sustain_sec  = SLOUCH_SUSTAIN_SEC_DEFAULT;

static slouch_state_t s_state = SLOUCH_UNKNOWN;

/* "Above threshold" continuous run, in seconds (counted via slouch_tick_sec). */
static uint16_t s_above_sec = 0;
static uint16_t s_below_sec = 0;
static uint32_t s_session_start_ms = 0;
static int16_t  s_session_max_dev_x10 = 0;

static uint32_t s_session_count       = 0;
static uint32_t s_total_time_sec      = 0;
static uint32_t s_longest_session_sec = 0;
static uint32_t s_last_upright_sec    = 0;     /* monotonic seconds counter   */
static uint32_t s_now_sec             = 0;

static slouch_event_t s_events[SLOUCH_EVENT_RING_SIZE];
static uint16_t       s_evt_head  = 0;
static uint16_t       s_evt_count = 0;

static const float GRAV_ALPHA = (float)SLOUCH_GRAV_ALPHA_Q15 / 32768.0f;

void slouch_init(void)
{
    s_grav_x = 0.0f; s_grav_y = 0.0f; s_grav_z = 1000.0f;
    s_grav_init = false;
    s_pitch_x10 = 0;
    s_calibrated = false;
    s_state = SLOUCH_UNKNOWN;
    s_above_sec = 0; s_below_sec = 0;
    s_session_start_ms = 0;
    s_session_max_dev_x10 = 0;
    s_session_count = 0;
    s_total_time_sec = 0;
    s_longest_session_sec = 0;
    s_last_upright_sec = 0;
    s_now_sec = 0;
    s_evt_head = s_evt_count = 0;
}

slouch_state_t slouch_get_state(void)              { return s_state; }
int16_t        slouch_current_pitch_deg_x10(void)  { return s_pitch_x10; }
int16_t        slouch_baseline_deg_x10(void)       { return s_baseline_x10; }
bool           slouch_is_calibrated(void)          { return s_calibrated; }

int16_t slouch_current_deviation_deg_x10(void)
{
    return (int16_t)((int32_t)s_pitch_x10 - (int32_t)s_baseline_x10);
}

uint32_t slouch_session_count(void)         { return s_session_count; }
uint32_t slouch_total_time_sec(void)        { return s_total_time_sec; }
uint32_t slouch_longest_session_sec(void)   { return s_longest_session_sec; }
uint32_t slouch_time_since_last_upright_sec(void)
{
    if (s_last_upright_sec == 0) return 0;
    if (s_now_sec < s_last_upright_sec) return 0;
    return s_now_sec - s_last_upright_sec;
}

void slouch_calibrate_now(void)
{
    /* Capture current pitch as the new upright baseline. */
    s_baseline_x10 = s_pitch_x10;
    s_calibrated   = true;
    s_state        = SLOUCH_UPRIGHT;
    s_above_sec = 0; s_below_sec = 0;
}

void slouch_set_baseline_deg_x10(int16_t baseline)
{
    s_baseline_x10 = baseline;
    if (baseline != SLOUCH_BASELINE_DEG_X10_DEFAULT) s_calibrated = true;
}

void slouch_reset_stats(void)
{
    s_session_count = 0;
    s_total_time_sec = 0;
    s_longest_session_sec = 0;
    s_evt_head = s_evt_count = 0;
    s_above_sec = 0; s_below_sec = 0;
    s_session_start_ms = 0;
    s_session_max_dev_x10 = 0;
}

void slouch_set_thresh_deg(uint8_t deg)
{
    if (deg < 1)  deg = 1;
    if (deg > 60) deg = 60;
    s_thresh_deg = deg;
}
uint8_t slouch_get_thresh_deg(void) { return s_thresh_deg; }

void slouch_set_sustain_sec(uint8_t sec)
{
    if (sec < 1)  sec = 1;
    if (sec > 60) sec = 60;
    s_sustain_sec = sec;
}
uint8_t slouch_get_sustain_sec(void) { return s_sustain_sec; }

size_t slouch_get_events_since(uint32_t since_ms,
                               slouch_event_t *out, size_t max)
{
    if (max == 0 || s_evt_count == 0) return 0;
    uint16_t start = (s_evt_head + SLOUCH_EVENT_RING_SIZE - s_evt_count)
                     % SLOUCH_EVENT_RING_SIZE;
    size_t o = 0;
    for (uint16_t i = 0; i < s_evt_count && o < max; i++) {
        uint16_t pos = (start + i) % SLOUCH_EVENT_RING_SIZE;
        if (s_events[pos].start_t_ms > since_ms) out[o++] = s_events[pos];
    }
    return o;
}

static void event_push(uint32_t start_ms, uint32_t dur_sec, int16_t max_dev_x10)
{
    s_events[s_evt_head].start_t_ms       = start_ms;
    s_events[s_evt_head].duration_sec     = dur_sec;
    s_events[s_evt_head].max_dev_deg_x10  = max_dev_x10;
    s_evt_head = (s_evt_head + 1) % SLOUCH_EVENT_RING_SIZE;
    if (s_evt_count < SLOUCH_EVENT_RING_SIZE) s_evt_count++;
}

void slouch_feed_sample(int16_t x_mg, int16_t y_mg, int16_t z_mg)
{
    /* Slow IIR per axis to extract gravity. α≈0.005 → ~8 s effective. */
    float fx = (float)x_mg, fy = (float)y_mg, fz = (float)z_mg;
    if (!s_grav_init) {
        s_grav_x = fx; s_grav_y = fy; s_grav_z = fz;
        s_grav_init = true;
    } else {
        s_grav_x += GRAV_ALPHA * (fx - s_grav_x);
        s_grav_y += GRAV_ALPHA * (fy - s_grav_y);
        s_grav_z += GRAV_ALPHA * (fz - s_grav_z);
    }

    /* Pitch about lateral axis: angle between gravity vector and the
     * Y-Z plane, in degrees. atan2(gx, sqrt(gy²+gz²)) is positive when
     * the device tilts forward/back. Identical convention to the
     * Atovio production firmware. */
    float yz = sqrtf(s_grav_y * s_grav_y + s_grav_z * s_grav_z);
    float p  = atan2f(s_grav_x, yz) * (180.0f / (float)M_PI);
    s_pitch_x10 = (int16_t)(p * 10.0f);

    /* State machine runs in slouch_tick_sec(); this is just sampling. */
}

void slouch_tick_sec(void)
{
    s_now_sec++;
    if (!s_calibrated) {
        s_state = SLOUCH_UNKNOWN;
        return;
    }
    int16_t dev_x10 = slouch_current_deviation_deg_x10();
    int16_t abs_dev_x10 = dev_x10 < 0 ? (int16_t)-dev_x10 : dev_x10;
    int16_t thr_x10     = (int16_t)s_thresh_deg * 10;

    if (abs_dev_x10 > thr_x10) {
        s_above_sec++;
        s_below_sec = 0;
        if (s_state == SLOUCH_SLOUCHING) {
            if (abs_dev_x10 > s_session_max_dev_x10)
                s_session_max_dev_x10 = abs_dev_x10;
            s_total_time_sec++;
        } else if (s_above_sec >= s_sustain_sec) {
            /* Transition to SLOUCHING. Session start was sustain_sec ago. */
            s_state             = SLOUCH_SLOUCHING;
            s_session_start_ms  = millis() - (uint32_t)s_sustain_sec * 1000UL;
            s_session_max_dev_x10 = abs_dev_x10;
            s_session_count++;
            /* Backfill the dwell time we sat above thr before flip. */
            s_total_time_sec   += s_sustain_sec;
        }
    } else {
        s_below_sec++;
        s_above_sec = 0;
        if (s_state == SLOUCH_SLOUCHING) {
            if (s_below_sec >= SLOUCH_RECOVER_SEC) {
                /* End session. */
                uint32_t dur_sec = (millis() - s_session_start_ms) / 1000UL;
                if (dur_sec > s_longest_session_sec) s_longest_session_sec = dur_sec;
                event_push(s_session_start_ms, dur_sec, s_session_max_dev_x10);
                s_state = SLOUCH_UPRIGHT;
                s_last_upright_sec = s_now_sec;
            } else {
                /* Still inside the recovery dwell — count it. */
                s_total_time_sec++;
            }
        } else {
            s_state = SLOUCH_UPRIGHT;
            s_last_upright_sec = s_now_sec;
        }
    }
}
