#pragma once
#include <stdint.h>
#include <stdbool.h>

void    resp_init(void);
void    resp_add_sample(int16_t z_mg);   /* call at RESP_SAMPLE_HZ rate */
bool    resp_result_ready(void);
uint8_t resp_get_bpm(void);               /* 0 if invalid               */
