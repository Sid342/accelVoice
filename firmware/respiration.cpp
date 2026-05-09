#include "respiration.h"
#include "app_config.h"
#include <Arduino.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#define BUF_MAX (15 * RESP_SAMPLE_HZ)

/* ── Existing windowed BPM (v1, kept for parity) ─────────────────────────── */
static int16_t  s_buf[BUF_MAX];
static uint16_t s_idx       = 0;
static bool     s_ready     = false;
static uint8_t  s_bpm       = 0;
static uint8_t  s_bpm_raw   = 0;
static uint8_t  s_bpm_min   = RESP_BPM_MIN;
static uint8_t  s_bpm_max   = RESP_BPM_MAX;
static uint8_t  s_win_sec   = RESP_WINDOW_SEC;
static uint16_t s_win_len   = RESP_WINDOW_SEC * RESP_SAMPLE_HZ;

/* ── v2 online detector (now driven by post-PCA composite) ───────────────── */
static int32_t      s_mean_q15        = 0;
static bool         s_mean_init       = false;
static int16_t      s_prev_v          = 0;
static bool         s_prev_init       = false;
static uint32_t     s_last_peak_ms    = 0;
static uint16_t     s_min_interval_ms = RESP_MIN_INTERVAL_MS;
static uint16_t     s_iir_alpha       = RESP_IIR_ALPHA_Q15;
static int16_t      s_min_amp_mg      = RESP_MIN_AMPLITUDE_MG;
static resp_axis_t  s_axis            = (resp_axis_t)RESP_AXIS_DEFAULT;
static uint16_t     s_last_interval_ms = 0;
static int16_t      s_last_amp_mg     = 0;
static uint32_t     s_event_total     = 0;

/* Peak / trough tracker between consecutive rising mean-crossings. */
static int16_t      s_cycle_max       = INT16_MIN;
static int16_t      s_cycle_min       = INT16_MAX;
static bool         s_cycle_init      = false;

static resp_event_t s_events[RESP_EVENT_RING_SIZE];
static uint16_t     s_evt_head    = 0;
static uint16_t     s_evt_count   = 0;

static int16_t      s_wave_v[RESP_WAVE_RING_SIZE];
static int16_t      s_wave_m[RESP_WAVE_RING_SIZE];
static uint16_t     s_wave_idx    = 0;
static uint16_t     s_wave_count  = 0;

/* ── v3 pipeline state ───────────────────────────────────────────────────── */

/* Median ring per axis (size RESP_MEDIAN_RING_LEN, length controlled by s_median_len). */
static int16_t      s_med_buf[3][RESP_MEDIAN_RING_LEN];
static uint8_t      s_med_idx   = 0;
static uint8_t      s_med_count = 0;
static uint8_t      s_median_len = RESP_MEDIAN_LEN_DEFAULT;

/* Butterworth band-pass: 4th-order = HPF biquad cascaded with LPF biquad,
 * separate state per axis. Float math (ESP32 has FPU; this is well under
 * 1% CPU at 25 Hz × 3 axes × 2 biquads). */
typedef struct {
    float b0, b1, b2, a1, a2;
    float z1, z2;
} biquad_t;

static biquad_t s_hpf[3];
static biquad_t s_lpf[3];
static bool     s_bp_active = true;        /* false if both cutoffs are 0      */
static bool     s_bp_hpf_on = true;
static bool     s_bp_lpf_on = true;
static uint8_t  s_bp_low_x10  = RESP_BP_LOW_HZ_X10_DEFAULT;   /* user-set     */
static uint8_t  s_bp_high_x10 = RESP_BP_HIGH_HZ_X10_DEFAULT;
static uint8_t  s_bp_low_active  = RESP_BP_LOW_HZ_X10_DEFAULT;
static uint8_t  s_bp_high_active = RESP_BP_HIGH_HZ_X10_DEFAULT;
static bool     s_adaptive_bp = RESP_ADAPTIVE_BP_DEFAULT;

/* PCA-lite: EMA of squared deviation per axis. */
static float    s_pca_mean[3]  = {0.0f, 0.0f, 0.0f};
static float    s_pca_var[3]   = {0.0f, 0.0f, 0.0f};
static bool     s_pca_init     = false;
static float    s_pca_w[3]     = {1.0f/3, 1.0f/3, 1.0f/3};

/* Schmitt trigger hysteresis on the composite signal. */
static uint8_t  s_hyst_mg     = RESP_HYSTERESIS_MG_DEFAULT;
typedef enum { SCHMITT_ARMED = 0, SCHMITT_ABOVE = 1 } schmitt_state_t;
static schmitt_state_t s_schmitt = SCHMITT_ARMED;

/* Activity proxy: EMA of magnitude variance, scaled in mg² × 0.1. */
static float    s_activity_var = 0.0f;
static float    s_activity_mean = 0.0f;
static bool     s_activity_init = false;

/* Autocorrelation ring of the post-fusion composite (int16 mg). */
#define RESP_AC_RING_LEN  (RESP_AUTOCORR_RING_MAX_SEC * RESP_SAMPLE_HZ)  /* 1500 */
static int16_t   s_ac_ring[RESP_AC_RING_LEN];
static uint16_t  s_ac_idx   = 0;
static uint16_t  s_ac_count = 0;
static uint8_t   s_ac_win_sec = RESP_AUTOCORR_WIN_SEC_DEF;
static uint32_t  s_ac_last_run_ms = 0;
static uint8_t   s_ac_bpm = 0;
static uint8_t   s_ac_conf = 0;

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static int16_t pick_axis_raw(int16_t x, int16_t y, int16_t z)
{
    switch (s_axis) {
        case RESP_AXIS_X: return x;
        case RESP_AXIS_Y: return y;
        default:          return z;
    }
}

static void wave_push(int16_t v, int16_t m)
{
    s_wave_v[s_wave_idx] = v;
    s_wave_m[s_wave_idx] = m;
    s_wave_idx++;
    if (s_wave_idx >= RESP_WAVE_RING_SIZE) s_wave_idx = 0;
    if (s_wave_count < RESP_WAVE_RING_SIZE) s_wave_count++;
}

static void event_push(uint32_t t_ms, int16_t amp_mg, uint16_t interval_ms)
{
    s_events[s_evt_head].t_ms         = t_ms;
    s_events[s_evt_head].amplitude_mg = amp_mg;
    s_events[s_evt_head].interval_ms  = interval_ms;
    s_evt_head = (s_evt_head + 1) % RESP_EVENT_RING_SIZE;
    if (s_evt_count < RESP_EVENT_RING_SIZE) s_evt_count++;
    s_event_total++;
}

static uint8_t count_peaks_window(uint16_t len)
{
    int32_t sum = 0;
    for (uint16_t i = 0; i < len; i++) sum += s_buf[i];
    int16_t mean = (int16_t)(sum / (int32_t)len);
    uint8_t peaks = 0;
    bool above = (s_buf[0] > mean);
    const uint8_t min_dist = RESP_SAMPLE_HZ;
    uint16_t dist = 0;
    for (uint16_t i = 1; i < len; i++) {
        bool now_above = (s_buf[i] > mean);
        if (dist < 65535) dist++;
        if (!above && now_above && dist >= min_dist) { peaks++; dist = 0; }
        above = now_above;
    }
    return peaks;
}

/* Tiny insertion sort for ≤9 ints. */
static int16_t median_of(const int16_t *src, uint8_t n)
{
    int16_t a[RESP_MEDIAN_RING_LEN];
    for (uint8_t i = 0; i < n; i++) a[i] = src[i];
    for (uint8_t i = 1; i < n; i++) {
        int16_t v = a[i];
        int8_t j = (int8_t)i - 1;
        while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; }
        a[j + 1] = v;
    }
    return a[n / 2];
}

/* Push raw axis sample to its median ring, return median of last `s_median_len`. */
static int16_t median_push(uint8_t axis, int16_t v)
{
    s_med_buf[axis][s_med_idx] = v;
    /* Index advance handled once per sample by caller via s_med_idx_advance() */
    if (s_median_len <= 1) return v;
    /* Walk back s_median_len entries from current position. */
    uint8_t n = s_median_len;
    if (n > s_med_count + 1) n = s_med_count + 1;   /* warm-up */
    if (n < 1) n = 1;
    int16_t buf[RESP_MEDIAN_RING_LEN];
    /* Newest sample is at s_med_idx; collect n entries ending there. */
    for (uint8_t i = 0; i < n; i++) {
        int idx = (int)s_med_idx - (int)i;
        if (idx < 0) idx += RESP_MEDIAN_RING_LEN;
        buf[i] = s_med_buf[axis][idx];
    }
    return median_of(buf, n);
}

/* Bilinear-transform 2nd-order Butterworth LPF biquad (Q=1/√2). */
static void biquad_lpf(biquad_t *bq, float fc, float fs)
{
    if (fc <= 0.0f) {
        /* pass-through */
        bq->b0 = 1.0f; bq->b1 = 0.0f; bq->b2 = 0.0f;
        bq->a1 = 0.0f; bq->a2 = 0.0f;
        return;
    }
    float w0 = 2.0f * (float)M_PI * fc / fs;
    float cosw = cosf(w0);
    float sinw = sinf(w0);
    float Q = 0.7071067811865f;     /* Butterworth */
    float alpha = sinw / (2.0f * Q);
    float a0 = 1.0f + alpha;
    float b0 = (1.0f - cosw) / 2.0f;
    float b1 = 1.0f - cosw;
    float b2 = (1.0f - cosw) / 2.0f;
    float a1 = -2.0f * cosw;
    float a2 = 1.0f - alpha;
    bq->b0 = b0 / a0;
    bq->b1 = b1 / a0;
    bq->b2 = b2 / a0;
    bq->a1 = a1 / a0;
    bq->a2 = a2 / a0;
}

/* Bilinear-transform 2nd-order Butterworth HPF biquad. */
static void biquad_hpf(biquad_t *bq, float fc, float fs)
{
    if (fc <= 0.0f) {
        bq->b0 = 1.0f; bq->b1 = 0.0f; bq->b2 = 0.0f;
        bq->a1 = 0.0f; bq->a2 = 0.0f;
        return;
    }
    float w0 = 2.0f * (float)M_PI * fc / fs;
    float cosw = cosf(w0);
    float sinw = sinf(w0);
    float Q = 0.7071067811865f;
    float alpha = sinw / (2.0f * Q);
    float a0 = 1.0f + alpha;
    float b0 =  (1.0f + cosw) / 2.0f;
    float b1 = -(1.0f + cosw);
    float b2 =  (1.0f + cosw) / 2.0f;
    float a1 = -2.0f * cosw;
    float a2 = 1.0f - alpha;
    bq->b0 = b0 / a0;
    bq->b1 = b1 / a0;
    bq->b2 = b2 / a0;
    bq->a1 = a1 / a0;
    bq->a2 = a2 / a0;
}

/* Direct Form II Transposed step. */
static inline float biquad_step(biquad_t *bq, float x)
{
    float y = bq->b0 * x + bq->z1;
    bq->z1 = bq->b1 * x - bq->a1 * y + bq->z2;
    bq->z2 = bq->b2 * x - bq->a2 * y;
    return y;
}

static void biquad_reset(biquad_t *bq) { bq->z1 = 0.0f; bq->z2 = 0.0f; }

static void prepare_filters(uint8_t low_x10, uint8_t high_x10)
{
    s_bp_low_active  = low_x10;
    s_bp_high_active = high_x10;
    float fs = (float)RESP_SAMPLE_HZ;
    float f_lo = (float)low_x10  * 0.1f;
    float f_hi = (float)high_x10 * 0.1f;
    s_bp_hpf_on = (low_x10  > 0);
    s_bp_lpf_on = (high_x10 > 0);
    s_bp_active = s_bp_hpf_on || s_bp_lpf_on;
    for (uint8_t a = 0; a < 3; a++) {
        biquad_hpf(&s_hpf[a], f_lo, fs);
        biquad_lpf(&s_lpf[a], f_hi, fs);
        biquad_reset(&s_hpf[a]);
        biquad_reset(&s_lpf[a]);
    }
}

static void update_adaptive_bp(void)
{
    if (!s_adaptive_bp) {
        if (s_bp_low_active != s_bp_low_x10 || s_bp_high_active != s_bp_high_x10) {
            prepare_filters(s_bp_low_x10, s_bp_high_x10);
        }
        return;
    }
    /* s_activity_var tracks a magnitude-variance EMA. */
    uint8_t lo, hi;
    if (s_activity_var < (float)RESP_ACTIVITY_LO_MG2) {
        lo = 1; hi = 5;
    } else if (s_activity_var < (float)RESP_ACTIVITY_HI_MG2) {
        lo = 2; hi = 6;
    } else {
        lo = 3; hi = 7;
    }
    if (lo != s_bp_low_active || hi != s_bp_high_active) {
        prepare_filters(lo, hi);
    }
}

/* PCA-lite: update per-axis variance EMAs and recompute weights. */
static void pca_update(float fx, float fy, float fz)
{
    const float alpha = (float)RESP_PCA_TAU_ALPHA_Q15 / 32768.0f;
    if (!s_pca_init) {
        s_pca_mean[0] = fx; s_pca_mean[1] = fy; s_pca_mean[2] = fz;
        s_pca_var[0]  = 1.0f; s_pca_var[1] = 1.0f; s_pca_var[2] = 1.0f;
        s_pca_init = true;
    } else {
        float v[3] = { fx, fy, fz };
        for (uint8_t i = 0; i < 3; i++) {
            float d = v[i] - s_pca_mean[i];
            s_pca_mean[i] += alpha * d;
            float dd = v[i] - s_pca_mean[i];
            s_pca_var[i]  += alpha * (dd * dd - s_pca_var[i]);
        }
    }
    float sum = s_pca_var[0] + s_pca_var[1] + s_pca_var[2] + 1e-3f;
    s_pca_w[0] = s_pca_var[0] / sum;
    s_pca_w[1] = s_pca_var[1] / sum;
    s_pca_w[2] = s_pca_var[2] / sum;
}

/* Activity variance (very cheap; used for adaptive BPF only). */
static void activity_update(int16_t x, int16_t y, int16_t z)
{
    float mag = sqrtf((float)x * x + (float)y * y + (float)z * z);
    const float alpha = 0.05f;
    if (!s_activity_init) {
        s_activity_mean = mag; s_activity_var = 0.0f; s_activity_init = true;
        return;
    }
    float d = mag - s_activity_mean;
    s_activity_mean += alpha * d;
    float dd = mag - s_activity_mean;
    s_activity_var  += alpha * (dd * dd - s_activity_var);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void resp_init(void)
{
    memset(s_buf, 0, sizeof(s_buf));
    s_idx = 0; s_ready = false; s_bpm = s_bpm_raw = 0;
    s_mean_q15 = 0; s_mean_init = false;
    s_prev_v = 0; s_prev_init = false;
    s_last_peak_ms = 0; s_last_interval_ms = 0;
    s_last_amp_mg = 0; s_event_total = 0;
    s_cycle_max = INT16_MIN; s_cycle_min = INT16_MAX; s_cycle_init = false;
    s_evt_head = s_evt_count = 0;
    s_wave_idx = s_wave_count = 0;

    /* v3 */
    memset(s_med_buf, 0, sizeof(s_med_buf));
    s_med_idx = s_med_count = 0;
    s_pca_init = false;
    s_pca_w[0] = s_pca_w[1] = s_pca_w[2] = 1.0f/3;
    s_pca_mean[0] = s_pca_mean[1] = s_pca_mean[2] = 0.0f;
    s_pca_var[0]  = s_pca_var[1]  = s_pca_var[2]  = 0.0f;
    s_schmitt = SCHMITT_ARMED;
    s_activity_init = false;
    s_activity_var  = 0.0f;
    s_activity_mean = 0.0f;
    memset(s_ac_ring, 0, sizeof(s_ac_ring));
    s_ac_idx = s_ac_count = 0;
    s_ac_last_run_ms = 0;
    s_ac_bpm = 0;
    s_ac_conf = 0;

    prepare_filters(s_bp_low_x10, s_bp_high_x10);
}

void resp_reset(void)
{
    s_idx = 0; s_ready = false; s_bpm = s_bpm_raw = 0;
    s_mean_init = false; s_prev_init = false;
    s_last_peak_ms = 0; s_last_interval_ms = 0;
    s_last_amp_mg = 0;
    s_cycle_max = INT16_MIN; s_cycle_min = INT16_MAX; s_cycle_init = false;
    s_evt_head = s_evt_count = 0;
    s_wave_idx = s_wave_count = 0;
    s_med_idx = s_med_count = 0;
    s_pca_init = false;
    s_schmitt = SCHMITT_ARMED;
    s_ac_idx = s_ac_count = 0;
    s_ac_bpm = 0; s_ac_conf = 0;
    for (uint8_t a = 0; a < 3; a++) { biquad_reset(&s_hpf[a]); biquad_reset(&s_lpf[a]); }
}

void resp_set_filter(uint8_t bpm_min, uint8_t bpm_max)
{
    if (bpm_min < 1)        bpm_min = 1;
    if (bpm_max > 60)       bpm_max = 60;
    if (bpm_min >= bpm_max) bpm_max = bpm_min + 1;
    s_bpm_min = bpm_min; s_bpm_max = bpm_max;
}
void resp_set_window_sec(uint8_t sec)
{
    if (sec < 5)  sec = 5;
    if (sec > 15) sec = 15;
    s_win_sec = sec;
    s_win_len = (uint16_t)sec * RESP_SAMPLE_HZ;
    resp_reset();
}

void resp_set_min_interval_ms(uint16_t ms)
{
    if (ms < 300)   ms = 300;
    if (ms > 10000) ms = 10000;
    s_min_interval_ms = ms;
}
uint16_t resp_get_min_interval_ms(void) { return s_min_interval_ms; }

void resp_set_iir_alpha_q15(uint16_t a)
{
    if (a < 50)    a = 50;
    if (a > 16384) a = 16384;
    s_iir_alpha = a;
}
uint16_t resp_get_iir_alpha_q15(void) { return s_iir_alpha; }

void resp_set_min_amplitude_mg(int16_t mg)
{
    if (mg < 0)   mg = 0;
    if (mg > 500) mg = 500;
    s_min_amp_mg = mg;
}
int16_t resp_get_min_amplitude_mg(void) { return s_min_amp_mg; }

void resp_set_axis(resp_axis_t a)
{
    if ((int)a < 0 || (int)a > 3) a = (resp_axis_t)RESP_AXIS_DEFAULT;
    if (a != s_axis) {
        s_axis = a;
        s_mean_init = false; s_prev_init = false;
        s_cycle_max = INT16_MIN; s_cycle_min = INT16_MAX; s_cycle_init = false;
        s_schmitt = SCHMITT_ARMED;
    }
}
resp_axis_t resp_get_axis(void) { return s_axis; }

void resp_set_median_len(uint8_t n)
{
    /* Snap to nearest odd ≤ 9. 0/1 → off. */
    if (n == 0) n = 1;
    if (n > RESP_MEDIAN_RING_LEN) n = RESP_MEDIAN_RING_LEN;
    if ((n & 1) == 0) n -= 1;       /* force odd */
    if (n < 1) n = 1;
    s_median_len = n;
}
uint8_t resp_get_median_len(void) { return s_median_len; }

void resp_set_bp_cutoffs(uint8_t low_x10, uint8_t high_x10)
{
    if (low_x10  > 5)  low_x10  = 5;
    if (high_x10 > 12) high_x10 = 12;
    /* Allow 0 (disable that side) but keep low < high when both active. */
    if (low_x10 > 0 && high_x10 > 0 && low_x10 >= high_x10) {
        if (high_x10 < 12) high_x10 = low_x10 + 1;
        else               low_x10  = high_x10 - 1;
    }
    s_bp_low_x10  = low_x10;
    s_bp_high_x10 = high_x10;
    if (!s_adaptive_bp) prepare_filters(s_bp_low_x10, s_bp_high_x10);
}
uint8_t resp_get_bp_low_hz_x10(void)  { return s_bp_low_x10; }
uint8_t resp_get_bp_high_hz_x10(void) { return s_bp_high_x10; }

void resp_set_adaptive_bp(bool en)
{
    if (en == s_adaptive_bp) return;
    s_adaptive_bp = en;
    if (!en) prepare_filters(s_bp_low_x10, s_bp_high_x10);
    else     update_adaptive_bp();
}
bool resp_get_adaptive_bp(void) { return s_adaptive_bp; }

void resp_set_hysteresis_mg(uint8_t mg)
{
    if (mg > 30) mg = 30;
    s_hyst_mg = mg;
}
uint8_t resp_get_hysteresis_mg(void) { return s_hyst_mg; }

void resp_set_autocorr_window_sec(uint8_t s)
{
    if (s < 10) s = 10;
    if (s > 60) s = 60;
    s_ac_win_sec = s;
}
uint8_t resp_get_autocorr_window_sec(void) { return s_ac_win_sec; }

void resp_get_axis_weights_q8(uint8_t out[3])
{
    if (!out) return;
    int wx = (int)lroundf(s_pca_w[0] * 256.0f);
    int wy = (int)lroundf(s_pca_w[1] * 256.0f);
    int wz = (int)lroundf(s_pca_w[2] * 256.0f);
    /* clamp + nudge sum to 256 */
    if (wx < 0) wx = 0; if (wx > 255) wx = 255;
    if (wy < 0) wy = 0; if (wy > 255) wy = 255;
    if (wz < 0) wz = 0; if (wz > 255) wz = 255;
    int sum = wx + wy + wz;
    if (sum > 0 && sum != 256) {
        /* push residual onto the largest bin */
        int diff = 256 - sum;
        if (wx >= wy && wx >= wz)      wx += diff;
        else if (wy >= wz)             wy += diff;
        else                           wz += diff;
        if (wx < 0) wx = 0; if (wx > 255) wx = 255;
        if (wy < 0) wy = 0; if (wy > 255) wy = 255;
        if (wz < 0) wz = 0; if (wz > 255) wz = 255;
    }
    out[0] = (uint8_t)wx;
    out[1] = (uint8_t)wy;
    out[2] = (uint8_t)wz;
}

uint8_t resp_get_active_bp_low_hz_x10(void)  { return s_bp_low_active; }
uint8_t resp_get_active_bp_high_hz_x10(void) { return s_bp_high_active; }

int16_t resp_current_activity_mg2_x10(void)
{
    /* Encode in 0.1 mg² units; clamp to int16 range. */
    float v = s_activity_var * 10.0f;
    if (v > 32760.0f) v = 32760.0f;
    if (v < 0.0f)     v = 0.0f;
    return (int16_t)v;
}

/* ── Per-sample pipeline ─────────────────────────────────────────────────── */

void resp_add_sample(int16_t x_mg, int16_t y_mg, int16_t z_mg)
{
    /* 1. Activity proxy (used by adaptive BPF when enabled). */
    activity_update(x_mg, y_mg, z_mg);
    update_adaptive_bp();

    /* 2. Median filter — push raw into per-axis ring, advance index. */
    /*    median_push() reads the current head before we advance the ring.
     *    To keep all three axes aligned, advance s_med_idx after all three. */
    int16_t mx = median_push(0, x_mg);
    int16_t my = median_push(1, y_mg);
    int16_t mz = median_push(2, z_mg);
    s_med_idx = (s_med_idx + 1) % RESP_MEDIAN_RING_LEN;
    if (s_med_count < RESP_MEDIAN_RING_LEN) s_med_count++;

    /* 3. Per-axis Butterworth band-pass. Float math. */
    float fx = (float)mx;
    float fy = (float)my;
    float fz = (float)mz;
    if (s_bp_active) {
        if (s_bp_hpf_on) {
            fx = biquad_step(&s_hpf[0], fx);
            fy = biquad_step(&s_hpf[1], fy);
            fz = biquad_step(&s_hpf[2], fz);
        }
        if (s_bp_lpf_on) {
            fx = biquad_step(&s_lpf[0], fx);
            fy = biquad_step(&s_lpf[1], fy);
            fz = biquad_step(&s_lpf[2], fz);
        }
    }

    /* 4. PCA-lite — track per-axis variance, weighted-sum to a single signal. */
    pca_update(fx, fy, fz);

    int16_t v;     /* axis-selected detector input */
    if (s_axis == RESP_AXIS_PCA) {
        float comp = s_pca_w[0] * fx + s_pca_w[1] * fy + s_pca_w[2] * fz;
        if (comp >  32000.0f) comp =  32000.0f;
        if (comp < -32000.0f) comp = -32000.0f;
        v = (int16_t)comp;
    } else {
        float chosen = (s_axis == RESP_AXIS_X) ? fx : (s_axis == RESP_AXIS_Y) ? fy : fz;
        if (chosen >  32000.0f) chosen =  32000.0f;
        if (chosen < -32000.0f) chosen = -32000.0f;
        v = (int16_t)chosen;
    }

    /* Stash into autocorrelation ring (post-fusion composite). */
    s_ac_ring[s_ac_idx] = v;
    s_ac_idx = (s_ac_idx + 1) % RESP_AC_RING_LEN;
    if (s_ac_count < RESP_AC_RING_LEN) s_ac_count++;

    /* ── Windowed BPM (v1, parallel) ─────────────────────────────────────── */
    if (s_idx >= BUF_MAX) s_idx = 0;
    s_buf[s_idx++] = v;
    if (s_idx >= s_win_len) {
        s_idx = 0;
        uint8_t peaks = count_peaks_window(s_win_len);
        uint8_t bpm   = (uint8_t)((uint16_t)peaks * 60u / s_win_sec);
        s_bpm_raw = bpm;
        if (bpm >= s_bpm_min && bpm <= s_bpm_max) {
            s_bpm = bpm; s_ready = true;
        } else {
            s_bpm = 0; s_ready = false;
        }
    }

    /* ── v2 online detector on composite signal ──────────────────────────── */
    if (!s_mean_init) {
        s_mean_q15 = (int32_t)v * 32768;
        s_mean_init = true;
    } else {
        int32_t cur = s_mean_q15 / 32768;
        int32_t err = (int32_t)v - cur;
        s_mean_q15 += err * (int32_t)s_iir_alpha;
    }
    int16_t mean_now = (int16_t)(s_mean_q15 / 32768);

    wave_push(v, mean_now);

    if (v > s_cycle_max || !s_cycle_init) s_cycle_max = v;
    if (v < s_cycle_min || !s_cycle_init) s_cycle_min = v;
    s_cycle_init = true;

    if (s_prev_init) {
        bool prev_below = (s_prev_v < mean_now);
        bool curr_above = (v >= mean_now);

        /* Schmitt gate around mean-line. With hyst=0 collapses to plain
         * rising mean-cross (legacy v2 behavior). */
        int16_t hi_th = mean_now + (int16_t)s_hyst_mg;
        int16_t lo_th = mean_now - (int16_t)s_hyst_mg;
        if (s_schmitt == SCHMITT_ABOVE && v <= lo_th) {
            s_schmitt = SCHMITT_ARMED;
        }

        bool schmitt_fire = prev_below && curr_above && (v >= hi_th) &&
                            (s_schmitt == SCHMITT_ARMED);

        if (schmitt_fire) {
            uint32_t now = millis();
            int16_t  amp = (int16_t)((int32_t)s_cycle_max - s_cycle_min);
            bool gate_amp = (amp >= s_min_amp_mg);
            bool gate_int = (s_last_peak_ms == 0 ||
                             (now - s_last_peak_ms) >= (uint32_t)s_min_interval_ms);

            if (gate_amp && gate_int) {
                uint16_t interval = 0;
                if (s_last_peak_ms != 0) {
                    interval = (uint16_t)(now - s_last_peak_ms);
                    s_last_interval_ms = interval;
                }
                s_last_amp_mg = amp;
                event_push(now, amp, interval);
                s_last_peak_ms = now;
            }
            s_cycle_max = v;
            s_cycle_min = v;
            s_schmitt   = SCHMITT_ABOVE;
        }
    }
    s_prev_v = v;
    s_prev_init = true;
}

bool     resp_result_ready(void)    { return s_ready;   }
uint8_t  resp_get_bpm(void)         { return s_bpm;     }
uint8_t  resp_get_bpm_raw(void)     { return s_bpm_raw; }
uint16_t resp_window_progress(void) { return s_idx;     }
uint16_t resp_window_len(void)      { return s_win_len; }

int16_t  resp_current_mean_mg(void)      { return (int16_t)(s_mean_q15 / 32768); }
int16_t  resp_current_amplitude_mg(void) { return s_last_amp_mg; }

resp_phase_t resp_current_phase(void)
{
    if (!s_mean_init || !s_prev_init) return RESP_PHASE_FLAT;
    int16_t mean_now = (int16_t)(s_mean_q15 / 32768);
    int16_t margin = s_min_amp_mg / 4;
    if (s_prev_v > mean_now + margin) return RESP_PHASE_INHALE;
    if (s_prev_v < mean_now - margin) return RESP_PHASE_EXHALE;
    return RESP_PHASE_FLAT;
}

uint16_t resp_instant_bpm(void)
{
    if (s_last_interval_ms == 0) return 0;
    uint32_t bpm = 60000UL / s_last_interval_ms;
    if (bpm > 255) bpm = 255;
    return (uint16_t)bpm;
}
uint32_t resp_total_events(void)     { return s_event_total; }
uint16_t resp_last_interval_ms(void) { return s_last_interval_ms; }
uint32_t resp_last_peak_ms(void)     { return s_last_peak_ms; }

size_t resp_get_events_since(uint32_t since_ms, resp_event_t *out, size_t max)
{
    if (max == 0 || s_evt_count == 0) return 0;
    uint16_t start = (s_evt_head + RESP_EVENT_RING_SIZE - s_evt_count) % RESP_EVENT_RING_SIZE;
    size_t o = 0;
    for (uint16_t i = 0; i < s_evt_count && o < max; i++) {
        uint16_t pos = (start + i) % RESP_EVENT_RING_SIZE;
        if (s_events[pos].t_ms > since_ms) out[o++] = s_events[pos];
    }
    return o;
}

size_t resp_get_wave(int16_t *out_axis, int16_t *out_mean, size_t max)
{
    if (s_wave_count == 0 || max == 0) return 0;
    size_t n = (s_wave_count < max) ? s_wave_count : max;
    uint16_t start = (s_wave_idx + RESP_WAVE_RING_SIZE - s_wave_count) % RESP_WAVE_RING_SIZE;
    if (s_wave_count > max) {
        start = (start + (s_wave_count - max)) % RESP_WAVE_RING_SIZE;
    }
    for (size_t i = 0; i < n; i++) {
        uint16_t pos = (start + i) % RESP_WAVE_RING_SIZE;
        if (out_axis) out_axis[i] = s_wave_v[pos];
        if (out_mean) out_mean[i] = s_wave_m[pos];
    }
    return n;
}

/* ── Autocorrelation track ───────────────────────────────────────────────── */

void resp_run_autocorr_tick(uint32_t now_ms)
{
    if (now_ms - s_ac_last_run_ms < RESP_AUTOCORR_PERIOD_MS) return;

    uint16_t need = (uint16_t)s_ac_win_sec * RESP_SAMPLE_HZ;
    if (need > RESP_AC_RING_LEN) need = RESP_AC_RING_LEN;
    if (s_ac_count < need) {
        /* not enough history yet; mark the tick so we don't spin */
        s_ac_last_run_ms = now_ms;
        return;
    }
    s_ac_last_run_ms = now_ms;

    /* Pull the last `need` samples into a contiguous float buffer. */
    static float work[RESP_AC_RING_LEN];
    uint16_t start = (s_ac_idx + RESP_AC_RING_LEN - need) % RESP_AC_RING_LEN;
    /* Compute mean for centering. */
    double sum = 0.0;
    for (uint16_t i = 0; i < need; i++) {
        uint16_t pos = (start + i) % RESP_AC_RING_LEN;
        work[i] = (float)s_ac_ring[pos];
        sum += work[i];
    }
    float mean = (float)(sum / (double)need);
    for (uint16_t i = 0; i < need; i++) work[i] -= mean;

    /* Lag bounds — 0.1 to 0.8 Hz → lag ∈ [fs/0.8 = 31, fs/0.1 = 250]. */
    uint16_t lag_min = 31;
    uint16_t lag_max = 250;
    if (lag_max > need / 2) lag_max = need / 2;
    if (lag_min >= lag_max) {
        /* window too short for anything useful */
        s_ac_bpm = 0; s_ac_conf = 0;
        return;
    }

    /* rxx[0] for normalization. */
    double r0 = 0.0;
    for (uint16_t i = 0; i < need; i++) r0 += (double)work[i] * work[i];
    if (r0 < 1e-3) { s_ac_bpm = 0; s_ac_conf = 0; return; }

    /* Search lag range, track arg-max and stats. */
    double sum_r = 0.0;
    double sum_r2 = 0.0;
    uint16_t n_lags = (uint16_t)(lag_max - lag_min + 1);
    double r_max = -1e30;
    uint16_t k_max = lag_min;
    static float lag_vals[260];   /* lag_max ≤ 250 */
    for (uint16_t k = lag_min; k <= lag_max; k++) {
        double r = 0.0;
        uint16_t lim = (uint16_t)(need - k);
        for (uint16_t i = 0; i < lim; i++) {
            r += (double)work[i] * (double)work[i + k];
        }
        /* normalize by overlap length so lag bias doesn't pull arg-max. */
        double rn = r / (double)lim;
        lag_vals[k - lag_min] = (float)rn;
        sum_r  += rn;
        sum_r2 += rn * rn;
        if (rn > r_max) { r_max = rn; k_max = k; }
    }

    /* BPM from lag. */
    if (k_max == 0) { s_ac_bpm = 0; s_ac_conf = 0; return; }
    uint32_t bpm = 60UL * RESP_SAMPLE_HZ / k_max;     /* = 1500 / k_max */
    if (bpm < 1)   bpm = 0;
    if (bpm > 60)  bpm = 60;
    s_ac_bpm = (uint8_t)bpm;

    /* Confidence: z-score-like (peak − mean) / std × 30, clamped 0..100. */
    double mean_r = sum_r / (double)n_lags;
    double var_r  = sum_r2 / (double)n_lags - mean_r * mean_r;
    if (var_r < 1e-9) var_r = 1e-9;
    double std_r  = sqrt(var_r);
    double z = (r_max - mean_r) / std_r;
    double conf = z * 30.0;
    if (conf < 0)   conf = 0;
    if (conf > 100) conf = 100;
    s_ac_conf = (uint8_t)conf;
}

uint8_t  resp_autocorr_bpm(void)            { return s_ac_bpm;  }
uint8_t  resp_autocorr_confidence_pct(void) { return s_ac_conf; }
uint32_t resp_autocorr_last_run_ms(void)    { return s_ac_last_run_ms; }
