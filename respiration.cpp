#include "respiration.h"
#include "app_config.h"
#include <stdint.h>
#include <string.h>

#define BUF_MAX (15 * RESP_SAMPLE_HZ)   /* 15-second max window */

static int16_t  s_buf[BUF_MAX];
static uint16_t s_idx       = 0;
static bool     s_ready     = false;
static uint8_t  s_bpm       = 0;
static uint8_t  s_bpm_raw   = 0;
static uint8_t  s_bpm_min   = RESP_BPM_MIN;
static uint8_t  s_bpm_max   = RESP_BPM_MAX;
static uint8_t  s_win_sec   = RESP_WINDOW_SEC;
static uint16_t s_win_len   = RESP_WINDOW_SEC * RESP_SAMPLE_HZ;

static uint8_t count_peaks(uint16_t len)
{
    int32_t sum = 0;
    for (uint16_t i = 0; i < len; i++) sum += s_buf[i];
    int16_t mean = (int16_t)(sum / (int32_t)len);

    uint8_t peaks = 0;
    bool    above = (s_buf[0] > mean);
    const uint8_t min_dist = RESP_SAMPLE_HZ;   /* min 1 s between peaks */
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
}

void resp_reset(void)
{
    s_idx     = 0;
    s_ready   = false;
    s_bpm     = 0;
    s_bpm_raw = 0;
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

void resp_add_sample(int16_t z_mg)
{
    if (s_idx >= BUF_MAX) s_idx = 0;
    s_buf[s_idx++] = z_mg;
    if (s_idx < s_win_len) return;

    s_idx = 0;
    uint8_t peaks = count_peaks(s_win_len);
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

bool     resp_result_ready(void)    { return s_ready;   }
uint8_t  resp_get_bpm(void)         { return s_bpm;     }
uint8_t  resp_get_bpm_raw(void)     { return s_bpm_raw; }
uint16_t resp_window_progress(void) { return s_idx;     }
uint16_t resp_window_len(void)      { return s_win_len; }
