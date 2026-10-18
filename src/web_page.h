#pragma once
// Halaman kontrol di http://192.168.4.1

const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="id"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Pelacak Satelit</title>
<style>
body{font-family:system-ui,sans-serif;background:#0b0f17;color:#e6e9ef;margin:0;padding:12px;max-width:560px}
h1{font-size:18px;margin:0 0 8px}h2{font-size:15px;margin:16px 0 6px;color:#9fb3c8}
canvas{width:100%;max-width:360px;display:block;margin:0 auto;background:#05080d;border-radius:50%}
.row{display:flex;gap:6px;flex-wrap:wrap;margin:6px 0}
button,select,input{font-size:15px;padding:10px 8px;border:0;border-radius:6px;background:#1f2a3a;color:#e6e9ef}
button{flex:1}button.on{background:#0a8a5f}
input{min-width:0;flex:1}input[type=checkbox]{flex:0 0 auto;width:20px;height:20px}
.pad{display:grid;grid-template-columns:repeat(3,1fr);gap:6px;max-width:220px;margin:6px auto}
.pad button{padding:14px 0}
#info{font:13px/1.5 monospace;white-space:pre-wrap;color:#9fe0b0}
#msg{color:#ffcf66;font-size:13px;min-height:16px}
table{width:100%;border-collapse:collapse;font-size:13px}td,th{padding:4px;border-bottom:1px solid #1f2a3a;text-align:left}
.vis{color:#ffd54f}label{display:flex;align-items:center;gap:8px;margin:4px 0;font-size:14px}
</style></head><body>
<h1>🛰️ Pelacak Satelit</h1>
<div class="row">
<select id="preset" onchange="if(this.value)sat(this.value)">
<option value="">Pilih satelit...</option>
<optgroup label="Terlihat mata"><option value="25544">ISS</option><option value="48274">Tiangong (CSS)</option><option value="20580">Hubble</option></optgroup>
<optgroup label="Cuaca"><option value="57166">Meteor-M2 3</option><option value="59051">Meteor-M2 4</option><option value="25338">NOAA 15</option><option value="28654">NOAA 18</option><option value="33591">NOAA 19</option></optgroup>
<optgroup label="Radio amatir"><option value="27607">SO-50</option></optgroup>
<optgroup label="Geostasioner (diam)"><option value="41836">Himawari-9</option><option value="43587">Merah Putih (Telkom-4)</option></optgroup>
</select>
<input id="norad" type="number" placeholder="NORAD ID"><button style="flex:0 0 auto" onclick="sat(norad.value)">Pakai</button>
</div>
<div id="msg"></div>
<canvas id="sky" width="360" height="360"></canvas>
<div id="info">menghubungkan...</div>
<div class="row">
<button id="bTrack" onclick="cmd('track')">TRACK</button>
<button onclick="cmd('stop')">STOP</button>
<button onclick="cmd('park')">PARK</button>
<button onclick="cmd('tle')">Update TLE</button>
</div>
<h2>Lintasan berikutnya</h2>
<table><thead><tr><th>Mulai</th><th>Durasi</th><th>El maks</th><th>Arah</th><th></th></tr></thead><tbody id="passes"></tbody></table>
<h2>Kalibrasi</h2>
<div class="row"><button onclick="cmd('north')">Set utara</button><button onclick="cmd('level')">Set datar</button></div>
<label><input type="checkbox" id="invPan" onchange="setv('invPan',this.checked)"> Balik arah pan</label>
<label><input type="checkbox" id="invTilt" onchange="setv('invTilt',this.checked)"> Balik arah tilt</label>
<div class="pad">
<span></span><button data-m="0,1">&#9650;</button><span></span>
<button data-m="-1,0">&#9664;</button><span></span><button data-m="1,0">&#9654;</button>
<span></span><button data-m="0,-1">&#9660;</button><span></span>
</div>
<div class="row"><input id="gaz" type="number" placeholder="Azimuth °"><input id="gel" type="number" placeholder="Elevasi °">
<button onclick="fetch(`/goto?az=${gaz.value}&el=${gel.value}`)">GOTO</button></div>
<h2>WiFi internet (untuk unduh TLE)</h2>
<div class="row"><input id="ws" placeholder="Nama WiFi / hotspot"><input id="wp" type="password" placeholder="Password">
<button style="flex:0 0 auto" onclick="fetch(`/wifi?s=${encodeURIComponent(ws.value)}&p=${encodeURIComponent(wp.value)}`)">Simpan</button></div>
<h2>Lokasi cadangan (jika GPS belum fix)</h2>
<div class="row"><input id="la" placeholder="Lintang"><input id="lo" placeholder="Bujur"><input id="al" placeholder="Tinggi m">
<button style="flex:0 0 auto" onclick="fetch(`/loc?lat=${la.value}&lon=${lo.value}&alt=${al.value||0}`)">Simpan</button></div>
<script>
const $=id=>document.getElementById(id);
const cv=$('sky'),cx=cv.getContext('2d'),C=180,R=165;
let move=null,init=false;
function cmd(c){fetch('/cmd?c='+c)}
function setv(k,v){fetch(`/set?k=${k}&v=${v?1:0}`)}
function sat(id){if(id>0){$('msg').textContent='Mengganti satelit...';fetch('/sat?id='+id)}}
document.querySelectorAll('[data-m]').forEach(b=>{
 b.addEventListener('pointerdown',e=>{e.preventDefault();move=b.dataset.m.split(',')});
 ['pointerup','pointerleave'].forEach(ev=>b.addEventListener(ev,()=>move=null));
});
setInterval(()=>{if(move)fetch(`/move?pan=${move[0]}&tilt=${move[1]}`)},150);
function xy(az,el){const r=R*(90-Math.max(el,0))/90,a=az*Math.PI/180;return[C+r*Math.sin(a),C-r*Math.cos(a)]}
function sky(s){
 cx.clearRect(0,0,360,360);cx.strokeStyle='#22324a';cx.lineWidth=1;
 for(const e of[0,30,60]){cx.beginPath();cx.arc(C,C,R*(90-e)/90,0,7);cx.stroke()}
 cx.beginPath();cx.moveTo(C,C-R);cx.lineTo(C,C+R);cx.moveTo(C-R,C);cx.lineTo(C+R,C);cx.stroke();
 cx.fillStyle='#6f86a3';cx.font='13px sans-serif';cx.textAlign='center';cx.textBaseline='middle';
 [['U',0],['T',90],['S',180],['B',270]].forEach(([t,a])=>{const[x,y]=xy(a,-6);cx.fillText(t,x,y)});
 if(s.path.length){cx.strokeStyle='#3d8bfd';cx.lineWidth=2;cx.beginPath();
  s.path.forEach((p,i)=>{const[x,y]=xy(p[0],p[1]);i?cx.lineTo(x,y):cx.moveTo(x,y)});cx.stroke();
  const[x,y]=xy(s.path[0][0],s.path[0][1]);cx.fillStyle='#3d8bfd';cx.beginPath();cx.arc(x,y,4,0,7);cx.fill()}
 const pa=((s.pan%360)+360)%360,[mx,my]=xy(pa,s.tilt);cx.strokeStyle='#ff5252';cx.lineWidth=2;
 cx.beginPath();cx.moveTo(mx-7,my);cx.lineTo(mx+7,my);cx.moveTo(mx,my-7);cx.lineTo(mx,my+7);cx.stroke();
 if(s.ready&&s.el>0){const[x,y]=xy(s.az,s.el);cx.fillStyle='#ffd54f';cx.beginPath();cx.arc(x,y,6,0,7);cx.fill()}
}
const t2=u=>new Date(u*1000).toLocaleString('id-ID',{weekday:'short',hour:'2-digit',minute:'2-digit'});
function dur(s){s=Math.max(0,Math.round(s));const h=Math.floor(s/3600),m=Math.floor(s%3600/60);return h?`${h}j ${m}m`:m?`${m}m ${s%60}d`:`${s}d`}
const dir=a=>['U','TL','T','TG','S','BD','B','BL'][Math.round(a/45)%8];
async function loop(){
 try{
  const s=await(await fetch('/status')).json();
  sky(s);
  let next='';
  if(s.geo)next='satelit geostasioner (diam di langit)';
  else if(s.passes.length){const p=s.passes[0];next=s.now<p[0]?`muncul dalam ${dur(p[0]-s.now)}`:`SEDANG LEWAT, hilang dalam ${dur(p[2]-s.now)}`}
  $('info').textContent=
   `${s.name||'(belum ada TLE)'} #${s.norad}  umur TLE ${s.tleAge??'-'} hari\n`+
   (s.ready?`az ${s.az.toFixed(1)}°  el ${s.el.toFixed(1)}°  jarak ${s.dist} km  tinggi ${s.satAlt} km\n${next}\n`:'')+
   `mount pan ${s.pan.toFixed(1)}°  tilt ${s.tilt.toFixed(1)}°  [${s.mode}]\n`+
   `GPS ${s.gpsFix?'FIX '+s.gpsSats+' sat':(s.gpsChars?'mencari sinyal...':'TIDAK ADA DATA')}  waktu ${s.timeSrc}  ${s.lat}, ${s.lon}\n`+
   `WiFi internet ${s.wifi||'tidak tersambung'}`;
  $('msg').textContent=s.msg;
  $('bTrack').classList.toggle('on',s.mode=='TRACK');
  $('passes').innerHTML=s.passes.map(p=>`<tr class="${p[6]==2?'vis':''}"><td>${t2(p[0])}</td><td>${dur(p[2]-p[0])}</td><td>${p[3].toFixed(0)}°</td><td>${dir(p[4])}→${dir(p[5])}</td><td>${p[6]==2?'👁 terlihat':''}</td></tr>`).join('')||'<tr><td colspan=5>-</td></tr>';
  if(!init){$('invPan').checked=s.invPan;$('invTilt').checked=s.invTilt;$('ws').value=s.ssid;$('la').value=s.lat;$('lo').value=s.lon;init=true}
 }catch(e){$('info').textContent='terputus, mencoba lagi...'}
 setTimeout(loop,500);
}
loop();
</script></body></html>)HTML";
