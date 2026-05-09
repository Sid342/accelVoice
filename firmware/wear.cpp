#include "wear.h"
#include "app_config.h"
#include <Arduino.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

/* State */
static wear_state_t s_state         = WEAR_STATE_ON_BODY;
static uint16_t     s_offbody_count = 0;
static bool         s_changed       = false;
static uint16_t     s_t_offbody_sec = ACCEL_OFFBODY_SEC;
static wear_force_t s_force         = WEAR_FORCE_AUTO;
static uint32_t     s_last_irq_ms   = 0;
static uint16_t     s_var_thresh_mg       = WEAR_VAR_THRESH_MG;
static uint16_t     s_grav_diff_thresh_mg = WEAR_GRAV_DIFF_THRESH_MG;

/* Variance (MAD) tracker over last ≤ 25 magnitude samples (1 s @ 25 Hz). */
#define WEAR_MAG_BUF_LEN 25
static int16_t s_mag_buf[WEAR_MAG_BUF_LEN];
static uint8_t s_mag_idx    = 0;
static uint8_t s_mag_filled = 0;

/* Gravity ring: WEAR_GRAV_RING_SEC entries, each = 1-second-averaged xyz.
 * Newest at [0]; oldest at [GRAV_RING - 1]. */
#define GRAV_RING WEAR_GRAV_RING_SEC
static int16_t  s_grav_ring[GRAV_RING][3];
static uint8_t  s_grav_filled = 0;
static int32_t  s_grav_sum[3] = {0, 0, 0};
static uint16_t s_grav_count  = 0;

/* Last-tick diagnostic values for UI. */
static int16_t     s_last_var_mg       = 0;
static int16_t     s_last_grav_diff_mg = 0;
static const char *s_last_signal       = "none";

void wear_init(void)
{
    s_state         = WEAR_STATE_ON_BODY;
    s_offbody_count = 0;
    s_changed       = true;
    s_mag_idx = s_mag_filled = 0;
    s_grav_filled = 0;
    s_grav_sum[0] = s_grav_sum[1] = s_grav_sum[2] = 0;
    s_grav_count  = 0;
    s_last_irq_ms       = 0;
    s_last_var_mg       = 0;
    s_last_grav_diff_mg = 0;
    s_last_signal       = "none";
}

void wear_set_timings(uint16_t still_sec, uint16_t offbody_sec)
{
    (void)still_sec;                         /* v2 ignores still_sec by design */
    if (offbody_sec == 0) offbody_sec = 1;
    s_t_offbody_sec = offbody_sec;
}

void wear_set_force(wear_force_t f)
{
    if (s_force != f) { s_force = f; s_changed = true; }
}
wear_force_t wear_get_force(void) { return s_force; }

void wear_set_var_thresh_mg(uint16_t mg)
{
    if (mg < 1)   mg = 1;
    if (mg > 500) mg = 500;
    s_var_thresh_mg = mg;
}
void wear_set_grav_diff_thresh_mg(uint16_t mg)
{
    if (mg < 1)    mg = 1;
    if (mg > 1000) mg = 1000;
    s_grav_diff_thresh_mg = mg;
}
uint16_t wear_get_var_thresh_mg(void)       { return s_var_thresh_mg; }
uint16_t wear_get_grav_diff_thresh_mg(void) { return s_grav_diff_thresh_mg; }

int16_t wear_current_var_mg(void)        { return s_last_var_mg; }
int16_t wear_current_grav_diff_mg(void)  { return s_last_grav_diff_mg; }
const char *wear_last_signal(void)       { return s_last_signal; }

void wear_notify_motion(void)
{
    s_last_irq_ms   = millis();
    s_offbody_count = 0;
    if (s_state == WEAR_STATE_OFF_BODY) {
        s_state       = WEAR_STATE_ON_BODY;
        s_changed     = true;
        s_last_signal = "irq";
    }
}

void wear_feed_sample(int16_t x_mg, int16_t y_mg, int16_t z_mg, int16_t mag_mg)
{
    s_mag_buf[s_mag_idx++] = mag_mg;
    if (s_mag_idx >= WEAR_MAG_BUF_LEN) s_mag_idx = 0;
    if (s_mag_filled < WEAR_MAG_BUF_LEN) s_mag_filled++;

    s_grav_sum[0] += x_mg;
    s_grav_sum[1] += y_mg;
    s_grav_sum[2] += z_mg;
    s_grav_count++;
}

/* MAD: mean(|m_i − mean|). Cheap proxy for std-dev, no sqrt. */
static int16_t compute_mad(void)
{
    if (s_mag_filled < 4) return 0;
    int32_t sum = 0;
    for (uint8_t i = 0; i < s_mag_filled; i++) sum += s_mag_buf[i];
    int16_t mean = (int16_t)(sum / s_mag_filled);
    int32_t mad_sum = 0;
    for (uint8_t i = 0; i < s_mag_filled; i++) {
        int32_t d = (int32_t)s_mag_buf[i] - mean;
        if (d < 0) d = -d;
        mad_sum += d;
    }
    return (int16_t)(mad_sum / s_mag_filled);
}

void wear_tick_sec(void)
{
    /* 1) Variance signal over the magnitude buffer. */
    s_last_var_mg = compute_mad();

    /* 2) Gravity-vector diff: current 1-sec avg vs entry that's about to fall
     *    off the ring (i.e. ~GRAV_RING sec old). Compute BEFORE shifting. */
    int16_t grav_diff = 0;
    if (s_grav_count > 0) {
        int16_t cur_avg[3] = {
            (int16_t)(s_grav_sum[0] / (int32_t)s_grav_count),
            (int16_t)(s_grav_sum[1] / (int32_t)s_grav_count),
            (int16_t)(s_grav_sum[2] / (int32_t)s_grav_count),
        };
        if (s_grav_filled >= GRAV_RING) {
            int32_t dx = (int32_t)cur_avg[0] - s_grav_ring[GRAV_RING - 1][0];
            int32_t dy = (int32_t)cur_avg[1] - s_grav_ring[GRAV_RING - 1][1];
            int32_t dz = (int32_t)cur_avg[2] - s_grav_ring[GRAV_RING - 1][2];
            int32_t sq = dx * dx + dy * dy + dz * dz;
            grav_diff  = (int16_t)sqrt((double)sq);
        }
        for (int8_t i = GRAV_RING - 1; i > 0; i--) {
            s_grav_ring[i][0] = s_grav_ring[i - 1][0];
            s_grav_ring[i][1] = s_grav_ring[i - 1][1];
            s_grav_ring[i][2] = s_grav_ring[i - 1][2];
        }
        s_grav_ring[0][0] = cur_avg[0];
        s_grav_ring[0][1] = cur_avg[1];
        s_grav_ring[0][2] = cur_avg[2];
        if (s_grav_filled < GRAV_RING) s_grav_filled++;
        s_grav_sum[0] = s_grav_sum[1] = s_grav_sum[2] = 0;
        s_grav_count  = 0;
    }
    s_last_grav_diff_mg = grav_diff;

    /* 3) Decide. Any of var / grav / recent IRQ keeps state ON-BODY. */
    bool sign       = false;
    const char *src = "none";
    if (s_last_var_mg > (int16_t)s_var_thresh_mg) {
        sign = true; src = "var";
    } else if (s_grav_filled >= GRAV_RING && grav_diff > (int16_t)s_grav_diff_thresh_mg) {
        sign = true; src = "grav";
    } else if (s_last_irq_ms != 0 &&
               (millis() - s_last_irq_ms) < ((uint32_t)s_t_offbody_sec * 1000UL)) {
        sign = true; src = "irq";
    }
    s_last_signal = src;

    if (sign) {
        s_offbody_count = 0;
        if (s_state == WEAR_STATE_OFF_BODY) {
            s_state   = WEAR_STATE_ON_BODY;
            s_changed = true;
        }
    } else if (s_state == WEAR_STATE_ON_BODY) {
        s_offbody_count++;
        if (s_offbody_count >= s_t_offbody_sec) {
            s_state   = WEAR_STATE_OFF_BODY;
            s_changed = true;
        }
    }
}

static wear_state_t resolve_with_force(wear_state_t auto_state)
{
    switch (s_force) {
        case WEAR_FORCE_ON:  return WEAR_STATE_ON_BODY;
        case WEAR_FORCE_OFF: return WEAR_STATE_OFF_BODY;
        default:             return auto_state;
    }
}
wear_state_t wear_get_state(void)  { return resolve_with_force(s_state); }
bool wear_state_changed(void)      { bool c = s_changed; s_changed = false; return c; }

uint16_t wear_remaining_sec(void)
{
    if (s_force != WEAR_FORCE_AUTO)    return 0;
    if (s_state != WEAR_STATE_ON_BODY) return 0;
    if (s_offbody_count == 0)          return 0;
    return s_t_offbody_sec - s_offbody_count;
}
