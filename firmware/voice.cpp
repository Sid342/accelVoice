#include "voice.h"
#include "app_config.h"
#include <Arduino.h>
#include <SPIFFS.h>
#include <driver/i2s.h>
#include <math.h>
#include <string.h>

#define I2S_PORT             I2S_NUM_0
#define I2S_DMA_BUF_COUNT    8
#define I2S_DMA_BUF_LEN      256          /* samples per DMA buffer */
#define I2S_READ_SAMPLES     256          /* per-call read size (32-bit samples) */

static voice_state_t s_state              = VOICE_IDLE;
static uint32_t      s_start_ms           = 0;
static uint32_t      s_last_voice_ms      = 0;   /* last time RMS > threshold */
static uint32_t      s_timeout_ms         = VOICE_TIMEOUT_MS;
static int16_t       s_vad_threshold      = VOICE_VAD_RMS_THRESH;
static uint32_t      s_silence_ms         = VOICE_VAD_SILENCE_MS;
static uint8_t       s_gain_shift         = VOICE_GAIN_SHIFT;
static bool          s_vad_enabled        = true;

static int16_t       s_current_rms        = 0;
static uint32_t      s_bytes_captured     = 0;
static char          s_last_reason[24]    = "(none)";

/* DSP — DC blocker (single-pole HPF) state. y[n] = α(y[n-1] + x[n] - x[n-1])
 * α ≈ 0.995 in Q15 → cutoff ~25 Hz @ 16 kHz fs (kills DC + sub-audible rumble). */
static bool    s_dc_blocker_en  = true;
static int32_t s_dc_prev_in     = 0;
static int32_t s_dc_prev_out    = 0;
#define DC_BLOCKER_ALPHA_Q15  32604   /* 0.99497 — gentle HPF */

/* Noise gate: clamp absolute samples below this threshold to 0. 0 = disabled.
 * Different from VAD — VAD only stops recording. Gate cleans up silence in
 * the recorded WAV.                                                           */
static int16_t s_noise_gate     = 0;

static File          s_file;
static bool          s_i2s_installed      = false;
static int32_t       s_raw[I2S_READ_SAMPLES];
static int16_t       s_pcm[I2S_READ_SAMPLES];

/* WAV header writer — minimal RIFF/WAVE/fmt /data layout. */
static void write_wav_header(File &f, uint32_t sample_rate, uint32_t data_bytes)
{
    uint8_t hdr[44];
    uint32_t file_size = data_bytes + 36;
    uint16_t channels = 1;
    uint16_t bits = 16;
    uint32_t byte_rate = sample_rate * channels * (bits / 8);
    uint16_t block_align = channels * (bits / 8);

    memcpy(hdr,      "RIFF", 4);
    memcpy(hdr + 4,  &file_size, 4);
    memcpy(hdr + 8,  "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    uint32_t fmt_size = 16; memcpy(hdr + 16, &fmt_size, 4);
    uint16_t fmt = 1;       memcpy(hdr + 20, &fmt, 2);
    memcpy(hdr + 22, &channels, 2);
    memcpy(hdr + 24, &sample_rate, 4);
    memcpy(hdr + 28, &byte_rate, 4);
    memcpy(hdr + 32, &block_align, 2);
    memcpy(hdr + 34, &bits, 2);
    memcpy(hdr + 36, "data", 4);
    memcpy(hdr + 40, &data_bytes, 4);

    f.seek(0);
    f.write(hdr, 44);
}

static bool i2s_setup(void)
{
    if (s_i2s_installed) return true;

    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    cfg.sample_rate          = VOICE_SAMPLE_RATE_HZ;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = I2S_DMA_BUF_COUNT;
    cfg.dma_buf_len          = I2S_DMA_BUF_LEN;
    cfg.use_apll             = false;
    cfg.tx_desc_auto_clear   = false;
    cfg.fixed_mclk           = 0;

    i2s_pin_config_t pins = {};
    pins.bck_io_num   = PIN_I2S_SCK;
    pins.ws_io_num    = PIN_I2S_WS;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num  = PIN_I2S_SD;

    if (i2s_driver_install(I2S_PORT, &cfg, 0, NULL) != ESP_OK) {
        Serial.println(F("[voice] i2s_driver_install FAILED"));
        return false;
    }
    if (i2s_set_pin(I2S_PORT, &pins) != ESP_OK) {
        Serial.println(F("[voice] i2s_set_pin FAILED"));
        return false;
    }
    s_i2s_installed = true;
    return true;
}

static void i2s_teardown(void)
{
    if (!s_i2s_installed) return;
    i2s_driver_uninstall(I2S_PORT);
    s_i2s_installed = false;
}

void voice_init(void)
{
    if (!SPIFFS.begin(true /* format on fail */)) {
        Serial.println(F("[voice] SPIFFS mount FAILED"));
    } else {
        Serial.printf("[voice] SPIFFS ok — %lu/%lu bytes used\n",
                      (unsigned long)SPIFFS.usedBytes(),
                      (unsigned long)SPIFFS.totalBytes());
    }
    s_state = VOICE_IDLE;
}

bool voice_start(uint32_t timeout_ms, bool vad_enabled)
{
    if (s_state != VOICE_IDLE) return false;

    if (!i2s_setup()) return false;

    /* Drain any stale DMA contents. */
    size_t junk;
    i2s_read(I2S_PORT, s_raw, sizeof(s_raw), &junk, 0);

    SPIFFS.remove(VOICE_WAV_PATH);
    s_file = SPIFFS.open(VOICE_WAV_PATH, FILE_WRITE);
    if (!s_file) {
        Serial.println(F("[voice] file open FAILED"));
        i2s_teardown();
        return false;
    }
    write_wav_header(s_file, VOICE_SAMPLE_RATE_HZ, 0);

    if (timeout_ms > 0) s_timeout_ms = timeout_ms;
    s_vad_enabled    = vad_enabled;
    s_start_ms       = millis();
    s_last_voice_ms  = s_start_ms;
    s_bytes_captured = 0;
    s_current_rms    = 0;
    s_dc_prev_in     = 0;
    s_dc_prev_out    = 0;
    s_state          = VOICE_STREAMING;
    Serial.printf("[voice] START — timeout=%lums vad=%d\n",
                  (unsigned long)s_timeout_ms, s_vad_enabled);
    return true;
}

static void finalize_file(void)
{
    if (!s_file) return;
    uint32_t data_bytes = s_bytes_captured;
    write_wav_header(s_file, VOICE_SAMPLE_RATE_HZ, data_bytes);
    s_file.close();
}

void voice_stop(const char *reason)
{
    if (s_state == VOICE_IDLE) return;
    s_state = VOICE_STOPPING;
    strncpy(s_last_reason, reason ? reason : "stop", sizeof(s_last_reason) - 1);
    s_last_reason[sizeof(s_last_reason) - 1] = '\0';
    finalize_file();
    i2s_teardown();
    s_state = VOICE_IDLE;
    Serial.printf("[voice] STOP (%s) — %lu bytes in %lu ms\n",
                  s_last_reason, (unsigned long)s_bytes_captured,
                  (unsigned long)(millis() - s_start_ms));
}

void voice_loop_tick(void)
{
    if (s_state != VOICE_STREAMING) return;

    /* Read one DMA chunk. Non-blocking with short timeout so we don't stall
     * the rest of the bench rig (HTTP, MPU sample loop, etc.).               */
    size_t bytes_read = 0;
    if (i2s_read(I2S_PORT, s_raw, sizeof(s_raw), &bytes_read,
                 pdMS_TO_TICKS(20)) != ESP_OK || bytes_read == 0) {
        return;
    }
    size_t samples = bytes_read / sizeof(int32_t);

    /* Convert 32-bit raw (24-bit MSB-aligned in upper bits) → int16 with
     * gain, optional DC blocker (HPF), optional noise gate.                   */
    int64_t sumsq = 0;
    for (size_t i = 0; i < samples; i++) {
        int32_t v = s_raw[i] >> s_gain_shift;

        if (s_dc_blocker_en) {
            int32_t y = v - s_dc_prev_in
                     + (int32_t)(((int64_t)s_dc_prev_out * DC_BLOCKER_ALPHA_Q15) >> 15);
            s_dc_prev_in  = v;
            s_dc_prev_out = y;
            v = y;
        }

        if (s_noise_gate > 0) {
            int32_t a = v < 0 ? -v : v;
            if (a < s_noise_gate) v = 0;
        }

        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        s_pcm[i] = (int16_t)v;
        sumsq += (int64_t)v * v;
    }
    s_current_rms = (int16_t)sqrt((double)(sumsq / (int64_t)samples));

    /* Append PCM to WAV. */
    if (s_file) {
        size_t bytes_to_write = samples * sizeof(int16_t);
        if (s_bytes_captured + bytes_to_write > VOICE_MAX_FILE_BYTES) {
            voice_stop("max_size");
            return;
        }
        s_file.write((uint8_t *)s_pcm, bytes_to_write);
        s_bytes_captured += bytes_to_write;
    }

    /* VAD: track last time RMS exceeded threshold; if silence too long, stop. */
    uint32_t now = millis();
    if (s_current_rms >= s_vad_threshold) {
        s_last_voice_ms = now;
    } else if (s_vad_enabled && (now - s_last_voice_ms >= s_silence_ms)
                              && (now - s_start_ms >= 500)) {
        voice_stop("silence");
        return;
    }

    /* Hard timeout. */
    if (now - s_start_ms >= s_timeout_ms) {
        voice_stop("timeout");
        return;
    }
}

voice_state_t voice_state(void)              { return s_state; }
const char   *voice_state_str(void)
{
    switch (s_state) {
        case VOICE_IDLE:      return "idle";
        case VOICE_STREAMING: return "streaming";
        case VOICE_STOPPING:  return "stopping";
    }
    return "?";
}
const char *voice_last_stop_reason(void)     { return s_last_reason; }
uint32_t    voice_elapsed_ms(void)
{
    if (s_state != VOICE_STREAMING) return 0;
    return millis() - s_start_ms;
}
uint32_t    voice_bytes_captured(void)       { return s_bytes_captured; }
int16_t     voice_current_rms(void)          { return s_current_rms; }

size_t voice_last_wav_size(void)
{
    if (!SPIFFS.exists(VOICE_WAV_PATH)) return 0;
    File f = SPIFFS.open(VOICE_WAV_PATH, FILE_READ);
    if (!f) return 0;
    size_t sz = f.size();
    f.close();
    return sz;
}

void    voice_set_vad_threshold(int16_t t)   { s_vad_threshold = t; }
void    voice_set_silence_ms(uint32_t m)     { s_silence_ms = m; }
void    voice_set_timeout_ms(uint32_t m)     { s_timeout_ms = m; }
void    voice_set_gain_shift(uint8_t s)
{
    if (s < 8)  s = 8;
    if (s > 18) s = 18;
    s_gain_shift = s;
}
int16_t  voice_get_vad_threshold(void)       { return s_vad_threshold; }
uint32_t voice_get_silence_ms(void)          { return s_silence_ms; }
uint32_t voice_get_timeout_ms(void)          { return s_timeout_ms; }
uint8_t  voice_get_gain_shift(void)          { return s_gain_shift; }

void    voice_set_dc_blocker(bool en)        { s_dc_blocker_en = en; }
bool    voice_get_dc_blocker(void)           { return s_dc_blocker_en; }
void    voice_set_noise_gate(int16_t t)
{
    if (t < 0)    t = 0;
    if (t > 5000) t = 5000;
    s_noise_gate = t;
}
int16_t voice_get_noise_gate(void)           { return s_noise_gate; }
