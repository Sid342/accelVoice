#include "respiration.h"
#include "app_config.h"
#include <stdint.h>
#include <string.h>

#define BUF_LEN (RESP_WINDOW_SEC * RESP_SAMPLE_HZ)   /* 250 @ 25 Hz × 10 s */

static int16_t  s_buf[BUF_LEN];
static uint16_t s_idx     = 0;
static bool     s_ready   = false;
static uint8_t  s_bpm     = 0;     /* filter-validated */
static uint8_t  s_bpm_raw = 0;     /* most recent estimate, valid or not */

static uint8_t count_peaks(void)
{
    int32_t sum = 0;
    for (uint16_t i = 0; i < BUF_LEN; i++) sum += s_buf[i];
    int16_t mean = (int16_t)(sum / (int32_t)BUF_LEN);

    uint8_t peaks    = 0;
    bool    above    = (s_buf[0] > mean);
    const uint8_t min_dist = RESP_SAMPLE_HZ;   /* min 1 s between peaks */
    uint8_t dist = 0;

    for (uint16_t i = 1; i < BUF_LEN; i++) {
        bool now_above = (s_buf[i] > mean);
        if (dist < 255) dist++;
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

void resp_reset(void) { resp_init(); }

void resp_add_sample(int16_t z_mg)
{
    s_buf[s_idx++] = z_mg;
    if (s_idx < BUF_LEN) return;

    s_idx = 0;
    uint8_t peaks = count_peaks();
    uint8_t bpm   = (uint8_t)((uint16_t)peaks * 60u / RESP_WINDOW_SEC);
    s_bpm_raw = bpm;

    if (bpm >= RESP_BPM_MIN && bpm <= RESP_BPM_MAX) {
        s_bpm   = bpm;
        s_ready = true;
    } else {
        s_bpm   = 0;
        s_ready = false;
    }
}

bool     resp_result_ready(void)  { return s_ready;   }
uint8_t  resp_get_bpm(void)       { return s_bpm;     }
uint8_t  resp_get_bpm_raw(void)   { return s_bpm_raw; }
uint16_t resp_window_progress(void) { return s_idx;   }
