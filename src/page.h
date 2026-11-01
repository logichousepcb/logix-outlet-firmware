#pragma once

#include <Arduino.h>

// %TITLE% is the device name; %VIEW% is "main" (outlets and energy) or "settings".
static const char PAGE_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%TITLE%</title>
<style>
:root{--bg:#f3f5f8;--card:#fff;--fg:#1d2430;--muted:#667085;--line:#d9dee7;--accent:#1f6feb;--ok:#1a7f37;--bad:#cf222e;--warn:#9a6700;--on:#f5b301}
@media (prefers-color-scheme:dark){:root{--bg:#0f1419;--card:#181f27;--fg:#e6edf3;--muted:#8b98a5;--line:#2b3440;--accent:#4493f8;--ok:#3fb950;--bad:#f85149;--warn:#d29922;--on:#f5c542}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
header{background:var(--card);border-bottom:1px solid var(--line);padding:14px 16px}
header .wrap{display:flex;flex-wrap:wrap;align-items:center;gap:8px 12px}
h1{font-size:19px;margin:0;flex:1 1 auto}
header a{color:var(--accent);text-decoration:none;font-size:14px}
main.wrap{padding:16px}
.wrap{max-width:760px;margin:0 auto}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:16px;margin-bottom:16px}
h2{font-size:15px;margin:0 0 12px;text-transform:uppercase;letter-spacing:.04em;color:var(--muted)}
.pill{font-size:12px;padding:2px 9px;border-radius:999px;border:1px solid var(--line);white-space:nowrap}
.pill.ok{color:var(--ok);border-color:var(--ok)}.pill.warn{color:var(--warn);border-color:var(--warn)}
dl{display:grid;grid-template-columns:max-content 1fr;gap:6px 16px;margin:0}
dt{color:var(--muted)}dd{margin:0;word-break:break-all}
label{display:block;font-size:13px;color:var(--muted);margin:10px 0 4px}
input[type=text],input[type=password],input[type=number]{width:100%;padding:9px 10px;border:1px solid var(--line);border-radius:7px;background:var(--bg);color:var(--fg);font:inherit}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-top:12px}
.grid3{display:grid;grid-template-columns:repeat(3,1fr);gap:0 10px}
button{font:inherit;padding:8px 14px;border-radius:7px;border:1px solid var(--line);background:var(--bg);color:var(--fg);cursor:pointer}
button.primary{background:var(--accent);border-color:var(--accent);color:#fff}
button.danger{color:var(--bad);border-color:var(--bad)}
button:disabled{opacity:.5;cursor:default}
.nets{list-style:none;margin:10px 0 0;padding:0;border:1px solid var(--line);border-radius:7px;max-height:220px;overflow:auto}
.nets:empty{display:none}
.nets li{display:flex;justify-content:space-between;gap:8px;padding:8px 10px;border-bottom:1px solid var(--line);cursor:pointer}
.nets li:last-child{border-bottom:0}.nets li:hover{background:var(--bg)}
.nets span.s{color:var(--muted);font-size:13px;white-space:nowrap}
.msg{margin-top:10px;font-size:14px;min-height:1em}
.msg.ok{color:var(--ok)}.msg.bad{color:var(--bad)}
.bar{height:10px;border-radius:5px;background:var(--bg);border:1px solid var(--line);overflow:hidden;margin-top:12px;display:none}
.bar>div{height:100%;width:0;background:var(--accent);transition:width .2s}
.hint{font-size:13px;color:var(--muted);margin:6px 0 0}
.outlets{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.outlet{display:flex;flex-direction:column;align-items:center;gap:8px;padding:18px 10px;border-radius:10px;border:2px solid var(--line);background:var(--bg);cursor:pointer;user-select:none}
.outlet .dot{width:18px;height:18px;border-radius:50%;background:var(--line)}
.outlet .st{font-size:28px;font-weight:600}
.outlet .nm{font-size:13px;color:var(--muted);text-align:center}
.outlet.on{border-color:var(--on)}.outlet.on .dot{background:var(--on);box-shadow:0 0 10px var(--on)}
table{width:100%;border-collapse:collapse}
td{padding:5px 0;border-bottom:1px solid var(--line)}td.v{text-align:right;font-variant-numeric:tabular-nums}
tr:last-child td{border-bottom:0}
.view-settings .only-main,.view-main .only-settings{display:none}
</style>
</head>
<body class="view-%VIEW%">
<header><div class="wrap"><h1 id="title">%TITLE%</h1><span id="net" class="pill">…</span>
<a class="only-main" href="/settings">Settings</a><a class="only-settings" href="/">Outlets</a></div></header>
<main class="wrap">

<section class="card only-main">
<h2>Outlets</h2>
<div class="outlets">
<div class="outlet" id="o1" onclick="toggle(1)"><span class="dot"></span><span class="st">–</span><span class="nm"></span></div>
<div class="outlet" id="o2" onclick="toggle(2)"><span class="dot"></span><span class="st">–</span><span class="nm"></span></div>
</div>
<div class="msg" id="omsg"></div>
</section>

<section class="card only-main">
<h2>Energy</h2>
<table id="energy"></table>
<p class="hint">One meter measures both outlets together.</p>
</section>

<section class="card only-settings">
<h2>Status</h2>
<dl id="status"></dl>
</section>

<section class="card only-settings">
<h2>Device</h2>
<label for="name">Name (outlets are "&lt;name&gt; Top" and "&lt;name&gt; Bottom")</label>
<input type="text" id="name" maxlength="32">
<label for="admin">Admin password (blank keeps the current one)</label>
<input type="password" id="admin" maxlength="64" autocomplete="new-password">
<label class="hint"><input type="checkbox" id="adminClear"> Remove the admin password</label>
<div class="row"><button class="primary" onclick="saveDevice()">Save</button></div>
<div class="msg" id="dmsg"></div>
</section>

<section class="card only-settings">
<h2>WiFi</h2>
<div class="row" style="margin-top:0"><button onclick="scan(true)" id="scanBtn">Scan</button><span class="hint" id="scanHint"></span></div>
<ul class="nets" id="nets"></ul>
<label for="ssid">Network (SSID)</label>
<input type="text" id="ssid" maxlength="32">
<label for="pass">Password</label>
<input type="password" id="pass" maxlength="63" autocomplete="new-password">
<div class="row"><button class="primary" onclick="saveWifi()">Save and connect</button><button class="danger" onclick="forget()">Forget WiFi</button></div>
<div class="msg" id="wmsg"></div>
</section>

<section class="card only-settings">
<h2>MQTT <span id="mqttPill" class="pill"></span></h2>
<label for="mhost">Broker host or IP (blank turns MQTT off)</label>
<input type="text" id="mhost" maxlength="64">
<div class="grid3">
<div><label for="mport">Port</label><input type="number" id="mport" min="1" max="65535"></div>
<div><label for="muser">User</label><input type="text" id="muser" maxlength="64"></div>
<div><label for="mpass">Password</label><input type="password" id="mpass" maxlength="64" autocomplete="new-password"></div>
</div>
<label class="hint"><input type="checkbox" id="mpassClear"> Remove the saved password</label>
<label for="mtopic">Base topic</label>
<input type="text" id="mtopic" maxlength="64">
<div class="row"><button class="primary" onclick="saveMqtt()">Save</button></div>
<div class="msg" id="mmsg"></div>
</section>

<section class="card only-settings">
<h2>Energy</h2>
<p class="hint" style="margin-top:0">Calibrate with an outlet on and a known load: enter what a reference meter shows (or the load's rating). Leave a field blank to skip it.</p>
<div class="grid3">
<div><label for="cv">Voltage (V)</label><input type="number" id="cv" step="0.1"></div>
<div><label for="cp">Power (W)</label><input type="number" id="cp" step="0.1"></div>
<div><label for="ci">Current (A)</label><input type="number" id="ci" step="0.001"></div>
</div>
<div class="row"><button class="primary" onclick="calibrate()">Calibrate</button></div>
<label for="etotal">Energy total (kWh)</label>
<input type="number" id="etotal" step="0.001">
<div class="row"><button onclick="setTotal()">Set total</button><button class="danger" onclick="resetEnergy()">Reset counters</button></div>
<div class="msg" id="emsg"></div>
</section>

<section class="card only-settings">
<h2>Firmware</h2>
<p class="hint" style="margin-top:0">Upload <code>firmware.bin</code> or <code>firmware.bin.gz</code>. The outlets keep their state through the restart.</p>
<div class="row"><input type="file" id="fw" accept=".bin,.gz"><button class="primary" onclick="upload()">Update</button></div>
<div class="bar" id="bar"><div></div></div>
<div class="msg" id="fmsg"></div>
</section>

<section class="card only-settings">
<h2>Maintenance</h2>
<div class="row" style="margin-top:0"><button onclick="reboot()">Restart</button><a href="/api/log" target="_blank" style="color:var(--accent)">Log</a></div>
<div class="msg" id="xmsg"></div>
</section>

</main>
<script>
const $=id=>document.getElementById(id);
const settings=document.body.classList.contains('view-settings');
function esc(s){return String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
function msg(id,r){const e=$(id);e.textContent=r.message||'';e.className='msg '+(r.ok?'ok':'bad')}
async function post(path,data){
  try{const r=await fetch(path,{method:'POST',body:new URLSearchParams(data)});return await r.json()}
  catch(e){return {ok:false,message:'No reply from the device.'}}
}
function afterRestart(id,r){msg(id,r);if(r.ok&&r.restart)setTimeout(()=>location.reload(),9000)}

function renderOutlets(c){
  c.outlets.forEach(o=>{
    const e=$('o'+o.outlet);e.classList.toggle('on',o.state==='on');
    e.querySelector('.st').textContent=o.state.toUpperCase();
    e.querySelector('.nm').textContent=o.name;
  });
  const n=c.energy,rows=[['Voltage',n.voltage,'V'],['Current',n.current.toFixed(3),'A'],['Active power',n.power,'W'],
    ['Apparent power',n.apparentPower,'VA'],['Reactive power',n.reactivePower,'var'],['Power factor',n.factor.toFixed(2),''],
    ['Energy today',n.today.toFixed(3),'kWh'],['Energy yesterday',n.yesterday.toFixed(3),'kWh'],
    ['Energy total',n.total.toFixed(3),'kWh'],['Total since',n.totalStartTime||'–','']];
  $('energy').innerHTML=rows.map(r=>`<tr><td>${r[0]}</td><td class="v">${esc(r[1])} ${r[2]}</td></tr>`).join('');
  $('title').textContent=c.name;document.title=c.name;
}
async function refresh(){
  try{const c=await (await fetch('/api/controller')).json();renderOutlets(c)}catch(e){}
}
async function toggle(n){
  const r=await post('/api/outlet',{outlet:n,state:'toggle'});if(!r.ok)msg('omsg',r);refresh();
}

async function status(){
  let s;try{s=await (await fetch('/api/status')).json()}catch(e){return}
  const pill=$('net');
  if(s.connected){pill.textContent=s.ip;pill.className='pill ok'}
  else if(s.mode==='portal'){pill.textContent='Setup AP';pill.className='pill warn'}
  else{pill.textContent='Offline';pill.className='pill warn'}
  if(!settings)return;
  const up=s.uptime,d=Math.floor(up/86400),h=Math.floor(up%86400/3600),m=Math.floor(up%3600/60);
  const rows=[['Firmware',s.version+' ('+s.build+')'],['WiFi',s.connected?`${s.savedSsid}, ${s.rssi} dBm, channel ${s.channel}`:(s.savedSsid?'not connected to '+s.savedSsid:'no network saved')],
    ['IP address',s.ip||'–'],['Hostname',s.hostname+'.local'],['Setup AP',s.mode==='portal'?`${s.apSsid} (${s.apIp}), ${s.apClients} connected`:'off'],
    ['MAC',s.mac],['Time',s.time||'not set'],['Uptime',`${d}d ${h}h ${m}m`],['Last restart',s.resetReason],
    ['Free heap',s.heap+' bytes'],['Firmware space',`${Math.round(s.sketch/1024)} KB used, ${Math.round(s.otaSpace/1024)} KB free for updates`],
    ['Button input (A0)',s.adc],['Calibration',`power ${s.meter.powerCal}, voltage ${s.meter.voltageCal}, current ${s.meter.currentCal}`]];
  $('status').innerHTML=rows.map(r=>`<dt>${r[0]}</dt><dd>${esc(r[1])}</dd>`).join('');
}

async function loadSettings(){
  try{
    const c=await (await fetch('/api/controller')).json();$('name').value=c.name;
    const m=await (await fetch('/api/mqtt')).json();
    $('mhost').value=m.host;$('mport').value=m.port;$('muser').value=m.user;$('mtopic').value=m.topic;
    $('mpass').placeholder=m.passSet?'(saved)':'';
    $('mqttPill').textContent=!m.enabled?'off':m.connected?'connected':'not connected';
    $('mqttPill').className='pill '+(m.connected?'ok':m.enabled?'warn':'');
    const s=await (await fetch('/api/status')).json();$('ssid').value=s.savedSsid;
    const e=await (await fetch('/api/energy')).json();$('etotal').value=e.energy.total.toFixed(3);
  }catch(e){}
}
async function saveDevice(){
  const d={name:$('name').value};
  if($('adminClear').checked)d.adminClear=1;else if($('admin').value)d.admin=$('admin').value;
  afterRestart('dmsg',await post('/api/device',d));
}
async function scan(fresh){
  $('scanBtn').disabled=true;$('scanHint').textContent='Scanning…';
  let r;
  for(let i=0;i<20;i++){
    try{r=await (await fetch('/api/scan'+(fresh&&i===0?'?fresh=1':''))).json()}catch(e){break}
    if(!r.scanning)break;await new Promise(x=>setTimeout(x,700));
  }
  $('scanBtn').disabled=false;$('scanHint').textContent='';
  if(!r||!r.networks)return;
  r.networks.sort((a,b)=>b.rssi-a.rssi);
  $('nets').innerHTML=r.networks.map(n=>`<li data-s="${esc(n.ssid)}"><span>${esc(n.ssid)}${n.secure?' 🔒':''}</span><span class="s">${n.rssi} dBm</span></li>`).join('');
  $('nets').querySelectorAll('li').forEach(li=>li.onclick=()=>{$('ssid').value=li.dataset.s;$('pass').focus()});
}
async function saveWifi(){afterRestart('wmsg',await post('/api/wifi',{ssid:$('ssid').value,pass:$('pass').value}))}
async function forget(){if(confirm('Erase the saved WiFi network? The device restarts into its setup AP.'))afterRestart('wmsg',await post('/api/forget',{}))}
async function saveMqtt(){
  const d={host:$('mhost').value,port:$('mport').value,user:$('muser').value,topic:$('mtopic').value};
  if($('mpassClear').checked)d.passClear=1;else if($('mpass').value)d.pass=$('mpass').value;
  msg('mmsg',await post('/api/mqtt',d));setTimeout(loadSettings,3000);
}
async function calibrate(){
  msg('emsg',await post('/api/energy/calibrate',{voltage:$('cv').value,power:$('cp').value,current:$('ci').value}));status();
}
async function setTotal(){msg('emsg',await post('/api/energy',{total:$('etotal').value}))}
async function resetEnergy(){if(confirm('Reset today, yesterday and total to zero?'))msg('emsg',await post('/api/energy',{reset:1}))}
async function reboot(){afterRestart('xmsg',await post('/api/reboot',{}))}
function upload(){
  const f=$('fw').files[0];if(!f){msg('fmsg',{ok:false,message:'Choose a firmware file first.'});return}
  const fd=new FormData();fd.append('firmware',f,f.name);
  const x=new XMLHttpRequest(),bar=$('bar');bar.style.display='block';
  x.upload.onprogress=e=>{if(e.lengthComputable)bar.firstChild.style.width=(100*e.loaded/e.total)+'%'};
  x.onload=()=>{let r;try{r=JSON.parse(x.responseText)}catch(e){r={ok:false,message:'Unexpected reply ('+x.status+').'}}afterRestart('fmsg',r)};
  x.onerror=()=>msg('fmsg',{ok:false,message:'Upload failed.'});
  x.open('POST','/update');x.send(fd);msg('fmsg',{ok:true,message:'Uploading…'});
}

status();setInterval(status,10000);
if(settings){loadSettings()}else{refresh();setInterval(refresh,2000)}
</script>
</body>
</html>
)rawliteral";
