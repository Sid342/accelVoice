# atovio-accel-bench-esp32

ESP32 DevKit V1 + MPU6050 portable bench for the nrfBLE Phase 2.5 algorithms.
Same wear / adaptive-output / respiration / step-count modules as
`atovio-accel-bench` (Nano), but with WiFi AP + a tiny live web UI so you can
walk around with the device on a USB power bank and watch readings on a phone.

## Why this exists alongside the Nano version

| Concern             | Nano (`atovio-accel-bench`) | ESP32 (this folder) |
|---------------------|-----------------------------|---------------------|
| Tether              | USB to laptop required      | USB power bank → free roaming |
| Logic-level fit     | 5 V (cheap clones marginal) | 3.3 V — matches MPU6050 exactly |
| CPU / RAM           | 16 MHz / 2 KB               | 240 MHz / 320 KB    |
| Algo headroom       | Tight                       | Plenty (FFT later)  |
| Live UI             | Serial only                 | Phone browser       |
| BLE prototyping     | —                           | Built-in (future)   |

Both rigs run identical algorithm code. ESP32 just adds WiFi.

## Hardware

- ESP32 DevKit V1 (38-pin) — generic FQBN `esp32:esp32:esp32`
- MPU6050 breakout (GY-521 typical)
- USB power bank for portable runs

### Wiring

| MPU6050 | ESP32 DevKit V1 | Notes                                       |
|---------|-----------------|---------------------------------------------|
| VCC     | Vin (5 V)       | GY-521 has onboard LDO, 5 V OK              |
| GND     | GND             |                                             |
| SDA     | D25 / GPIO 25   | I²C data — no strap concern                 |
| SCL     | D26 / GPIO 26   | I²C clock — no strap concern                |
| INT     | D27 / GPIO 27   | motion interrupt — no strap concern         |
| AD0     | GND             | I²C addr 0x68 (default)                     |

Status LED = onboard GPIO 2 (blue). Solid HIGH = on-body, LOW = off-body,
brief 50 ms LOW-pulse on each step.

**Avoid GPIO 12 for I²C** — it's a strapping pin. The GY-521 has 4.7 kΩ
pull-ups on SDA/SCL that would yank GPIO 12 HIGH at boot, putting the ESP32
into 1.8 V flash mode and breaking boot. GPIO 25/26/27 are strap-free.

## Build & flash

```bash
# one-time setup
arduino-cli config set board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32
arduino-cli lib install "MPU6050@1.4.4"

# compile (from this folder)
arduino-cli compile --fqbn esp32:esp32:esp32 .

# find the port and flash
arduino-cli board list
arduino-cli upload -p /dev/cu.usbserial-XXXX --fqbn esp32:esp32:esp32 .

# serial monitor (optional — works alongside WiFi UI)
arduino-cli monitor -p /dev/cu.usbserial-XXXX -c baudrate=115200
```

If your specific dev board is recognized as `esp32doit-devkit-v1`, you may
prefer `--fqbn esp32:esp32:esp32doit-devkit-v1`. The generic `esp32` works
either way.

## Live web UI

After flashing, the ESP32 starts a WiFi access point:

- **SSID:** `atovio-bench`
- **Password:** `atovio1234`

Phone or laptop joins → open `http://192.168.4.1/` in a browser.

The page shows four live cards (wear / steps / respiration BPM / Z-axis mg)
and a 20-second sparkline of Z-axis. It polls `/data` every 100 ms.

`/data` is also a plain JSON endpoint:

```bash
curl http://192.168.4.1/data
# {"t":12345,"w":1,"x":-3,"y":42,"z":1098,"bpm":0,"st":0}
```

To switch to STA mode (join your home WiFi instead of hosting an AP), edit
`atovio-accel-bench-esp32.ino` — replace `WiFi.softAP(...)` with
`WiFi.mode(WIFI_STA); WiFi.begin(ssid, pass);` and add credential constants.
For most field testing, AP mode is the right call.

## Validation matrix

| Test               | Setup                              | Expected                                |
|--------------------|------------------------------------|------------------------------------------|
| Boot               | power on                           | "MPU6050 online", AP up, GPIO2 LED HIGH |
| Web UI             | join AP, browse 192.168.4.1        | live cards updating @ 10 Hz             |
| Wear off-body      | sit on desk ≥ 35 s                 | card flips to OFF, LED LOW              |
| Wear on-body       | pick up                            | card flips to ON within 1 s, LED HIGH   |
| Respiration        | tape to chest, breathe normally    | `bpm` 10..20 after ~10 s                |
| Step count         | walk while holding                 | `st` increments at human cadence; LED flickers |

## Files

| File                          | Responsibility                        |
|-------------------------------|---------------------------------------|
| `atovio-accel-bench-esp32.ino`| main — WiFi AP, HTTP, telemetry, LED  |
| `app_config.h`                | pins, thresholds, WiFi creds          |
| `web_index.h`                 | embedded HTML viewer (PROGMEM)        |
| `accel.{h,cpp}`               | MPU6050 init + mg read + motion IRQ   |
| `wear.{h,cpp}`                | on-body / off-body state machine      |
| `respiration.{h,cpp}`         | Z-axis peak-count BPM                 |
| `steps.{h,cpp}`               | magnitude + EMA baseline + cadence    |

`wear`, `respiration`, `steps` are byte-identical to the Nano version — same
algorithm code, just compiled for ESP32.
