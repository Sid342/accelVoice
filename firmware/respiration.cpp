#include "respiration.h"
#include "app_config.h"
#include <Arduino.h>
#include <stdint.h>
#include <string.h>

#define BUF_MAX (15 * RESP_SAMPLE_HZ)   /* 15-second max window */

/* ── Existing windowed BPM ────────────────────────────────────────────────── */
static int16_t  s_buf[BUF_MAX];
static uint16_t s_idx       = 0;
static bool     s_ready     = false;
static uint8_t  s_bpm       = 0;
static uint8_t  s_bpm_raw   = 0;
static uint8_t  s_bpm_min   = RESP_BPM_MIN;
static uint8_t  s_bpm_max   = RESP_BPM_MAX;
static uint8_t  s_win_sec   = RESP_WINDOW_SEC;
static uint16_t s_win_len   = RESP_WINDOW_SEC * RESP_SAMPLE_HZ;

/* ── v2 online detector ──────────────────────────────────────────────────── */
static int32_t  s_mean_q15        = 0;       /* mean (mg) × 32768 */
static bool     s_mean_init       = false;
static int16_t  s_prev_z          = 0;
static bool     s_prev_init       = false;
static uint32_t s_last_peak_ms    = 0;
static uint16_t s_min_interval_ms = RESP_MIN_INTERVAL_MS;
static uint16_t s_iir_alpha       = RESP_IIR_ALPHA_Q15;
static uint16_t s_last_interval_ms = 0;
static uint32_t s_event_total     = 0;

static resp_event_t s_events[RESP_EVENT_RING_SIZE];
static uint16_t     s_evt_head    = 0;
static uint16_t     s_evt_count   = 0;

static int16_t  s_wave_z[RESP_WAVE_RING_SIZE];
static int16_t  s_wave_m[RESP_WAVE_RING_SIZE];
static uint16_t s_wave_idx        = 0;
static uint16_t s_wave_count      = 0;

static void wave_push(int16_t z, int16_t m)
{
    s_wave_z[s_wave_idx] = z;
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
    bool    above = (s_buf[0] > mean);
    const uint8_t min_dist = RESP_SAMPLE_HZ;
    uint16_t dist = 0;
    for (uint16_t i = 1; i < len; i++) {
        bool now_above = (s_buf[i] > mean);
        if (dist < 65535) dist++;
        if (!above && now_above && dist >= min_dist) {
            peaks++;
            dist = 0;
        }
        above = now_above;
    }
    return peaks;
}

void resp_init(void)
{
    memset(s_buf, 0, sizeof(s_buf));
    s_idx     = 0;
    s_ready   = false;
    s_bpm     = 0;
    s_bpm_raw = 0;
    s_mean_q15 = 0;
    s_mean_init = false;
    s_prev_z = 0;
    s_prev_init = false;
    s_last_peak_ms = 0;
    s_last_interval_ms = 0;
    s_evt_head = s_evt_count = 0;
    s_event_total = 0;
    s_wave_idx = s_wave_count = 0;
}

void resp_reset(void)
{
    s_idx     = 0;
    s_ready   = false;
    s_bpm     = 0;
    s_bpm_raw = 0;
    s_mean_init = false;
    s_prev_init = false;
    s_last_peak_ms = 0;
    s_last_interval_ms = 0;
    s_evt_head = s_evt_count = 0;
    s_wave_idx = s_wave_count = 0;
}

void resp_set_filter(uint8_t bpm_min, uint8_t bpm_max)
{
    if (bpm_min < 1)        bpm_min = 1;
    if (bpm_max > 60)       bpm_max = 60;
    if (bpm_min >= bpm_max) bpm_max = bpm_min + 1;
    s_bpm_min = bpm_min;
    s_bpm_max = bpm_max;
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
    if (ms < 500)   ms = 500;
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

void resp_add_sample(int16_t z_mg)
{
    /* ── Windowed BPM (v1) ───────────────────────────────────────────────── */
    if (s_idx >= BUF_MAX) s_idx = 0;
    s_buf[s_idx++] = z_mg;
    if (s_idx >= s_win_len) {
        s_idx = 0;
        uint8_t peaks = count_peaks_window(s_win_len);
        uint8_t bpm   = (uint8_t)((uint16_t)peaks * 60u / s_win_sec);
        s_bpm_raw = bpm;
        if (bpm >= s_bpm_min && bpm <= s_bpm_max) {
            s_bpm   = bpm;
            s_ready = true;
        } else {
            s_bpm   = 0;
            s_ready = false;
        }
    }

    /* ── Online detector (v2) ────────────────────────────────────────────── */
    if (!s_mean_init) {
        s_mean_q15 = (int32_t)z_mg * 32768;
        s_mean_init = true;
    } else {
        int32_t cur_mean = s_mean_q15 / 32768;
        int32_t err      = (int32_t)z_mg - cur_mean;
        s_mean_q15      += err * (int32_t)s_iir_alpha;
    }
    int16_t mean_now = (int16_t)(s_mean_q15 / 32768);

    wave_push(z_mg, mean_now);

    if (s_prev_init) {
        bool prev_below = (s_prev_z < mean_now);
        bool curr_above = (z_mg     >= mean_now);
        if (prev_below && curr_above) {
            uint32_t now = millis();
            if (s_last_peak_ms == 0 ||
                (now - s_last_peak_ms) >= (uint32_t)s_min_interval_ms) {
                uint16_t interval = 0;
                if (s_last_peak_ms != 0) {
                    interval           = (uint16_t)(now - s_last_peak_ms);
                    s_last_interval_ms = interval;
                }
                int16_t amp = (int16_t)((int32_t)z_mg - mean_now);
                event_push(now, amp, interval);
                s_last_peak_ms = now;
            }
        }
    }
    s_prev_z    = z_mg;
    s_prev_init = true;
}

bool     resp_result_ready(void)    { return s_ready;   }
uint8_t  resp_get_bpm(void)         { return s_bpm;     }
uint8_t  resp_get_bpm_raw(void)     { return s_bpm_raw; }
uint16_t resp_window_progress(void) { return s_idx;     }
uint16_t resp_window_len(void)      { return s_win_len; }

int16_t  resp_current_mean_mg(void) { return (int16_t)(s_mean_q15 / 32768); }

uint16_t resp_instant_bpm(void)
{
    if (s_last_interval_ms == 0) return 0;
    uint32_t bpm = 60000UL / s_last_interval_ms;
    if (bpm > 255) bpm = 255;
    return (uint16_t)bpm;
}

uint32_t resp_total_events(void) { return s_event_total; }

size_t resp_get_events_since(uint32_t since_ms, resp_event_t *out, size_t max)
{
    if (max == 0 || s_evt_count == 0) return 0;
    uint16_t start = (s_evt_head + RESP_EVENT_RING_SIZE - s_evt_count) % RESP_EVENT_RING_SIZE;
    size_t o = 0;
    for (uint16_t i = 0; i < s_evt_count && o < max; i++) {
        uint16_t pos = (start + i) % RESP_EVENT_RING_SIZE;
        if (s_events[pos].t_ms > since_ms) {
            out[o++] = s_events[pos];
        }
    }
    return o;
}

size_t resp_get_wave(int16_t *out_z, int16_t *out_mean, size_t max)
{
    if (s_wave_count == 0 || max == 0) return 0;
    size_t n = (s_wave_count < max) ? s_wave_count : max;
    uint16_t start = (s_wave_idx + RESP_WAVE_RING_SIZE - s_wave_count) % RESP_WAVE_RING_SIZE;
    if (s_wave_count > max) {
        start = (start + (s_wave_count - max)) % RESP_WAVE_RING_SIZE;
    }
    for (size_t i = 0; i < n; i++) {
        uint16_t pos = (start + i) % RESP_WAVE_RING_SIZE;
        if (out_z)    out_z[i]    = s_wave_z[pos];
        if (out_mean) out_mean[i] = s_wave_m[pos];
    }
    return n;
}
