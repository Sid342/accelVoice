# accelVoice

ESP32 bench rig for the Atovio nRF52 Phase 2.5 algorithms — wear / respiration
/ steps / mode sim / battery sim / BLE find / voice capture / Deepgram STT —
all controllable from a web UI at `http://192.168.4.1` so the bench can be
tuned and validated without reflashing.

## Repo layout

```
.
├── README.md            ← you are here
├── aboutme.md           ← project overview, hardware, pins, build, status
├── app.md               ← every HTTP endpoint, JSON schemas, tunables tables
├── nrf connect.md       ← port plan: ESP32 bench → nRF52 / NCS / Zephyr
├── docs/                ← earlier specs (v2/v3 design notes)
└── firmware/            ← Arduino sketch (open this folder in Arduino IDE)
    ├── firmware.ino
    ├── app_config.h
    ├── sketch.yaml
    ├── accel.{cpp,h}      wear.{cpp,h}        respiration.{cpp,h}
    ├── steps.{cpp,h}      mode.{cpp,h}        battsim.{cpp,h}
    ├── samplelog.{cpp,h}  motionlog.{cpp,h}   find.{cpp,h}
    ├── wifi_sta.{cpp,h}   ble_find.{cpp,h}    config.{cpp,h}
    ├── voice.{cpp,h}      stt.{cpp,h}
    └── web_index.h
```

## Quick start

```bash
# one-time
arduino-cli core update-index
arduino-cli core install esp32:esp32
arduino-cli lib  install "MPU6050@1.4.4"

# build + flash
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app firmware/
arduino-cli upload  --fqbn esp32:esp32:esp32:PartitionScheme=huge_app \
                    -p /dev/cu.SLAB_USBtoUART firmware/
```

After flash → join WiFi `atovio-bench` / `atovio1234` → open
`http://192.168.4.1`. See `aboutme.md` for the full picture, `app.md` for
the API + every tunable, and `nrf connect.md` when you're ready to port.
