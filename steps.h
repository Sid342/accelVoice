#pragma once
#include <stdint.h>

void     steps_init(void);
void     steps_add_sample(int16_t x_mg, int16_t y_mg, int16_t z_mg);
uint32_t steps_get_count(void);
void     steps_reset(void);
