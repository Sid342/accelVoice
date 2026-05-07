#include "motionlog.h"
#include <stdio.h>
#include <string.h>

#define MOTIONLOG_DEPTH 20

static uint32_t s_buf[MOTIONLOG_DEPTH];
static uint8_t  s_head  = 0;
static uint8_t  s_have  = 0;
static uint32_t s_total = 0;

void motionlog_init(void)
{
    s_head  = 0;
    s_have  = 0;
    s_total = 0;
    memset(s_buf, 0, sizeof(s_buf));
}

void motionlog_push(uint32_t now_ms)
{
    s_buf[s_head] = now_ms;
    s_head = (s_head + 1) % MOTIONLOG_DEPTH;
    if (s_have < MOTIONLOG_DEPTH) s_have++;
    s_total++;
}

uint32_t motionlog_count(void) { return s_total; }

size_t motionlog_dump_json(char *out, size_t maxlen, uint32_t now_ms)
{
    size_t n = 0;
    n += snprintf(out + n, maxlen - n, "{\"total\":%lu,\"events\":[",
                  (unsigned long)s_total);

    uint8_t start = (s_head + MOTIONLOG_DEPTH - s_have) % MOTIONLOG_DEPTH;
    for (uint8_t i = 0; i < s_have; i++) {
        uint32_t t = s_buf[(start + i) % MOTIONLOG_DEPTH];
        int32_t  rel_s = (int32_t)((int64_t)now_ms - (int64_t)t) / 1000;
        n += snprintf(out + n, maxlen - n, "%s%d", i ? "," : "", -(int)rel_s);
    }
    n += snprintf(out + n, maxlen - n, "]}");
    return n;
}
