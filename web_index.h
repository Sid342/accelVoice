#pragma once
#include <Arduino.h>

/* atovio-accel-bench-esp32 — embedded web viewer.
 * Single PROGMEM HTML doc, vanilla JS. Polls /data every 100 ms.            */
static const char WEB_INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>atovio bench v2</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;font-family:-apple-system,BlinkMacSystemFont,sans-serif}
body{background:#0e0e10;color:#e6e6e6;padding:10px;line-height:1.4;font-size:14px}
h1{font-size:13px;color:#888;font-weight:500;margin-bottom:6px;display:flex;align-items:center;gap:8px;flex-wrap:wrap}
h2{font-size:11px;color:#666;font-weight:500;text-transform:uppercase;letter-spacing:.6px;margin:14px 0 6px}
.dot{width:8px;height:8px;border-radius:50%;background:#666;display:inline-block}
.dot.live{background:#4ade80}.dot.warn{background:#f59e0b}
.grid4{display:grid;grid-template-columns:repeat(4,1fr);gap:6px}
.grid2{display:grid;grid-template-columns:repeat(2,1fr);gap:6px}
@media(max-width:520px){.grid4{grid-template-columns:repeat(2,1fr)}}
.card{background:#1a1a1d;border-radius:8px;padding:10px;border:1px solid #1a1a1d}
.lbl{font-size:10px;color:#777;text-transform:uppercase;letter-spacing:.5px}
.val{font-size:26px;font-weight:600;margin-top:2px;font-variant-numeric:tabular-nums}
.val.on{color:#4ade80} .val.off{color:#f87171} .val.warn{color:#f59e0b}
.sub{font-size:10px;color:#666;margin-top:1px;font-variant-numeric:tabular-nums}
.card.bpm-invalid{border-color:#7f1d1d}
.card.bpm-valid{border-color:#14532d}
canvas{width:100%;height:170px;background:#1a1a1d;border-radius:8px;display:block}
.tog{display:flex;gap:4px;flex-wrap:wrap;margin:6px 0}
.tog label{display:inline-flex;align-items:center;gap:4px;background:#1a1a1d;padding:4px 8px;border-radius:6px;cursor:pointer;font-size:12px}
.tog input{accent-color:#4ade80}
.row{display:flex;gap:8px;align-items:center;margin:6px 0;flex-wrap:wrap}
.row label{font-size:12px;color:#999;min-width:120px}
.row input[type=range]{flex:1;min-width:120px;accent-color:#4ade80}
.row .num{font-variant-numeric:tabular-nums;color:#e6e6e6;min-width:60px;text-align:right;font-size:12px}
button,select{background:#222;color:#e6e6e6;border:1px solid #333;border-radius:6px;padding:6px 10px;cursor:pointer;font-size:12px}
button:hover{background:#2a2a2d}
button.act{background:#1f3a2a;border-color:#2d5e3f}
button.warn{background:#3a2820;border-color:#6e3e2a}
.actions{display:flex;gap:6px;flex-wrap:wrap;margin-top:6px}
.modeline{display:flex;align-items:center;gap:8px;flex-wrap:wrap;margin-top:6px}
pre{background:#000;color:#9cf;padding:6px;border-radius:6px;font-size:11px;overflow-x:auto;font-family:Menlo,monospace}
.mlog{font-family:Menlo,monospace;font-size:11px;color:#888;padding:8px;background:#1a1a1d;border-radius:6px;min-height:30px}
hr{border:0;border-top:1px solid #222;margin:12px 0}
</style></head><body>

<h1>
  atovio-accel-bench-esp32 v2
  <span class="dot" id="dot"></span><span id="state">connecting…</span>
  <span style="margin-left:auto;color:#555">OTA: atovio.local · /data /config /api/type10 /log.csv</span>
</h1>

<div class="grid4">
  <div class="card"><div class="lbl">Wear</div><div id="w" class="val off">…</div><div class="sub" id="ws">—</div></div>
  <div class="card"><div class="lbl">Steps</div><div id="st" class="val">0</div><div class="sub" id="cad">—</div></div>
  <div class="card" id="bpmcard"><div class="lbl">Respiration</div><div id="bpm" class="val">0</div><div class="sub" id="bsub">—</div></div>
  <div class="card"><div class="lbl">Ionizer (sim)</div><div id="ion" class="val off">OFF</div><div class="sub" id="isub">—</div></div>
</div>

<h2>Live plot</h2>
<div class="tog">
  <label><input type="checkbox" data-axis="x"> X</label>
  <label><input type="checkbox" data-axis="y"> Y</label>
  <label><input type="checkbox" data-axis="z" checked> Z</label>
  <label><input type="checkbox" data-axis="mag"> |mag|</label>
</div>
<canvas id="plot" width="800" height="170"></canvas>

<h2>Mode + Wear override + Battery sim</h2>
<div class="modeline">
  Mode:
  <select id="modeSel">
    <option value="off">OFF</option>
    <option value="normal" selected>NORMAL</option>
    <option value="turbo">TURBO</option>
  </select>
  &nbsp;Wear:
  <select id="wearSel">
    <option value="auto" selected>auto (SM)</option>
    <option value="on">force ON</option>
    <option value="off">force OFF</option>
  </select>
</div>
<div class="row"><label>Battery %</label>
  <input type="range" id="batPct" min="0" max="100" value="80">
  <span id="batPctV" class="num">80</span></div>
<div class="modeline">
  <label><input type="checkbox" id="batChg"> Charging</label>
  <label><input type="checkbox" id="batFlt"> Fault</label>
  <span id="batSt" style="color:#888;font-size:12px"></span>
</div>

<h2>Thresholds (saved to NVS)</h2>
<div class="row"><label>Motion thresh</label>
  <input type="range" id="motThr" min="10" max="200" step="2" value="50">
  <span id="motThrV" class="num">50 mg</span></div>
<div class="row"><label>Still seconds</label>
  <input type="range" id="stillS" min="1" max="30" step="1" value="5">
  <span id="stillSV" class="num">5 s</span></div>
<div class="row"><label>Off-body seconds</label>
  <input type="range" id="offS" min="5" max="120" step="1" value="30">
  <span id="offSV" class="num">30 s</span></div>
<div class="row"><label>Step thresh</label>
  <input type="range" id="stpThr" min="50" max="500" step="10" value="200">
  <span id="stpThrV" class="num">200 mg</span></div>

<h2>Actions</h2>
<div class="actions">
  <button class="act" onclick="cal()">Calibrate (flat rest)</button>
  <button onclick="rst('/steps/reset')">Reset steps</button>
  <button onclick="rst('/resp/reset')">Reset respiration</button>
  <button onclick="t10()">Send Type 11 → see Type 10</button>
  <button onclick="dl()">Download log.csv</button>
</div>
<pre id="t10out" style="display:none"></pre>

<h2>Motion event log (sec ago)</h2>
<div class="mlog" id="mlog">—</div>

<script>
/* ── Plot state ──────────────────────────────────────────────────────────── */
const N=400;
const buf={x:new Float32Array(N),y:new Float32Array(N),z:new Float32Array(N),mag:new Float32Array(N)};
let head=0,have=0;
const cv=document.getElementById('plot');const ctx=cv.getContext('2d');
const COLORS={x:'#fb7185',y:'#fbbf24',z:'#4ade80',mag:'#60a5fa'};
function axesEnabled(){
  const a={};document.querySelectorAll('[data-axis]').forEach(c=>a[c.dataset.axis]=c.checked);return a;
}
function draw(){
  const w=cv.width,h=cv.height;ctx.fillStyle='#1a1a1d';ctx.fillRect(0,0,w,h);
  if(have<2)return;
  const a=axesEnabled();
  let mn=1e9,mx=-1e9,any=false;
  for(const k of ['x','y','z','mag']){if(!a[k])continue;any=true;
    for(let i=0;i<have;i++){const v=buf[k][(head-have+i+N)%N];if(v<mn)mn=v;if(v>mx)mx=v;}}
  if(!any){return;}
  if(mx-mn<10){const c=(mx+mn)/2;mn=c-50;mx=c+50;}
  ctx.font='10px -apple-system';ctx.fillStyle='#666';
  ctx.fillText(mx.toFixed(0)+' mg',4,12);ctx.fillText(mn.toFixed(0)+' mg',4,h-4);
  for(const k of ['x','y','z','mag']){
    if(!a[k])continue;
    ctx.strokeStyle=COLORS[k];ctx.lineWidth=1.4;ctx.beginPath();
    for(let i=0;i<have;i++){
      const v=buf[k][(head-have+i+N)%N];
      const x=(i/(N-1))*w;
      const y=h-((v-mn)/(mx-mn))*(h-8)-4;
      if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);
    }
    ctx.stroke();
  }
}

/* ── Polling /data ───────────────────────────────────────────────────────── */
let fail=0,suppressUntil=0;
async function tick(){
  try{
    const r=await fetch('/data',{cache:'no-store'});const d=await r.json();
    document.getElementById('dot').className='dot live';
    document.getElementById('state').textContent='live · uptime '+d.uptime_s+' s';
    /* Wear card */
    const wEl=document.getElementById('w');wEl.textContent=d.wear==='on'?'ON':'OFF';
    wEl.className='val '+(d.wear==='on'?'on':'off');
    let ws=d.wear==='on'?'on body':'off body';
    if(d.wear_forced!=='auto')ws+=' (forced)';
    if(d.wear==='on'&&d.wear_remaining_s>0)ws+=' · '+d.wear_remaining_s+' s left';
    document.getElementById('ws').textContent=ws;
    /* Steps */
    document.getElementById('st').textContent=d.steps;
    let cad='—';
    if(d.step_cadence_ms>0)cad=d.step_cadence_ms+' ms · '+d.steps_per_min+' /min';
    document.getElementById('cad').textContent=cad;
    /* BPM */
    const bpmEl=document.getElementById('bpm');
    const bpmCard=document.getElementById('bpmcard');
    const shown=d.bpm_valid?d.bpm:d.bpm_raw;
    bpmEl.textContent=shown||'—';
    bpmCard.className='card '+(d.bpm_valid?'bpm-valid':'bpm-invalid');
    let bsub=d.bpm_valid?'valid · '+d.bpm+' BPM':(d.bpm_raw?'raw '+d.bpm_raw+' BPM (filtered)':'collecting…');
    bsub+=' · '+d.resp_progress+'/'+d.resp_window_len;
    document.getElementById('bsub').textContent=bsub;
    /* Ionizer */
    const ionEl=document.getElementById('ion');
    ionEl.textContent=d.ionizer?'ON':'OFF';
    ionEl.className='val '+(d.ionizer?'on':'off');
    document.getElementById('isub').textContent=d.mode.toUpperCase()+' · battery '+d.battery.state;
    /* Battery state inline */
    document.getElementById('batSt').textContent='state='+d.battery.state;
    /* Plot push */
    buf.x[head]=d.x;buf.y[head]=d.y;buf.z[head]=d.z;buf.mag[head]=d.mag;
    head=(head+1)%N;if(have<N)have++;
    draw();
    /* Sliders/sel — only sync from server when not actively interacted */
    if(Date.now()>suppressUntil){
      setSlider('motThr',d.cfg.motion_thresh_mg,'mg');
      setSlider('stillS',d.cfg.still_sec,'s');
      setSlider('offS',d.cfg.offbody_sec,'s');
      setSlider('stpThr',d.cfg.step_thresh_mg,'mg');
      setSel('modeSel',d.mode);
      setSel('wearSel',d.wear_forced);
      setSlider('batPct',d.battery.pct,'');
      document.getElementById('batChg').checked=d.battery.charging;
      document.getElementById('batFlt').checked=d.battery.fault;
    }
    fail=0;
  }catch(e){
    fail++;document.getElementById('dot').className='dot';
    document.getElementById('state').textContent='offline ('+fail+')';
  }
}
function setSlider(id,val,unit){
  const el=document.getElementById(id);if(!el)return;
  if(document.activeElement!==el){el.value=val;}
  document.getElementById(id+'V').textContent=val+(unit?' '+unit:'');
}
function setSel(id,val){
  const el=document.getElementById(id);if(!el)return;
  if(document.activeElement!==el){el.value=val;}
}
setInterval(tick,100);tick();

/* ── Local UI handlers ───────────────────────────────────────────────────── */
function suppress(){suppressUntil=Date.now()+1500;}
function pj(url,body){return fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:body});}

document.getElementById('modeSel').addEventListener('change',e=>{
  suppress();pj('/mode',JSON.stringify({mode:e.target.value}));
});
document.getElementById('wearSel').addEventListener('change',e=>{
  suppress();pj('/wear/force',JSON.stringify({state:e.target.value}));
});

['motThr','stillS','offS','stpThr'].forEach(id=>{
  const el=document.getElementById(id);
  const update=()=>{
    suppress();
    let body={};
    if(id==='motThr')body.motion_thresh_mg=+el.value;
    if(id==='stillS')body.still_sec=+el.value;
    if(id==='offS')  body.offbody_sec=+el.value;
    if(id==='stpThr')body.step_thresh_mg=+el.value;
    document.getElementById(id+'V').textContent=el.value+(id==='motThr'||id==='stpThr'?' mg':' s');
    clearTimeout(el._t);
    el._t=setTimeout(()=>pj('/config',JSON.stringify(body)),200);
  };
  el.addEventListener('input',update);
});

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

function cal(){suppress();pj('/accel/calibrate','{}').then(()=>tick());}
function rst(u){suppress();pj(u,'{}').then(()=>tick());}
async function t10(){
  const r=await fetch('/api/type10',{cache:'no-store'});const j=await r.json();
  const el=document.getElementById('t10out');el.style.display='block';el.textContent=JSON.stringify(j,null,2);
}
function dl(){window.location='/log.csv';}

/* ── Motion log refresh @ 1 Hz ───────────────────────────────────────────── */
async function mtick(){
  try{
    const r=await fetch('/motionlog',{cache:'no-store'});const d=await r.json();
    const events=d.events.map(s=>s+' s').join(' · ')||'(none)';
    document.getElementById('mlog').textContent='total='+d.total+' · '+events;
  }catch(e){}
}
setInterval(mtick,1000);mtick();
</script></body></html>
)HTML";
