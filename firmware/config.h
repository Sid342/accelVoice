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
    /* Respiration */
    uint8_t  bpm_min;
    uint8_t  bpm_max;
    uint8_t  resp_window_sec;
    /* Respiration v2 */
    uint16_t resp_min_interval_ms;
    uint16_t resp_iir_alpha_q15;
    /* Calibration */
    int16_t  cal_x_mg;
    int16_t  cal_y_mg;
    int16_t  cal_z_mg;
    /* WiFi STA */
    char     wifi_ssid[CFG_WIFI_FIELD_LEN];
    char     wifi_pass[CFG_WIFI_FIELD_LEN];
    bool     wifi_sta_enabled;
} cfg_t;

void   cfg_load(void);
void   cfg_save(void);
cfg_t *cfg_get(void);
void   cfg_apply(void);     /* push runtime values into modules */
