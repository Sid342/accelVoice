#pragma once
#include <Arduino.h>

/* Single-file embedded HTML viewer. Polls /data every 100 ms.
 * No CDN deps — works in AP mode without internet.                            */
static const char WEB_INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>atovio bench</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0;font-family:-apple-system,sans-serif}
  body{background:#0e0e10;color:#e6e6e6;padding:12px;line-height:1.4}
  h1{font-size:14px;color:#888;font-weight:500;margin-bottom:8px}
  .grid{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;margin-bottom:12px}
  .card{background:#1a1a1d;border-radius:8px;padding:12px}
  .lbl{font-size:11px;color:#777;text-transform:uppercase;letter-spacing:.5px}
  .val{font-size:32px;font-weight:600;margin-top:4px;font-variant-numeric:tabular-nums}
  .val.on{color:#4ade80} .val.off{color:#f87171}
  .sub{font-size:11px;color:#666;margin-top:2px;font-variant-numeric:tabular-nums}
  canvas{width:100%;height:160px;background:#1a1a1d;border-radius:8px;display:block}
  .status{display:flex;align-items:center;gap:6px;font-size:11px;color:#666;margin-top:8px}
  .dot{width:8px;height:8px;border-radius:50%;background:#666}
  .dot.live{background:#4ade80}
</style></head><body>
<h1>atovio-accel-bench-esp32</h1>
<div class="grid">
  <div class="card"><div class="lbl">Wear</div><div id="w" class="val off">…</div><div class="sub" id="ws">—</div></div>
  <div class="card"><div class="lbl">Steps</div><div id="st" class="val">0</div><div class="sub">since boot</div></div>
  <div class="card"><div class="lbl">Respiration</div><div id="bpm" class="val">0</div><div class="sub">BPM</div></div>
  <div class="card"><div class="lbl">Z-axis</div><div id="z" class="val">—</div><div class="sub" id="xy">x=— y=—</div></div>
</div>
<canvas id="plot" width="600" height="160"></canvas>
<div class="status"><span class="dot" id="dot"></span><span id="state">connecting…</span></div>
<script>
const N=200;const buf=new Float32Array(N);let head=0,have=0;
const cv=document.getElementById('plot');const ctx=cv.getContext('2d');
function draw(){
  const w=cv.width,h=cv.height;ctx.fillStyle='#1a1a1d';ctx.fillRect(0,0,w,h);
  if(have<2)return;
  let mn=1e9,mx=-1e9;for(let i=0;i<have;i++){const v=buf[(head-have+i+N)%N];if(v<mn)mn=v;if(v>mx)mx=v;}
  if(mx-mn<10){const c=(mx+mn)/2;mn=c-50;mx=c+50;}
  ctx.strokeStyle='#4ade80';ctx.lineWidth=1.5;ctx.beginPath();
  for(let i=0;i<have;i++){
    const v=buf[(head-have+i+N)%N];
    const x=(i/(N-1))*w;
    const y=h-((v-mn)/(mx-mn))*(h-8)-4;
    if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);
  }
  ctx.stroke();
  ctx.fillStyle='#666';ctx.font='10px -apple-system';
  ctx.fillText(mx.toFixed(0)+'mg',4,12);ctx.fillText(mn.toFixed(0)+'mg',4,h-4);
}
let fail=0;
async function tick(){
  try{
    const r=await fetch('/data',{cache:'no-store'});const d=await r.json();
    document.getElementById('dot').className='dot live';
    document.getElementById('state').textContent='live · uptime '+Math.floor(d.t/1000)+'s';
    const w=document.getElementById('w');w.textContent=d.w?'ON':'OFF';
    w.className='val '+(d.w?'on':'off');
    document.getElementById('ws').textContent=d.w?'on body':'off body';
    document.getElementById('st').textContent=d.st;
    document.getElementById('bpm').textContent=d.bpm;
    document.getElementById('z').textContent=d.z+' mg';
    document.getElementById('xy').textContent='x='+d.x+' y='+d.y;
    buf[head]=d.z;head=(head+1)%N;if(have<N)have++;
    draw();fail=0;
  }catch(e){
    fail++;document.getElementById('dot').className='dot';
    document.getElementById('state').textContent='offline ('+fail+')';
  }
}
setInterval(tick,100);tick();
</script></body></html>
)HTML";
