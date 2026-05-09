# app.md — web UI + HTTP API

The bench rig embeds its own web app at `http://192.168.4.1` (AP) or
`http://atovio.local` (mDNS). All control is over plain HTTP — no auth, no
TLS — because this device only ever lives on the bench network.

## Tabs

| Tab | What it shows |
|---|---|
| **Live** | wear state, accel x/y/z + magnitude, BPM, steps, ionizer state, battery, multi-axis plot |
| **Tune** | sliders for every wear / step / respiration threshold; calibrate accel; reset steps & resp; force wear |
| **System** | uptime, free heap, SPIFFS, mode override (OFF/NORMAL/TURBO), battery sim, settings export/import |
| **Network** | AP info, STA join form (SSID/pass), mDNS hostname, OTA status |
| **Find** | "find my device" 30 s LED strobe, BLE advertise on-demand for phone find |
| **Voice** | record, VAD, gain/timeout/silence sliders, DC blocker, noise gate, WAV download/play, **Cloud STT** |

## HTTP endpoints

### Live + config

| Method | Path | Body | Returns |
|---|---|---|---|
| GET | `/` | — | embedded HTML UI |
| GET | `/data` | — | full live snapshot JSON (see schema below) |
| GET | `/data2` | — | extended diagnostics for v2 features (wear / resp / steps) |
| GET | `/resp/events` | `?since=<ms>` | `{now,total,events:[{t,amp,int}…]}` |
| GET | `/resp/wave` | `?n=<N>` | `{n,z:[…],m:[…]}` — paired Z + running mean |
| GET | `/steps/events` | `?since=<ms>` | `{now,total,signal,thr,events:[…]}` |
| POST | `/steps/groundtruth` | `{}` | record a tap; returns `{count}` |
| GET | `/steps/groundtruth` | — | `{count}` |
| POST | `/steps/groundtruth/clear` | `{}` | empty the GT ring |
| GET | `/steps/eval` | `?window_sec=&tol_ms=` | `{detected,groundtruth,matched,precision_pct,recall_pct}` |
| GET | `/config` | — | tunable thresholds JSON |
| POST | `/config` | tunables JSON | applies + persists, returns same shape |
| POST | `/mode` | `{"mode":"off|normal|turbo"}` | new mode |
| POST | `/wear/force` | `{"force":"auto|on|off"}` | new force state |
| POST | `/steps/reset` | `{}` | `{"steps":0}` |
| POST | `/resp/reset` | `{}` | `{"reset":true}` |
| POST | `/accel/calibrate` | `{}` | new cal_x/y/z, persisted |
| POST | `/battery` | `{"pct":N,"charging":bool,"fault":bool}` | new battery state |
| GET | `/api/type10` | — | BLE Type 10 shape (vitals + state) |
| GET | `/log.csv` | — | rolling CSV of last N samples |
| GET | `/motionlog` | — | motion IRQ event timestamps |

### Find + BLE

| Method | Path | Body | Returns |
|---|---|---|---|
| POST | `/find` | `{"seconds":30}` | `{"active":true,"remaining_s":30}` |
| POST | `/find/stop` | `{}` | `{"active":false}` |
| POST | `/ble/start` | `{"minutes":5}` | `{"advertising":true,"remaining_s":300}` |
| POST | `/ble/stop` | `{}` | `{"advertising":false}` |

BLE init is **deferred** to the first `/ble/start` call — saves ~30 KB heap
during boot and avoids the BLE-vs-WiFi RF coexistence stall observed when both
init eagerly.

### WiFi + system

| Method | Path | Body | Returns |
|---|---|---|---|
| GET | `/wifi` | — | sta_status, ssid, rssi, ip |
| POST | `/wifi` | `{"ssid":"…","pass":"…","enabled":true}` | applies, returns status |
| GET | `/system` | — | uptime, heap, SPIFFS, partition |
| GET | `/settings/export` | — | full NVS JSON dump |
| POST | `/settings/import` | NVS JSON | restores |

### Voice

| Method | Path | Body | Returns |
|---|---|---|---|
| GET | `/voice/status` | — | state, RMS, bytes, last_reason, all tunables |
| POST | `/voice/start` | `{"timeout_ms":N,"vad":true}` | start record |
| POST | `/voice/stop` | `{}` | stop |
| POST | `/voice/config` | `{vad_threshold,silence_ms,timeout_ms,gain_shift,noise_gate,dc_blocker}` | applies, returns status |
| GET | `/voice/last.wav` | — | streams WAV file (`audio/wav`, attachment) |

### Cloud STT (Deepgram)

| Method | Path | Body | Returns |
|---|---|---|---|
| GET | `/stt/status` | — | state, key_hint, model, last transcript, last err |
| POST | `/stt/key` | `{"key":"…","model":"nova-2"}` | persists, returns status |
| POST | `/stt/run` | `{}` | **blocks 1–3 s**, returns full status with new transcript |

`POST /stt/run` is intentionally synchronous — the WebServer is single-threaded
anyway. The browser shows a "Transcribing…" state until the response lands.
Future streaming version flips this to WebSocket.

## JSON schemas

### `/data` (live snapshot)

```json
{
  "t": 12345,                 // millis
  "uptime_s": 12,
  "wear": "on|off",
  "wear_forced": "auto|on|off",
  "wear_remaining_s": 0,
  "x": -12, "y": 4, "z": 998, "mag": 1003,
  "bpm": 14, "bpm_valid": true, "bpm_raw": 14,
  "resp_progress": 250, "resp_window_len": 250,
  "steps": 42, "step_cadence_ms": 612, "steps_per_min": 98,
  "mode": "off|normal|turbo",
  "ionizer": true,
  "battery": {"pct":85,"charging":false,"fault":false,"state":"discharging"},
  "motion_irq_count": 17,
  "find":  {"active":false,"remaining_s":0},
  "ble":   {"advertising":false,"remaining_s":0},
  "net":   {"ap_ip":"192.168.4.1","sta_status":"…","sta_ip":"…",
            "sta_ssid":"…","sta_rssi":-62,"sta_enabled":true},
  "cfg":   {"motion_thresh_mg":50, "still_sec":5, "offbody_sec":30,
            "step_thresh_mg":200, "step_min_ms":300, "step_max_ms":1500,
            "bpm_min":8, "bpm_max":30, "resp_window_sec":10,
            "cal_x":0, "cal_y":0, "cal_z":0}
}
```

### `/voice/status`

```json
{
  "state": "idle|streaming|stopping",
  "elapsed_ms": 0, "bytes": 0, "rms": 0,
  "last_reason": "vad-silence|timeout|cmd|max-bytes",
  "last_wav_size": 0,
  "vad_threshold": 150, "silence_ms": 1500, "timeout_ms": 10000,
  "gain_shift": 14, "dc_blocker": true, "noise_gate": 0
}
```

### `/stt/status`

```json
{
  "state": "idle|running|done|error",
  "key_hint": "set (****abcd) | not set",
  "model": "nova-2",
  "http": 200, "rc": 0, "dur_ms": 1820, "wav_bytes": 312044,
  "transcript": "what's my heart rate today",
  "err": ""
}
```

`rc` codes: 0 ok · -1 no key · -2 no STA · -3 no WAV · -4 http begin failed
· -5 http error · -6 no transcript field · -7 voice still active.

## Tunable parameters

Every parameter below is **runtime-tunable from the UI** and **persisted to
NVS** under namespace `cfg` unless noted. Compile-time defaults live in
`app_config.h`.

### Accel + wear

| Name | Default | Range | Unit | Persists | Lives in |
|---|---:|---|---|---|---|
| `motion_thresh_mg` | 50 | 10–510 | mg | yes | `cfg` |
| `offbody_sec` | 30 | 5–300 | s | yes | `cfg` |
| `wear_var_thresh_mg` | 5 | 1–50 | mg | yes | `cfg` |
| `wear_grav_diff_thresh_mg` | 20 | 5–200 | mg | yes | `cfg` |
| `still_sec` | 5 | 1–30 | s (retired by v2) | yes | `cfg` |
| `cal_x_mg` / `cal_y_mg` / `cal_z_mg` | 0 | ±2000 | mg | yes | `cfg` |
| `wear_force` | auto | auto/on/off | — | no (volatile) | RAM |

### Steps

| Name | Default | Range | Unit | Persists | Lives in |
|---|---:|---|---|---|---|
| `step_thresh_mg` | 200 | 50–1000 | mg above gravity | yes | `cfg` |
| `step_min_ms` | 300 | 50–5000 | ms | yes | `cfg` |
| `step_max_ms` | 1500 | 100–10000 | ms | yes | `cfg` |
| `step_adaptive` | false | bool | — | yes | `cfg` |
| `step_bandpass` | false | bool | — | yes | `cfg` |
| `step_axis` | mag | mag/x/y/z | — | yes | `cfg` |
| `step_amp_window_ms` | 2000 | 500–5000 | ms | yes | `cfg` |

### Respiration

| Name | Default | Range | Unit | Persists | Lives in |
|---|---:|---|---|---|---|
| `bpm_min` | 8 | 1–30 | BPM | yes | `cfg` |
| `bpm_max` | 30 | 5–60 | BPM | yes | `cfg` |
| `resp_window_sec` | 10 | 5–15 | s | yes | `cfg` |
| `resp_min_interval_ms` | 2000 | 1000–7500 | ms | yes | `cfg` |
| `resp_iir_alpha_q15` | 1638 | 100–8192 | Q15 | yes | `cfg` |

### Voice capture

| Name | Default | Range | Unit | Persists | Lives in |
|---|---:|---|---|---|---|
| `vad_threshold` | 150 | 0–5000 | RMS | yes | `voice` |
| `silence_ms` | 1500 | 200–10000 | ms | yes | `voice` |
| `timeout_ms` | 10000 | 1000–60000 | ms | yes | `voice` |
| `gain_shift` | 14 | 8–18 | bit-shift | yes | `voice` |
| `dc_blocker` | true | bool | — | yes | `voice` |
| `noise_gate` | 0 | 0–5000 | abs-sample | yes | `voice` |
| `VOICE_SAMPLE_RATE_HZ` | 16000 | compile-time | Hz | — | `app_config.h` |
| `VOICE_BITS_PER_SAMPLE` | 16 | compile-time | bits | — | `app_config.h` |
| `VOICE_MAX_FILE_BYTES` | 900000 | compile-time | bytes | — | `app_config.h` |
| `VOICE_WAV_PATH` | `/last.wav` | compile-time | — | — | `app_config.h` |

### STT

| Name | Default | Range | Persists | Lives in |
|---|---:|---|---|---|
| `key` | (none) | string | yes | NVS `stt` |
| `model` | `nova-2` | string | yes | NVS `stt` |
| `STT_HTTP_TIMEOUT_MS` | 30000 | compile-time | — | `app_config.h` |

### NVS namespaces

| Namespace | Owner | Keys |
|---|---|---|
| `cfg` | `config.cpp` | thresholds + calibration + WiFi STA creds |
| `voice` | `voice.cpp` | VAD, silence, timeout, gain, DC, gate |
| `stt` | `stt.cpp` | key, model |

`POST /settings/export` and `POST /settings/import` round-trip everything in
`cfg` — voice and stt namespaces are intentionally **excluded** from export
(API key is sensitive; voice DSP is rig-specific).

## DSP notes

**DC blocker** is a single-pole IIR HPF in Q15:

```
y[n] = x[n] − x[n-1] + α·y[n-1],   α = 32604/32768 ≈ 0.99497
```

That's −3 dB at ~25 Hz with `fs = 16 kHz`. Removes the constant offset INMP441
puts on its raw output and any sub-audible mechanical rumble.

**Noise gate** is a hard absolute-threshold gate (no hysteresis, no smoothing).
Below the threshold, the sample is forced to zero. Start at 100–300 if you
hear hiss in silent gaps after the DC blocker is on.

## Known quirks

- **STT requires STA**, not AP. The phone joining the ESP32's AP gives the ESP32
  no upstream internet. Configure home WiFi in **Network** tab first.
- **`setInsecure()` on TLS** — the rig does not verify Deepgram's cert. Bench
  only. Production must pin a CA bundle.
- **Boot bounce on USB-UART clones** — see `aboutme.md` for full erase recovery.
- **Tab visibility polling** is at 150 ms for Voice (fast, for live RMS) and
  1 s for Live; STT only refreshes on action and tab open.
