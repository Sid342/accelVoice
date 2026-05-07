#include "wifi_sta.h"
#include <Arduino.h>
#include <WiFi.h>

static net_sta_status_t s_status        = NET_STA_OFF;
static char             s_sta_ip[20]    = "";
static char             s_ap_ip[20]     = "";
static char             s_sta_ssid[64]  = "";
static uint32_t         s_connect_start = 0;
static const uint32_t   STA_TIMEOUT_MS  = 20000;

static void copy_ip(IPAddress ip, char *out, size_t outsz)
{
    snprintf(out, outsz, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

void net_init(const char *ap_ssid, const char *ap_pass)
{
    /* Always start AP. STA is enabled separately if creds present. */
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid, ap_pass);
    delay(100);
    copy_ip(WiFi.softAPIP(), s_ap_ip, sizeof(s_ap_ip));
    s_status = NET_STA_OFF;
}

void net_sta_apply(const char *ssid, const char *pass, bool enable)
{
    if (!enable || ssid == nullptr || ssid[0] == '\0') {
        if (s_status != NET_STA_OFF) {
            WiFi.disconnect(true /* erase config */);
            WiFi.mode(WIFI_AP);
        }
        s_status = NET_STA_OFF;
        s_sta_ip[0] = '\0';
        s_sta_ssid[0] = '\0';
        return;
    }
    /* Switch to AP+STA dual mode. Keep AP up so the client doesn't lose us
     * if the home WiFi join fails.                                            */
    WiFi.mode(WIFI_AP_STA);
    strncpy(s_sta_ssid, ssid, sizeof(s_sta_ssid) - 1);
    s_sta_ssid[sizeof(s_sta_ssid) - 1] = '\0';
    WiFi.begin(ssid, pass);
    s_status        = NET_STA_CONNECTING;
    s_connect_start = millis();
}

void net_loop_tick(void)
{
    if (s_status == NET_STA_CONNECTING) {
        if (WiFi.status() == WL_CONNECTED) {
            s_status = NET_STA_CONNECTED;
            copy_ip(WiFi.localIP(), s_sta_ip, sizeof(s_sta_ip));
            Serial.print(F("[wifi-sta] connected @ "));
            Serial.println(s_sta_ip);
        } else if (millis() - s_connect_start > STA_TIMEOUT_MS) {
            s_status = NET_STA_FAILED;
            Serial.println(F("[wifi-sta] timeout — staying on AP"));
        }
    } else if (s_status == NET_STA_CONNECTED) {
        if (WiFi.status() != WL_CONNECTED) {
            s_status = NET_STA_CONNECTING;
            s_connect_start = millis();
            s_sta_ip[0] = '\0';
        }
    }
}

net_sta_status_t net_sta_status(void) { return s_status; }

const char *net_sta_status_str(void)
{
    switch (s_status) {
        case NET_STA_OFF:        return "off";
        case NET_STA_CONNECTING: return "connecting";
        case NET_STA_CONNECTED:  return "connected";
        case NET_STA_FAILED:     return "failed";
    }
    return "?";
}

const char *net_sta_ip(void)    { return s_sta_ip;   }
const char *net_ap_ip(void)     { return s_ap_ip;    }
const char *net_sta_ssid(void)  { return s_sta_ssid; }
int8_t      net_sta_rssi(void)
{
    if (s_status != NET_STA_CONNECTED) return 0;
    return (int8_t)WiFi.RSSI();
}
