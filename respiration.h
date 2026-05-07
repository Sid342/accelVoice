#pragma once
#include <stdint.h>
#include <stdbool.h>

void     resp_init(void);
void     resp_reset(void);
void     resp_add_sample(int16_t z_mg);
bool     resp_result_ready(void);
uint8_t  resp_get_bpm(void);          /* 0 if outside filter */
uint8_t  resp_get_bpm_raw(void);      /* latest, unfiltered */
uint16_t resp_window_progress(void);  /* current fill index */
uint16_t resp_window_len(void);       /* total samples in window */

/* Runtime tunables. */
void     resp_set_filter(uint8_t bpm_min, uint8_t bpm_max);
void     resp_set_window_sec(uint8_t sec);   /* 5..15 supported */
