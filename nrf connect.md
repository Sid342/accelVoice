# nrf connect.md — porting the bench rig to nRF52 (NCS / Zephyr)

Plan for taking everything proven on this ESP32 bench and reimplementing on
the production nRF52 silicon. Read `aboutme.md` and `app.md` first — this doc
assumes you've used the rig.

## TL;DR

- **Target SoC: nRF52832** (current Atovio spec). Switch to **nRF52840** only
  if voice + flash budget force it; the QFN footprints are different so PCB
  respin is required.
- **Drop WiFi.** No on-device HTTPS, no on-device WebServer.
- **Phone is the gateway.** Anything that hits the cloud (Deepgram, OTA) goes
  via the phone over BLE.
- **Storage:** Zephyr `settings` subsystem (NVS or flash-backed) replaces
  Arduino `Preferences`. WAV blobs do **not** stay on the device — they
  stream out frame-by-frame.
- **All algorithms are firmware-portable as-is.** The LIS2DW12 work and the
  voice DSP work, both proved on this rig, drop in cleanly.

## Why phone-as-gateway

| Constraint | nRF52832 | Implication |
|---|---|---|
| RAM | 64 KB | mbedTLS handshake alone is 30–40 KB → no on-device TLS |
| Flash | 512 KB | SoftDevice + app + DFU partition leaves ~200 KB → no large WAV store |
| Radio | BLE only | no WiFi → no direct cloud anyway |
| Audio | PDM peripheral, no I²S DMA-to-flash | streaming is the natural shape |

Conclusion: the firmware **captures audio, runs DSP, and emits BLE frames**;
the phone app does TLS, talks to Deepgram, and shows the result. Same as the
final product topology — the bench's "device-talks-to-Deepgram-directly"
design is a rig-only shortcut.

## Subsystem mapping

| Bench rig (ESP32) | nRF52 (NCS / Zephyr) | Notes |
|---|---|---|
| `MPU6050` over I²C | **LIS2DW12** over I²C | already locked in plan `2026-05-06-phase2.5-accelerometer-zephyr.md`; bench thresholds port directly because both report mg |
| `INMP441` I²S 16/16k | `INMP441` PDM peripheral | nRF52832 has PDM; same mic, change driver |
| ESP32 NVS via `Preferences` | Zephyr `settings` subsys | one-to-one key port — see "Persistence map" below |
| `WiFi` AP/STA | drop | dev-only feature |
| `WebServer` HTTP | **NUS** + custom GATT services | command/JSON over BLE |
| `WiFiClientSecure` + `HTTPClient` | drop | phone calls Deepgram |
| `SPIFFS` for `/last.wav` | streaming-only | optional 32 KB ring on internal flash for diag captures |
| `ArduinoOTA` | **MCUboot** + SMP over BLE | Zephyr standard |
| `ESPmDNS` | drop | not on BLE |
| `ble_find` advertise | already BLE-native | trivial |
| `voice.cpp` DSP (DC blocker + noise gate) | port byte-for-byte | pure C, no platform deps |

## Phase 2.5 portability summary

The Phase 2.5 features (cough, slouch, fall, AUTO mode, Live cards) are
designed for direct LIS2DW12 portability. Quick cheat sheet:

| Feature | Hardware change | Software change | Notes |
|---|---|---|---|
| **ODR 25→100 Hz** | LIS2DW12 `CTRL1.ODR=0x4` (LP1) | `ACCEL_FAST_ODR_HZ=100`, decimate 1-in-4 to wear/resp/steps | already done on bench |
| **Cough** | none | port `cough.cpp` byte-for-byte | one slow IIR + two biquads + envelope LPF |
| **Slouch** | none | port `slouch.cpp`; gate on wear+upright | float `atan2` per sample, 25 Hz |
| **Fall** | LIS2DW12 free-fall + sleep IRQs | replace stage-1 dwell + stage-3 MAD with hardware IRQs | major code reduction in production |
| **Mode AUTO** | none | port enum + `ionizer_state()`; default boot AUTO | NORMAL/TURBO are debug modes |
| **Cough wave** | none | none — `/cough/wave` is bench-only | replace with BLE notification for production |

**No gyro features.** LIS2DW12 has no gyro. None of the Phase-2.5
algorithms (cough, slouch, fall) use a gyro signal. Anything the spec
might suggest about gyro fusion (e.g. complementary filter) is
intentionally absent.

## Phase 2.5 portability notes

**ODR is now 100 Hz on the bench**, not 25 Hz. Cough (and the upcoming
Fall) detector consumes the full rate; resp/steps/wear are decimated 1-in-4
to keep their 25 Hz calibration. On LIS2DW12 set `CTRL1.ODR = 0x4`
(100 Hz, low-power mode 1) and the high-performance bit cleared. Power
cost is negligible at LP1.

**Cough detector** (`firmware/cough.cpp`) is pure software (one slow IIR
mean tracker, two biquads, one envelope LPF, peak + cluster gate). Ports
verbatim to LIS2DW12. Constants port unchanged because both sensors
report mg. Cluster window and threshold are the only field-tuneables.

**Slouch detector** (`firmware/slouch.cpp`) is a slow IIR gravity tracker
(α≈0.005, ~8 s effective at 25 Hz) plus an `atan2`-based pitch projection
plus a per-second state machine. Float math (one atan2, one sqrt per
second). Ports verbatim. The "Sit Up Straight" calibration step is part
of the production onboarding flow and writes baseline to settings under
key `sl_base`. Note: the LIS2DW12 has a hardware sleep/idle interrupt that
can wake the slouch SM only when the user stops moving — opportunity to
gate the per-second tick to save MCU wake events.

**Fall detector** (`firmware/fall.cpp`) is a 3-stage software SM at
100 Hz. Bench algorithm ports as-is, but **LIS2DW12 has hardware
free-fall and double-tap interrupts** that make most of the bench logic
unnecessary in production:

- LIS2DW12 `WAKE_UP_THS` + `WAKE_UP_DUR` give a hardware free-fall
  interrupt — fire it on the IRQ pin and only run the impact/stillness
  parts of the SM in software.
- LIS2DW12 has a built-in tap detector for impact discrimination.
- Stillness can be replaced by the inactivity (sleep) interrupt.

The hand-rolled software SM here is the validation baseline; the
production port should swap stages 1 and 3 for hardware-assisted
detection while keeping the 800 ms / 5 s gate timings.

**Mode AUTO** — production default. AUTO follows wear; NORMAL/TURBO are
bench/lab modes that ignore the wear sensor and always run the ionizer
(respecting OFF + battery). AUTO is what production firmware should
boot to; NORMAL/TURBO are accessible only via debug BLE write.

**No gyro features.** LIS2DW12 has no gyro. None of the Phase-2.5
algorithms (cough, slouch, fall) use gyro signal. Anything labelled
"gyro" in the algorithm doc is intentionally absent.

## Audio path

The DSP block from `voice.cpp` (gain shift → DC blocker IIR → noise gate →
RMS) ports verbatim. What changes is everything around it:

```
ESP32:    PDM/I²S DMA → 16-bit ring → DSP → SPIFFS WAV → HTTP POST → Deepgram
nRF52:    PDM DMA     → 16-bit ring → DSP → BLE NUS    → phone     → Deepgram
```

Frame size: **20 ms (320 samples @ 16 kHz)** = 640 bytes per frame, fits two
ATT MTU=247 packets. Phone reassembles, posts, gets transcript back, displays.

If BLE bandwidth is tight (legacy connection params), drop to **8 kHz mono**
at the source — Deepgram handles 8 k natively, and that halves BLE load to
~128 kbps which is comfortable on a 2M PHY.

## Persistence map

NVS key port from ESP32 `Preferences` to Zephyr `settings`:

| Bench namespace | Key | Zephyr settings path | Notes |
|---|---|---|---|
| `cfg` | `motion_thresh_mg` | `atovio/wear/motion_thresh_mg` | uint16 |
| `cfg` | `still_sec` | `atovio/wear/still_sec` | uint16 |
| `cfg` | `offbody_sec` | `atovio/wear/offbody_sec` | uint16 |
| `cfg` | `step_thresh_mg` | `atovio/steps/thresh_mg` | uint16 |
| `cfg` | `step_min_ms` | `atovio/steps/min_ms` | uint16 |
| `cfg` | `step_max_ms` | `atovio/steps/max_ms` | uint16 |
| `cfg` | `bpm_min` | `atovio/resp/bpm_min` | uint8 |
| `cfg` | `bpm_max` | `atovio/resp/bpm_max` | uint8 |
| `cfg` | `resp_window_sec` | `atovio/resp/window_sec` | uint8 |
| `cfg` | `cal_x_mg/y/z` | `atovio/accel/cal_x|y|z` | int16 |
| `voice` | `vad_threshold` | `atovio/voice/vad_thr` | int16 |
| `voice` | `silence_ms` | `atovio/voice/silence_ms` | uint32 |
| `voice` | `timeout_ms` | `atovio/voice/timeout_ms` | uint32 |
| `voice` | `gain_shift` | `atovio/voice/gain_shift` | uint8 |
| `voice` | `dc_blocker` | `atovio/voice/dc_blocker` | bool |
| `voice` | `noise_gate` | `atovio/voice/noise_gate` | int16 |
| `stt` | `key` | **not persisted on device** | phone holds the key |
| `stt` | `model` | **not persisted on device** | phone-side preference |

Defaults stay identical. Re-validate on the first nRF build by exporting
the bench's `/settings/export` JSON and writing a one-shot tool that imports
each value through the corresponding GATT write — that gives a known-good
starting calibration without re-tuning on body.

## BLE service shape

Bench `/api/type10` JSON becomes a binary characteristic (size matters on
BLE). Define one service for vitals + state, and reuse NUS for the voice
frames + control JSON.

| Service | UUID (placeholder) | Characteristic | Direction |
|---|---|---|---|
| Atovio Vitals | `a701-…` | Type 10 (vitals) | notify |
| Atovio Vitals | `a702-…` | Type 11 (extended) | notify |
| Atovio Voice | NUS `0001-…` | TX (frames + control) | notify |
| Atovio Voice | NUS `0002-…` | RX (commands) | write |
| Atovio Tunables | `a710-…` | one char per param group | read/write |

Voice protocol (already specced for nRF):

| Type | Payload |
|---|---|
| 12 | start: `{sr, bps, channels, vad, gain}` |
| 13 | frame: raw int16 PCM, length = frame size |
| 14 | end: `{reason, frames, bytes}` |

## Phone-side responsibilities

What was the rig's job that becomes the phone's job:

- HTTPS to Deepgram (with proper cert pinning, not `setInsecure()`)
- Holding the Deepgram API key (Keychain / Keystore, not NVS)
- Reassembling Type 13 frames to a WAV (or streaming directly via Deepgram
  WebSocket — preferred)
- OTA: pulling firmware bundles, pushing over SMP via Nordic DFU
- "Find my device" UI (the device side stays as a strobe + advertise)

## Files that port directly

These are pure C and have **no Arduino / ESP-IDF** deps once you replace the
buffer-source side:

- `wear.cpp` (state machine; I²C read becomes `sensor_sample_fetch`)
- `respiration.cpp` (peak-detect + window stats)
- `steps.cpp` (peak detect with cadence guards)
- DSP block from `voice.cpp` (`apply_gain_shift`, DC blocker, noise gate, RMS)

## Files that must be rewritten

- `accel.cpp` → Zephyr sensor driver bindings for LIS2DW12
- `voice.cpp` outer shell → nrfx PDM ring → DSP → BLE notify
- `stt.cpp` → **deleted** on device; lives in phone app
- `wifi_sta.cpp`, `ble_find.cpp` (NimBLE), `web_index.h`, `samplelog.cpp`
  HTTP wrapper → all dropped or replaced by GATT equivalents

## Test parity matrix

Before declaring nRF feature-parity with the bench, the same test on both
must produce the same numbers (±1 unit):

| Test | Bench command | nRF equivalent |
|---|---|---|
| Wear ON detect (5 s motion) | shake → wait 5 s → `/data` shows `wear:on` | same, via GATT vitals notify |
| Off-body (still 30 s) | place still → `/data` shows `wear:off` | same |
| Steps (10 paces) | walk → `/data.steps` increments | same |
| BPM (paced 14 BPM) | breathe → `/data.bpm ≈ 14` | same |
| VAD silence stop | record → silence 1.5 s → auto-stop | same |
| WAV size | tap-to-talk 3 s → ~96 KB | same byte count emitted via Type 13 |
| STT round-trip | bench POSTs to Deepgram | phone POSTs WAV reassembled from Type 13 |

## Open questions for the port

- **PHY**: 1M (longer range, lower throughput) vs 2M (voice-friendly). Pick
  based on real bench RSSI on the watch wearer.
- **Connection interval**: 7.5–15 ms for voice frames vs 30–50 ms for
  battery-friendly idle. Switch dynamically when voice starts.
- **Diagnostic capture**: should the device keep a 32 KB ring of the **last**
  voice attempt in internal flash for post-mortem when STT confidence is low?
  Bench has SPIFFS WAV; nRF would need a deliberate ring.
- **Encryption**: voice frames over NUS in plain. Phone is paired/bonded so
  link-layer encryption covers it — but confirm with security review before
  moving the voice payload off-bench.
- **Sample rate**: keep 16 kHz or drop to 8 kHz? 8 k halves BLE load and
  Deepgram says "no measurable WER cost on conversational speech" — but
  validate on real phrases before locking it.

## Order of work

1. Land LIS2DW12 driver + accel sample loop (locked plan
   `2026-05-06-phase2.5-accelerometer-zephyr.md` already covers this)
2. Port `wear.cpp` / `respiration.cpp` / `steps.cpp` byte-for-byte
3. Add Zephyr `settings` keys with bench defaults
4. PDM driver + DSP port — confirm bench DC-blocker output matches (capture
   1 s on both, compare RMS before/after blocker)
5. NUS Type 12/13/14 framing
6. Phone app: reassemble + Deepgram WebSocket
7. Tunables service — bench's `/config` POST shape becomes a GATT write
8. Run the parity matrix above. If every row matches, ship it to first hardware.
