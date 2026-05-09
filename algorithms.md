# algorithms.md — what each algorithm does, why, and which knob does what

This doc is the **single source of truth for the algorithm logic** running on
the bench: wear detection, respiration BPM, step count, and voice DSP. Each
section gives the current code's behaviour, what each tunable controls, and
where the logic is known to be weak. Read `app.md` for the HTTP shape and
`nrf connect.md` for the port plan; the same constants port to nRF directly.

All units are **mg** (1 mg = 1/1000 of 1 g; gravity ≈ 1000 mg) unless noted.
The **sample loop ODR is 100 Hz** (Phase 2.5 bump). The 25 Hz consumers
(wear, respiration, steps) are fed via 1-in-4 decimation. The 100 Hz path
is consumed by **cough** (and **fall** in commit 4).

## Index

1. [Device modes (OFF / NORMAL / TURBO / AUTO)](#device-modes)
2. [Wear detection](#wear-detection)
3. [Respiration BPM](#respiration-bpm)
4. [Step counting](#step-counting)
5. [Cough detection](#cough-detection)
6. [Slouch detection](#slouch-detection)
7. [Voice DSP](#voice-dsp)
8. [Tunables — full table by subsystem](#tunables--full-table-by-subsystem)
9. [Persistence map (NVS)](#persistence-map-nvs)
10. [Glossary](#glossary)

---

## Device modes

`firmware/mode.cpp` exposes four modes that gate the ionizer behaviour:

| Mode | Ionizer rule | Use |
|---|---|---|
| `OFF` | always off (overrides everything except battery) | manual disable |
| `NORMAL` | always on, ignoring wear (respects OFF + battery) | bench / lab |
| `TURBO` | same as NORMAL; flagged as boosted upstream | high-output session |
| `AUTO` | follows wear: on-body=on, off-body=off | production default |

Master gates apply to every mode: `OFF` short-circuits to false, and a
battery override (`battsim_overrides_ionizer()`) forces false regardless of
mode. Mode is persisted in NVS under key `app_mode`; new devices boot AUTO.

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

### v2 (shipped) — three signals OR'd

`firmware/wear.cpp` now computes three signals; **any one** keeps the device
ON-BODY. None of three for `offbody_sec` consecutive seconds → OFF-BODY.

| Signal | Worn (still body) | On a desk | How |
|---|---|---|---|
| Mean absolute deviation of magnitude over 1 s | 5–20 mg | < 1 mg | `compute_mad()` over the last 25 samples — proxy for std-dev, no sqrt |
| Gravity-vector diff over 5 s | constant micro-drift (≥ 17 mg per 1° rotation) | pinned ±0.1° | `|g_now − g_5s_ago|` from a 5-entry ring of 1-second-averaged xyz |
| Hardware motion IRQ > `motion_thresh_mg` | rare bursts | none | MPU6050 INT line, kept as a fast wake/refresh path |

The state machine pseudocode:

```
on every accel sample (25 Hz):
    wear_feed_sample(x, y, z, mag)            # update MAD ring + grav accumulator
on motion IRQ:
    last_irq_ms := now
    if state == OFF-BODY: state := ON-BODY    # immediate wake
every 1 s tick:
    var       := MAD over last 25 mag samples
    grav_diff := |current 1-s avg − ring[oldest]|   if ring full
    sign      := (var > VAR_THR) OR (grav_diff > GRAV_THR)
                  OR (now − last_irq_ms < offbody_sec·1000)
    if sign: offbody_count := 0
    else:    offbody_count++; if >= offbody_sec → OFF-BODY
```

`still_sec` from v1 is **retired**. The cfg field stays for migration but the
state machine ignores it. Production silicon (LIS2DW12) does this in hardware
via the activity / inactivity classifier; the bench logic is the software
equivalent.

### Wear tunables

| Knob | Default | Range | Unit | Meaning |
|---|---:|---|---|---|
| `motion_thresh_mg` | 50 | 10–510 | mg | MPU6050 hardware motion-detect threshold |
| `offbody_sec` | 30 | 5–300 | s | seconds with no signal of life before declaring OFF-BODY |
| `wear_var_thresh_mg` | 5 | 1–50 | mg | MAD threshold for "body micro-motion present" |
| `wear_grav_diff_thresh_mg` | 20 | 5–200 | mg | gravity-vector 5-sec diff threshold (≈ 17 mg per 1° rotation) |
| `still_sec` | 5 | 1–30 | s | **retired by v2**; kept in cfg for migration |
| `wear_force` | auto | auto/on/off | — | UI override; bypasses the state machine |

Live diagnostics are exposed via `GET /data2` — current var, current grav-diff,
and which of the three signals last kept the device ON. The Live tab wear card
surfaces all three so you can see what's keeping the SM alive.

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

### v2 (shipped) — online peak detector + per-breath events + waveform

The v1 windowed BPM is **kept as-is** and still drives the Live tab BPM card.
Running in parallel:

1. **Running mean** via IIR (Q15 fixed point): `mean += α · (z − mean)`,
   α default 1638 ≈ 0.05 → ~4 s effective window. Tracks slow drift in the
   chest-axis baseline without being faked by individual breaths.
2. **Online peak detection** every sample: rising-cross of `mean` AND
   `now − last_peak_ms ≥ resp_min_interval_ms` triggers an event push.
3. **Event ring** — 64 entries of `{t_ms, amplitude_mg, interval_ms}`,
   surfaced via `GET /resp/events?since=<t_ms>`.
4. **Wave ring** — 300 paired `(z, mean)` samples (~12 s @ 25 Hz),
   surfaced via `GET /resp/wave?n=<N>`.
5. **Instantaneous BPM** = `60000 / last_interval_ms` exposed in `/data2`.

The Tune tab now shows a live **breath waveform** canvas: blue line = z,
grey line = running mean, red ticks = detected peaks. Direct visual
validation — "I just took a breath at the marker, did a tick appear?"

When the windowed BPM filter zeroes the value (raw outside [bpm_min,
bpm_max]), the Live BPM card now shows the raw value as a sub-line so
the failure is no longer silent.

### v3 (shipped) — full literature-grounded pipeline

The v2 detector is kept and now consumes the **post-fusion composite** instead
of a raw axis. The full per-sample @ 25 Hz pipeline:

```
raw x_mg, y_mg, z_mg
  ↓ 5-tap median per axis              (spike rejection — VMD paper trick)
  ↓ 4th-order Butterworth band-pass    (cascade: HPF biquad + LPF biquad)
       per axis, default 0.1–0.8 Hz    (Schipper 4th-order LPF 0.8 Hz)
  ↓ PCA-lite axis fusion               (variance-weighted sum;
       weights = EMAvar_i / Σ EMAvar    weights tracked over ~4 s)
  ↓ Schmitt rising-cross @ mean        (re-arm requires dipping below mean−H
       with hysteresis H_mg              before another cross above mean+H counts)
  ↓ existing min_interval + min_amp gates
  → emit event {t, amp, interval}     (waveform + event ring unchanged)
```

A second track runs in parallel: every 5 s, **autocorrelation** on the last
N s of the composite (N = `resp_autocorr_window_sec`, default 30 s). The
search is restricted to lags ∈ [31, 250] @ 25 Hz, i.e. the 12–48 BPM band.
BPM = 1500 / arg-max-lag. Confidence is a z-score-style peak prominence:
`(rxx[k*] − mean) / std × 30`, clamped 0..100. ~165 k mults per run, one
run per 5 s — ~33 k mults/s on the ESP32 FPU, well under capacity.

Why every step exists:
- **Median filter**: motion-induced spikes (placement, hand contact) corrupt
  the band-passed signal for many seconds. Median is non-linear and rejects
  them without smearing breath edges.
- **Per-axis Butterworth band-pass**: the breath band sits below 0.8 Hz for
  rest, ≤0.7 Hz under exertion; everything above is heartbeat/motion, below
  is gravity drift.
- **PCA-lite**: Schipper et al. report single-axis Z LoA of ±18 BPM but
  axis-fused (PCA) LoA of ±2 BPM — about 9× tighter — when the orientation
  varies during wear.
- **Schmitt hysteresis**: kills false re-triggers around the mean line
  caused by ripple in the mean tracker.
- **Adaptive band-pass**: on activity, breath rate climbs and the chest-axis
  motion broadens — the cutoffs widen accordingly.
- **Autocorrelation BPM**: independent BPM estimate that doesn't depend on
  detecting individual events. When the per-event detector is in a noisy
  regime, autocorrelation often still locks on the periodicity.

Diagnostics on `/data2.resp`:
- `pca_w{x,y,z}_q8` — current axis weights × 256 (sum ≈ 256)
- `bp_low_active_x10`, `bp_high_active_x10` — current Butterworth cutoffs
  (in 0.1 Hz units), reflect adaptive override when on
- `activity_mg2_x10` — overall accel variance proxy in 0.1 mg² units
- `bpm_autocorr` + `bpm_autocorr_confidence_pct` + `autocorr_age_ms`

The Breath tab now shows a live PCA weight strip ("PCA  X 0.62  Y 0.21  Z 0.17"),
the active band-pass band, the activity, and a fifth card for autocorr BPM.

### Respiration tunables

| Knob | Default | Range | Unit | Meaning |
|---|---:|---|---|---|
| `bpm_min` | 8 | 1–30 | BPM | windowed BPM filter low edge |
| `bpm_max` | 30 | 5–60 | BPM | windowed BPM filter high edge |
| `resp_window_sec` | 10 | 5–15 | s | length of the windowed-BPM ring |
| `resp_min_interval_ms` | 1500 | 300–10000 | ms | online detector: reject peaks closer than this |
| `resp_iir_alpha_q15` | 1638 | 50–16384 | Q15 | online detector: mean-tracker speed |
| `resp_min_amplitude_mg` | 10 | 0–500 | mg | peak-to-peak gate |
| `resp_axis` | pca | z/x/y/pca | — | detection axis; `pca` = variance-weighted fusion |
| `resp_median_len` | 5 | 1/3/5/7/9 | taps | per-axis median (1 = off) |
| `resp_bp_low_hz_x10` | 1 | 0–5 | 0.1 Hz | Butterworth HPF cutoff (0 = off) |
| `resp_bp_high_hz_x10` | 8 | 0–12 | 0.1 Hz | Butterworth LPF cutoff (0 = off) |
| `resp_hysteresis_mg` | 0 | 0–30 | mg | Schmitt re-arm deadband (0 = legacy) |
| `resp_adaptive_bp` | false | bool | — | activity-driven cutoff override |
| `resp_autocorr_window_sec` | 30 | 10–60 | s | autocorr analysis window |
| `RESP_SAMPLE_HZ` | 25 | compile-time | Hz | must match ODR |
| `RESP_EVENT_RING_SIZE` | 64 | compile-time | — | how many recent peaks to keep |
| `RESP_WAVE_RING_SIZE` | 300 | compile-time | — | wave-ring length (~12 s @ 25 Hz) |

Adaptive band-pass map (when `resp_adaptive_bp = true`):

| Magnitude variance EMA | Active band |
|---|---|
| < 50 mg² (sitting / resting) | 0.1–0.5 Hz |
| 50–500 mg² (walking) | 0.2–0.6 Hz |
| ≥ 500 mg² (running / very active) | 0.3–0.7 Hz |

> **References (consensus from the four cited papers).**
> [1] Schipper et al. — abdominal accelerometry: 4th-order Butterworth LPF at
> 0.8 Hz, PCA across XYZ, 60-s autocorrelation. Single-axis Z gave Limits of
> Agreement of ±18 BPM; PCA-fused gave ±2 BPM (~9× tighter).
> [2] PMC3203837 — adaptive Butterworth band-pass tied to activity:
> rest 0.2–0.4 Hz, walking 0.2–0.6 Hz, running 0.3–0.7 Hz; HPF at 1 Hz.
> [3] IEEE 10932997 (VMD paper) — preprocessing chain: normalize → median
> filter → VMD → IMF₂ → peak detect. Median filter is the spike-rejection
> step adopted here.
> [4] Schipper et al. (autocorr methodology) — autocorrelation on a long
> centered window with the dominant lag in the breath band as the BPM
> estimator; confidence from peak prominence over the lag-result mean.

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

### v2 (shipped) — adaptive + bandpass + events + ground-truth

All four toggles default **off** so behavior on a fresh build is byte-identical
to v1. Flip them on the Tune tab as you tune.

1. **Adaptive threshold** (`step_adaptive`): track `amp = max(delta) − min(delta)`
   over a sliding `step_amp_window_ms` window. Effective threshold becomes
   `max(STEP_ADAPTIVE_MIN_MG = 80, 50 % × amp)`. Auto-calibrates per user.
2. **Band-pass IIR** (`step_bandpass`): cascaded single-pole HPF (0.5 Hz) +
   LPF (3 Hz), Q15 fixed point. When enabled, replaces the slow-EMA baseline
   path entirely (`detect_signal = bandpass(axis_signal)`). Kills typing /
   clapping / driving false positives.
3. **Per-step event stream** — `GET /steps/events?since=<t_ms>` returns
   events with `{t, amplitude_mg, interval_ms}` plus the current signal
   and effective threshold. Drives the live trace plot.
4. **Ground-truth tap**:
   - `POST /steps/groundtruth` records the tap timestamp.
   - `POST /steps/groundtruth/clear` empties the GT ring.
   - `GET /steps/eval?window_sec=&tol_ms=` returns
     `{detected, groundtruth, matched, precision_pct, recall_pct}`
     via greedy nearest-match within tolerance.
5. **Per-axis selection** (`step_axis`): `mag` / `x` / `y` / `z`. Wrist
   swing on X/Y often gives a cleaner walking signature than collapsed
   magnitude.

The Tune tab now shows a live **step trace** canvas (60 s rolling window):
blue = signal, red dashed = effective threshold, green ticks = detected
steps, yellow ticks = ground-truth taps. Direct visual comparison of what
the algorithm sees vs what your foot does.

50 Hz ODR mode is **not** in this drop — same caveat as before, leaving
it as a deliberate next move.

### Steps tunables

| Knob | Default | Range | Unit | Meaning |
|---|---:|---|---|---|
| `step_thresh_mg` | 200 | 50–1000 | mg | base threshold (used when `step_adaptive=off`) |
| `step_min_ms` | 300 | 50–5000 | ms | minimum interval between steps |
| `step_max_ms` | 1500 | 100–10000 | ms | maximum interval between steps |
| `step_adaptive` | off | bool | — | when on, threshold = max(80, 50 %·amp) |
| `step_bandpass` | off | bool | — | 0.5–3 Hz IIR cascade replaces EMA baseline |
| `step_axis` | mag | mag/x/y/z | — | which signal to peak-detect on |
| `step_amp_window_ms` | 2000 | 500–5000 | ms | window for adaptive amplitude tracker |
| `STEP_ADAPTIVE_MIN_MG` | 80 | compile-time | mg | floor for adaptive threshold |
| `STEP_HPF_ALPHA_Q15` | 28890 | compile-time | Q15 | bandpass HPF α (≈ 0.88, fc ≈ 0.5 Hz) |
| `STEP_LPF_ALPHA_Q15` | 15422 | compile-time | Q15 | bandpass LPF α (≈ 0.47, fc ≈ 3 Hz) |
| `STEP_EVENT_RING_SIZE` | 64 | compile-time | — | event ring depth |
| `STEP_GT_RING_SIZE` | 128 | compile-time | — | ground-truth tap ring depth |

---

## Cough detection

Source: `firmware/cough.cpp`. Pipeline runs at the full **100 Hz** sample
rate.

### Pipeline

```
mag = √(x² + y² + z²)
ac  = mag − slow_IIR_mean(mag, α=0.02)         # ~2 s mean tracker
bp  = biquad_HPF(5 Hz) → biquad_LPF(15 Hz)     # 2nd-order Butterworth pair
env = α=0.1 LPF of |bp|                        # ~100 ms envelope
peak: rising edge of env across thresh_mg, ≥ 100 ms since last peak
cluster: ≥ min_peaks peaks within cluster_window_ms → fire event
post-fire lockout: 1500 ms
```

### Why this band

A cough's mechanical signature on a wrist accelerometer is a brief 80–200 ms
burst that sits centered around 8–12 Hz. The 5 Hz HPF kills slow body
motion (gait, breath, postural sway) and the 15 Hz LPF kills sensor noise
and electrical pickup. The envelope LPF averages the rectified energy so a
single 200 ms burst becomes a single rounded peak.

### Cluster gate

A real cough often produces 2–4 sharp pulses in quick succession (initial
glottal closure + reflex bursts). Requiring ≥ `min_peaks` (default 3)
within `cluster_window_ms` (default 800) inside the envelope rejects most
single-shock false positives (door slam, table thump, button press).

The post-fire lockout (1.5 s) prevents one prolonged cough from firing
twice.

### Ground-truth eval

`POST /cough/gt` records a tap timestamp. `GET /cough/eval?window_ms=1500`
runs greedy nearest-match: each GT tap claims the nearest unmatched
detection within ±window_ms; unmatched detections become FP, unmatched
GT become FN. Returns `{tp, fp, fn, precision_pct, recall_pct}`.
Matches the pattern used by `steps_eval()`.

### Known limitations

- **Talking** can produce envelope peaks if the user is gesturing
  vigorously (false positives). Mitigation: bump `min_peaks` to 4 in
  conversational contexts.
- **Phone vibration** held against the wrist looks identical to a cough on
  a wrist accel. Out of scope to discriminate.
- **Single sharp coughs** (one big pulse, no reflex bursts) require
  `min_peaks=2` to be detected.
- **Off-wrist activity** is ignored: the firmware feeds samples to
  `cough_feed_sample` only when `wear_get_state() == ON_BODY`.

### Cough tunables

| Param | Default | Range | Unit | Persists | NVS key |
|---|---:|---|---|---|---|
| `cough_thresh_mg` | 80 | 10–1000 | mg | yes | `c_thr` |
| `cough_cluster_ms` | 800 | 200–3000 | ms | yes | `c_cwin` |
| `cough_min_peaks` | 3 | 1–8 | count | yes | `c_mp` |

Compile-time:

| Constant | Value | Notes |
|---|---:|---|
| `COUGH_BP_LOW_HZ` | 5 | band-pass low cutoff |
| `COUGH_BP_HIGH_HZ` | 15 | band-pass high cutoff |
| `COUGH_PEAK_MIN_INTERVAL_MS` | 100 | min spacing between peaks |
| `COUGH_LOCKOUT_MS` | 1500 | post-fire silence |
| `COUGH_AC_ALPHA_Q15` | 655 | AC mean tracker (~2 s) |
| `COUGH_ENV_ALPHA_Q15` | 3277 | envelope LPF (~100 ms) |

---

## Slouch detection

Source: `firmware/slouch.cpp`. Pipeline runs at the **25 Hz** decimated path
(slouch is slow — 100 Hz is wasted on it).

### Pipeline

```
xyz → very-slow IIR per axis (α≈0.005, ~8 s effective) → gravity vector
gravity → atan2(gx, √(gy²+gz²)) × 180/π → pitch (deg×10)
deviation = pitch − baseline
state machine, ticked once/second:
  if |dev| > thresh_deg:
    above_sec++; below_sec=0
    if above_sec ≥ sustain_sec and state != SLOUCHING:
      state ← SLOUCHING; record session_start; bump session_count
  else:
    below_sec++; above_sec=0
    if state == SLOUCHING and below_sec ≥ SLOUCH_RECOVER_SEC (=2):
      state ← UPRIGHT; emit session event
```

### Calibration

Baseline pitch is captured by `POST /slouch/calibrate` (UI button "Sit Up
Straight + Calibrate Now" with a 3 s countdown). The captured pitch is
written to NVS as `sl_base` (deg×10) and to runtime via
`slouch_set_baseline_deg_x10()`. Until calibration the state is
`SLOUCH_UNKNOWN`.

### Why one axis

We use pitch (forward/back tilt) rather than full 3D orientation because:

1. The wrist-worn target rotates freely about the lateral axis; only
   forward/back tilt of the spine signals slouch.
2. atan2 of two scalars is one float, not a quaternion math stack.
3. Roll variation (wrist twist) and yaw (turning) are noise from the
   posture-detection point of view.

### Known limitations

- **Lying down** registers as a different baseline pitch and would fire
  SLOUCHING. Production firmware should gate slouch on
  `wear_get_state() == ON_BODY` plus an "is the user roughly vertical"
  check (we do the first; the second is left to a follow-up).
- **Wrist orientation** matters: if the user re-mounts the device with
  the AD0/Vin pins flipped, baseline is invalidated and re-calibration
  is required. Production hardware fixes orientation.
- **Slow drift** over hours: the IIR is α≈0.005 (very slow), so gradual
  posture changes (5 minutes of slowly slumping) get tracked into the
  baseline rather than registering as slouch. Trade-off: this also makes
  the algorithm robust to wrist re-orientation. Tune
  `SLOUCH_GRAV_ALPHA_Q15` if you need different behaviour.

### Slouch tunables

| Param | Default | Range | Unit | Persists | NVS key |
|---|---:|---|---|---|---|
| `slouch_baseline_deg_x10` | 0 | ±900 | deg×10 | yes (via calibrate) | `sl_base` |
| `slouch_thresh_deg` | 15 | 1–60 | deg | yes | `sl_thr` |
| `slouch_sustain_sec` | 5 | 1–60 | s | yes | `sl_sus` |

Compile-time:

| Constant | Value | Notes |
|---|---:|---|
| `SLOUCH_RECOVER_SEC` | 2 | seconds below thr to end session |
| `SLOUCH_GRAV_ALPHA_Q15` | 164 | gravity tracker α≈0.005 (~8 s) |

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
| `offbody_sec` | 30 | 5–300 | s | yes | `cfg` |
| `wear_var_thresh_mg` | 5 | 1–50 | mg | yes | `cfg` |
| `wear_grav_diff_thresh_mg` | 20 | 5–200 | mg | yes | `cfg` |
| `still_sec` | 5 | 1–30 | s (retired) | yes | `cfg` |
| `cal_x_mg`, `cal_y_mg`, `cal_z_mg` | 0 | ±2000 | mg | yes | `cfg` |
| `wear_force` | auto | auto/on/off | — | no (volatile) | RAM |

### Steps

| Param | Default | Range | Unit | Persists | NVS namespace |
|---|---:|---|---|---|---|
| `step_thresh_mg` | 200 | 50–1000 | mg | yes | `cfg` |
| `step_min_ms` | 300 | 50–5000 | ms | yes | `cfg` |
| `step_max_ms` | 1500 | 100–10000 | ms | yes | `cfg` |
| `step_adaptive` | false | bool | — | yes | `cfg` |
| `step_bandpass` | false | bool | — | yes | `cfg` |
| `step_axis` | mag | mag/x/y/z | — | yes | `cfg` |
| `step_amp_window_ms` | 2000 | 500–5000 | ms | yes | `cfg` |

### Respiration

| Param | Default | Range | Unit | Persists | NVS namespace |
|---|---:|---|---|---|---|
| `bpm_min` | 8 | 1–30 | BPM | yes | `cfg` |
| `bpm_max` | 30 | 5–60 | BPM | yes | `cfg` |
| `resp_window_sec` | 10 | 5–15 | s | yes | `cfg` |
| `resp_min_interval_ms` | 1500 | 300–10000 | ms | yes | `cfg` |
| `resp_iir_alpha_q15` | 1638 | 50–16384 | Q15 | yes | `cfg` |
| `resp_min_amplitude_mg` | 10 | 0–500 | mg | yes | `cfg` |
| `resp_axis` | pca | z/x/y/pca | — | yes | `cfg` |
| `resp_median_len` | 5 | 1/3/5/7/9 | taps | yes | `cfg` |
| `resp_bp_low_hz_x10` | 1 | 0–5 | 0.1 Hz | yes | `cfg` |
| `resp_bp_high_hz_x10` | 8 | 0–12 | 0.1 Hz | yes | `cfg` |
| `resp_hysteresis_mg` | 0 | 0–30 | mg | yes | `cfg` |
| `resp_adaptive_bp` | false | bool | — | yes | `cfg` |
| `resp_autocorr_window_sec` | 30 | 10–60 | s | yes | `cfg` |

### Cough

| Param | Default | Range | Unit | Persists | NVS namespace |
|---|---:|---|---|---|---|
| `cough_thresh_mg` | 80 | 10–1000 | mg | yes | `cfg` |
| `cough_cluster_ms` | 800 | 200–3000 | ms | yes | `cfg` |
| `cough_min_peaks` | 3 | 1–8 | count | yes | `cfg` |

### Slouch

| Param | Default | Range | Unit | Persists | NVS namespace |
|---|---:|---|---|---|---|
| `slouch_baseline_deg_x10` | 0 | ±900 | deg×10 | yes (via calibrate) | `cfg` |
| `slouch_thresh_deg` | 15 | 1–60 | deg | yes | `cfg` |
| `slouch_sustain_sec` | 5 | 1–60 | s | yes | `cfg` |

### Mode

| Param | Default | Range | Persists | NVS namespace |
|---|---:|---|---|---|
| `mode` | `auto` | off/normal/turbo/auto | yes | `cfg` |

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
