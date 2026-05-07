#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t t_ms;
    int16_t  x_mg, y_mg, z_mg, mag_mg;
    uint8_t  bpm;
    uint16_t steps;
    uint8_t  wear;       /* 0/1 */
    uint8_t  mode;       /* app_mode_t */
    uint8_t  ionizer;    /* 0/1 */
} sample_row_t;

void   samplelog_init(void);
void   samplelog_push(const sample_row_t *r);
size_t samplelog_dump_csv(char *out, size_t maxlen);   /* writes CSV w/ header */
