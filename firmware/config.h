#pragma once
#include <stdint.h>
#include <stdbool.h>

#define CFG_WIFI_FIELD_LEN 64

typedef struct {
    /* Wear */
    uint16_t motion_thresh_mg;
    uint16_t still_sec;                   /* v1; ignored by v2, kept for migration */
    uint16_t offbody_sec;
    /* Wear v2 */
    uint16_t wear_var_thresh_mg;
    uint16_t wear_grav_diff_thresh_mg;
    /* Steps */
    uint16_t step_thresh_mg;
    uint16_t step_min_ms;
    uint16_t step_max_ms;
    /* Steps v2 */
    bool     step_adaptive;
    bool     step_bandpass;
    uint8_t  step_axis;            /* 0=mag 1=x 2=y 3=z */
    uint16_t step_amp_window_ms;
    /* Respiration */
    uint8_t  bpm_min;
    uint8_t  bpm_max;
    uint8_t  resp_window_sec;
    /* Respiration v2 */
    uint16_t resp_min_interval_ms;
    uint16_t resp_iir_alpha_q15;
    int16_t  resp_min_amplitude_mg;
    uint8_t  resp_axis;            /* 0=Z 1=X 2=Y 3=PCA */
    /* Respiration v3 — front-of-pipeline */
    uint8_t  resp_median_len;          /* 1/3/5/7/9 (1=off) */
    uint8_t  resp_bp_low_hz_x10;       /* 0..5 (0=off) */
    uint8_t  resp_bp_high_hz_x10;      /* 0..12 (0=off) */
    uint8_t  resp_hysteresis_mg;       /* 0..30 */
    bool     resp_adaptive_bp;
    uint8_t  resp_autocorr_window_sec; /* 10..60 */
    /* Calibration */
    int16_t  cal_x_mg;
    int16_t  cal_y_mg;
    int16_t  cal_z_mg;
    /* WiFi STA */
    char     wifi_ssid[CFG_WIFI_FIELD_LEN];
    char     wifi_pass[CFG_WIFI_FIELD_LEN];
    bool     wifi_sta_enabled;
    /* App mode (persisted across boots) */
    uint8_t  mode;                     /* app_mode_t cast; default APP_MODE_AUTO */
} cfg_t;

void   cfg_load(void);
void   cfg_save(void);
cfg_t *cfg_get(void);
void   cfg_apply(void);     /* push runtime values into modules */
