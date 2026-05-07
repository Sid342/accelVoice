#pragma once
#include <stdint.h>
#include <stddef.h>

void   motionlog_init(void);
void   motionlog_push(uint32_t now_ms);
size_t motionlog_dump_json(char *out, size_t maxlen, uint32_t now_ms);
uint32_t motionlog_count(void);   /* total motion events since boot */
