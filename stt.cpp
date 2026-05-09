#include "stt.h"
#include "app_config.h"
#include "voice.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <string.h>

static Preferences s_prefs;
static char        s_key[96]   = {0};
static char        s_model[32] = STT_DEFAULT_MODEL;
static char        s_hint[48]  = "not set";

static stt_state_t s_state             = STT_IDLE;
static char        s_transcript[1024]  = {0};
static char        s_err[160]          = {0};
static int         s_http_status       = 0;
static int         s_rc                = 0;
static uint32_t    s_dur_ms            = 0;
static size_t      s_wav_bytes         = 0;

static void rebuild_hint(void)
{
    size_t n = strlen(s_key);
    if (n == 0) { strcpy(s_hint, "not set"); return; }
    if (n <= 4) { snprintf(s_hint, sizeof(s_hint), "set (%u chars)", (unsigned)n); return; }
    snprintf(s_hint, sizeof(s_hint), "set (****%s)", s_key + n - 4);
}

void stt_init(void)
{
    s_prefs.begin("stt", false);
    s_prefs.getString("key",   s_key,   sizeof(s_key));
    s_prefs.getString("model", s_model, sizeof(s_model));
    s_prefs.end();
    if (s_model[0] == 0) strcpy(s_model, STT_DEFAULT_MODEL);
    rebuild_hint();
}

bool stt_set_key(const char *key)
{
    s_prefs.begin("stt", false);
    if (!key || !key[0]) {
        s_prefs.remove("key");
        s_key[0] = 0;
    } else {
        strncpy(s_key, key, sizeof(s_key) - 1);
        s_key[sizeof(s_key) - 1] = 0;
        s_prefs.putString("key", s_key);
    }
    s_prefs.end();
    rebuild_hint();
    return true;
}

bool stt_has_key(void)            { return s_key[0] != 0; }
const char *stt_key_hint(void)    { return s_hint; }

bool stt_set_model(const char *m)
{
    if (!m || !m[0]) return false;
    strncpy(s_model, m, sizeof(s_model) - 1);
    s_model[sizeof(s_model) - 1] = 0;
    s_prefs.begin("stt", false);
    s_prefs.putString("model", s_model);
    s_prefs.end();
    return true;
}
const char *stt_get_model(void)   { return s_model; }

/* Pull "transcript":"…" out of the response, JSON-unescaping minimally.
 * Returns true if a transcript field was found (even if empty). */
static bool extract_transcript(const String &resp, char *out, size_t max)
{
    int p = resp.indexOf("\"transcript\":\"");
    if (p < 0) return false;
    p += 14;
    size_t o = 0;
    while (p < (int)resp.length() && o + 1 < max) {
        char c = resp.charAt(p++);
        if (c == '\\' && p < (int)resp.length()) {
            char n = resp.charAt(p++);
            switch (n) {
                case '"':  out[o++] = '"';  break;
                case '\\': out[o++] = '\\'; break;
                case '/':  out[o++] = '/';  break;
                case 'n':  out[o++] = '\n'; break;
                case 't':  out[o++] = '\t'; break;
                case 'r':  out[o++] = '\r'; break;
                case 'b':  out[o++] = '\b'; break;
                case 'f':  out[o++] = '\f'; break;
                case 'u':
                    if (p + 4 <= (int)resp.length()) p += 4;
                    out[o++] = '?';
                    break;
                default:   out[o++] = n;   break;
            }
        } else if (c == '"') {
            break;
        } else {
            out[o++] = c;
        }
    }
    out[o] = 0;
    return true;
}

int stt_transcribe_last_wav(void)
{
    s_state         = STT_RUNNING;
    s_transcript[0] = 0;
    s_err[0]        = 0;
    s_http_status   = 0;
    s_wav_bytes     = 0;
    uint32_t t0     = millis();

    if (!stt_has_key()) {
        strcpy(s_err, "no api key set");
        s_rc = -1; s_state = STT_ERROR; s_dur_ms = millis() - t0; return s_rc;
    }
    if (WiFi.status() != WL_CONNECTED) {
        strcpy(s_err, "wifi STA not connected (need internet)");
        s_rc = -2; s_state = STT_ERROR; s_dur_ms = millis() - t0; return s_rc;
    }
    if (voice_state() != VOICE_IDLE) {
        strcpy(s_err, "voice still active; stop recording first");
        s_rc = -7; s_state = STT_ERROR; s_dur_ms = millis() - t0; return s_rc;
    }
    if (!SPIFFS.exists(VOICE_WAV_PATH)) {
        strcpy(s_err, "no recording on flash");
        s_rc = -3; s_state = STT_ERROR; s_dur_ms = millis() - t0; return s_rc;
    }
    File f = SPIFFS.open(VOICE_WAV_PATH, "r");
    if (!f) {
        strcpy(s_err, "cannot open wav");
        s_rc = -3; s_state = STT_ERROR; s_dur_ms = millis() - t0; return s_rc;
    }
    s_wav_bytes = f.size();
    if (s_wav_bytes < 100) {
        f.close();
        snprintf(s_err, sizeof(s_err), "wav too small (%u B)", (unsigned)s_wav_bytes);
        s_rc = -3; s_state = STT_ERROR; s_dur_ms = millis() - t0; return s_rc;
    }

    WiFiClientSecure client;
    client.setInsecure();   /* bench: skip cert verification */
    HTTPClient http;
    char url[192];
    snprintf(url, sizeof(url),
             "https://api.deepgram.com/v1/listen?model=%s&smart_format=true&punctuate=true",
             s_model);
    if (!http.begin(client, url)) {
        f.close();
        strcpy(s_err, "http.begin failed");
        s_rc = -4; s_state = STT_ERROR; s_dur_ms = millis() - t0; return s_rc;
    }
    char auth[128];
    snprintf(auth, sizeof(auth), "Token %s", s_key);
    http.addHeader("Authorization", auth);
    http.addHeader("Content-Type", "audio/wav");
    http.setConnectTimeout(15000);
    http.setTimeout(STT_HTTP_TIMEOUT_MS);

    int code = http.sendRequest("POST", &f, s_wav_bytes);
    f.close();
    s_http_status = code;

    if (code <= 0) {
        snprintf(s_err, sizeof(s_err), "http err: %s",
                 HTTPClient::errorToString(code).c_str());
        http.end();
        s_rc = -5; s_state = STT_ERROR; s_dur_ms = millis() - t0; return s_rc;
    }
    String body = http.getString();
    http.end();

    if (code < 200 || code >= 300) {
        snprintf(s_err, sizeof(s_err), "http %d: %.110s", code, body.c_str());
        s_rc = -5; s_state = STT_ERROR; s_dur_ms = millis() - t0; return s_rc;
    }
    if (!extract_transcript(body, s_transcript, sizeof(s_transcript))) {
        snprintf(s_err, sizeof(s_err), "no transcript field (resp %u B)", (unsigned)body.length());
        s_rc = -6; s_state = STT_ERROR; s_dur_ms = millis() - t0; return s_rc;
    }

    s_rc = 0; s_state = STT_DONE;
    s_dur_ms = millis() - t0;
    return 0;
}

stt_state_t stt_state(void)              { return s_state; }
const char *stt_last_transcript(void)    { return s_transcript; }
const char *stt_last_err(void)           { return s_err; }
int         stt_last_http_status(void)   { return s_http_status; }
int         stt_last_rc(void)            { return s_rc; }
uint32_t    stt_last_duration_ms(void)   { return s_dur_ms; }
size_t      stt_last_wav_bytes(void)     { return s_wav_bytes; }
