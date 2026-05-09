# Atovio accel + voice bench (ESP32)

ESP32 DevKit V1 rig that runs every Phase 2.5 algorithm slated for the nRF52
silicon — wear detection, respiration BPM, step counter, mode simulator,
battery simulator, BLE find-my, voice capture, and cloud STT — all tunable
from a web UI at **`http://192.168.4.1`** (AP `atovio-bench`, password
`atovio1234`) or `http://atovio.local`. No reflashes needed during a session.

## What this rig is for

Validate the algorithms and the developer / app-side contract **before**
committing to nRF52 hardware:

- prove every threshold, window length, and DSP block at the bench, on a real
  body, with live readings and CSV log export
- generate the JSON shape (BLE Type 10 / 11 today, Type 12-14 voice next)
  that the phone app will speak to the production firmware
- exercise the cloud STT path (Deepgram) with real WAVs from a real mic, so
  the prompt + accuracy work is done before nRF gets touched

## Hardware

| Block | Part | Notes |
|---|---|---|
| MCU | ESP32 DevKit V1 (WROOM-32) | strap-pin GPIOs avoided |
| Accelerometer | MPU6050 (GY-521 module) | I²C 0x68, INT for motion wake |
| Microphone | INMP441 | I²S 16 kHz, 16-bit mono |
| Status | onboard blue LED (GPIO 2) | pulses per step, off-body strobe |

### Pin map

| Function | GPIO | Notes |
|---|---:|---|
| MPU SDA | 25 | I²C |
| MPU SCL | 26 | I²C |
| MPU INT | 27 | motion wake |
| Status LED | 2 | onboard |
| INMP441 WS | 32 | LRCLK |
| INMP441 SCK | 33 | BCLK |
| INMP441 SD | 35 | DATA, input-only pin |

L/R pin on the mic must be tied to **GND** for left-channel data on this
driver config.

## Module map

All firmware sources live under `firmware/`. Open that folder in the Arduino
IDE; from `arduino-cli`, point at `firmware/` (see commands below).

| File (under `firmware/`) | Purpose |
|---|---|
| `firmware.ino` | sketch entrypoint, HTTP routes, main loop |
| `app_config.h` | compile-time defaults for every tunable |
| `accel.cpp/.h` | MPU6050 init + sample loop |
| `wear.cpp/.h` | on-body / off-body state machine |
| `respiration.cpp/.h` | BPM analysis over a 10 s window |
| `steps.cpp/.h` | peak-detect step counter |
| `mode.cpp/.h` | OFF / NORMAL / TURBO ionizer simulator |
| `battsim.cpp/.h` | battery / charging simulator |
| `samplelog.cpp/.h` | rolling CSV buffer for `/log.csv` |
| `motionlog.cpp/.h` | motion-IRQ ring buffer |
| `find.cpp/.h` | "find my device" LED strobe |
| `wifi_sta.cpp/.h` | optional STA-mode join (for cloud STT) |
| `ble_find.cpp/.h` | on-demand BLE advertise for phone-side find-my |
| `voice.cpp/.h` | I²S capture, VAD, DC blocker, noise gate, WAV writer |
| `stt.cpp/.h` | Deepgram REST client (HTTPS POST `/v1/listen`) |
| `config.cpp/.h` | NVS-backed runtime settings |
| `web_index.h` | embedded tabbed web UI (HTML/CSS/JS) |
| `sketch.yaml` | `default_fqbn: esp32:esp32:esp32:PartitionScheme=huge_app` |

## Build + flash

```bash
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app firmware/
arduino-cli upload  --fqbn esp32:esp32:esp32:PartitionScheme=huge_app \
                    -p /dev/cu.SLAB_USBtoUART firmware/
```

`huge_app` is required: app image with WiFi + BLE + HTTPClient + WiFiClientSecure
+ SPIFFS won't fit `min_spiffs`. Voice WAVs live in the residual SPIFFS slice
(~190 KB after `huge_app`); cap at `VOICE_MAX_FILE_BYTES = 900_000` is set for
the previous partition layout — adjust if you change the table.

If upload fails repeatedly with "invalid head of packet" or "chip stopped
responding" on this DevKit clone, fully erase before retrying:

```bash
~/Library/Arduino15/packages/esp32/tools/esptool_py/5.2.0/esptool \
  --chip esp32 -p /dev/cu.SLAB_USBtoUART -b 460800 erase-flash
```

A full erase wipes NVS — accel calibration and Deepgram key must be re-entered
afterwards from the web UI.

## What's implemented (Phase 2.5 bench)

- ✅ Wear state machine with force-override
- ✅ Respiration BPM (peak-detect over 10 s window, 8–30 BPM gate)
- ✅ Step counter with min/max cadence guards
- ✅ Mode simulator drives "ionizer-would-be" boolean to LED
- ✅ Battery simulator (pct, charging, fault, state)
- ✅ Live multi-axis accel plot in browser
- ✅ Threshold tuning sliders, all persisted to NVS
- ✅ CSV log download (`/log.csv`)
- ✅ Motion IRQ log (`/motionlog`)
- ✅ BLE Type 10 endpoint (`/api/type10`)
- ✅ AP + STA WiFi, mDNS, ArduinoOTA
- ✅ Voice capture: tap-to-talk + hold-to-talk, VAD silence stop, hard timeout
- ✅ Voice DSP: gain shift, single-pole IIR DC blocker, noise gate
- ✅ WAV download / in-browser playback
- ✅ Cloud STT via Deepgram (key in NVS, model selectable)
- ✅ BLE find-my (on-demand advertise)

## What's next

- BLE Type 11 (live vitals push) to phone app
- Voice protocol Types 12-14 (start, frame, end) over BLE NUS for offline STT
- Streaming STT (WebSocket to Deepgram) instead of POST-after-record
- Port to nRF52 (see `nrf connect.md`)

## Status as of 2026-05-09

Working. Voice capture tested with INMP441; DC blocker + noise gate added
after first capture had hum. Deepgram REST integration ships in this push.
Bench rig is the canonical reference for algorithm behaviour until the nRF
firmware reaches feature parity.
