#include "cough.h"
#include "app_config.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>

/* Pipeline @ 100 Hz:
 *   mag → AC (subtract slow IIR mean) → BP 5–15 Hz biquad cascade
 *       → |·| → envelope LPF → peak detect → cluster gate
 */

/* ── Biquad (Direct Form II Transposed; matches respiration.cpp) ─────────── */
typedef struct {
    float b0, b1, b2, a1, a2;
    float z1, z2;
} biquad_t;

static biquad_t s_hpf;
static biquad_t s_lpf;

static void biquad_lpf(biquad_t *bq, float fc, float fs)
{
    float w0 = 2.0f * (float)M_PI * fc / fs;
    float cosw = cosf(w0);
    float sinw = sinf(w0);
    float Q = 0.7071067811865f;
    float alpha = sinw / (2.0f * Q);
    float a0 = 1.0f + alpha;
    float b0 = (1.0f - cosw) / 2.0f;
    float b1 =  1.0f - cosw;
    float b2 = (1.0f - cosw) / 2.0f;
    float a1 = -2.0f * cosw;
    float a2 = 1.0f - alpha;
    bq->b0 = b0 / a0;
    bq->b1 = b1 / a0;
    bq->b2 = b2 / a0;
    bq->a1 = a1 / a0;
    bq->a2 = a2 / a0;
    bq->z1 = bq->z2 = 0.0f;
}
static void biquad_hpf(biquad_t *bq, float fc, float fs)
{
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
    bq->z1 = bq->z2 = 0.0f;
}
static inline float biquad_step(biquad_t *bq, float x)
{
    float y = bq->b0 * x + bq->z1;
    bq->z1 = bq->b1 * x - bq->a1 * y + bq->z2;
    bq->z2 = bq->b2 * x - bq->a2 * y;
    return y;
}

/* ── State ───────────────────────────────────────────────────────────────── */
static float    s_mean_mg     = 0.0f;
static bool     s_mean_init   = false;
static float    s_env_mg      = 0.0f;

static uint16_t s_thresh_mg   = COUGH_THRESH_MG_DEFAULT;
static uint16_t s_cluster_ms  = COUGH_CLUSTER_MS_DEFAULT;
static uint8_t  s_min_peaks   = COUGH_MIN_PEAKS_DEFAULT;

static bool     s_env_above   = false;
static uint32_t s_last_peak_ms     = 0;
static uint32_t s_cluster_start_ms = 0;
static uint8_t  s_cluster_peaks    = 0;
static uint16_t s_cluster_max_amp  = 0;
static uint32_t s_lockout_until    = 0;

static uint32_t s_total_count   = 0;
static uint32_t s_last_event_ms = 0;

/* Event ring */
static cough_event_t s_events[COUGH_EVENT_RING_SIZE];
static uint16_t      s_evt_head  = 0;
static uint16_t      s_evt_count = 0;

/* Recent fire times for per-minute rate (separate ring of timestamps). */
static uint32_t s_fire_times[COUGH_EVENT_RING_SIZE];
static uint16_t s_fire_head  = 0;
static uint16_t s_fire_count = 0;

/* Ground truth */
static uint32_t s_gt[COUGH_GT_RING_SIZE];
static uint16_t s_gt_head  = 0;
static uint16_t s_gt_count = 0;

/* Envelope wave ring (decimated, for /cough/wave). */
static int16_t  s_wave[COUGH_WAVE_RING_SIZE];
static uint16_t s_wave_head    = 0;
static uint16_t s_wave_count   = 0;
static uint8_t  s_wave_decim_n = 0;

void cough_init(void)
{
    biquad_hpf(&s_hpf, (float)COUGH_BP_LOW_HZ,  (float)ACCEL_FAST_ODR_HZ);
    biquad_lpf(&s_lpf, (float)COUGH_BP_HIGH_HZ, (float)ACCEL_FAST_ODR_HZ);
    s_mean_mg = 0.0f; s_mean_init = false;
    s_env_mg = 0.0f;
    s_env_above = false;
    s_last_peak_ms = 0;
    s_cluster_start_ms = 0;
    s_cluster_peaks = 0;
    s_cluster_max_amp = 0;
    s_lockout_until = 0;
    s_total_count = 0;
    s_last_event_ms = 0;
    s_evt_head = s_evt_count = 0;
    s_fire_head = s_fire_count = 0;
    s_gt_head = s_gt_count = 0;
    s_wave_head = s_wave_count = 0;
    s_wave_decim_n = 0;
}

uint16_t cough_get_thresh_mg(void)             { return s_thresh_mg;  }
uint16_t cough_get_cluster_window_ms(void)     { return s_cluster_ms; }
uint8_t  cough_get_min_peaks(void)             { return s_min_peaks;  }
uint32_t cough_total(void)                     { return s_total_count; }
uint32_t cough_last_event_ms(void)             { return s_last_event_ms; }
int16_t  cough_current_envelope_mg(void)       { return (int16_t)s_env_mg; }
uint8_t  cough_current_cluster_count(void)     { return s_cluster_peaks; }
uint16_t cough_wave_period_ms(void)
{
    return (uint16_t)((1000U / ACCEL_FAST_ODR_HZ) * COUGH_WAVE_DECIM);
}

void cough_set_thresh_mg(uint16_t mg)
{
    if (mg < 10)   mg = 10;
    if (mg > 1000) mg = 1000;
    s_thresh_mg = mg;
}
void cough_set_cluster_window_ms(uint16_t ms)
{
    if (ms < 200)  ms = 200;
    if (ms > 3000) ms = 3000;
    s_cluster_ms = ms;
}
void cough_set_min_peaks(uint8_t n)
{
    if (n < 1) n = 1;
    if (n > 8) n = 8;
    s_min_peaks = n;
}

uint16_t cough_rate_per_min(void)
{
    /* count of fires in last COUGH_RATE_WINDOW_MS. */
    uint32_t now = millis();
    uint32_t cutoff = (now > COUGH_RATE_WINDOW_MS) ? (now - COUGH_RATE_WINDOW_MS) : 0;
    uint16_t n = 0;
    for (uint16_t i = 0; i < s_fire_count; i++) {
        if (s_fire_times[i] >= cutoff) n++;
    }
    return n;
}

size_t cough_get_wave(int16_t *out, size_t max)
{
    if (max == 0 || s_wave_count == 0) return 0;
    size_t n = (s_wave_count < max) ? s_wave_count : max;
    /* iterate oldest → newest. */
    uint16_t start = (s_wave_head + COUGH_WAVE_RING_SIZE - s_wave_count) % COUGH_WAVE_RING_SIZE;
    /* skip oldest if more than max */
    if (s_wave_count > max) start = (start + (s_wave_count - max)) % COUGH_WAVE_RING_SIZE;
    for (size_t i = 0; i < n; i++) {
        out[i] = s_wave[(start + i) % COUGH_WAVE_RING_SIZE];
    }
    return n;
}

size_t cough_get_events_since(uint32_t since_ms, cough_event_t *out, size_t max)
{
    if (max == 0 || s_evt_count == 0) return 0;
    uint16_t start = (s_evt_head + COUGH_EVENT_RING_SIZE - s_evt_count) % COUGH_EVENT_RING_SIZE;
    size_t o = 0;
    for (uint16_t i = 0; i < s_evt_count && o < max; i++) {
        uint16_t pos = (start + i) % COUGH_EVENT_RING_SIZE;
        if (s_events[pos].t_ms > since_ms) out[o++] = s_events[pos];
    }
    return o;
}

void cough_add_gt(uint32_t t_ms)
{
    s_gt[s_gt_head] = t_ms;
    s_gt_head = (s_gt_head + 1) % COUGH_GT_RING_SIZE;
    if (s_gt_count < COUGH_GT_RING_SIZE) s_gt_count++;
}
void cough_clear_gt(void) { s_gt_head = s_gt_count = 0; }
size_t cough_get_gt_count(void) { return s_gt_count; }

size_t cough_get_gt_since(uint32_t since_ms, uint32_t *out, size_t max)
{
    if (max == 0 || s_gt_count == 0) return 0;
    uint16_t start = (s_gt_head + COUGH_GT_RING_SIZE - s_gt_count) % COUGH_GT_RING_SIZE;
    size_t o = 0;
    for (uint16_t i = 0; i < s_gt_count && o < max; i++) {
        uint16_t pos = (start + i) % COUGH_GT_RING_SIZE;
        if (s_gt[pos] > since_ms) out[o++] = s_gt[pos];
    }
    return o;
}

/* Greedy nearest-match within ±window_ms; matches steps.cpp / steps_eval. */
void cough_eval(uint16_t window_ms, uint16_t *tp, uint16_t *fp, uint16_t *fn)
{
    /* Pull all detections since boot. */
    uint32_t det[COUGH_EVENT_RING_SIZE];
    uint16_t det_n = 0;
    if (s_evt_count > 0) {
        uint16_t start = (s_evt_head + COUGH_EVENT_RING_SIZE - s_evt_count) % COUGH_EVENT_RING_SIZE;
        for (uint16_t i = 0; i < s_evt_count; i++) {
            uint16_t pos = (start + i) % COUGH_EVENT_RING_SIZE;
            det[det_n++] = s_events[pos].t_ms;
        }
    }
    uint32_t gtt[COUGH_GT_RING_SIZE];
    uint16_t gt_n = 0;
    if (s_gt_count > 0) {
        uint16_t start = (s_gt_head + COUGH_GT_RING_SIZE - s_gt_count) % COUGH_GT_RING_SIZE;
        for (uint16_t i = 0; i < s_gt_count; i++) {
            uint16_t pos = (start + i) % COUGH_GT_RING_SIZE;
            gtt[gt_n++] = s_gt[pos];
        }
    }
    bool det_used[COUGH_EVENT_RING_SIZE];
    memset(det_used, 0, sizeof(det_used));
    uint16_t matched = 0;
    for (uint16_t i = 0; i < gt_n; i++) {
        int32_t  best   = -1;
        uint32_t best_d = (uint32_t)window_ms + 1;
        for (uint16_t j = 0; j < det_n; j++) {
            if (det_used[j]) continue;
            uint32_t d = (det[j] > gtt[i]) ? det[j] - gtt[i] : gtt[i] - det[j];
            if (d <= (uint32_t)window_ms && d < best_d) { best_d = d; best = j; }
        }
        if (best >= 0) { det_used[best] = true; matched++; }
    }
    if (tp) *tp = matched;
    if (fp) *fp = (det_n > matched) ? (det_n - matched) : 0;
    if (fn) *fn = (gt_n  > matched) ? (gt_n  - matched) : 0;
}

static void event_push(uint32_t t_ms, uint8_t peaks, uint16_t dur, uint16_t amp)
{
    s_events[s_evt_head].t_ms              = t_ms;
    s_events[s_evt_head].peak_count        = peaks;
    s_events[s_evt_head].duration_ms       = dur;
    s_events[s_evt_head].peak_amplitude_mg = amp;
    s_evt_head = (s_evt_head + 1) % COUGH_EVENT_RING_SIZE;
    if (s_evt_count < COUGH_EVENT_RING_SIZE) s_evt_count++;

    s_fire_times[s_fire_head] = t_ms;
    s_fire_head = (s_fire_head + 1) % COUGH_EVENT_RING_SIZE;
    if (s_fire_count < COUGH_EVENT_RING_SIZE) s_fire_count++;
}

void cough_feed_sample(int16_t x_mg, int16_t y_mg, int16_t z_mg)
{
    /* 1) magnitude (mg). */
    float mag = sqrtf((float)x_mg * x_mg + (float)y_mg * y_mg + (float)z_mg * z_mg);

    /* 2) AC: slow IIR mean → high-pass-ish in software. */
    if (!s_mean_init) { s_mean_mg = mag; s_mean_init = true; }
    /* α = 0.02 → 1−α = 0.98 */
    s_mean_mg += 0.02f * (mag - s_mean_mg);
    float ac = mag - s_mean_mg;

    /* 3) Band-pass HPF→LPF biquad cascade @ 5–15 Hz. */
    float bp = biquad_step(&s_hpf, ac);
    bp       = biquad_step(&s_lpf, bp);

    /* 4) Rectify + envelope LPF (α≈0.1 → ~100 ms time constant). */
    float rect = bp < 0.0f ? -bp : bp;
    s_env_mg += 0.1f * (rect - s_env_mg);

    uint32_t now = millis();

    /* 5) Decimated wave push for /cough/wave. */
    if (++s_wave_decim_n >= COUGH_WAVE_DECIM) {
        s_wave_decim_n = 0;
        s_wave[s_wave_head] = (int16_t)s_env_mg;
        s_wave_head = (s_wave_head + 1) % COUGH_WAVE_RING_SIZE;
        if (s_wave_count < COUGH_WAVE_RING_SIZE) s_wave_count++;
    }

    /* 6) Lockout check. */
    if (now < s_lockout_until) return;

    /* 7) Peak detection: rising edge above threshold + min spacing. */
    bool peak_now = false;
    if (!s_env_above && s_env_mg > (float)s_thresh_mg) {
        if ((now - s_last_peak_ms) >= COUGH_PEAK_MIN_INTERVAL_MS) {
            peak_now = true;
            s_last_peak_ms = now;
        }
        s_env_above = true;
    } else if (s_env_above && s_env_mg < (float)s_thresh_mg * 0.5f) {
        s_env_above = false;
    }

    if (peak_now) {
        uint16_t amp = (uint16_t)s_env_mg;
        if (s_cluster_peaks == 0 || (now - s_cluster_start_ms) > s_cluster_ms) {
            /* New cluster */
            s_cluster_start_ms = now;
            s_cluster_peaks    = 1;
            s_cluster_max_amp  = amp;
        } else {
            s_cluster_peaks++;
            if (amp > s_cluster_max_amp) s_cluster_max_amp = amp;
        }

        /* 8) Fire event when cluster has enough peaks. */
        if (s_cluster_peaks >= s_min_peaks) {
            uint16_t dur_ms = (uint16_t)(now - s_cluster_start_ms);
            event_push(now, s_cluster_peaks, dur_ms, s_cluster_max_amp);
            s_total_count++;
            s_last_event_ms = now;
            /* Lockout + reset cluster. */
            s_lockout_until    = now + COUGH_LOCKOUT_MS;
            s_cluster_peaks    = 0;
            s_cluster_max_amp  = 0;
            s_cluster_start_ms = 0;
        }
    } else if (s_cluster_peaks > 0 && (now - s_cluster_start_ms) > s_cluster_ms) {
        /* Cluster expired without firing. */
        s_cluster_peaks    = 0;
        s_cluster_max_amp  = 0;
        s_cluster_start_ms = 0;
    }
}
