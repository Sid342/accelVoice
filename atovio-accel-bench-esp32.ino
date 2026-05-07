/*
 * atovio-accel-bench-esp32
 * ESP32 DevKit V1 + MPU6050 bench rig — same algorithms as the Nano variant
 * but with WiFi AP + web UI so you can walk around with the device powered
 * by a USB power bank and watch live data on a phone browser.
 *
 * Hookup:
 *   MPU6050 VCC → ESP32 3V3
 *   MPU6050 GND → GND
 *   MPU6050 SDA → GPIO 21
 *   MPU6050 SCL → GPIO 22
 *   MPU6050 INT → GPIO 4
 *   MPU6050 AD0 → GND (I²C addr 0x68)
 *   Status LED  = GPIO 2 (onboard)
 *
 * Usage:
 *   1. Flash sketch.
 *   2. Phone/laptop joins WiFi "atovio-bench" (password "atovio1234").
 *   3. Browse to http://192.168.4.1/  → live cards + plot.
 *   4. Or hit http://192.168.4.1/data for JSON snapshot (curl-friendly).
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>

#include "app_config.h"
#include "accel.h"
#include "wear.h"
#include "respiration.h"
#include "steps.h"
#include "web_index.h"

static WebServer http(WIFI_HTTP_PORT);

static uint32_t last_sample_ms    = 0;
static uint32_t last_tick_ms      = 0;
static uint32_t last_telem_ms     = 0;
static uint32_t step_pulse_until  = 0;
static uint32_t last_step_count   = 0;

static int16_t last_x_mg = 0, last_y_mg = 0, last_z_mg = 0;

static const uint16_t SAMPLE_PERIOD_MS = 1000U / ACCEL_ODR_HZ;

static void apply_status_led(uint32_t now)
{
    bool on_body = (wear_get_state() == WEAR_STATE_ON_BODY);
    bool pulsing = (now < step_pulse_until);
    bool drive_high = on_body && !pulsing;
    digitalWrite(PIN_LED_STATUS, drive_high ? HIGH : LOW);
}

static void handle_root(void)
{
    http.send_P(200, "text/html", WEB_INDEX_HTML);
}

static void handle_data(void)
{
    char buf[160];
    snprintf(buf, sizeof(buf),
             "{\"t\":%lu,\"w\":%d,\"x\":%d,\"y\":%d,\"z\":%d,"
             "\"bpm\":%d,\"st\":%lu}",
             (unsigned long)millis(),
             wear_get_state() == WEAR_STATE_ON_BODY ? 1 : 0,
             last_x_mg, last_y_mg, last_z_mg,
             resp_result_ready() ? resp_get_bpm() : 0,
             (unsigned long)steps_get_count());
    http.sendHeader("Cache-Control", "no-store");
    http.send(200, "application/json", buf);
}

static void handle_404(void)
{
    http.send(404, "text/plain", "not found");
}

static void wifi_ap_start(void)
{
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
    IPAddress ip = WiFi.softAPIP();
    Serial.print(F("[wifi] AP "));
    Serial.print(WIFI_AP_SSID);
    Serial.print(F(" up @ http://"));
    Serial.print(ip);
    Serial.println(F("/"));
}

static void http_start(void)
{
    http.on("/", handle_root);
    http.on("/data", handle_data);
    http.onNotFound(handle_404);
    http.begin();
    Serial.print(F("[http] listening on :"));
    Serial.println(WIFI_HTTP_PORT);
}

void setup(void)
{
    pinMode(PIN_LED_STATUS, OUTPUT);
    digitalWrite(PIN_LED_STATUS, LOW);

    Serial.begin(SERIAL_BAUD);
    delay(200);
    Serial.println(F("\n[bench] atovio-accel-bench-esp32 boot"));

    if (!accel_init()) {
        Serial.println(F("[bench] FATAL: MPU6050 not detected"));
        while (1) {
            digitalWrite(PIN_LED_STATUS, HIGH); delay(100);
            digitalWrite(PIN_LED_STATUS, LOW);  delay(100);
        }
    }
    Serial.println(F("[bench] MPU6050 online"));

    wear_init();
    resp_init();
    steps_init();

    wifi_ap_start();
    http_start();

    Serial.println(F("# t=ms w=wear(0=off,1=on) x/y/z=mg bpm=respiration st=steps"));
}

void loop(void)
{
    uint32_t now = millis();

    /* HTTP service — non-blocking when no client. */
    http.handleClient();

    if (accel_motion_flag_take()) {
        wear_notify_motion();
    }

    if (now - last_sample_ms >= SAMPLE_PERIOD_MS) {
        last_sample_ms = now;
        accel_read_mg(&last_x_mg, &last_y_mg, &last_z_mg);
        if (wear_get_state() == WEAR_STATE_ON_BODY) {
            resp_add_sample(last_z_mg);
            steps_add_sample(last_x_mg, last_y_mg, last_z_mg);
        }
    }

    if (now - last_tick_ms >= 1000) {
        last_tick_ms = now;
        wear_tick_sec();
    }

    uint32_t sc = steps_get_count();
    if (sc != last_step_count) {
        last_step_count  = sc;
        step_pulse_until = now + STEP_LED_PULSE_MS;
    }

    if (wear_state_changed()) {
        Serial.print(F("[wear] "));
        Serial.println(wear_get_state() == WEAR_STATE_ON_BODY ? F("ON-BODY")
                                                              : F("OFF-BODY"));
    }

    apply_status_led(now);

    if (now - last_telem_ms >= TELEMETRY_PERIOD_MS) {
        last_telem_ms = now;
        Serial.print(F("t="));    Serial.print(now);
        Serial.print(F(" w="));   Serial.print(wear_get_state() == WEAR_STATE_ON_BODY ? 1 : 0);
        Serial.print(F(" x="));   Serial.print(last_x_mg);
        Serial.print(F(" y="));   Serial.print(last_y_mg);
        Serial.print(F(" z="));   Serial.print(last_z_mg);
        Serial.print(F(" bpm=")); Serial.print(resp_result_ready() ? resp_get_bpm() : 0);
        Serial.print(F(" st="));  Serial.println(sc);
    }
}
