#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    NET_STA_OFF        = 0,
    NET_STA_CONNECTING = 1,
    NET_STA_CONNECTED  = 2,
    NET_STA_FAILED     = 3,
} net_sta_status_t;

void              net_init(const char *ap_ssid, const char *ap_pass);
void              net_sta_apply(const char *ssid, const char *pass, bool enable);
void              net_loop_tick(void);
net_sta_status_t  net_sta_status(void);
const char       *net_sta_status_str(void);
const char       *net_sta_ip(void);
const char       *net_ap_ip(void);
const char       *net_sta_ssid(void);
int8_t            net_sta_rssi(void);
