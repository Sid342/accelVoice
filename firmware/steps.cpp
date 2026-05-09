#include "steps.h"
#include "app_config.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>
#include <limits.h>

/* ── State ───────────────────────────────────────────────────────────────── */
static uint32_t s_count           = 0;
static int16_t  s_baseline_mg     = 1000;
static uint32_t s_last_peak_ms    = 0;
static uint32_t s_prev_step_ms    = 0;
static uint32_t s_last_cadence_ms = 0;
static bool     s_above           = false;
static uint16_t s_thresh_mg       = STEP_PEAK_THRESH_MG;
static uint16_t s_min_ms          = STEP_MIN_INTERVAL_MS;
static uint16_t s_max_ms          = STEP_MAX_INTERVAL_MS;

/* v2 toggles + tunables */
static bool         s_adaptive      = false;
static bool         s_bandpass      = false;
static step_axis_t  s_axis          = STEP_AXIS_MAG;
static uint16_t     s_amp_window_ms = STEP_AMP_WINDOW_MS;

/* Bandpass IIR cascade (HPF then LPF), per-sample state. */
static int32_t s_hpf_y      = 0;
static int32_t s_hpf_x_prev = 0;
static int32_t s_lpf_y      = 0;

/* Adaptive amplitude tracker — ring of last N delta samples. */
static int16_t  s_amp_buf[STEP_AMP_BUF_MAX];
static uint16_t s_amp_size   = STEP_AMP_WINDOW_MS / 40;   /* @25 Hz, 40 ms/sample */
static uint16_t s_amp_idx    = 0;
static uint16_t s_amp_filled = 0;

/* Live signals */
static int16_t s_last_signal    = 0;
static int16_t s_last_threshold = STEP_PEAK_THRESH_MG;

/* Event ring */
static step_event_t s_events[STEP_EVENT_RING_SIZE];
static uint16_t s_evt_head  = 0;
static uint16_t s_evt_count = 0;
static uint32_t s_evt_total = 0;

/* Ground-truth ring */
static uint32_t s_gt[STEP_GT_RING_SIZE];
static uint16_t s_gt_head  = 0;
static uint16_t s_gt_count = 0;

static int16_t mag_mg(int16_t x, int16_t y, int16_t z)
{
    int32_t sq = (int32_t)x * x + (int32_t)y * y + (int32_t)z * z;
    return (int16_t)sqrt((double)sq);
}

void steps_init(void)
{
    s_count = 0;
    s_baseline_mg = 1000;
    s_last_peak_ms = 0;
    s_prev_step_ms = 0;
    s_last_cadence_ms = 0;
    s_above = false;
    s_hpf_y = s_hpf_x_prev = s_lpf_y = 0;
    s_amp_size = (s_amp_window_ms / 40);
    if (s_amp_size > STEP_AMP_BUF_MAX) s_amp_size = STEP_AMP_BUF_MAX;
    s_amp_idx = 0;
    s_amp_filled = 0;
    s_last_signal = 0;
    s_last_threshold = (int16_t)s_thresh_mg;
    s_evt_head = s_evt_count = 0;
    s_evt_total = 0;
    s_gt_head = s_gt_count = 0;
}

void steps_reset(void)
{
    s_count = 0;
    s_prev_step_ms = 0;
    s_last_cadence_ms = 0;
    s_evt_head = s_evt_count = 0;
    s_evt_total = 0;
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
    if (min_ms < 50)      min_ms = 50;
    if (max_ms > 5000)    max_ms = 5000;
    if (min_ms >= max_ms) max_ms = min_ms + 100;
    s_min_ms = min_ms;
    s_max_ms = max_ms;
}

void steps_set_adaptive(bool en) { s_adaptive = en; }
bool steps_get_adaptive(void)    { return s_adaptive; }
void steps_set_bandpass(bool en)
{
    if (en && !s_bandpass) {
        s_hpf_y = s_hpf_x_prev = s_lpf_y = 0;
    }
    s_bandpass = en;
}
bool steps_get_bandpass(void) { return s_bandpass; }

void steps_set_axis(step_axis_t a)
{
    if ((int)a < 0 || (int)a > 3) a = STEP_AXIS_MAG;
    s_axis = a;
}
step_axis_t steps_get_axis(void) { return s_axis; }

void steps_set_amp_window_ms(uint16_t ms)
{
    if (ms < 500)  ms = 500;
    if (ms > 5000) ms = 5000;
    s_amp_window_ms = ms;
    s_amp_size = ms / 40;
    if (s_amp_size > STEP_AMP_BUF_MAX) s_amp_size = STEP_AMP_BUF_MAX;
    s_amp_idx = 0;
    s_amp_filled = 0;
}
uint16_t steps_get_amp_window_ms(void) { return s_amp_window_ms; }

uint32_t steps_get_cadence_ms(void)
{
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

int16_t  steps_current_signal(void)    { return s_last_signal; }
int16_t  steps_current_baseline(void)  { return s_baseline_mg; }
int16_t  steps_current_threshold(void) { return s_last_threshold; }

uint32_t steps_total_events(void) { return s_evt_total; }

size_t steps_get_events_since(uint32_t since_ms, step_event_t *out, size_t max)
{
    if (max == 0 || s_evt_count == 0) return 0;
    uint16_t start = (s_evt_head + STEP_EVENT_RING_SIZE - s_evt_count) % STEP_EVENT_RING_SIZE;
    size_t o = 0;
    for (uint16_t i = 0; i < s_evt_count && o < max; i++) {
        uint16_t pos = (start + i) % STEP_EVENT_RING_SIZE;
        if (s_events[pos].t_ms > since_ms) out[o++] = s_events[pos];
    }
    return o;
}

void steps_groundtruth_tap(uint32_t t_ms)
{
    s_gt[s_gt_head] = t_ms;
    s_gt_head = (s_gt_head + 1) % STEP_GT_RING_SIZE;
    if (s_gt_count < STEP_GT_RING_SIZE) s_gt_count++;
}
void steps_groundtruth_clear(void) { s_gt_head = s_gt_count = 0; }
size_t steps_get_groundtruth_count(void) { return s_gt_count; }

void steps_eval(uint16_t window_sec, uint16_t tol_ms,
                uint32_t *out_detected, uint32_t *out_groundtruth,
                uint32_t *out_matched,
                uint16_t *out_prec_pct, uint16_t *out_recall_pct)
{
    uint32_t now    = millis();
    uint32_t cutoff = ((uint32_t)window_sec * 1000UL >= now)
                        ? 0
                        : now - (uint32_t)window_sec * 1000UL;

    uint32_t det_times[STEP_EVENT_RING_SIZE];
    uint16_t det_n = 0;
    if (s_evt_count > 0) {
        uint16_t start = (s_evt_head + STEP_EVENT_RING_SIZE - s_evt_count) % STEP_EVENT_RING_SIZE;
        for (uint16_t i = 0; i < s_evt_count; i++) {
            uint16_t pos = (start + i) % STEP_EVENT_RING_SIZE;
            if (s_events[pos].t_ms >= cutoff) det_times[det_n++] = s_events[pos].t_ms;
        }
    }
    uint32_t gt_times[STEP_GT_RING_SIZE];
    uint16_t gt_n = 0;
    if (s_gt_count > 0) {
        uint16_t start = (s_gt_head + STEP_GT_RING_SIZE - s_gt_count) % STEP_GT_RING_SIZE;
        for (uint16_t i = 0; i < s_gt_count; i++) {
            uint16_t pos = (start + i) % STEP_GT_RING_SIZE;
            if (s_gt[pos] >= cutoff) gt_times[gt_n++] = s_gt[pos];
        }
    }

    /* Greedy match: each GT picks nearest unmatched detected step within tol. */
    bool det_used[STEP_EVENT_RING_SIZE];
    memset(det_used, 0, sizeof(det_used));
    uint32_t matched = 0;
    for (uint16_t i = 0; i < gt_n; i++) {
        int32_t  best = -1;
        uint32_t best_d = (uint32_t)tol_ms + 1;
        for (uint16_t j = 0; j < det_n; j++) {
            if (det_used[j]) continue;
            uint32_t d = (det_times[j] > gt_times[i])
                           ? det_times[j] - gt_times[i]
                           : gt_times[i] - det_times[j];
            if (d <= (uint32_t)tol_ms && d < best_d) {
                best_d = d;
                best   = j;
            }
        }
        if (best >= 0) { det_used[best] = true; matched++; }
    }

    if (out_detected)    *out_detected    = det_n;
    if (out_groundtruth) *out_groundtruth = gt_n;
    if (out_matched)     *out_matched     = matched;
    if (out_prec_pct)    *out_prec_pct    = det_n ? (uint16_t)((matched * 100UL) / det_n) : 0;
    if (out_recall_pct)  *out_recall_pct  = gt_n  ? (uint16_t)((matched * 100UL) / gt_n)  : 0;
}

static void event_push(uint32_t t_ms, int16_t amp, uint16_t interval)
{
    s_events[s_evt_head].t_ms         = t_ms;
    s_events[s_evt_head].amplitude_mg = amp;
    s_events[s_evt_head].interval_ms  = interval;
    s_evt_head = (s_evt_head + 1) % STEP_EVENT_RING_SIZE;
    if (s_evt_count < STEP_EVENT_RING_SIZE) s_evt_count++;
    s_evt_total++;
}

static int16_t pick_axis(int16_t x, int16_t y, int16_t z, int16_t mag)
{
    switch (s_axis) {
        case STEP_AXIS_X: return x;
        case STEP_AXIS_Y: return y;
        case STEP_AXIS_Z: return z;
        default:          return mag;
    }
}

/* Cascade: HPF (cutoff 0.5 Hz) → LPF (cutoff 3 Hz), Q15. */
static int32_t apply_bandpass(int32_t x)
{
    int32_t hpf_y = (int32_t)(((int64_t)STEP_HPF_ALPHA_Q15 *
                               (s_hpf_y + (x - s_hpf_x_prev))) >> 15);
    s_hpf_y      = hpf_y;
    s_hpf_x_prev = x;
    int32_t lpf_y = (int32_t)((((int64_t)STEP_LPF_ALPHA_Q15 * s_lpf_y) +
                               ((int64_t)(32768 - STEP_LPF_ALPHA_Q15) * hpf_y)) >> 15);
    s_lpf_y = lpf_y;
    return lpf_y;
}

void steps_add_sample(int16_t x_mg, int16_t y_mg, int16_t z_mg)
{
    int16_t mag = mag_mg(x_mg, y_mg, z_mg);
    int16_t raw = pick_axis(x_mg, y_mg, z_mg, mag);

    /* Detect signal: bandpass replaces baseline-EMA path; otherwise
     * use the historical (signal − slow_baseline) form. */
    int32_t delta;
    if (s_bandpass) {
        delta = apply_bandpass((int32_t)raw);
    } else {
        s_baseline_mg = s_baseline_mg + (raw - s_baseline_mg) / 16;
        delta         = (int32_t)raw - s_baseline_mg;
    }
    s_last_signal = (int16_t)delta;

    /* Adaptive threshold: track max−min over the amp window. */
    if (s_amp_size > 0 && s_amp_size <= STEP_AMP_BUF_MAX) {
        s_amp_buf[s_amp_idx] = (int16_t)delta;
        s_amp_idx = (s_amp_idx + 1) % s_amp_size;
        if (s_amp_filled < s_amp_size) s_amp_filled++;
    }

    int16_t eff_thresh = (int16_t)s_thresh_mg;
    if (s_adaptive && s_amp_filled > 4) {
        int16_t mn = INT16_MAX, mx = INT16_MIN;
        for (uint16_t i = 0; i < s_amp_filled; i++) {
            int16_t v = s_amp_buf[i];
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
        int32_t amp = (int32_t)mx - mn;
        int32_t adp = (amp * STEP_ADAPTIVE_FRAC_PCT) / 100;
        if (adp < STEP_ADAPTIVE_MIN_MG) adp = STEP_ADAPTIVE_MIN_MG;
        if (adp > 2000)                  adp = 2000;
        eff_thresh = (int16_t)adp;
    }
    s_last_threshold = eff_thresh;

    uint32_t now = millis();
    if (!s_above && delta > eff_thresh) {
        uint32_t gap = now - s_last_peak_ms;
        if (gap >= s_min_ms && gap <= s_max_ms) {
            s_count++;
            uint16_t interval = 0;
            if (s_prev_step_ms != 0) {
                s_last_cadence_ms = now - s_prev_step_ms;
                interval          = (uint16_t)s_last_cadence_ms;
            }
            s_prev_step_ms = now;
            event_push(now, (int16_t)delta, interval);
        }
        s_last_peak_ms = now;
        s_above        = true;
    } else if (s_above && delta < (eff_thresh / 2)) {
        s_above = false;
    }
}
