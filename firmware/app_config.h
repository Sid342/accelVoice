#pragma once

/* ── Hardware pins (ESP32 DevKit V1) ──────────────────────────────────────── */
#define PIN_I2C_SDA       25    /* MPU6050 SDA → GPIO 25 (D25)                */
#define PIN_I2C_SCL       26    /* MPU6050 SCL → GPIO 26 (D26)                */
#define PIN_MPU_INT       27    /* MPU6050 INT → GPIO 27 (D27)                */
#define PIN_LED_STATUS    2     /* Onboard blue LED on most ESP32 DevKit V1   */

/* ── Accelerometer / Wear detection ───────────────────────────────────────── */
#define ACCEL_ODR_HZ            25    /* samples/sec during normal operation    */
#define ACCEL_MOTION_THRESH_MG  50    /* mg — motion threshold for wear detect  */
#define ACCEL_STILL_SEC         5     /* seconds stationary → off-body suspect  */
#define ACCEL_OFFBODY_SEC       30    /* seconds off-body → adaptive output     */

/* MPU6050 motion detect threshold register: LSB = 2 mg (datasheet 4.6).
 * 50 mg / 2 = 25.                                                              */
#define MPU_MOT_THR_REG         (ACCEL_MOTION_THRESH_MG / 2)
#define MPU_MOT_DUR_REG         1     /* ms — minimum duration above threshold  */

/* ── Respiration sensing ──────────────────────────────────────────────────── */
#define RESP_SAMPLE_HZ          25    /* must match ACCEL_ODR_HZ                */
#define RESP_WINDOW_SEC         10    /* analysis window (250 samples @ 25 Hz)  */
#define RESP_BPM_MIN            8     /* < 8 BPM = not breathing                */
#define RESP_BPM_MAX            30    /* > 30 BPM = noise                       */

/* ── Step counter ─────────────────────────────────────────────────────────── */
#define STEP_PEAK_THRESH_MG     200   /* mg above gravity baseline              */
#define STEP_MIN_INTERVAL_MS    300   /* ~3.3 Hz max cadence                    */
#define STEP_MAX_INTERVAL_MS    1500  /* ~0.66 Hz min cadence                   */

/* ── Serial / Telemetry ───────────────────────────────────────────────────── */
#define SERIAL_BAUD             115200
#define TELEMETRY_PERIOD_MS     100   /* 10 Hz CSV-style live stream            */

/* ── Status LED ───────────────────────────────────────────────────────────── */
#define STEP_LED_PULSE_MS       50    /* on-body LED dips LOW this long per step */

/* ── WiFi (AP mode by default — no router needed) ─────────────────────────── */
#define WIFI_AP_SSID            "atovio-bench"
#define WIFI_AP_PASSWORD        "atovio1234"     /* WPA2, ≥ 8 chars            */
#define WIFI_HTTP_PORT          80

/* ── Voice / I²S microphone (INMP441 default) ────────────────────────────── */
#define PIN_I2S_WS              32     /* WS / LRCLK → INMP441 WS            */
#define PIN_I2S_SCK             33     /* SCK / BCLK → INMP441 SCK           */
#define PIN_I2S_SD              35     /* SD / DATA  → INMP441 SD (input only) */

#define VOICE_SAMPLE_RATE_HZ    16000  /* 16 kHz wideband; 8000 also supported */
#define VOICE_BITS_PER_SAMPLE   16     /* PCM stored as 16-bit, raw is 24/32 */
#define VOICE_GAIN_SHIFT        14     /* INMP441 32-bit raw → int16 via >>14 (≈4× gain) */

#define VOICE_TIMEOUT_MS        10000  /* hard stop after this long          */
#define VOICE_VAD_RMS_THRESH    150    /* below this RMS for silence_ms → stop */
#define VOICE_VAD_SILENCE_MS    1500   /* sustained silence to trigger stop  */
#define VOICE_MAX_FILE_BYTES    900000 /* leave room in 1 MB SPIFFS partition */
#define VOICE_WAV_PATH          "/last.wav"

/* ── Cloud STT (Deepgram) ─────────────────────────────────────────────────── */
#define STT_DEFAULT_MODEL       "nova-2"   /* nova-3 also works if account allows */
#define STT_HTTP_TIMEOUT_MS     30000      /* read timeout for /v1/listen        */
