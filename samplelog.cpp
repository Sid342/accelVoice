#include "samplelog.h"
#include <stdio.h>
#include <string.h>

#define LOG_DEPTH 100   /* 10 s @ 10 Hz telemetry rate */

static sample_row_t s_buf[LOG_DEPTH];
static uint16_t     s_head = 0;
static uint16_t     s_have = 0;

void samplelog_init(void)
{
    s_head = 0;
    s_have = 0;
    memset(s_buf, 0, sizeof(s_buf));
}

void samplelog_push(const sample_row_t *r)
{
    s_buf[s_head] = *r;
    s_head = (s_head + 1) % LOG_DEPTH;
    if (s_have < LOG_DEPTH) s_have++;
}

size_t samplelog_dump_csv(char *out, size_t maxlen)
{
    size_t n = 0;
    n += snprintf(out + n, maxlen - n,
                  "t_ms,wear,x_mg,y_mg,z_mg,mag_mg,bpm,steps,mode,ionizer\n");

    uint16_t start = (s_head + LOG_DEPTH - s_have) % LOG_DEPTH;
    for (uint16_t i = 0; i < s_have && n + 64 < maxlen; i++) {
        const sample_row_t *r = &s_buf[(start + i) % LOG_DEPTH];
        n += snprintf(out + n, maxlen - n,
                      "%lu,%u,%d,%d,%d,%d,%u,%u,%u,%u\n",
                      (unsigned long)r->t_ms, r->wear,
                      r->x_mg, r->y_mg, r->z_mg, r->mag_mg,
                      r->bpm, r->steps, r->mode, r->ionizer);
    }
    return n;
}
