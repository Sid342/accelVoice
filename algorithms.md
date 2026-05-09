# algorithms.md — what each algorithm does, why, and which knob does what

This doc is the **single source of truth for the algorithm logic** running on
the bench: wear detection, respiration BPM, step count, and voice DSP. Each
section gives the current code's behaviour, what each tunable controls, and
where the logic is known to be weak. Read `app.md` for the HTTP shape and
`nrf connect.md` for the port plan; the same constants port to nRF directly.

All units are **mg** (1 mg = 1/1000 of 1 g; gravity ≈ 1000 mg) unless noted.
ODR is **25 Hz** (one sample every 40 ms).

## Index

1. [Wear detection](#wear-detection)
2. [Respiration BPM](#respiration-bpm)
3. [Step counting](#step-counting)
4. [Voice DSP](#voice-dsp)
5. [Tunables — full table by subsystem](#tunables--full-table-by-subsystem)
6. [Persistence map (NVS)](#persistence-map-nvs)
7. [Glossary](#glossary)

---

## Wear detection

### What the code does today

Source: `firmware/wear.cpp`, `firmware/accel.cpp` (motion IRQ wiring),
`firmware/firmware.ino` (the loop calls).

```
state := ON-BODY at boot
on every MPU6050 motion IRQ (>= MOTION_THRESH_MG):
    still_sec, offbody_sec := 0
    if state == OFF-BODY: state := ON-BODY
every 1 s tick:
    if state == ON-BODY:
        still_sec++
        if still_sec >= STILL_SEC:
            offbody_sec++
            if offbody_sec >= OFFBODY_SEC:
                state := OFF-BODY
```

So the device flips OFF-BODY when it sees no >50 mg motion impulse for
`STILL_SEC + OFFBODY_SEC` seconds total (default 5 + 30 = 35 s).

### How motion is detected

The MPU6050 has a hardware **motion-detection peripheral**: it compares the
high-pass-filtered acceleration of any axis against a threshold register
(LSB = 2 mg) and asserts the INT pin when crossed. We map:

```
MPU_MOT_THR_REG = MOTION_THRESH_MG / 2
```

Setting `MOTION_THRESH_MG = 50` writes register value 25, meaning
"interrupt on > 50 mg high-pass-filtered jerk on any axis." The IRQ is the
**only** signal feeding the wear state machine.

### Why this fails on body

The implicit assumption is *worn = moving > 50 mg*. That is wrong. A wrist
device on someone sitting still produces only **5–15 mg** of micro-motion
(breathing, heartbeat, tremor) — well under the IRQ threshold. After 35 s of
stillness the device declares OFF-BODY even though it's clearly being worn.

Lowering the IRQ threshold doesn't rescue this:
- The MPU6050 motion-detect peripheral has a noise floor — at very low
  thresholds it either fires constantly from chip noise or unpredictably.
- Even if it triggered, you've turned a *hardware-debounced wear signal*
  into *random sensor noise* — the state machine becomes meaningless.

The right fix is to use a **different signal**, not a smaller threshold.

### Better signals (proposed v2 — not yet implemented)

| Signal | Worn (still body) | On a desk | Cost |
|---|---|---|---|
| Std-dev of magnitude over 1 s window | 5–20 mg | < 1 mg | 25 mults/s |
| Gravity-vector angle change over 5 s | constant micro-drift | pinned ±0.1° | one acos per second |
| Hardware impulse IRQ > 50 mg | rare bursts | none | already free |

**Proposed rule:** state stays ON-BODY if **any** of:
- 1 s sliding-window std-dev of magnitude > `WEAR_VAR_THRESH_MG` (default 5)
- Gravity vector angle change vs 5 s ago > `WEAR_ANGLE_THRESH_DEG` (default 1.5°)
- HW motion IRQ in the last `WEAR_OFFBODY_SEC` seconds

If none of three for `WEAR_OFFBODY_SEC` (default 60) → OFF-BODY.

`STILL_SEC` becomes obsolete — it's the wrong gate. Production silicon
(LIS2DW12) does this in hardware via the activity / inactivity classifier;
the bench algorithm above is the software equivalent.

### Wear tunables

| Knob | Default | Range | Unit | Meaning today |
|---|---:|---|---|---|
| `motion_thresh_mg` | 50 | 10–510 | mg | MPU6050 hardware motion-detect threshold; below = no IRQ |
| `still_sec` | 5 | 1–30 | s | seconds with no IRQ before "going still" begins counting |
| `offbody_sec` | 30 | 5–300 | s | seconds of "still" beyond `still_sec` before declaring OFF-BODY |
| `wear_force` | auto | auto/on/off | — | UI override; for bench debugging — bypasses the SM entirely |

**Proposed v2 additions** (TBD when we implement):

| Knob | Default | Range | Unit | Meaning |
|---|---:|---|---|---|
| `wear_var_thresh_mg` | 5 | 1–50 | mg | per-second std-dev of magnitude — above this = body micro-motion present |
| `wear_angle_thresh_deg` | 1.5 | 0.1–10 | deg | gravity-vector angle change over 5 s — above this = orientation drifting (worn) |

---

## Respiration BPM

### What the code does today

Source: `firmware/respiration.cpp`. Sample-rate must equal `ACCEL_ODR_HZ`.

```
buf[0..N-1] = circular ring of last N Z-axis readings, N = WINDOW_SEC × 25
on each new Z sample:
    buf[idx++] := z_mg
    if idx < window_len: return
    idx := 0
    mean := average of buf[]
    peaks := count rising mean-crossings in buf[] with min spacing 1 s
    bpm_raw := peaks × 60 / WINDOW_SEC
    if BPM_MIN <= bpm_raw <= BPM_MAX:
        bpm := bpm_raw, ready := true
    else:
        bpm := 0, ready := false
```

Default window is 10 s (250 samples). Default filter is 8–30 BPM.

### Why this is hard to validate

The algorithm emits a single BPM number once every 10 s. The user has
**no way to see what the algorithm thought a breath was**, only the final
filtered count. When the value sits at 0 the only thing you know is "raw was
outside [8, 30]" — could be 50 (noisy) or 4 (missed) or 30 (hovering).

### What we want instead — per-breath events

**Proposed v2:**

1. **Online peak detection** instead of windowed reset:
   - Running mean via IIR: `mean += α · (z − mean)`, α ≈ 0.05 (≈ 4 s effective window).
   - Rising-edge detection: previous sample below `mean`, current at-or-above `mean`, AND `t_now − t_last_peak ≥ RESP_MIN_INTERVAL_MS`.
2. **Event ring** of `{t_ms, amplitude_mg, interval_ms}` — last N peaks.
3. **New endpoint** `GET /resp/events?since=<t_ms>` returns events since the
   given timestamp. UI polls at 2 Hz, draws a vertical tick at every detected
   peak time on top of the breath waveform.
4. **New endpoint** `GET /resp/wave?n=300` returns last N samples of
   `{z, mean}` paired so the UI can plot the raw signal vs the running mean
   it's tracking.
5. Keep the windowed BPM as today but **also** expose instantaneous BPM =
   `60000 / interval_ms` for the latest peak.

Now the user can compare: "I just took a breath at the marker → did a tick
appear?" Direct visual validation, no waiting 10 s.

### Respiration tunables

| Knob | Default | Range | Unit | Meaning today |
|---|---:|---|---|---|
| `bpm_min` | 8 | 1–30 | BPM | below this, raw BPM is silently zeroed |
| `bpm_max` | 30 | 5–60 | BPM | above this, raw BPM is silently zeroed |
| `resp_window_sec` | 10 | 5–15 | s | length of the analysis ring |
| `RESP_SAMPLE_HZ` | 25 | compile-time | Hz | must match ODR |

**Proposed v2 additions:**

| Knob | Default | Range | Unit | Meaning |
|---|---:|---|---|---|
| `resp_min_interval_ms` | 2000 | 1000–7500 | ms | reject peaks closer than this (= 60000 / max BPM) |
| `resp_iir_alpha_q15` | 1638 | 100–8192 | Q15 | mean-tracker speed; 1638 ≈ α=0.05 = 4 s eff window |
| `resp_event_ring_size` | 64 | compile-time | — | how many recent peaks to keep |

---

## Step counting

### What the code does today

Source: `firmware/steps.cpp`. Runs at ODR (25 Hz) only when ON-BODY.

```
on each (x, y, z) sample:
    m := sqrt(x² + y² + z²)
    baseline := baseline + (m − baseline) / 16          # slow EMA, ≈640 ms tau
    delta := m − baseline
    if (not above) AND (delta > THRESH_MG):
        gap := now − last_peak_ms
        if MIN_MS <= gap <= MAX_MS:
            count++; cadence := now − prev_step_ms; prev_step := now
        last_peak := now
        above := true
    elif above AND (delta < THRESH_MG / 2):                # hysteresis
        above := false
```

So a "step" is a magnitude bump above gravity-baseline by `THRESH_MG`,
with cadence between `MIN_MS` and `MAX_MS` (default 300–1500 ms ≈ 0.66–3.3 Hz).

### How to tune today (no code change)

1. Walk a known count (count out loud, e.g. 30 paces).
2. Watch the **Live** tab; compare detected `steps` against your true count.
3. Lower `step_thresh_mg` until you stop missing real steps; raise it until
   you stop counting arm-only motion (typing, gestures).
4. If you get **double-counts per footfall**, raise `step_min_ms`.
5. If the **first step after standing still** doesn't register, raise
   `step_max_ms` (the gap from `last_peak_ms` is what gates the very first
   detection too — when nothing has happened in a long time, gap can exceed
   `max_ms` and the step is dropped).

### Known weaknesses

- **25 Hz is marginal.** Step impulses can be < 80 ms wide; at 40 ms sample
  spacing you may sample either side of the peak and miss it. Bumping ODR
  to 50 Hz is the single biggest accuracy win.
- **Magnitude collapses three axes.** Wrist swing in the X/Y plane has a
  larger amplitude than the vertical impact on Z; magnitude folds them
  together so signature shape is muddier. Per-axis (Z-only or X-only)
  detection can be cleaner depending on placement.
- **No band-pass.** Typing, clapping, driving on bumpy roads all produce
  magnitude transients above 200 mg. Without a 0.5–3 Hz filter, every one
  of those is a candidate "step."
- **Fixed threshold doesn't adapt.** A light walker may need 80 mg, a heavy
  one is fine at 200. Today the user must tune by hand each time.
- **EMA baseline τ ≈ 640 ms.** Faster than gravity drift but slower than a
  fast walking transient — fine for steady walks, can lag at start/stop.

### Proposed v2

1. **Adaptive threshold:** track `amp_2s = max(m) − min(m)` over the last 2 s
   sliding window; set effective threshold to `max(80, 0.5 × amp_2s)`.
   Auto-calibrates per user without UI fiddling.
2. **Band-pass IIR (0.5–3 Hz):** second-order biquad on magnitude before the
   peak detector. Kills the typing / clapping / driving false positives.
3. **Per-step event stream** — `GET /steps/events?since=<t_ms>`. UI marks a
   tick at each detected step on the magnitude plot. Same diagnostic value
   as the respiration event stream.
4. **Ground-truth tap:** a button in the Tune tab the user taps on each real
   step. Firmware compares the timestamp of the tap to detected steps in a
   ±300 ms window → reports precision and recall over the last 20 s walk.
5. **Optional 50 Hz ODR mode:** flag in `app_config.h` → adjusts
   `RESP_SAMPLE_HZ`, the loop pacing, and the EMA τ to match. Expensive on
   nRF battery so leave off by default.

### Steps tunables

| Knob | Default | Range | Unit | Meaning today |
|---|---:|---|---|---|
| `step_thresh_mg` | 200 | 50–1000 | mg | peak amplitude above gravity baseline that counts as a step |
| `step_min_ms` | 300 | 50–5000 | ms | minimum interval between steps (rejects bounce / double counts) |
| `step_max_ms` | 1500 | 100–10000 | ms | maximum interval between steps (resets cadence after a long pause) |

**Proposed v2 additions:**

| Knob | Default | Range | Unit | Meaning |
|---|---:|---|---|---|
| `step_adaptive` | off | bool | — | when on, threshold = max(80, 0.5·amp_2s) instead of fixed |
| `step_bandpass_en` | off | bool | — | enable 0.5–3 Hz IIR before peak detect |
| `step_axis` | mag | mag/x/y/z | — | what signal to peak-detect on |
| `step_amp_window_ms` | 2000 | 500–5000 | ms | window for adaptive amplitude tracker |

---

## Voice DSP

Source: `firmware/voice.cpp`. Runs after each I²S DMA buffer (≈10 ms) before
the WAV writer and the VAD silence detector see the samples.

### Pipeline

```
raw int32 (24-bit MSB-aligned)
    ↓ x >> GAIN_SHIFT          (gain shift: scale to int16 range)
    ↓ DC blocker IIR (optional, default ON)
    ↓ noise gate    (optional, default OFF)
    ↓ clamp ±32767
    → pcm int16 → WAV file + RMS for VAD
```

### Gain shift

INMP441 outputs 24-bit data left-justified in 32-bit slots. The default
shift of 14 brings full-scale into int16 range with about 4× extra gain on
top — enough for typical close-talk speech without clipping. Values mean:

| Shift | Scaling | Rough effect |
|---:|---|---|
| 18 | ÷ 2¹⁸ | very quiet, low chance of clipping |
| 14 (default) | ÷ 2¹⁴ | good for close-talk speech |
| 12 | ÷ 2¹² | ~4× louder; fine for far-field, may clip on shouts |
| 10 | ÷ 2¹⁰ | very loud; often clips |

Lower shift = louder.

### DC blocker (single-pole IIR HPF, Q15)

```
y[n] = x[n] − x[n−1] + α · y[n−1],   α = 32604 / 32768 ≈ 0.99497
```

Cutoff ≈ 25 Hz at fs = 16 kHz. Removes:
- the constant ~−1 % DC offset INMP441 puts on its raw stream,
- mechanical / structure-borne low-frequency rumble that has no speech
  content but inflates the RMS reading (so VAD never sees "silence").

Almost always wanted; default ON.

### Noise gate (hard absolute-threshold, no smoothing)

```
if abs(sample) < gate_threshold:
    sample := 0
```

No hysteresis, no attack/release. Use it to clean up silent gaps between
words after the DC blocker is enabled — start at 100–300 if you still hear
hiss; raise until silence is silent. Too high and you'll cut quiet syllables.

### RMS + VAD

The post-DSP samples are squared and summed per buffer; `voice_current_rms`
is `sqrt(sum / N)`. The VAD compares this to `vad_threshold` — if the live
RMS stays below that for `silence_ms`, the recording auto-stops with reason
`vad-silence`.

### Voice tunables

| Knob | Default | Range | Unit | Meaning |
|---|---:|---|---|---|
| `vad_threshold` | 150 | 0–5000 | RMS | live RMS below this = silence; sustained silence ends record |
| `silence_ms` | 1500 | 200–10000 | ms | sustained-silence window before auto-stop |
| `timeout_ms` | 10000 | 1000–60000 | ms | hard ceiling on a single capture |
| `gain_shift` | 14 | 8–18 | bit-shift | see table above |
| `dc_blocker` | true | bool | — | toggles the IIR HPF |
| `noise_gate` | 0 | 0–5000 | abs sample | zero samples below this absolute value |
| `VOICE_SAMPLE_RATE_HZ` | 16000 | compile-time | Hz | I²S clock + WAV header |
| `VOICE_BITS_PER_SAMPLE` | 16 | compile-time | bits | WAV format |
| `VOICE_MAX_FILE_BYTES` | 900000 | compile-time | bytes | hard cap; SPIFFS may cap lower |

---

## Tunables — full table by subsystem

This is the consolidated view of every runtime-tunable parameter. Compile-time
constants are listed for reference but cannot be changed without re-flashing.

### Wear

| Param | Default | Range | Unit | Persists | NVS namespace |
|---|---:|---|---|---|---|
| `motion_thresh_mg` | 50 | 10–510 | mg | yes | `cfg` |
| `still_sec` | 5 | 1–30 | s | yes | `cfg` |
| `offbody_sec` | 30 | 5–300 | s | yes | `cfg` |
| `cal_x_mg`, `cal_y_mg`, `cal_z_mg` | 0 | ±2000 | mg | yes | `cfg` |
| `wear_force` | auto | auto/on/off | — | no (volatile) | RAM |

### Steps

| Param | Default | Range | Unit | Persists | NVS namespace |
|---|---:|---|---|---|---|
| `step_thresh_mg` | 200 | 50–1000 | mg | yes | `cfg` |
| `step_min_ms` | 300 | 50–5000 | ms | yes | `cfg` |
| `step_max_ms` | 1500 | 100–10000 | ms | yes | `cfg` |

### Respiration

| Param | Default | Range | Unit | Persists | NVS namespace |
|---|---:|---|---|---|---|
| `bpm_min` | 8 | 1–30 | BPM | yes | `cfg` |
| `bpm_max` | 30 | 5–60 | BPM | yes | `cfg` |
| `resp_window_sec` | 10 | 5–15 | s | yes | `cfg` |

### Voice

| Param | Default | Range | Unit | Persists | NVS namespace |
|---|---:|---|---|---|---|
| `vad_threshold` | 150 | 0–5000 | RMS | yes | `voice` |
| `silence_ms` | 1500 | 200–10000 | ms | yes | `voice` |
| `timeout_ms` | 10000 | 1000–60000 | ms | yes | `voice` |
| `gain_shift` | 14 | 8–18 | bit-shift | yes | `voice` |
| `dc_blocker` | true | bool | — | yes | `voice` |
| `noise_gate` | 0 | 0–5000 | abs sample | yes | `voice` |

### STT

| Param | Default | Range | Persists | NVS namespace |
|---|---:|---|---|---|
| `key` | (none) | string | yes | `stt` |
| `model` | `nova-2` | string | yes | `stt` |

---

## Persistence map (NVS)

The bench uses three NVS namespaces. Every tunable above maps to one.

| Namespace | Owner | What's stored | Wiped by |
|---|---|---|---|
| `cfg` | `config.cpp` | wear / step / resp thresholds, accel calibration, WiFi STA creds | full flash erase |
| `voice` | `voice.cpp` | VAD threshold, silence ms, timeout, gain shift, DC blocker, noise gate | full flash erase |
| `stt` | `stt.cpp` | Deepgram API key, model | full flash erase |

`POST /settings/export` round-trips `cfg` only. `voice` and `stt` are
deliberately excluded (API key is sensitive; voice DSP is rig-specific).

A full `esptool erase-flash` wipes all three — that's the recovery path when
upload gets stuck. Re-enter calibration (Tune → Calibrate), Deepgram key
(Voice → Cloud STT), and STA creds (Network) afterwards.

---

## Glossary

- **mg** — milli-g; 1 mg = 1/1000 of 1 g of acceleration; 1 g ≈ 1000 mg.
- **ODR** — output data rate; how many samples per second the accel produces.
- **EMA** — exponential moving average; `y += (x − y) × α`. Slow / fast tracking via α.
- **IIR** — infinite-impulse-response filter; recursive, cheap, can do HPF / LPF / band-pass with one or two taps. We use them for DC blocker and proposed band-pass.
- **HPF / LPF / BPF** — high / low / band-pass filter.
- **RMS** — root-mean-square; energy-equivalent amplitude of a buffer of samples.
- **VAD** — voice-activity detection; here, a simple "is RMS above threshold for long enough?" gate.
- **Q15** — fixed-point format; integer where 32768 represents 1.0. Lets us multiply small fractions without floats.
- **NVS** — non-volatile storage; ESP32's flash-backed key-value store, accessed via `Preferences`.
- **DC offset** — constant signal component that's not actually sound; INMP441 has one and the DC blocker removes it.
