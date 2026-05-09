#pragma once
#include <stdint.h>
#include <stdbool.h>

void     find_init(void);
void     find_start(uint32_t duration_ms);   /* default 30000 if 0 */
void     find_stop(void);
bool     find_active(uint32_t now_ms);
bool     find_led_high(uint32_t now_ms);     /* desired LED level while active */
uint32_t find_remaining_ms(uint32_t now_ms);
