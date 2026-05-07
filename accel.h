#pragma once
#include <stdint.h>
#include <stdbool.h>

bool accel_init(void);

/* Returns mg with calibration offsets applied. */
void accel_read_mg(int16_t *x_mg, int16_t *y_mg, int16_t *z_mg);

/* Returns mg WITHOUT calibration offsets applied — used by calibration. */
void accel_read_mg_raw(int16_t *x_mg, int16_t *y_mg, int16_t *z_mg);

bool accel_motion_flag_take(void);

/* Runtime tunables. */
void accel_set_motion_thresh_mg(uint16_t mg);
void accel_set_offsets_mg(int16_t ox, int16_t oy, int16_t oz);
void accel_get_offsets_mg(int16_t *ox, int16_t *oy, int16_t *oz);

/* Capture current XYZ baseline at flat rest. Averages 1 s of samples and
 * stores as offset such that calibrated reads should produce x≈0, y≈0,
 * z≈+1000 mg when device is flat. Caller must save NVS afterwards.          */
void accel_calibrate(void);
