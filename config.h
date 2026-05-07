#pragma once
#include <stdint.h>
#include <stdbool.h>

#define CFG_WIFI_FIELD_LEN 64

typedef struct {
    /* Wear */
    uint16_t motion_thresh_mg;
    uint16_t still_sec;
    uint16_t offbody_sec;
    /* Steps */
    uint16_t step_thresh_mg;
    uint16_t step_min_ms;
    uint16_t step_max_ms;
    /* Respiration */
    uint8_t  bpm_min;
    uint8_t  bpm_max;
    uint8_t  resp_window_sec;
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
