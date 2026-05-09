#pragma once
#include <stdint.h>

void     steps_init(void);
void     steps_add_sample(int16_t x_mg, int16_t y_mg, int16_t z_mg);
uint32_t steps_get_count(void);
void     steps_reset(void);

/* Runtime tunables. */
void     steps_set_thresh_mg(uint16_t mg);
void     steps_set_intervals_ms(uint16_t min_ms, uint16_t max_ms);

/* UI helpers. */
uint32_t steps_get_cadence_ms(void);
uint16_t steps_get_per_min(void);
