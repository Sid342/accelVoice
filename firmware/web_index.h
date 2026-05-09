#pragma once
#include <Arduino.h>

/* atovio-accel-bench-esp32 — embedded web viewer (v3, tabbed).
 * Single PROGMEM HTML doc, vanilla JS. /data polled at 10 Hz.
 * Other endpoints fetched on-demand when tab opens.                          */
static const char WEB_INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>atovio bench</title>
<style>
:root{
  --bg:#0a0a0c; --panel:#15151a; --panel-2:#1d1d24; --line:#26262e;
  --text:#e8e8ec; --dim:#7a7a85; --muted:#52525b;
  --green:#34d399; --red:#f87171; --amber:#fbbf24; --blue:#60a5fa; --pink:#fb7185;
}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--text);
  font:14px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;
  padding-bottom:40px}
header{position:sticky;top:0;background:rgba(10,10,12,0.92);backdrop-filter:blur(8px);
  z-index:100;border-bottom:1px solid var(--line);padding:10px 14px}
.brand{display:flex;align-items:center;gap:10px;margin-bottom:8px;flex-wrap:wrap}
.brand h1{font-size:14px;font-weight:600;letter-spacing:-0.01em}
.brand .badge{font-size:10px;color:var(--dim);background:var(--panel);padding:2px 6px;border-radius:4px}
.brand .status{display:flex;align-items:center;gap:6px;margin-left:auto;font-size:12px;color:var(--dim)}
.dot{width:8px;height:8px;border-radius:50%;background:var(--muted);transition:background .2s}
.dot.live{background:var(--green);box-shadow:0 0 8px rgba(52,211,153,0.5)}
.dot.warn{background:var(--amber)}
.dot.err{background:var(--red)}
nav{display:flex;gap:2px;overflow-x:auto;-webkit-overflow-scrolling:touch}
nav button{background:transparent;color:var(--dim);border:0;padding:8px 12px;border-radius:6px;
  cursor:pointer;font-size:13px;font-weight:500;white-space:nowrap;transition:all .15s}
nav button:hover{color:var(--text);background:var(--panel)}
nav button.active{color:var(--green);background:var(--panel-2)}
main{max-width:1100px;margin:14px auto;padding:0 14px}
.tab{display:none}
.tab.active{display:block}

.section{background:var(--panel);border-radius:10px;padding:14px;margin-bottom:12px;border:1px solid var(--line)}
.section h2{font-size:11px;font-weight:600;color:var(--dim);text-transform:uppercase;
  letter-spacing:0.08em;margin-bottom:10px;display:flex;align-items:center;gap:8px}
.section h2 .hint{font-size:11px;font-weight:400;color:var(--muted);text-transform:none;letter-spacing:0}

.cards{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}
@media(max-width:640px){.cards{grid-template-columns:repeat(2,1fr)}}
.card{background:var(--panel-2);border-radius:8px;padding:12px;border:1px solid transparent;transition:border-color .2s}
.card.bpm-valid{border-color:rgba(52,211,153,0.35)}
.card.bpm-invalid{border-color:rgba(248,113,113,0.25)}
.card .lbl{font-size:10px;color:var(--dim);text-transform:uppercase;letter-spacing:0.05em}
.card .val{font-size:28px;font-weight:600;margin-top:3px;font-variant-numeric:tabular-nums;line-height:1.1}
.card .val.on{color:var(--green)}
.card .val.off{color:var(--red)}
.card .val.warn{color:var(--amber)}
.card .sub{font-size:11px;color:var(--muted);margin-top:3px;font-variant-numeric:tabular-nums}

canvas{width:100%;height:180px;background:var(--panel-2);border-radius:8px;display:block}

.tog{display:flex;flex-wrap:wrap;gap:6px;margin:10px 0 6px}
.tog label{display:inline-flex;align-items:center;gap:6px;background:var(--panel-2);padding:5px 10px;
  border-radius:6px;cursor:pointer;font-size:12px;border:1px solid transparent;transition:all .15s}
.tog label:has(input:checked){border-color:var(--green);color:var(--green)}
.tog input{accent-color:var(--green);cursor:pointer}

.row{display:flex;gap:10px;align-items:center;margin:8px 0;flex-wrap:wrap}
.row label{font-size:12px;color:var(--dim);min-width:140px}
.row input[type=range]{flex:1;min-width:120px;accent-color:var(--green);cursor:pointer}
.row input[type=text],.row input[type=password]{flex:1;background:var(--panel-2);
  color:var(--text);border:1px solid var(--line);border-radius:6px;padding:6px 10px;font-size:13px}
.row input[type=text]:focus,.row input[type=password]:focus{outline:0;border-color:var(--green)}
.row .num{font-variant-numeric:tabular-nums;color:var(--text);min-width:74px;text-align:right;font-size:12px}

button.btn{background:var(--panel-2);color:var(--text);border:1px solid var(--line);
  border-radius:6px;padding:7px 12px;cursor:pointer;font-size:12px;font-weight:500;transition:all .15s}
button.btn:hover{background:#23232b;border-color:var(--muted)}
button.btn.act{background:#0e2620;border-color:#1f4f3f;color:var(--green)}
button.btn.act:hover{background:#0f3329}
button.btn.warn{background:#321a16;border-color:#5a2e22;color:#f87171}
button.btn.warn:hover{background:#411f1a}
button.btn.primary{background:#0c2540;border-color:#1d4d7d;color:var(--blue)}
button.btn.primary:hover{background:#0e2c4d}

.actions{display:flex;flex-wrap:wrap;gap:6px;margin-top:6px}

select{background:var(--panel-2);color:var(--text);border:1px solid var(--line);
  border-radius:6px;padding:5px 8px;cursor:pointer;font-size:12px}

.modeline{display:flex;align-items:center;gap:8px;flex-wrap:wrap;margin-top:6px;font-size:12px}
.modeline label{display:inline-flex;align-items:center;gap:5px;cursor:pointer}

pre{background:#000;color:#9ee0ff;padding:8px;border-radius:6px;font-size:11px;
  overflow-x:auto;font-family:ui-monospace,Menlo,monospace;margin-top:6px}
.mlog{font-family:ui-monospace,Menlo,monospace;font-size:11px;color:var(--dim);
  padding:10px;background:var(--panel-2);border-radius:6px;min-height:32px}

.kv{display:grid;grid-template-columns:auto 1fr;gap:6px 14px;font-size:12px}
.kv dt{color:var(--dim)}
.kv dd{font-family:ui-monospace,Menlo,monospace;color:var(--text);word-break:break-all}

.note{font-size:11px;color:var(--muted);margin-top:6px;line-height:1.5}
.banner{padding:10px 12px;border-radius:8px;font-size:13px;margin-bottom:10px}
.banner.warn{background:rgba(251,191,36,0.10);border:1px solid rgba(251,191,36,0.3);color:var(--amber)}
.banner.ok{background:rgba(52,211,153,0.10);border:1px solid rgba(52,211,153,0.3);color:var(--green)}
hr{border:0;border-top:1px solid var(--line);margin:12px 0}
</style></head><body>

<header>
  <div class="brand">
    <h1>atovio · accel bench</h1>
    <span class="badge">esp32 · v3</span>
    <div class="status"><span class="dot" id="dot"></span><span id="state">connecting…</span></div>
  </div>
  <nav id="tabnav">
    <button data-tab="live"    class="active">Live</button>
    <button data-tab="tune">Tune</button>
    <button data-tab="system">System</button>
    <button data-tab="network">Network</button>
    <button data-tab="find">Find</button>
    <button data-tab="voice">Voice</button>
  </nav>
</header>

<main>

<!-- ─────────────── LIVE ─────────────── -->
<div class="tab active" id="tab-live">
  <div class="section">
    <h2>Live readings</h2>
    <div class="cards">
      <div class="card"><div class="lbl">Wear</div><div id="w" class="val off">…</div><div class="sub" id="ws">—</div><div class="sub" id="wsig" style="font-size:11px;opacity:0.7">—</div></div>
      <div class="card"><div class="lbl">Steps</div><div id="st" class="val">0</div><div class="sub" id="cad">—</div></div>
      <div class="card" id="bpmcard"><div class="lbl">Respiration</div><div id="bpm" class="val">0</div><div class="sub" id="bsub">—</div></div>
      <div class="card"><div class="lbl">Ionizer (sim)</div><div id="ion" class="val off">OFF</div><div class="sub" id="isub">—</div></div>
    </div>
  </div>

  <div class="section">
    <h2>Plot <span class="hint">— toggle axes, ~40 s history</span></h2>
    <div class="tog">
      <label><input type="checkbox" data-axis="x"> X</label>
      <label><input type="checkbox" data-axis="y"> Y</label>
      <label><input type="checkbox" data-axis="z" checked> Z</label>
      <label><input type="checkbox" data-axis="mag"> |mag|</label>
    </div>
    <canvas id="plot" width="900" height="180"></canvas>
  </div>

  <div class="section">
    <h2>Motion event log <span class="hint">— last 20 hardware IRQ events (sec ago)</span></h2>
    <div class="mlog" id="mlog">—</div>
  </div>
</div>

<!-- ─────────────── TUNE ─────────────── -->
<div class="tab" id="tab-tune">
  <div class="section">
    <h2>Wear detection</h2>
    <div class="row"><label>Motion threshold</label>
      <input type="range" id="motThr" min="10" max="200" step="2" value="50">
      <span id="motThrV" class="num">50 mg</span></div>
    <div class="row"><label>Still seconds</label>
      <input type="range" id="stillS" min="1" max="30" step="1" value="5">
      <span id="stillSV" class="num">5 s</span></div>
    <div class="row"><label>Off-body seconds</label>
      <input type="range" id="offS" min="5" max="120" step="1" value="30">
      <span id="offSV" class="num">30 s</span></div>
    <div class="row"><label>Variance threshold (v2)</label>
      <input type="range" id="wVar" min="1" max="50" step="1" value="5">
      <span id="wVarV" class="num">5 mg</span></div>
    <div class="row"><label>Gravity-diff threshold (v2)</label>
      <input type="range" id="wGrav" min="5" max="200" step="1" value="20">
      <span id="wGravV" class="num">20 mg</span></div>
    <div class="note">v2 keeps the device ON-BODY if <strong>any</strong> of three signals fire each second:
      magnitude variance &gt; var-thr, 5 s gravity-vector drift &gt; grav-thr, or hardware motion IRQ.
      <em>Still seconds</em> is no longer used (kept only for cfg migration).
      <strong>Live diagnostics</strong> on the wear card show which signal kept it on.</div>
  </div>

  <div class="section">
    <h2>Step counter</h2>
    <div class="row"><label>Peak threshold</label>
      <input type="range" id="stpThr" min="50" max="500" step="10" value="200">
      <span id="stpThrV" class="num">200 mg</span></div>
    <div class="row"><label>Min interval (fast)</label>
      <input type="range" id="stpMin" min="100" max="600" step="10" value="300">
      <span id="stpMinV" class="num">300 ms</span></div>
    <div class="row"><label>Max interval (slow)</label>
      <input type="range" id="stpMax" min="800" max="3000" step="50" value="1500">
      <span id="stpMaxV" class="num">1500 ms</span></div>
    <div class="note">|mag − baseline| &gt; threshold → peak. Counts only if min ≤ time-since-last ≤ max.</div>
  </div>

  <div class="section">
    <h2>Respiration / breathing sensitivity</h2>
    <div class="row"><label>BPM filter min</label>
      <input type="range" id="bpmMin" min="1" max="20" step="1" value="8">
      <span id="bpmMinV" class="num">8 BPM</span></div>
    <div class="row"><label>BPM filter max</label>
      <input type="range" id="bpmMax" min="15" max="60" step="1" value="30">
      <span id="bpmMaxV" class="num">30 BPM</span></div>
    <div class="row"><label>Window length</label>
      <input type="range" id="respWin" min="5" max="15" step="1" value="10">
      <span id="respWinV" class="num">10 s</span></div>
    <div class="note">Wider filter range = more readings (incl. noise). Shorter window = faster but less stable.
      Buffer reset whenever window changes.</div>
  </div>

  <div class="section">
    <h2>Calibration</h2>
    <div class="kv" id="calKV">
      <dt>Offset X</dt><dd id="calX">—</dd>
      <dt>Offset Y</dt><dd id="calY">—</dd>
      <dt>Offset Z</dt><dd id="calZ">—</dd>
    </div>
    <div class="actions" style="margin-top:10px">
      <button class="btn act" onclick="cal()">Calibrate (1 s flat-rest)</button>
    </div>
    <div class="note">Place flat on table, click → averages 1 s of XYZ, stores offset to NVS, target Z = +1000 mg.</div>
  </div>
</div>

<!-- ─────────────── SYSTEM ─────────────── -->
<div class="tab" id="tab-system">
  <div class="section">
    <h2>Mode + Wear override</h2>
    <div class="modeline">
      Mode:
      <select id="modeSel">
        <option value="off">OFF</option>
        <option value="normal" selected>NORMAL</option>
        <option value="turbo">TURBO</option>
      </select>
      Wear:
      <select id="wearSel">
        <option value="auto" selected>auto (state machine)</option>
        <option value="on">force ON</option>
        <option value="off">force OFF</option>
      </select>
    </div>
  </div>

  <div class="section">
    <h2>Battery simulator <span class="hint">— validates production override semantics</span></h2>
    <div class="row"><label>Battery %</label>
      <input type="range" id="batPct" min="0" max="100" value="80">
      <span id="batPctV" class="num">80</span></div>
    <div class="modeline">
      <label><input type="checkbox" id="batChg"> Charging</label>
      <label><input type="checkbox" id="batFlt"> Fault</label>
      <span id="batSt" style="color:var(--dim)"></span>
    </div>
    <div class="note">≤ 5% → CRITICAL · ≤ 15% → LOW_BAT · charging or fault → override. Any non-OK → ionizer forced off.</div>
  </div>

  <div class="section">
    <h2>Actions</h2>
    <div class="actions">
      <button class="btn" onclick="rst('/steps/reset')">Reset steps</button>
      <button class="btn" onclick="rst('/resp/reset')">Reset respiration</button>
      <button class="btn primary" onclick="t10()">Send Type 11 → see Type 10</button>
      <button class="btn" onclick="dl('/log.csv')">Download log.csv</button>
      <button class="btn" onclick="dl('/settings/export')">Export settings</button>
    </div>
    <pre id="t10out" style="display:none"></pre>
  </div>
</div>

<!-- ─────────────── NETWORK ─────────────── -->
<div class="tab" id="tab-network">
  <div class="section">
    <h2>WiFi</h2>
    <div class="kv">
      <dt>AP SSID</dt><dd id="apSsid">atovio-bench</dd>
      <dt>AP IP</dt><dd id="apIp">—</dd>
      <dt>STA status</dt><dd id="staStatus">—</dd>
      <dt>STA SSID</dt><dd id="staSsid">—</dd>
      <dt>STA IP</dt><dd id="staIp">—</dd>
      <dt>STA RSSI</dt><dd id="staRssi">—</dd>
    </div>
    <hr>
    <h2 style="margin-top:0">Join your home WiFi (STA)</h2>
    <div class="note" style="margin-bottom:8px">Once connected, the device is reachable on your home network at <code>atovio.local</code>. AP stays up as fallback.</div>
    <div class="row"><label>SSID</label>
      <input type="text" id="wifiSsid" placeholder="MyHomeWiFi" autocomplete="off"></div>
    <div class="row"><label>Password</label>
      <input type="password" id="wifiPass" placeholder="••••••••" autocomplete="off"></div>
    <div class="modeline" style="margin-top:6px">
      <label><input type="checkbox" id="wifiEn"> Enable STA mode</label>
      <button class="btn act" onclick="saveWifi()">Save &amp; connect</button>
    </div>
  </div>

  <div class="section">
    <h2>OTA firmware update</h2>
    <div class="kv" id="sysKV">
      <dt>Hostname</dt><dd>atovio.local</dd>
      <dt>OTA port</dt><dd>3232</dd>
      <dt>Build</dt><dd id="sysBuild">—</dd>
      <dt>Free heap</dt><dd id="sysHeap">—</dd>
      <dt>Chip</dt><dd id="sysChip">—</dd>
    </div>
    <div class="note" style="margin-top:8px">Push next firmware over WiFi (your machine must be on the same network as the ESP32):</div>
    <pre>arduino-cli upload --upload-port atovio.local --fqbn esp32:esp32:esp32 .</pre>
  </div>
</div>

<!-- ─────────────── FIND ─────────────── -->
<div class="tab" id="tab-find">
  <div class="section">
    <h2>LED strobe — find by sight</h2>
    <div class="actions">
      <button class="btn warn" id="findBtn" onclick="findStart()">Strobe LED for 30 s</button>
      <button class="btn" id="findStop" onclick="findStop()" style="display:none">Stop</button>
    </div>
    <div class="note" id="findState">Onboard blue LED flashes at 10 Hz so you can spot the board across a room.</div>
  </div>

  <div class="section">
    <h2>BLE proximity — find by signal strength</h2>
    <div class="actions">
      <button class="btn primary" id="bleBtn" onclick="bleStart()">Start BLE advertising (5 min)</button>
      <button class="btn" id="bleStop" onclick="bleStop()" style="display:none">Stop</button>
    </div>
    <div class="note" id="bleState"></div>
    <div class="note" style="margin-top:10px">
      While advertising, open any BLE scanner app on your phone:
      <ul style="margin:6px 0 6px 18px;color:var(--dim)">
        <li>iOS Settings → Bluetooth (look for <strong>atovio-bench</strong>)</li>
        <li>LightBlue (iOS / Android)</li>
        <li>nRF Connect (iOS / Android) — also lets you write to the ring characteristic to remotely strobe the LED</li>
      </ul>
      RSSI in dBm climbs (less negative) as you walk closer. -30 ≈ touching, -70 ≈ across a room, -90 ≈ losing it.
      <strong>This is signal-strength only, not GPS — only works within ~10 m radio range.</strong>
    </div>
  </div>
</div>

<!-- ─────────────── VOICE ─────────────── -->
<div class="tab" id="tab-voice">
  <div class="section">
    <h2>Recording</h2>
    <div class="cards" style="grid-template-columns:repeat(3,1fr)">
      <div class="card"><div class="lbl">State</div><div id="vState" class="val off">…</div><div class="sub" id="vElapsed">—</div></div>
      <div class="card"><div class="lbl">RMS (live)</div><div id="vRms" class="val">0</div><div class="sub" id="vRmsBar">—</div></div>
      <div class="card"><div class="lbl">Bytes</div><div id="vBytes" class="val">0</div><div class="sub" id="vReason">—</div></div>
    </div>
    <div class="actions" style="margin-top:10px">
      <button class="btn act" id="vTalk" onclick="vStart()">● Talk (tap)</button>
      <button class="btn warn" id="vStop" onclick="vStop()" style="display:none">■ Stop</button>
      <button class="btn primary" id="vHold"
              onmousedown="vStart()" onmouseup="vStop()"
              ontouchstart="event.preventDefault();vStart()" ontouchend="event.preventDefault();vStop()">
        ⌨ Hold to Talk
      </button>
    </div>
    <div class="note">Tap = record until VAD silence or hard timeout. Hold = record while button is held.</div>
  </div>

  <div class="section">
    <h2>Last recording</h2>
    <div class="kv">
      <dt>File</dt><dd id="vFileSize">—</dd>
      <dt>Stop reason</dt><dd id="vLastReason">—</dd>
    </div>
    <div class="actions" style="margin-top:10px">
      <button class="btn" onclick="dl('/voice/last.wav')">Download last.wav</button>
      <button class="btn" onclick="vPlay()">Play in browser</button>
    </div>
    <audio id="vPlayer" controls style="display:none;width:100%;margin-top:8px"></audio>
  </div>

  <div class="section">
    <h2>Tunables</h2>
    <div class="row"><label>VAD threshold (RMS)</label>
      <input type="range" id="vadThr" min="0" max="2000" step="10" value="150">
      <span id="vadThrV" class="num">150</span></div>
    <div class="row"><label>Silence to stop</label>
      <input type="range" id="silMs" min="200" max="5000" step="100" value="1500">
      <span id="silMsV" class="num">1500 ms</span></div>
    <div class="row"><label>Hard timeout</label>
      <input type="range" id="vTo" min="1000" max="60000" step="500" value="10000">
      <span id="vToV" class="num">10000 ms</span></div>
    <div class="row"><label>Gain shift</label>
      <input type="range" id="vGain" min="10" max="18" step="1" value="14">
      <span id="vGainV" class="num">14 (4×)</span></div>
    <div class="modeline">
      <label><input type="checkbox" id="dcBlk" checked onchange="vDcToggle()"> DC blocker (kills hum/rumble)</label>
    </div>
    <div class="row"><label>Noise gate (mute below)</label>
      <input type="range" id="ngate" min="0" max="2000" step="10" value="0">
      <span id="ngateV" class="num">0 (off)</span></div>
    <div class="note">VAD compares live RMS against threshold; below threshold for silence period → auto-stop.
      Lower gain shift = louder; raise if clipping.
      <strong>DC blocker</strong> removes the constant low-frequency rumble most digital MEMS mics produce — usually the biggest fix for "background noise".
      <strong>Noise gate</strong> zeros samples below the threshold (cleans up silent gaps); start with 100–300 if hiss persists.</div>
  </div>

  <div class="section">
    <h2>Cloud STT (Deepgram)</h2>
    <div class="cards" style="grid-template-columns:repeat(3,1fr)">
      <div class="card"><div class="lbl">State</div><div id="sttState" class="val">idle</div><div class="sub" id="sttDur">—</div></div>
      <div class="card"><div class="lbl">API key</div><div id="sttKey" class="val" style="font-size:14px">not set</div><div class="sub" id="sttModel">model: —</div></div>
      <div class="card"><div class="lbl">Last HTTP</div><div id="sttHttp" class="val">—</div><div class="sub" id="sttRc">rc: —</div></div>
    </div>
    <div class="row" style="margin-top:10px"><label>API key</label>
      <input type="password" id="sttKeyIn" placeholder="paste Deepgram token" style="flex:1;min-width:200px">
    </div>
    <div class="row"><label>Model</label>
      <input type="text" id="sttModelIn" value="nova-2" style="flex:1;min-width:160px">
      <span class="num" style="font-size:11px">nova-2 / nova-3 / enhanced</span>
    </div>
    <div class="actions" style="margin-top:8px">
      <button class="btn primary" onclick="sttSaveKey()">Save key + model</button>
      <button class="btn warn" onclick="sttClearKey()">Clear key</button>
      <button class="btn act" id="sttRunBtn" onclick="sttRun()">Transcribe last.wav</button>
    </div>
    <div style="margin-top:10px">
      <div class="lbl">Transcript</div>
      <pre id="sttTx" style="background:#0e1116;color:#dfe7ef;padding:10px;border-radius:6px;min-height:48px;white-space:pre-wrap;word-break:break-word">—</pre>
      <div class="sub" id="sttErr" style="color:#e58383"></div>
    </div>
    <div class="note">Requires <strong>WiFi STA</strong> connected to your home network (AP mode has no upstream internet).
      Key stored in NVS, never returned in clear. TLS uses <code>setInsecure()</code> — bench-only; for production pin Deepgram's CA.
      Cost: ~$0.004/min on nova-2.</div>
  </div>

  <div class="section">
    <h2>Hookup (INMP441)</h2>
    <div class="kv">
      <dt>VDD</dt><dd>3V3</dd>
      <dt>GND</dt><dd>GND</dd>
      <dt>L/R</dt><dd>GND (left channel)</dd>
      <dt>WS</dt><dd>GPIO 32</dd>
      <dt>SCK</dt><dd>GPIO 33</dd>
      <dt>SD</dt><dd>GPIO 35 (input-only)</dd>
    </div>
    <div class="note">If RMS stays at 0 with no signal motion when you talk → check power LED on mic, GND continuity, and that L/R is tied to GND not VDD.</div>
  </div>
</div>

</main>

<script>
/* ── Tab switching ───────────────────────────────────────────────────────── */
const TABS=['live','tune','system','network','find','voice'];
function showTab(name){
  TABS.forEach(t=>{
    document.getElementById('tab-'+t).classList.toggle('active',t===name);
    document.querySelector('#tabnav button[data-tab="'+t+'"]').classList.toggle('active',t===name);
  });
  if(name==='network'){fetchWifi();fetchSystem();}
  if(name==='voice'){fetchVoice();fetchStt();}
}
document.querySelectorAll('#tabnav button').forEach(b=>{
  b.addEventListener('click',()=>showTab(b.dataset.tab));
});

/* ── Plot state ──────────────────────────────────────────────────────────── */
const N=400;
const buf={x:new Float32Array(N),y:new Float32Array(N),z:new Float32Array(N),mag:new Float32Array(N)};
let head=0,have=0;
const cv=document.getElementById('plot');const ctx=cv.getContext('2d');
const COLORS={x:'var(--pink)',y:'var(--amber)',z:'var(--green)',mag:'var(--blue)'};
const HEX={x:'#fb7185',y:'#fbbf24',z:'#34d399',mag:'#60a5fa'};
function axesEnabled(){const a={};document.querySelectorAll('[data-axis]').forEach(c=>a[c.dataset.axis]=c.checked);return a;}
function draw(){
  const w=cv.width,h=cv.height;ctx.fillStyle='#1d1d24';ctx.fillRect(0,0,w,h);
  if(have<2)return;
  const a=axesEnabled();
  let mn=1e9,mx=-1e9,any=false;
  for(const k of ['x','y','z','mag']){if(!a[k])continue;any=true;
    for(let i=0;i<have;i++){const v=buf[k][(head-have+i+N)%N];if(v<mn)mn=v;if(v>mx)mx=v;}}
  if(!any)return;
  if(mx-mn<10){const c=(mx+mn)/2;mn=c-50;mx=c+50;}
  ctx.font='10px ui-monospace,Menlo';ctx.fillStyle='#52525b';
  ctx.fillText(mx.toFixed(0)+' mg',6,12);ctx.fillText(mn.toFixed(0)+' mg',6,h-4);
  for(const k of ['x','y','z','mag']){
    if(!a[k])continue;
    ctx.strokeStyle=HEX[k];ctx.lineWidth=1.5;ctx.beginPath();
    for(let i=0;i<have;i++){
      const v=buf[k][(head-have+i+N)%N];
      const x=(i/(N-1))*w;
      const y=h-((v-mn)/(mx-mn))*(h-12)-6;
      if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);
    }
    ctx.stroke();
  }
}

/* ── Polling helpers ─────────────────────────────────────────────────────── */
let suppressUntil=0;
function suppress(){suppressUntil=Date.now()+1500;}
function pj(url,body){return fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:body||'{}'});}
function setSlider(id,val,unit){
  const el=document.getElementById(id);if(!el)return;
  if(document.activeElement!==el){el.value=val;}
  document.getElementById(id+'V').textContent=val+(unit?' '+unit:'');
}
function setSel(id,val){
  const el=document.getElementById(id);if(!el)return;
  if(document.activeElement!==el)el.value=val;
}
function setText(id,v){const el=document.getElementById(id);if(el)el.textContent=v;}

/* ── /data tick ──────────────────────────────────────────────────────────── */
let fail=0;
async function tick(){
  try{
    const r=await fetch('/data',{cache:'no-store'});const d=await r.json();
    document.getElementById('dot').className='dot live';
    document.getElementById('state').textContent='live · '+d.uptime_s+' s';
    /* Live cards */
    const wEl=document.getElementById('w');wEl.textContent=d.wear==='on'?'ON':'OFF';
    wEl.className='val '+(d.wear==='on'?'on':'off');
    let ws=d.wear==='on'?'on body':'off body';
    if(d.wear_forced!=='auto')ws+=' (forced)';
    if(d.wear==='on'&&d.wear_remaining_s>0)ws+=' · '+d.wear_remaining_s+' s left';
    setText('ws',ws);
    setText('st',d.steps);
    setText('cad',d.step_cadence_ms>0?d.step_cadence_ms+' ms · '+d.steps_per_min+' /min':'—');
    const bpmEl=document.getElementById('bpm');
    const bpmCard=document.getElementById('bpmcard');
    bpmEl.textContent=(d.bpm_valid?d.bpm:d.bpm_raw)||'—';
    bpmCard.className='card '+(d.bpm_valid?'bpm-valid':'bpm-invalid');
    let bsub=d.bpm_valid?'valid · '+d.bpm+' BPM':(d.bpm_raw?'raw '+d.bpm_raw+' (filtered)':'collecting…');
    bsub+=' · '+d.resp_progress+'/'+d.resp_window_len;
    setText('bsub',bsub);
    const ionEl=document.getElementById('ion');
    ionEl.textContent=d.ionizer?'ON':'OFF';
    ionEl.className='val '+(d.ionizer?'on':'off');
    setText('isub',d.mode.toUpperCase()+' · battery '+d.battery.state);
    setText('batSt','state='+d.battery.state);
    /* Plot */
    buf.x[head]=d.x;buf.y[head]=d.y;buf.z[head]=d.z;buf.mag[head]=d.mag;
    head=(head+1)%N;if(have<N)have++;
    if(document.getElementById('tab-live').classList.contains('active'))draw();
    /* Sync sliders/selectors when not actively edited */
    if(Date.now()>suppressUntil){
      setSlider('motThr',d.cfg.motion_thresh_mg,'mg');
      setSlider('stillS',d.cfg.still_sec,'s');
      setSlider('offS',d.cfg.offbody_sec,'s');
      if('wear_var_thresh_mg' in d.cfg) setSlider('wVar',d.cfg.wear_var_thresh_mg,'mg');
      if('wear_grav_diff_thresh_mg' in d.cfg) setSlider('wGrav',d.cfg.wear_grav_diff_thresh_mg,'mg');
      setSlider('stpThr',d.cfg.step_thresh_mg,'mg');
      setSlider('stpMin',d.cfg.step_min_ms,'ms');
      setSlider('stpMax',d.cfg.step_max_ms,'ms');
      setSlider('bpmMin',d.cfg.bpm_min,'BPM');
      setSlider('bpmMax',d.cfg.bpm_max,'BPM');
      setSlider('respWin',d.cfg.resp_window_sec,'s');
      setSel('modeSel',d.mode);
      setSel('wearSel',d.wear_forced);
      setSlider('batPct',d.battery.pct,'');
      document.getElementById('batChg').checked=d.battery.charging;
      document.getElementById('batFlt').checked=d.battery.fault;
      setText('calX',d.cfg.cal_x+' mg');
      setText('calY',d.cfg.cal_y+' mg');
      setText('calZ',d.cfg.cal_z+' mg');
    }
    /* Find / BLE state */
    if(d.find&&d.find.active){
      document.getElementById('findBtn').style.display='none';
      document.getElementById('findStop').style.display='';
      setText('findState','Strobing · '+d.find.remaining_s+' s left.');
    }else{
      document.getElementById('findBtn').style.display='';
      document.getElementById('findStop').style.display='none';
      setText('findState','Onboard blue LED flashes at 10 Hz so you can spot the board across a room.');
    }
    if(d.ble&&d.ble.advertising){
      document.getElementById('bleBtn').style.display='none';
      document.getElementById('bleStop').style.display='';
      setText('bleState','Advertising as "atovio-bench" — '+d.ble.remaining_s+' s left. Scan with your phone.');
    }else{
      document.getElementById('bleBtn').style.display='';
      document.getElementById('bleStop').style.display='none';
      setText('bleState','Click to make the device discoverable for 5 minutes.');
    }
    /* Network status (lazy fields) */
    setText('apIp',d.net.ap_ip);
    setText('staStatus',d.net.sta_status+(d.net.sta_enabled?'':' (disabled)'));
    setText('staIp',d.net.sta_ip||'—');
    setText('staSsid',d.net.sta_ssid||'—');
    setText('staRssi',d.net.sta_rssi?d.net.sta_rssi+' dBm':'—');
    fail=0;
  }catch(e){
    fail++;document.getElementById('dot').className='dot err';
    document.getElementById('state').textContent='offline ('+fail+')';
  }
}
setInterval(tick,100);tick();

/* ── /data2 — extended diagnostics (wear v2 / resp v2 / steps v2) ──────── */
async function tick2(){
  try{
    const r=await fetch('/data2',{cache:'no-store'});const d=await r.json();
    if(d.wear){
      const w=d.wear;
      setText('wsig','sig: '+w.signal+' · var '+w.var+'/'+w.var_thr+' · grav '+w.grav+'/'+w.grav_thr+' mg');
    }
  }catch(e){}
}
setInterval(tick2,1000);tick2();

/* ── Slider/Selector handlers ────────────────────────────────────────────── */
function bindSlider(id,unit,key,debounce){
  const el=document.getElementById(id);
  el.addEventListener('input',()=>{
    suppress();
    document.getElementById(id+'V').textContent=el.value+' '+unit;
    clearTimeout(el._t);
    const body={};body[key]=+el.value;
    el._t=setTimeout(()=>pj('/config',JSON.stringify(body)),debounce||200);
  });
}
bindSlider('motThr','mg','motion_thresh_mg');
bindSlider('stillS','s','still_sec');
bindSlider('offS','s','offbody_sec');
bindSlider('stpThr','mg','step_thresh_mg');
bindSlider('stpMin','ms','step_min_ms');
bindSlider('stpMax','ms','step_max_ms');
bindSlider('bpmMin','BPM','bpm_min');
bindSlider('bpmMax','BPM','bpm_max');
bindSlider('respWin','s','resp_window_sec');
bindSlider('wVar','mg','wear_var_thresh_mg');
bindSlider('wGrav','mg','wear_grav_diff_thresh_mg');

document.getElementById('modeSel').addEventListener('change',e=>{suppress();pj('/mode',JSON.stringify({mode:e.target.value}));});
document.getElementById('wearSel').addEventListener('change',e=>{suppress();pj('/wear/force',JSON.stringify({state:e.target.value}));});

['batPct','batChg','batFlt'].forEach(id=>{
  const el=document.getElementById(id);
  const update=()=>{
    suppress();
    const body={pct:+document.getElementById('batPct').value,
                charging:document.getElementById('batChg').checked,
                fault:document.getElementById('batFlt').checked};
    document.getElementById('batPctV').textContent=document.getElementById('batPct').value;
    clearTimeout(el._t);
    el._t=setTimeout(()=>pj('/battery',JSON.stringify(body)),200);
  };
  el.addEventListener('input',update);
  el.addEventListener('change',update);
});

/* ── Action functions ────────────────────────────────────────────────────── */
function cal(){suppress();pj('/accel/calibrate').then(()=>tick());}
function rst(u){suppress();pj(u).then(()=>tick());}
async function t10(){
  const r=await fetch('/api/type10',{cache:'no-store'});const j=await r.json();
  const el=document.getElementById('t10out');el.style.display='block';el.textContent=JSON.stringify(j,null,2);
}
function dl(u){window.location=u;}

function findStart(){suppress();pj('/find',JSON.stringify({seconds:30}));}
function findStop(){suppress();pj('/find/stop');}

function bleStart(){suppress();pj('/ble/start',JSON.stringify({minutes:5}));}
function bleStop(){suppress();pj('/ble/stop');}

/* ── WiFi STA ────────────────────────────────────────────────────────────── */
async function fetchWifi(){
  try{
    const r=await fetch('/wifi',{cache:'no-store'});const d=await r.json();
    setText('apSsid',d.ap_ssid);
    if(document.activeElement!==document.getElementById('wifiSsid'))
      document.getElementById('wifiSsid').value=d.sta_ssid||'';
    document.getElementById('wifiEn').checked=d.sta_enabled;
  }catch(e){}
}
function saveWifi(){
  const ssid=document.getElementById('wifiSsid').value;
  const pass=document.getElementById('wifiPass').value;
  const en=document.getElementById('wifiEn').checked;
  pj('/wifi',JSON.stringify({ssid:ssid,password:pass,enabled:en})).then(fetchWifi);
}

/* ── System info ─────────────────────────────────────────────────────────── */
async function fetchSystem(){
  try{
    const r=await fetch('/system',{cache:'no-store'});const d=await r.json();
    setText('sysBuild',d.build_date);
    setText('sysHeap',(d.free_heap/1024).toFixed(1)+' KB');
    setText('sysChip',d.chip_model+' · '+d.sdk);
  }catch(e){}
}

/* ── Voice ───────────────────────────────────────────────────────────────── */
async function fetchVoice(){
  try{
    const r=await fetch('/voice/status',{cache:'no-store'});const d=await r.json();
    const sEl=document.getElementById('vState');
    sEl.textContent=d.state.toUpperCase();
    sEl.className='val '+(d.state==='streaming'?'on':'off');
    setText('vElapsed',d.elapsed_ms?(d.elapsed_ms/1000).toFixed(1)+' s':'—');
    setText('vRms',d.rms);
    /* horizontal RMS bar via shaded text */
    const barLen=Math.min(20,Math.round(d.rms/100));
    setText('vRmsBar','█'.repeat(barLen)+'·'.repeat(20-barLen));
    setText('vBytes',d.bytes);
    setText('vReason',d.last_reason);
    setText('vFileSize',d.last_wav_size?(d.last_wav_size/1024).toFixed(1)+' KB':'—');
    setText('vLastReason',d.last_reason);
    document.getElementById('vTalk').style.display=d.state==='streaming'?'none':'';
    document.getElementById('vStop').style.display=d.state==='streaming'?'':'none';
    if(Date.now()>suppressUntil){
      setSlider('vadThr',d.vad_threshold,'');
      setSlider('silMs',d.silence_ms,'ms');
      setSlider('vTo',d.timeout_ms,'ms');
      const g=d.gain_shift;
      document.getElementById('vGain').value=g;
      document.getElementById('vGainV').textContent=g+' ('+Math.pow(2,18-g).toFixed(0)+'×)';
      document.getElementById('dcBlk').checked=d.dc_blocker;
      const ng=d.noise_gate;
      document.getElementById('ngate').value=ng;
      document.getElementById('ngateV').textContent=ng?ng:'0 (off)';
    }
  }catch(e){}
}
function vStart(){suppress();pj('/voice/start',JSON.stringify({timeout_ms:+document.getElementById('vTo').value,vad:true})).then(fetchVoice);}
function vStop(){suppress();pj('/voice/stop','{}').then(fetchVoice);}
function vPlay(){
  const a=document.getElementById('vPlayer');
  a.style.display='block';
  a.src='/voice/last.wav?'+Date.now();
  a.play();
}
['vadThr','silMs','vTo'].forEach(id=>{
  const el=document.getElementById(id);
  el.addEventListener('input',()=>{
    suppress();
    document.getElementById(id+'V').textContent=el.value+(id==='vadThr'?'':' ms');
    clearTimeout(el._t);
    const body={};
    if(id==='vadThr')body.vad_threshold=+el.value;
    if(id==='silMs') body.silence_ms=+el.value;
    if(id==='vTo')   body.timeout_ms=+el.value;
    el._t=setTimeout(()=>pj('/voice/config',JSON.stringify(body)),200);
  });
});
document.getElementById('vGain').addEventListener('input',e=>{
  suppress();
  const g=+e.target.value;
  document.getElementById('vGainV').textContent=g+' ('+Math.pow(2,18-g).toFixed(0)+'×)';
  clearTimeout(e.target._t);
  e.target._t=setTimeout(()=>pj('/voice/config',JSON.stringify({gain_shift:g})),200);
});
document.getElementById('ngate').addEventListener('input',e=>{
  suppress();
  const v=+e.target.value;
  document.getElementById('ngateV').textContent=v?v:'0 (off)';
  clearTimeout(e.target._t);
  e.target._t=setTimeout(()=>pj('/voice/config',JSON.stringify({noise_gate:v})),200);
});
function vDcToggle(){
  suppress();
  const en=document.getElementById('dcBlk').checked;
  pj('/voice/config',JSON.stringify({dc_blocker:en}));
}
setInterval(()=>{if(document.getElementById('tab-voice').classList.contains('active'))fetchVoice();},150);

/* ── Cloud STT (Deepgram) ───────────────────────────────────────────────── */
async function fetchStt(){
  try{
    const r=await fetch('/stt/status',{cache:'no-store'});const d=await r.json();
    const st=document.getElementById('sttState');
    setText('sttState',d.state);
    st.className='val '+(d.state==='error'?'off':(d.state==='done'?'on':''));
    setText('sttDur', d.dur_ms?d.dur_ms+' ms':'—');
    setText('sttKey', d.key_hint);
    setText('sttModel','model: '+d.model);
    setText('sttHttp', d.http||'—');
    setText('sttRc','rc: '+d.rc+(d.wav_bytes?' · '+d.wav_bytes+' B':''));
    document.getElementById('sttTx').textContent=d.transcript||'—';
    document.getElementById('sttErr').textContent=d.err||'';
    const mi=document.getElementById('sttModelIn'); if(mi&&document.activeElement!==mi) mi.value=d.model||'nova-2';
  }catch(e){}
}
function sttSaveKey(){
  const key=document.getElementById('sttKeyIn').value;
  const model=document.getElementById('sttModelIn').value||'nova-2';
  pj('/stt/key',JSON.stringify({key:key,model:model})).then(()=>{document.getElementById('sttKeyIn').value='';fetchStt();});
}
function sttClearKey(){pj('/stt/key',JSON.stringify({key:''})).then(fetchStt);}
function sttRun(){
  const btn=document.getElementById('sttRunBtn');
  btn.disabled=true;btn.textContent='Transcribing…';
  document.getElementById('sttTx').textContent='…';
  document.getElementById('sttErr').textContent='';
  setText('sttState','running');
  pj('/stt/run','{}').then(r=>r.json()).then(d=>{
    setText('sttState',d.state);
    setText('sttDur', d.dur_ms?d.dur_ms+' ms':'—');
    setText('sttHttp', d.http||'—');
    setText('sttRc','rc: '+d.rc+(d.wav_bytes?' · '+d.wav_bytes+' B':''));
    document.getElementById('sttTx').textContent=d.transcript||'—';
    document.getElementById('sttErr').textContent=d.err||'';
    const st=document.getElementById('sttState');
    st.className='val '+(d.state==='error'?'off':(d.state==='done'?'on':''));
  }).catch(e=>{document.getElementById('sttErr').textContent='request failed: '+e;})
    .finally(()=>{btn.disabled=false;btn.textContent='Transcribe last.wav';});
}

/* ── Motion log @ 1 Hz ───────────────────────────────────────────────────── */
async function mtick(){
  try{
    const r=await fetch('/motionlog',{cache:'no-store'});const d=await r.json();
    const events=d.events.map(s=>s+' s').join(' · ')||'(none)';
    setText('mlog','total='+d.total+' · '+events);
  }catch(e){}
}
setInterval(mtick,1000);mtick();
</script></body></html>
)HTML";
