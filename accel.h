#pragma once
#include <stdint.h>
#include <stdbool.h>

bool accel_init(void);
void accel_read_mg(int16_t *x_mg, int16_t *y_mg, int16_t *z_mg);
bool accel_motion_flag_take(void);   /* true once if motion IRQ fired since last call */
