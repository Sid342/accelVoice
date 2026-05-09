#pragma once

/* ── Hardware pins (ESP32 DevKit V1) ──────────────────────────────────────── */
#define PIN_I2C_SDA       25    /* MPU6050 SDA → GPIO 25 (D25)                */
#define PIN_I2C_SCL       26    /* MPU6050 SCL → GPIO 26 (D26)                */
#define PIN_MPU_INT       27    /* MPU6050 INT → GPIO 27 (D27)                */
#define PIN_LED_STATUS    2     /* Onboard blue LED on most ESP32 DevKit V1   */

/* ── Accelerometer / Wear detection ───────────────────────────────────────── */
/* ACCEL_ODR_HZ is the legacy 25-Hz rate kept for resp/steps/wear pipelines.
 * ACCEL_FAST_ODR_HZ is the new sample-loop rate; cough+fall consume every
 * tick, while resp/steps/wear are decimated 1-in-(FAST/ODR). */
#define ACCEL_ODR_HZ            25    /* samples/sec for resp+steps+wear feed   */
#define ACCEL_FAST_ODR_HZ       100   /* sample-loop rate (drives cough + fall) */
#define ACCEL_MOTION_THRESH_MG  50    /* mg — motion threshold for wear detect  */
#define ACCEL_STILL_SEC         5     /* seconds stationary → off-body suspect  */
#define ACCEL_OFFBODY_SEC       30    /* seconds off-body → adaptive output     */

/* MPU6050 motion detect threshold register: LSB = 2 mg (datasheet 4.6).
 * 50 mg / 2 = 25.                                                              */
#define MPU_MOT_THR_REG         (ACCEL_MOTION_THRESH_MG / 2)
#define MPU_MOT_DUR_REG         1     /* ms — minimum duration above threshold  */

/* ── Wear v2 (variance + gravity vector) ─────────────────────────────────── */
#define WEAR_VAR_THRESH_MG       5    /* MAD of magnitude over 1 s; below = still */
#define WEAR_GRAV_DIFF_THRESH_MG 20   /* 5-sec gravity diff ≈ sin(θ)·1g; 20 mg ≈ 1.1° */
#define WEAR_GRAV_RING_SEC       5    /* gravity-vector age window in seconds   */

/* ── Respiration sensing ──────────────────────────────────────────────────── */
#define RESP_SAMPLE_HZ          25    /* must match ACCEL_ODR_HZ                */
#define RESP_WINDOW_SEC         10    /* analysis window (250 samples @ 25 Hz)  */
#define RESP_BPM_MIN            8     /* < 8 BPM = not breathing                */
#define RESP_BPM_MAX            30    /* > 30 BPM = noise                       */

/* ── Respiration v2 (online peak detector) ───────────────────────────────── */
#define RESP_MIN_INTERVAL_MS    1500  /* min ms between peaks (40 BPM ceiling)  */
#define RESP_IIR_ALPHA_Q15      1638  /* mean tracker α ≈ 0.05 (≈ 4 s eff window) */
#define RESP_MIN_AMPLITUDE_MG   10    /* peak-to-peak amplitude gate (filters noise) */
#define RESP_AXIS_DEFAULT       3     /* 0=Z 1=X 2=Y 3=PCA-lite (v3 default)     */
#define RESP_EVENT_RING_SIZE    64
#define RESP_WAVE_RING_SIZE     300   /* 12 s @ 25 Hz of (axis, mean) pairs     */

/* ── Respiration v3 (median + Butterworth + PCA-lite + autocorr) ─────────── */
#define RESP_MEDIAN_LEN_DEFAULT     5   /* 1/3/5/7/9 (1 = off)                  */
#define RESP_MEDIAN_RING_LEN        9   /* must hold the largest allowed N      */
#define RESP_BP_LOW_HZ_X10_DEFAULT  1   /* 0.1 Hz HPF (0 = off)                 */
#define RESP_BP_HIGH_HZ_X10_DEFAULT 8   /* 0.8 Hz LPF (0 = off)                 */
#define RESP_HYSTERESIS_MG_DEFAULT  0   /* 0 = legacy zero-cross rearm          */
#define RESP_ADAPTIVE_BP_DEFAULT    false
#define RESP_AUTOCORR_WIN_SEC_DEF   30  /* 10..60 s; 30 s × 25 Hz = 750 samples */
#define RESP_AUTOCORR_PERIOD_MS     5000  /* run every 5 s                      */
#define RESP_AUTOCORR_RING_MAX_SEC  60    /* worst-case ring length             */
#define RESP_PCA_TAU_ALPHA_Q15      328   /* α ≈ 0.01 → ~4 s effective window   */
#define RESP_ACTIVITY_LO_MG2        50    /* sitting/resting                    */
#define RESP_ACTIVITY_HI_MG2        500   /* running threshold                  */

/* ── Step counter ─────────────────────────────────────────────────────────── */
#define STEP_PEAK_THRESH_MG     200   /* mg above gravity baseline              */
#define STEP_MIN_INTERVAL_MS    300   /* ~3.3 Hz max cadence                    */
#define STEP_MAX_INTERVAL_MS    1500  /* ~0.66 Hz min cadence                   */

/* ── Steps v2 ─────────────────────────────────────────────────────────────── */
#define STEP_AMP_WINDOW_MS      2000  /* sliding window for adaptive threshold  */
#define STEP_ADAPTIVE_FRAC_PCT  50    /* effective_thresh = max(MIN, FRAC% × amp) */
#define STEP_ADAPTIVE_MIN_MG    80    /* floor for adaptive threshold           */
#define STEP_AXIS_DEFAULT       0     /* 0=mag 1=x 2=y 3=z                      */
/* Bandpass IIR coefficients @ fs=25 Hz (single-pole HPF + LPF cascade). */
#define STEP_HPF_ALPHA_Q15      28890 /* exp(-2π·0.5/25) ≈ 0.8814               */
#define STEP_LPF_ALPHA_Q15      15422 /* exp(-2π·3.0/25) ≈ 0.4706               */
#define STEP_EVENT_RING_SIZE    64
#define STEP_GT_RING_SIZE       128
#define STEP_AMP_BUF_MAX        128   /* covers up to 5120 ms @ 25 Hz           */

/* ── Slouch detection (gravity pitch vs baseline) ─────────────────────────── */
#define SLOUCH_BASELINE_DEG_X10_DEFAULT  0    /* deg×10; assumes Z-up neutral  */
#define SLOUCH_THRESH_DEG_DEFAULT        15   /* deg deviation gate            */
#define SLOUCH_SUSTAIN_SEC_DEFAULT       5    /* seconds above thr → SLOUCHING */
#define SLOUCH_RECOVER_SEC               2    /* seconds below thr → UPRIGHT   */
#define SLOUCH_GRAV_ALPHA_Q15            164  /* α≈0.005 → ~8 s @ 25 Hz        */
#define SLOUCH_EVENT_RING_SIZE           32   /* recent slouch sessions        */

/* ── Cough detection (5–15 Hz envelope cluster) ───────────────────────────── */
#define COUGH_THRESH_MG_DEFAULT     80    /* envelope rising threshold, mg     */
#define COUGH_CLUSTER_MS_DEFAULT    800   /* peaks within window form cluster  */
#define COUGH_MIN_PEAKS_DEFAULT     3     /* peaks needed to fire event        */
#define COUGH_LOCKOUT_MS            1500  /* after fire, ignore further peaks  */
#define COUGH_BP_LOW_HZ             5     /* band-pass low cutoff (Hz)         */
#define COUGH_BP_HIGH_HZ            15    /* band-pass high cutoff (Hz)        */
#define COUGH_PEAK_MIN_INTERVAL_MS  100   /* min spacing between peaks         */
#define COUGH_EVENT_RING_SIZE       32    /* recent events                     */
#define COUGH_GT_RING_SIZE          64    /* ground-truth taps                 */
#define COUGH_RATE_WINDOW_MS        60000 /* rolling per-min window            */
#define COUGH_WAVE_RING_SIZE        100   /* /cough/wave ring (10 s @ 10 Hz)   */
#define COUGH_WAVE_DECIM            10    /* push 1-in-N envelope samples      */
#define COUGH_AC_ALPHA_Q15          655   /* slow mean IIR α≈0.02 (~2 s @100Hz)*/
#define COUGH_ENV_ALPHA_Q15         3277  /* envelope LPF α≈0.1 (~100 ms)      */

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
