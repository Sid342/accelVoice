#pragma once
#include <stdint.h>
#include <stdbool.h>

/* Runtime-tunable thresholds — defaults in app_config.h, overridable from
 * /config endpoint, persisted in NVS namespace "atovio".                    */
typedef struct {
    uint16_t motion_thresh_mg;   /* 10..200 */
    uint16_t still_sec;          /* 1..30   */
    uint16_t offbody_sec;        /* 5..120  */
    uint16_t step_thresh_mg;     /* 50..500 */
    int16_t  cal_x_mg;
    int16_t  cal_y_mg;
    int16_t  cal_z_mg;
} cfg_t;

void   cfg_load(void);                /* read NVS → in-memory; defaults if missing */
void   cfg_save(void);                /* write in-memory → NVS                     */
cfg_t *cfg_get(void);                 /* mutable pointer to in-memory config       */
void   cfg_apply(void);               /* push current values into wear/accel/steps */
