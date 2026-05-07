/*
 * atovio-accel-bench-esp32 — runtime-controllable bench rig (v2)
 *
 * Single firmware that exposes every Phase 2.5 feature for tuning + validation
 * via a web UI at http://atovio.local/ — no reflashes needed for normal work.
 * OTA enabled on the same WiFi AP, so future builds can be pushed without USB.
 *
 * Hookup (ESP32 DevKit V1):
 *   MPU6050 VCC → Vin (5 V, GY-521 has on-board LDO)
 *   MPU6050 GND → GND
 *   MPU6050 SDA → GPIO 25 (D25)
 *   MPU6050 SCL → GPIO 26 (D26)
 *   MPU6050 INT → GPIO 27 (D27)
 *   MPU6050 AD0 → GND       (I²C addr 0x68)
 *   Status LED   = onboard GPIO 2
 *
 * WiFi: AP mode, SSID "atovio-bench", pwd "atovio1234". Phone joins the AP →
 * http://atovio.local/  or  http://192.168.4.1/.
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>

#include "app_config.h"
#include "accel.h"
#include "wear.h"
#include "respiration.h"
#include "steps.h"
#include "mode.h"
#include "battsim.h"
#include "config.h"
#include "samplelog.h"
#include "motionlog.h"
#include "find.h"
#include "wifi_sta.h"
#include "ble_find.h"
#include "web_index.h"

static WebServer http(WIFI_HTTP_PORT);

static uint32_t last_sample_ms    = 0;
static uint32_t last_tick_ms      = 0;
static uint32_t last_telem_ms     = 0;
static uint32_t step_pulse_until  = 0;
static uint32_t last_step_count   = 0;

static int16_t last_x_mg = 0, last_y_mg = 0, last_z_mg = 0, last_mag_mg = 0;

static const uint16_t SAMPLE_PERIOD_MS = 1000U / ACCEL_ODR_HZ;

/* ── Tiny JSON-body helpers (no ArduinoJson dependency) ─────────────────── */
static int kv_int(const String &b, const char *key, int dflt)
{
    String pat = String("\"") + key + "\"";
    int p = b.indexOf(pat);
    if (p < 0) return dflt;
    p = b.indexOf(':', p);
    if (p < 0) return dflt;
    while (p + 1 < (int)b.length() && (b[p + 1] == ' ' || b[p + 1] == '\t')) p++;
    return b.substring(p + 1).toInt();
}
static bool kv_bool(const String &b, const char *key, bool dflt)
{
    String pat = String("\"") + key + "\"";
    int p = b.indexOf(pat);
    if (p < 0) return dflt;
    p = b.indexOf(':', p);
    if (p < 0) return dflt;
    String tail = b.substring(p + 1);
    tail.trim();
    if (tail.startsWith("true"))  return true;
    if (tail.startsWith("false")) return false;
    return dflt;
}
static String kv_str(const String &b, const char *key)
{
    String pat = String("\"") + key + "\"";
    int p = b.indexOf(pat);
    if (p < 0) return String();
    p = b.indexOf(':', p);
    if (p < 0) return String();
    int q = b.indexOf('"', p);
    if (q < 0) return String();
    int q2 = b.indexOf('"', q + 1);
    if (q2 < 0) return String();
    return b.substring(q + 1, q2);
}

/* ── LED encoding ────────────────────────────────────────────────────────── */
static void apply_status_led(uint32_t now)
{
    /* Find-my-device strobe overrides everything. */
    if (find_active(now)) {
        digitalWrite(PIN_LED_STATUS, find_led_high(now) ? HIGH : LOW);
        return;
    }
    bool ionizer = ionizer_state();
    bool pulse   = (now < step_pulse_until);
    digitalWrite(PIN_LED_STATUS, (ionizer && !pulse) ? HIGH : LOW);
}

/* ── /data live snapshot ────────────────────────────────────────────────── */
static void handle_data(void)
{
    char buf[1024];
    cfg_t *c = cfg_get();
    snprintf(buf, sizeof(buf),
        "{\"t\":%lu,"
        "\"uptime_s\":%lu,"
        "\"wear\":\"%s\","
        "\"wear_forced\":\"%s\","
        "\"wear_remaining_s\":%u,"
        "\"x\":%d,\"y\":%d,\"z\":%d,\"mag\":%d,"
        "\"bpm\":%u,\"bpm_valid\":%s,\"bpm_raw\":%u,"
        "\"resp_progress\":%u,\"resp_window_len\":%u,"
        "\"steps\":%lu,\"step_cadence_ms\":%lu,\"steps_per_min\":%u,"
        "\"mode\":\"%s\","
        "\"ionizer\":%s,"
        "\"battery\":{\"pct\":%u,\"charging\":%s,\"fault\":%s,\"state\":\"%s\"},"
        "\"motion_irq_count\":%lu,"
        "\"find\":{\"active\":%s,\"remaining_s\":%lu},"
        "\"ble\":{\"advertising\":%s,\"remaining_s\":%lu},"
        "\"net\":{\"ap_ip\":\"%s\",\"sta_status\":\"%s\",\"sta_ip\":\"%s\","
                  "\"sta_ssid\":\"%s\",\"sta_rssi\":%d,\"sta_enabled\":%s},"
        "\"cfg\":{\"motion_thresh_mg\":%u,\"still_sec\":%u,\"offbody_sec\":%u,"
                 "\"step_thresh_mg\":%u,\"step_min_ms\":%u,\"step_max_ms\":%u,"
                 "\"bpm_min\":%u,\"bpm_max\":%u,\"resp_window_sec\":%u,"
                 "\"cal_x\":%d,\"cal_y\":%d,\"cal_z\":%d}}",
        (unsigned long)millis(),
        (unsigned long)(millis() / 1000UL),
        wear_get_state() == WEAR_STATE_ON_BODY ? "on" : "off",
        wear_get_force() == WEAR_FORCE_AUTO ? "auto" : (wear_get_force() == WEAR_FORCE_ON ? "on" : "off"),
        wear_remaining_sec(),
        last_x_mg, last_y_mg, last_z_mg, last_mag_mg,
        resp_get_bpm(), resp_result_ready() ? "true" : "false", resp_get_bpm_raw(),
        resp_window_progress(), (unsigned)(RESP_WINDOW_SEC * RESP_SAMPLE_HZ),
        (unsigned long)steps_get_count(), (unsigned long)steps_get_cadence_ms(), steps_get_per_min(),
        mode_str(mode_get()),
        ionizer_state() ? "true" : "false",
        battsim_get_pct(), battsim_is_charging() ? "true" : "false",
        battsim_is_fault() ? "true" : "false", battsim_state_str(battsim_get_state()),
        (unsigned long)motionlog_count(),
        find_active(millis()) ? "true" : "false",
        (unsigned long)((find_remaining_ms(millis()) + 999) / 1000),
        ble_find_is_advertising() ? "true" : "false",
        (unsigned long)((ble_find_remaining_ms() + 999) / 1000),
        net_ap_ip(), net_sta_status_str(), net_sta_ip(),
        net_sta_ssid(), (int)net_sta_rssi(), c->wifi_sta_enabled ? "true" : "false",
        c->motion_thresh_mg, c->still_sec, c->offbody_sec,
        c->step_thresh_mg, c->step_min_ms, c->step_max_ms,
        c->bpm_min, c->bpm_max, c->resp_window_sec,
        c->cal_x_mg, c->cal_y_mg, c->cal_z_mg);
    http.sendHeader("Cache-Control", "no-store");
    http.send(200, "application/json", buf);
}

/* ── /config GET + POST ─────────────────────────────────────────────────── */
static void handle_config_get(void)
{
    char buf[400];
    cfg_t *c = cfg_get();
    snprintf(buf, sizeof(buf),
             "{\"motion_thresh_mg\":%u,\"still_sec\":%u,\"offbody_sec\":%u,"
             "\"step_thresh_mg\":%u,\"step_min_ms\":%u,\"step_max_ms\":%u,"
             "\"bpm_min\":%u,\"bpm_max\":%u,\"resp_window_sec\":%u,"
             "\"cal_x\":%d,\"cal_y\":%d,\"cal_z\":%d}",
             c->motion_thresh_mg, c->still_sec, c->offbody_sec,
             c->step_thresh_mg, c->step_min_ms, c->step_max_ms,
             c->bpm_min, c->bpm_max, c->resp_window_sec,
             c->cal_x_mg, c->cal_y_mg, c->cal_z_mg);
    http.send(200, "application/json", buf);
}
static void handle_config_post(void)
{
    String body = http.arg("plain");
    cfg_t *c = cfg_get();
    int v;
    v = kv_int(body, "motion_thresh_mg", -1);
    if (v >= 10 && v <= 510) c->motion_thresh_mg = v;
    v = kv_int(body, "still_sec", -1);
    if (v >= 1 && v <= 30) c->still_sec = v;
    v = kv_int(body, "offbody_sec", -1);
    if (v >= 5 && v <= 300) c->offbody_sec = v;
    v = kv_int(body, "step_thresh_mg", -1);
    if (v >= 50 && v <= 1000) c->step_thresh_mg = v;
    v = kv_int(body, "step_min_ms", -1);
    if (v >= 50 && v <= 5000) c->step_min_ms = v;
    v = kv_int(body, "step_max_ms", -1);
    if (v >= 100 && v <= 10000) c->step_max_ms = v;
    v = kv_int(body, "bpm_min", -1);
    if (v >= 1 && v <= 30) c->bpm_min = v;
    v = kv_int(body, "bpm_max", -1);
    if (v >= 5 && v <= 60) c->bpm_max = v;
    v = kv_int(body, "resp_window_sec", -1);
    if (v >= 5 && v <= 15) c->resp_window_sec = v;
    cfg_apply();
    cfg_save();
    handle_config_get();
}

/* ── /mode POST ─────────────────────────────────────────────────────────── */
static void handle_mode_post(void)
{
    String body = http.arg("plain");
    String s = kv_str(body, "mode");
    if (s.length() > 0) mode_set(mode_from_str(s.c_str()));
    char buf[40];
    snprintf(buf, sizeof(buf), "{\"mode\":\"%s\"}", mode_str(mode_get()));
    http.send(200, "application/json", buf);
}

/* ── /wear/force POST ───────────────────────────────────────────────────── */
static void handle_wear_force_post(void)
{
    String body = http.arg("plain");
    String s = kv_str(body, "state");
    wear_force_t f = WEAR_FORCE_AUTO;
    if      (s == "on")  f = WEAR_FORCE_ON;
    else if (s == "off") f = WEAR_FORCE_OFF;
    wear_set_force(f);
    char buf[40];
    snprintf(buf, sizeof(buf), "{\"state\":\"%s\"}",
             f == WEAR_FORCE_AUTO ? "auto" : (f == WEAR_FORCE_ON ? "on" : "off"));
    http.send(200, "application/json", buf);
}

/* ── /steps/reset, /resp/reset ──────────────────────────────────────────── */
static void handle_steps_reset(void)
{
    steps_reset();
    http.send(200, "application/json", "{\"steps\":0}");
}
static void handle_resp_reset(void)
{
    resp_reset();
    http.send(200, "application/json", "{\"bpm\":0}");
}

/* ── /accel/calibrate ───────────────────────────────────────────────────── */
static void handle_accel_calibrate(void)
{
    accel_calibrate();
    int16_t ox, oy, oz;
    accel_get_offsets_mg(&ox, &oy, &oz);
    cfg_t *c = cfg_get();
    c->cal_x_mg = ox; c->cal_y_mg = oy; c->cal_z_mg = oz;
    cfg_save();
    char buf[80];
    snprintf(buf, sizeof(buf), "{\"cal_x\":%d,\"cal_y\":%d,\"cal_z\":%d}", ox, oy, oz);
    http.send(200, "application/json", buf);
}

/* ── /battery POST ──────────────────────────────────────────────────────── */
static void handle_battery_post(void)
{
    String body = http.arg("plain");
    int  pct      = kv_int(body, "pct", battsim_get_pct());
    bool charging = kv_bool(body, "charging", battsim_is_charging());
    bool fault    = kv_bool(body, "fault", battsim_is_fault());
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    battsim_set((uint8_t)pct, charging, fault);
    char buf[120];
    snprintf(buf, sizeof(buf),
             "{\"pct\":%u,\"charging\":%s,\"fault\":%s,\"state\":\"%s\"}",
             battsim_get_pct(), battsim_is_charging() ? "true" : "false",
             battsim_is_fault() ? "true" : "false",
             battsim_state_str(battsim_get_state()));
    http.send(200, "application/json", buf);
}

/* ── BLE Type 10 protocol-shape simulator ───────────────────────────────── */
static void handle_api_type10(void)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"t\":10,\"wear\":%d,\"bpm\":%u}",
             wear_get_state() == WEAR_STATE_ON_BODY ? 1 : 0,
             resp_get_bpm());
    http.send(200, "application/json", buf);
}

/* ── /log.csv ───────────────────────────────────────────────────────────── */
static void handle_log_csv(void)
{
    static char csv[6144];
    size_t n = samplelog_dump_csv(csv, sizeof(csv));
    http.sendHeader("Content-Disposition", "attachment; filename=log.csv");
    http.send(200, "text/csv", csv);
    (void)n;
}

/* ── /motionlog ─────────────────────────────────────────────────────────── */
static void handle_motionlog(void)
{
    char buf[512];
    motionlog_dump_json(buf, sizeof(buf), millis());
    http.send(200, "application/json", buf);
}

/* ── /wifi GET (status) + POST (set creds) ─────────────────────────────── */
static void handle_wifi_get(void)
{
    cfg_t *c = cfg_get();
    char buf[300];
    snprintf(buf, sizeof(buf),
             "{\"sta_enabled\":%s,\"sta_ssid\":\"%s\",\"sta_status\":\"%s\","
             "\"sta_ip\":\"%s\",\"sta_rssi\":%d,\"ap_ip\":\"%s\","
             "\"ap_ssid\":\"%s\"}",
             c->wifi_sta_enabled ? "true" : "false", c->wifi_ssid,
             net_sta_status_str(), net_sta_ip(), (int)net_sta_rssi(),
             net_ap_ip(), WIFI_AP_SSID);
    http.send(200, "application/json", buf);
}
static void handle_wifi_post(void)
{
    String body = http.arg("plain");
    cfg_t *c = cfg_get();
    String ssid = kv_str(body, "ssid");
    String pass = kv_str(body, "password");
    bool   en   = kv_bool(body, "enabled", c->wifi_sta_enabled);

    if (ssid.length() > 0) {
        strncpy(c->wifi_ssid, ssid.c_str(), CFG_WIFI_FIELD_LEN - 1);
        c->wifi_ssid[CFG_WIFI_FIELD_LEN - 1] = '\0';
    }
    if (pass.length() > 0) {
        strncpy(c->wifi_pass, pass.c_str(), CFG_WIFI_FIELD_LEN - 1);
        c->wifi_pass[CFG_WIFI_FIELD_LEN - 1] = '\0';
    }
    c->wifi_sta_enabled = en;
    cfg_save();
    net_sta_apply(c->wifi_ssid, c->wifi_pass, en);
    handle_wifi_get();
}

/* ── /ble/start, /ble/stop ─────────────────────────────────────────────── */
static void handle_ble_start(void)
{
    String body = http.arg("plain");
    int min = kv_int(body, "minutes", 5);
    if (min < 1)  min = 1;
    if (min > 60) min = 60;
    ble_find_start_adv((uint32_t)min * 60UL * 1000UL);
    char buf[80];
    snprintf(buf, sizeof(buf), "{\"advertising\":true,\"remaining_min\":%d}", min);
    http.send(200, "application/json", buf);
}
static void handle_ble_stop(void)
{
    ble_find_stop_adv();
    http.send(200, "application/json", "{\"advertising\":false}");
}

/* ── /system (OTA + network + build info) ──────────────────────────────── */
static void handle_system(void)
{
    char buf[400];
    snprintf(buf, sizeof(buf),
             "{\"hostname\":\"atovio\","
              "\"ap_ip\":\"%s\",\"sta_ip\":\"%s\","
              "\"ota_port\":3232,"
              "\"upload_cmd\":\"arduino-cli upload --upload-port atovio.local --fqbn esp32:esp32:esp32 .\","
              "\"build_date\":\"" __DATE__ " " __TIME__ "\","
              "\"free_heap\":%lu,\"chip_model\":\"%s\","
              "\"sdk\":\"%s\"}",
             net_ap_ip(), net_sta_ip(),
             (unsigned long)ESP.getFreeHeap(),
             ESP.getChipModel(), ESP.getSdkVersion());
    http.send(200, "application/json", buf);
}

/* ── /settings/export, /settings/import ────────────────────────────────── */
static void handle_settings_export(void)
{
    char buf[600];
    cfg_t *c = cfg_get();
    snprintf(buf, sizeof(buf),
             "{\"motion_thresh_mg\":%u,\"still_sec\":%u,\"offbody_sec\":%u,"
             "\"step_thresh_mg\":%u,\"step_min_ms\":%u,\"step_max_ms\":%u,"
             "\"bpm_min\":%u,\"bpm_max\":%u,\"resp_window_sec\":%u,"
             "\"cal_x\":%d,\"cal_y\":%d,\"cal_z\":%d,"
             "\"wifi_ssid\":\"%s\",\"wifi_sta_enabled\":%s}",
             c->motion_thresh_mg, c->still_sec, c->offbody_sec,
             c->step_thresh_mg, c->step_min_ms, c->step_max_ms,
             c->bpm_min, c->bpm_max, c->resp_window_sec,
             c->cal_x_mg, c->cal_y_mg, c->cal_z_mg,
             c->wifi_ssid, c->wifi_sta_enabled ? "true" : "false");
    http.sendHeader("Content-Disposition", "attachment; filename=atovio-settings.json");
    http.send(200, "application/json", buf);
}
static void handle_settings_import(void)
{
    String body = http.arg("plain");
    cfg_t *c = cfg_get();
    int v;
    v = kv_int(body, "motion_thresh_mg", -1); if (v > 0) c->motion_thresh_mg = v;
    v = kv_int(body, "still_sec", -1);        if (v > 0) c->still_sec = v;
    v = kv_int(body, "offbody_sec", -1);      if (v > 0) c->offbody_sec = v;
    v = kv_int(body, "step_thresh_mg", -1);   if (v > 0) c->step_thresh_mg = v;
    v = kv_int(body, "step_min_ms", -1);      if (v > 0) c->step_min_ms = v;
    v = kv_int(body, "step_max_ms", -1);      if (v > 0) c->step_max_ms = v;
    v = kv_int(body, "bpm_min", -1);          if (v > 0) c->bpm_min = v;
    v = kv_int(body, "bpm_max", -1);          if (v > 0) c->bpm_max = v;
    v = kv_int(body, "resp_window_sec", -1);  if (v > 0) c->resp_window_sec = v;
    cfg_apply();
    cfg_save();
    http.send(200, "application/json", "{\"ok\":true}");
}

/* ── /find (start), /find/stop ─────────────────────────────────────────── */
static void handle_find_post(void)
{
    String body = http.arg("plain");
    int sec = kv_int(body, "seconds", 30);
    if (sec < 1)   sec = 1;
    if (sec > 600) sec = 600;
    find_start((uint32_t)sec * 1000UL);
    char buf[80];
    snprintf(buf, sizeof(buf), "{\"active\":true,\"remaining_s\":%d}", sec);
    http.send(200, "application/json", buf);
}
static void handle_find_stop(void)
{
    find_stop();
    http.send(200, "application/json", "{\"active\":false,\"remaining_s\":0}");
}

/* ── / HTML viewer ──────────────────────────────────────────────────────── */
static void handle_root(void) { http.send_P(200, "text/html", WEB_INDEX_HTML); }
static void handle_404(void)  { http.send(404, "text/plain", "not found"); }

static void wifi_start(void)
{
    net_init(WIFI_AP_SSID, WIFI_AP_PASSWORD);
    Serial.print(F("[wifi] AP ")); Serial.print(WIFI_AP_SSID);
    Serial.print(F(" up @ http://")); Serial.print(net_ap_ip()); Serial.println(F("/"));
    cfg_t *c = cfg_get();
    if (c->wifi_sta_enabled && c->wifi_ssid[0] != '\0') {
        Serial.print(F("[wifi] joining STA: ")); Serial.println(c->wifi_ssid);
        net_sta_apply(c->wifi_ssid, c->wifi_pass, true);
    }
}

static void mdns_start(void)
{
    if (MDNS.begin("atovio")) {
        MDNS.addService("http", "tcp", WIFI_HTTP_PORT);
        Serial.println(F("[mdns] http://atovio.local/"));
    } else {
        Serial.println(F("[mdns] FAILED"));
    }
}

static void ota_start(void)
{
    ArduinoOTA.setHostname("atovio");
    ArduinoOTA.onStart([]() {
        Serial.println(F("[ota] start"));
    });
    ArduinoOTA.onEnd([]() {
        Serial.println(F("\n[ota] done"));
    });
    ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
        Serial.printf("[ota] %u%%\r", (p * 100u) / t);
    });
    ArduinoOTA.onError([](ota_error_t e) {
        Serial.printf("[ota] error %u\n", e);
    });
    ArduinoOTA.begin();
    Serial.println(F("[ota] ready"));
}

static void http_start(void)
{
    http.on("/",                 HTTP_GET,  handle_root);
    http.on("/data",             HTTP_GET,  handle_data);
    http.on("/config",           HTTP_GET,  handle_config_get);
    http.on("/config",           HTTP_POST, handle_config_post);
    http.on("/mode",             HTTP_POST, handle_mode_post);
    http.on("/wear/force",       HTTP_POST, handle_wear_force_post);
    http.on("/steps/reset",      HTTP_POST, handle_steps_reset);
    http.on("/resp/reset",       HTTP_POST, handle_resp_reset);
    http.on("/accel/calibrate",  HTTP_POST, handle_accel_calibrate);
    http.on("/battery",          HTTP_POST, handle_battery_post);
    http.on("/api/type10",       HTTP_GET,  handle_api_type10);
    http.on("/log.csv",          HTTP_GET,  handle_log_csv);
    http.on("/motionlog",        HTTP_GET,  handle_motionlog);
    http.on("/find",             HTTP_POST, handle_find_post);
    http.on("/find/stop",        HTTP_POST, handle_find_stop);
    http.on("/wifi",             HTTP_GET,  handle_wifi_get);
    http.on("/wifi",             HTTP_POST, handle_wifi_post);
    http.on("/ble/start",        HTTP_POST, handle_ble_start);
    http.on("/ble/stop",         HTTP_POST, handle_ble_stop);
    http.on("/system",           HTTP_GET,  handle_system);
    http.on("/settings/export",  HTTP_GET,  handle_settings_export);
    http.on("/settings/import",  HTTP_POST, handle_settings_import);
    http.onNotFound(handle_404);
    http.begin();
    Serial.print(F("[http] listening on :")); Serial.println(WIFI_HTTP_PORT);
}

void setup(void)
{
    pinMode(PIN_LED_STATUS, OUTPUT);
    digitalWrite(PIN_LED_STATUS, LOW);

    Serial.begin(SERIAL_BAUD);
    delay(200);
    Serial.println(F("\n[bench] atovio-accel-bench-esp32 v2 boot"));

    if (!accel_init()) {
        Serial.println(F("[bench] FATAL: MPU6050 not detected"));
        while (1) {
            digitalWrite(PIN_LED_STATUS, HIGH); delay(100);
            digitalWrite(PIN_LED_STATUS, LOW);  delay(100);
        }
    }

    cfg_load();
    cfg_apply();

    wear_init();
    resp_init();
    steps_init();
    samplelog_init();
    motionlog_init();
    find_init();
    mode_set(APP_MODE_NORMAL);
    battsim_set(80, false, false);

    Serial.println(F("[bench] modules ready"));

    wifi_start();
    mdns_start();
    ota_start();
    ble_find_init();
    http_start();

    Serial.println(F("# t=ms w=wear(0|1) x/y/z=mg mag bpm steps mode ion"));
}

static int16_t int_abs16(int16_t v) { return v < 0 ? (int16_t)-v : v; }

static int16_t mag_mg(int16_t x, int16_t y, int16_t z)
{
    int32_t sq = (int32_t)x * x + (int32_t)y * y + (int32_t)z * z;
    return (int16_t)sqrt((double)sq);
}

void loop(void)
{
    uint32_t now = millis();

    ArduinoOTA.handle();
    http.handleClient();
    net_loop_tick();
    ble_find_loop_tick();

    if (accel_motion_flag_take()) {
        wear_notify_motion();
        motionlog_push(now);
    }

    if (now - last_sample_ms >= SAMPLE_PERIOD_MS) {
        last_sample_ms = now;
        accel_read_mg(&last_x_mg, &last_y_mg, &last_z_mg);
        last_mag_mg = mag_mg(last_x_mg, last_y_mg, last_z_mg);

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

        sample_row_t r;
        r.t_ms     = now;
        r.x_mg     = last_x_mg;
        r.y_mg     = last_y_mg;
        r.z_mg     = last_z_mg;
        r.mag_mg   = last_mag_mg;
        r.bpm      = resp_result_ready() ? resp_get_bpm() : 0;
        r.steps    = (uint16_t)sc;
        r.wear     = wear_get_state() == WEAR_STATE_ON_BODY ? 1 : 0;
        r.mode     = (uint8_t)mode_get();
        r.ionizer  = ionizer_state() ? 1 : 0;
        samplelog_push(&r);

        Serial.printf("t=%lu w=%u x=%d y=%d z=%d mag=%d bpm=%u st=%lu mode=%s ion=%u\n",
                      (unsigned long)now, r.wear, r.x_mg, r.y_mg, r.z_mg, r.mag_mg,
                      r.bpm, (unsigned long)sc, mode_str(mode_get()), r.ionizer);
    }
}
