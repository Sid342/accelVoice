#pragma once
#include <stdint.h>

void     steps_init(void);
void     steps_add_sample(int16_t x_mg, int16_t y_mg, int16_t z_mg);
uint32_t steps_get_count(void);
void     steps_reset(void);

/* Runtime tunable. */
void     steps_set_thresh_mg(uint16_t mg);

/* UI helpers. */
uint32_t steps_get_cadence_ms(void);   /* gap between last two steps; 0 if none */
uint16_t steps_get_per_min(void);      /* derived from cadence; 0 if stale     */
