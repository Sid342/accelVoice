# atovio-accel-bench-esp32 — Runtime control & multi-feature test rig

**Date:** 2026-05-07
**Status:** Approved (verbal). One final firmware push.
**Goal:** Single firmware that exposes every Phase 2.5 feature in the locked
nrfBLE plan for tuning + validation without reflashing. Web UI at
`http://atovio.local/` (or `http://192.168.4.1/`) drives all tests.

## Scope (12 features)

### Already in firmware (no change needed at the algorithm level)
1. Wear detection state machine (5 s + 30 s, software)
2. Respiration BPM (Z-axis peak count, 25 Hz × 10 s, 8–30 BPM filter)
3. Step counter (magnitude + EMA baseline + cadence-gated peaks)
4. 10 Hz serial telemetry CSV (kept as wired-debug fallback)

### Added in this push
5. **Mode simulator** — `app_mode_t = OFF | NORMAL | TURBO`
   - Web dropdown switches between modes
   - Drives "ionizer would be ON/OFF" indicator
   - Logic: `ionizer_on = (mode != OFF) && (wear == ON_BODY) && !battery_override`
6. **Force-wear override** — `auto | force_on | force_off`
   - Bypasses wear SM for instant testing without 35 s wait
7. **Live thresholds** (4 sliders, NVS-persisted)
   - `motion_thresh_mg` (default 50, range 10–200, applied to MPU6050 reg)
   - `still_sec` (default 5, range 1–30)
   - `offbody_sec` (default 30, range 5–120)
   - `step_thresh_mg` (default 200, range 50–500)
8. **Manual resets** — buttons: Steps reset, Respiration buffer reset
9. **Multi-axis live plot** — toggles for X, Y, Z, |mag| traces, ~400 samples client-side
10. **BLE Type 10/11 protocol shape** — `GET /api/type10` returns
    `{"t":10,"wear":<0|1>,"bpm":<n>}` matching nrfBLE plan §3.1 exactly. Web UI
    has "Send Type 11" button that fetches this for visual inspection.
11. **CSV log download** — `GET /log.csv` returns last 10 s of samples,
    columns `t_ms,wear,x_mg,y_mg,z_mg,mag_mg,bpm,steps,mode,ionizer`
12. **Adaptive output indicator** — colored card showing live ionizer-would-be
    state derived from mode + wear + battery override

### Newly added in this revision (Claude-proposed, user approved)
13. **OTA firmware update** — `ArduinoOTA` over the AP network. Future flashes
    happen via `arduino-cli upload --upload-port atovio.local --fqbn esp32:esp32:esp32 .`
    No USB needed.
14. **Zero-offset calibration** — "Calibrate (flat rest)" button captures
    instantaneous XYZ over 1 s window, stores offset to NVS, subtracts on
    every read. Eliminates the ~+98 mg Z-axis bias. Improves respiration
    signal cleanliness materially.
15. **Battery simulator** — slider 0–100 %, plus toggles CHARGING / FAULT.
    Drives the production-firmware mode logic from the nrfBLE plan:
    - bat ≤ 5 % → `CRITICAL` (ionizer forced off, regardless of wear/mode)
    - bat ≤ 15 % → `LOW_BAT` (ionizer forced off)
    - charging toggle → `CHARGING` mode
    - fault toggle → `FAULT` mode
    Validates that adaptive output correctly defers to battery state.
16. **mDNS** — registers `atovio.local` so users can browse there instead of
    `192.168.4.1`.

### Bundled UX wins (no API surface)
- Step cadence display (ms between last two steps + steps/min)
- Respiration estimate always shown with valid/invalid badge (don't hide if
  outside 8–30 BPM filter — show with red border)
- Motion-event log: last 20 motion-IRQ timestamps (relative)
- Wear countdown: live "X s until off-body" when in still period

## Architecture changes

### New modules

| File | Responsibility |
|---|---|
| `mode.{h,cpp}` | `app_mode_t` enum + getter/setter; derives `ionizer_state(mode, wear, bat)` |
| `config.{h,cpp}` | NVS-backed tunables. Load on boot, save on POST `/config`. Applies updates to wear/steps/accel modules. |
| `samplelog.{h,cpp}` | Ring buffer of last 10 s of samples for `/log.csv` |
| `motionlog.{h,cpp}` | Ring of last 20 motion IRQ timestamps for UI |
| `battsim.{h,cpp}` | Simulated battery state. Provides `battery_override_t` to mode logic |

### Refactored modules

| File | Change |
|---|---|
| `wear.{h,cpp}` | `ACCEL_STILL_SEC` / `ACCEL_OFFBODY_SEC` `#define`s become runtime vars w/ setters; expose `wear_force(state_t)` for override; expose `wear_remaining_sec()` for countdown |
| `accel.{h,cpp}` | `accel_set_motion_thresh_mg(mg)` writes MPU6050 reg live; `accel_calibrate()` captures offset; `accel_read_mg()` subtracts offset |
| `steps.{h,cpp}` | `STEP_PEAK_THRESH_MG` becomes runtime var; expose `steps_get_cadence_ms()` and `steps_get_per_min()`; `steps_reset()` already exists |
| `respiration.{h,cpp}` | `resp_reset()` already in API (already provides `_init`); add `resp_get_estimate_raw()` returning latest BPM regardless of filter validity |

### New HTTP routes

```
GET  /                — HTML viewer (rewritten)
GET  /data            — live snapshot JSON (extended)
GET  /config          — current tunables JSON
POST /config          — JSON body, persists NVS
POST /mode            — {"mode":"off|normal|turbo"}
POST /wear/force      — {"state":"auto|on|off"}
POST /steps/reset
POST /resp/reset
POST /accel/calibrate — captures zero offset
POST /battery         — {"pct":75,"charging":false,"fault":false}
GET  /api/type10      — {"t":10,"wear":<>,"bpm":<>}
GET  /log.csv         — last 10 s, CSV
GET  /motionlog       — JSON array of last 20 motion event timestamps
```

OTA listens on standard ArduinoOTA port (3232 UDP/TCP).

### Live snapshot JSON (`GET /data`) — extended

```json
{
  "t": 12345,
  "uptime_s": 12,
  "wear": "on|off",
  "wear_forced": "auto|on|off",
  "wear_remaining_s": 12,
  "x": -3, "y": 42, "z": 998, "mag": 999,
  "bpm": 14, "bpm_valid": true, "bpm_raw": 14,
  "steps": 23, "step_cadence_ms": 540, "steps_per_min": 111,
  "mode": "normal",
  "ionizer": "on",
  "battery": { "pct": 80, "charging": false, "state": "ok" },
  "motion_irq_count": 47
}
```

### NVS schema (Preferences namespace `atovio`)

| Key | Type | Default |
|---|---|---|
| `mot_thr_mg` | uint16 | 50 |
| `still_sec` | uint16 | 5 |
| `offbody_sec` | uint16 | 30 |
| `step_thr_mg` | uint16 | 200 |
| `cal_x` | int16 | 0 |
| `cal_y` | int16 | 0 |
| `cal_z` | int16 | 0 |

## Web UI layout

```
┌─ Header: atovio-accel-bench-esp32 · v2 · uptime 12 m ─────────┐
│ Connection dot · http://atovio.local · OTA: ready             │
├──────────────────────────────────────────────────────────────┤
│ ┌Wear─────┐ ┌Steps────┐ ┌Resp BPM─┐ ┌Ionizer──┐               │
│ │ ON      │ │ 23      │ │ 14 ✓    │ │ ON      │               │
│ │ auto    │ │ 540 ms  │ │ valid   │ │ NORMAL  │               │
│ └─────────┘ └─────────┘ └─────────┘ └─────────┘               │
├──────────────────────────────────────────────────────────────┤
│ Live plot — toggles: [X] [Y] [Z] [|mag|]                     │
│ ┌───────────────────────────────────────────────────────┐    │
│ │  ~~~~~~~~~~~~~~~~~ Z-axis sparkline ~~~~~~~~~~~~~~~  │    │
│ └───────────────────────────────────────────────────────┘    │
├──────────────────────────────────────────────────────────────┤
│ Controls                                                      │
│  Mode: [OFF / NORMAL / TURBO]                                 │
│  Wear: [Auto / Force ON / Force OFF]                          │
│  Battery: ━━━━━━━━━━━ 80%   [ ] Charging  [ ] Fault          │
│                                                               │
│  Thresholds (NVS)                                             │
│   Motion thresh:   [────●────] 50 mg                          │
│   Still secs:      [──●──────]  5 s                           │
│   Off-body secs:   [────●────] 30 s                           │
│   Step thresh:     [──────●──] 200 mg                         │
│                                                               │
│  Actions: [Calibrate] [Reset Steps] [Reset Resp] [Send T11]  │
│           [Download log.csv]                                  │
├──────────────────────────────────────────────────────────────┤
│ Motion event log (last 20):                                   │
│  -42 s · -38 s · -31 s · -22 s · ...                          │
└──────────────────────────────────────────────────────────────┘
```

All in one PROGMEM HTML string, vanilla JS, no CDN.

## Out of scope (explicitly NOT in this push)

- Real BLE peripheral (Type 10/11 returned via HTTP only — sufficient for
  protocol-shape validation; full BLE waits for nrf silicon)
- FFT respiration (peak-count is enough for first validation; revisit only
  if peak-count produces unstable BPM in real chest-mount tests)
- Auth on web UI / OTA password (AP-mode local-only, low risk)
- Charging/discharge curve simulation (battery sim is a static slider only)
- Multi-client SSE / WebSocket (10 Hz polling is fine for one viewer)

## Resource budget

| | Current | Estimated post-add |
|---|---|---|
| Flash | 979 KB / 74 % | ~1.05 MB / 80 % |
| SRAM (heap) | 49 KB / 15 % | ~80 KB / 25 % |

Mostly bloat from `Preferences`, `ArduinoOTA`, `ESPmDNS`, plus larger HTML.
Within budget, no partition table changes.

## Validation plan

After flash, end-to-end checklist via the web UI alone:

| # | Action | Expected |
|---|---|---|
| 1 | Browse `http://atovio.local/` | page loads, cards populate, plot ticks @ 10 Hz |
| 2 | Click Calibrate at flat rest | Z drops from ~1098 to ~1000 mg |
| 3 | Force-wear OFF | wear card flips, ionizer card → OFF, GPIO 2 dims |
| 4 | Force-wear AUTO, sit still 35 s | wear → OFF naturally, countdown matches |
| 5 | Mode = TURBO, wear = ON | ionizer card → ON (TURBO) |
| 6 | Battery slider → 4 %, mode = NORMAL, wear = ON | ionizer card → OFF (battery override) |
| 7 | Walk while holding | step count rises, cadence card shows ms gap |
| 8 | Tape to chest, breathe normally | bpm 10–20 within 10 s, valid badge green |
| 9 | Move motion thresh slider to 200 mg | small wiggles no longer fire IRQ |
| 10 | `curl http://atovio.local/api/type10` | exact `{"t":10,"wear":1,"bpm":14}` shape |
| 11 | `curl http://atovio.local/log.csv > log.csv` | 100 rows of CSV with all columns |
| 12 | Power-cycle ESP32 | thresholds + calibration restore from NVS |
| 13 | `arduino-cli upload --upload-port atovio.local …` | OTA flash succeeds without USB |
