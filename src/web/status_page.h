// Milestone 2 phone page: live hub + pod status. Replaced by the full
// control UI in milestone 4. Single file, no external resources (the AP
// has no internet).
#pragma once

static const char kStatusPage[] = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Pod Hub</title>
<style>
:root{--bg:#000;--fg:#fff;--dim:#9a9a9a;--card:#161616;--ok:#3ddc84;--bad:#ff5a4e}
@media (prefers-color-scheme:light){:root{--bg:#fff;--fg:#000;--dim:#555;--card:#f0f0f0;--ok:#0a7a3d;--bad:#b3261e}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:18px/1.35 -apple-system,system-ui,sans-serif;padding:16px}
h1{font-size:24px;margin:0 0 12px}
.pods{display:flex;gap:10px;flex-wrap:wrap;margin-bottom:16px}
.pod{width:64px;height:64px;border-radius:50%;display:flex;align-items:center;justify-content:center;
 font-size:28px;font-weight:700;color:#000;background:#555}
.card{background:var(--card);border-radius:12px;padding:12px 14px;margin-bottom:12px}
.row{display:flex;justify-content:space-between;padding:4px 0}
.k{color:var(--dim)}
table{width:100%;border-collapse:collapse;font-size:16px}
td,th{padding:6px 4px;text-align:right}td:first-child,th:first-child{text-align:left}
th{color:var(--dim);font-weight:500}
.err{color:var(--bad)}
</style></head><body>
<h1>Pod Hub</h1>
<div class="pods" id="pods"></div>
<div class="card" id="hub"></div>
<div class="card"><table><thead><tr><th>Pod</th><th>Signal</th><th>Drops</th><th>Worst gap</th></tr></thead>
<tbody id="tbl"></tbody></table></div>
<p class="k" id="msg">Connecting…</p>
<script>
const $=id=>document.getElementById(id);
const t=ms=>{const s=Math.floor(ms/1000);return Math.floor(s/60)+':'+String(s%60).padStart(2,'0')};
const kb=b=>Math.round(b/1024)+' KB';
async function tick(){
 try{
  const r=await fetch('/api/status',{cache:'no-store'});const d=await r.json();
  $('pods').innerHTML=d.pods.map(p=>`<div class="pod" style="background:${p.st=='connected'?p.c:'#555'}">${p.n}</div>`).join('');
  const rows=[['Uptime',t(d.up)],['Pods connected',d.pods.filter(p=>p.st=='connected').length+' / '+d.pods.length],
   ['All connected after',d.allConnMs?(d.allConnMs/1000).toFixed(1)+' s':'—'],
   ['Free memory',kb(d.heap)+' (lowest '+kb(d.minHeap)+')'],['Biggest block',kb(d.largest)],
   ['Soak test',d.soak.on?`on: ${d.soak.hit} hits, ${d.soak.miss} misses`:'off']];
  $('hub').innerHTML=rows.map(([k,v])=>`<div class="row"><span class="k">${k}</span><span>${v}</span></div>`).join('');
  $('tbl').innerHTML=d.pods.map(p=>`<tr><td>${p.n} ${p.st=='connected'?'':'<span class=err>'+p.st+'</span>'}</td><td>${p.rssi?p.rssi+' dB':'—'}</td><td>${p.drops}</td><td>${p.worst?(p.worst/1000).toFixed(1)+' s':'—'}</td></tr>`).join('');
  $('msg').textContent='Updated '+new Date().toLocaleTimeString();
 }catch(e){$('msg').textContent='Lost contact with the hub…'}
}
tick();setInterval(tick,2000);
</script></body></html>)HTML";
