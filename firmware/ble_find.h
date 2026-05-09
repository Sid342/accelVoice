#pragma once
#include <stdint.h>
#include <stdbool.h>

void     ble_find_init(void);
void     ble_find_start_adv(uint32_t duration_ms);   /* default 5 min if 0 */
void     ble_find_stop_adv(void);
void     ble_find_loop_tick(void);
bool     ble_find_is_advertising(void);
uint32_t ble_find_remaining_ms(void);
