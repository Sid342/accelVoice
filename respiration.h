#pragma once
#include <stdint.h>
#include <stdbool.h>

void    resp_init(void);
void    resp_reset(void);                /* clear buffer, restart window  */
void    resp_add_sample(int16_t z_mg);   /* call at RESP_SAMPLE_HZ rate   */
bool    resp_result_ready(void);
uint8_t resp_get_bpm(void);              /* 0 if outside 8–30 BPM filter  */
uint8_t resp_get_bpm_raw(void);          /* latest BPM regardless of filter */
uint16_t resp_window_progress(void);     /* 0..(WINDOW_LEN-1) — fill state */
