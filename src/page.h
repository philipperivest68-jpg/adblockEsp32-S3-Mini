#pragma once

const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html>
<head>
<meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>S3MiniPihole</title>
<style>
  body   { font:14px system-ui,sans-serif; margin:0; background:#0d1117; color:#c9d1d9 }
  header { background:#161b22; padding:14px 18px; border-bottom:1px solid #30363d }
  h1     { margin:0; font-size:18px }
  h1 span{ color:#3fb950 }
  h2     { font-size:14px; color:#8b949e; margin:18px 0 8px }
  .wrap  { padding:16px; max-width:1000px; margin:auto }

  .cards { display:flex; flex-wrap:wrap; gap:10px; margin-bottom:16px }
  .card  { background:#161b22; border:1px solid #30363d; border-radius:8px; padding:12px 16px; flex:1; min-width:120px }
  .card .v { font-size:22px; font-weight:600 }
  .card .l { color:#8b949e; font-size:12px }

  table  { width:100%; border-collapse:collapse; background:#161b22; border-radius:8px; overflow:hidden; margin-bottom:18px }
  th,td  { padding:8px 10px; text-align:left; border-bottom:1px solid #21262d; font-size:13px }
  th     { background:#21262d; color:#8b949e }
  tr:hover td { background:#1c2128 }

  .b   { color:#f85149 }
  .a   { color:#3fb950 }
  .tag { background:#30363d; border-radius:4px; padding:1px 6px; font-size:11px }
  .ban { color:#f85149 }

  button { background:#21262d; color:#c9d1d9; border:1px solid #30363d; border-radius:5px; padding:4px 9px; cursor:pointer }
  button:hover { background:#30363d }
  input, select { background:#0d1117; border:1px solid #30363d; color:#c9d1d9; border-radius:5px; padding:6px }

  /* default-credentials warning banner (shown from JS) */
  .warn { display:none; background:#3b1d1d; border:1px solid #f85149; color:#ffb3ae;
          border-radius:8px; padding:10px 14px; margin-bottom:14px; font-size:13px }
  /* pause / blocking-status bar */
  /* 24h hourly chart */
  .hist  { display:flex; align-items:flex-end; gap:3px; height:90px; padding:10px; background:#161b22;
           border:1px solid #30363d; border-radius:8px; margin-bottom:4px }
  .hist div { flex:1; display:flex; flex-direction:column-reverse; height:100%; min-width:4px }
  .hist i { display:block; width:100% }
  .hist .hb { background:#f85149 } .hist .ha { background:#238636 }
  .hlbl  { display:flex; justify-content:space-between; color:#8b949e; font-size:11px; margin-bottom:18px }
  .muted { color:#8b949e }
  .dom   { word-break:break-all }
  .bar  { display:flex; align-items:center; gap:12px; margin-bottom:14px;
          padding:12px 14px; background:#161b22; border:1px solid #30363d; border-radius:8px }
</style>
</head>
<body>
<header><h1>&#128737; S3MiniPihole <span id=host></span></h1></header>
<div class=wrap>

  <div id=credwarn class=warn>
    &#9888; <b>Default credentials in use.</b> WEB_PASS/AP_PASS in <code>secrets.h</code>
    are still the placeholder values &mdash; set real ones and reflash.
  </div>

  <div id=blockbar class=bar>
    <span id=blockdot style=font-size:20px>&#128737;</span>
    <b id=blockstate style=flex:1 data-on=1>Blocking active</b>
    <select id=pausedur>
      <option value=30>30s</option>
      <option value=300 selected>5 min</option>
      <option value=1800>30 min</option>
      <option value=0>until I re-enable</option>
    </select>
    <button id=pausebtn onclick=togglePause()>Pause</button>
  </div>

  <div class=cards id=sys></div>

  <h2>LAST 24 HOURS <span class=muted style=font-weight:normal>(<span style=color:#f85149>&#9632;</span> blocked
    <span style=color:#238636>&#9632;</span> allowed, per hour)</span></h2>
  <div class=hist id=hist></div>
  <div class=hlbl><span>-24h</span><span>-12h</span><span>now</span></div>

  <h2>TOP BLOCKED DOMAINS</h2>
  <table id=tt>
    <thead><tr><th>#</th><th>Domain</th><th>Hits</th><th>Last hit</th></tr></thead>
    <tbody></tbody>
  </table>

  <h2>RECENTLY BLOCKED <span class=muted style=font-weight:normal>(last 50)</span></h2>
  <div style=margin-bottom:8px>
    <input id=rf placeholder="filter by domain or client IP" size=30 oninput=drawRecent()>
    <button onclick="if(confirm('Clear recent + top blocked lists?'))fetch('/clearlog',{headers:CSRF_HDRS}).then(load)">Clear</button>
  </div>
  <table id=rt>
    <thead><tr><th>When</th><th>Domain</th><th>Client</th><th>Type</th><th>Reason</th></tr></thead>
    <tbody></tbody>
  </table>

  <h2>CLIENTS</h2>
  <table id=ct>
    <thead><tr><th>Client</th><th>MAC</th><th>Blocked</th><th>Allowed</th><th></th></tr></thead>
    <tbody></tbody>
  </table>

  <h2>CUSTOM BLOCKED DOMAINS</h2>
  <div style=margin-bottom:8px>
    <input id=dom placeholder="ads.example.com" size=30>
    <button onclick=addDom()>Block domain</button>
  </div>
  <table id=cl><tbody></tbody></table>

  <h2>BLOCKLIST &mdash; UPLOAD</h2>
  <form id=upf style=margin-bottom:6px>
    <input type=file id=blf accept=.bin>
    <button>Upload blocklist</button>
    <span id=upmsg style=color:#8b949e></span>
  </form>
  <div style="color:#8b949e;font-size:12px;margin-bottom:18px">
    build <code>blocklist.bin</code> with <code>make blocklist</code>, then upload here &mdash; no USB
  </div>

  <h2>BLOCKLIST &mdash; REMOTE AUTO-UPDATE</h2>
  <div style=margin-bottom:6px>
    <input id=uurl placeholder="https://host/blocklist.bin" size=40>
    every <input id=uiv size=2 value=24>h
    <button onclick=saveUpd()>Save</button>
    <button onclick=fetchNow()>Fetch now</button>
  </div>
  <div style="color:#8b949e;font-size:12px;margin-bottom:18px">
    device pulls a prebuilt <code>blocklist.bin</code> on a schedule (e.g. a GitHub
    release asset). last: <span id=ustat>&mdash;</span>
  </div>

  <h2>WIFI</h2>
  <div style=margin-bottom:18px>
    <button onclick="if(confirm('Forget saved WiFi and reboot into the setup portal?'))forgetWifi()">Forget WiFi</button>
  </div>

</div>
<script>
function fmt(n){return n.toLocaleString()}
function esc(s){return String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
// A plain <img>/<form> CSRF can't set a custom header, only same-origin fetch()
// can -- so requiring this on every mutating request blocks drive-by CSRF.
const CSRF_HDRS={'X-Requested-With':'esp-adblock'}
const QT={1:'A',2:'NS',5:'CNAME',6:'SOA',12:'PTR',15:'MX',16:'TXT',28:'AAAA',33:'SRV',64:'SVCB',65:'HTTPS'};
function ago(s){return s<60?s+'s ago':s<3600?Math.floor(s/60)+'m ago':s<86400?Math.floor(s/3600)+'h ago':Math.floor(s/86400)+'d ago'}
let qd={recent:[],top:[],hours:[]};
function drawRecent(){let f=rf.value.trim().toLowerCase();
let r=qd.recent.filter(e=>!f||e.d.includes(f)||e.ip.includes(f));
rt.tBodies[0].innerHTML=r.map(e=>`<tr><td class=muted>${ago(e.age)}</td><td class=dom>${esc(e.d||'(unparsed)')}</td><td>${e.ip}</td>
<td><span class=tag>${QT[e.qt]||e.qt}</span></td><td>${e.why==1?'<span class=b>client banned</span>':'blocklist'}</td></tr>`).join('')
||'<tr><td colspan=5 class=muted>'+(f?'no match':'nothing blocked yet')+'</td></tr>';}
function drawInsights(){
let mx=Math.max(1,...qd.hours.map(h=>h[0]+h[1]));
hist.innerHTML=qd.hours.map((h,i)=>{let t=h[0]+h[1],p=t?Math.round(h[0]*100/t):0;
return `<div title="${23-i}h ago: ${fmt(h[0])} blocked / ${fmt(h[1])} allowed (${p}%)"><i class=hb style=height:${h[0]*100/mx}%></i><i class=ha style=height:${h[1]*100/mx}%></i></div>`}).join('');
tt.tBodies[0].innerHTML=qd.top.map((e,i)=>`<tr><td class=muted>${i+1}</td><td class=dom>${esc(e.d)}</td><td class=b>${fmt(e.n)}</td><td class=muted>${ago(e.age)}</td></tr>`).join('')
||'<tr><td colspan=4 class=muted>nothing blocked yet</td></tr>';
drawRecent();}
function togglePause(){fetch(blockstate.dataset.on=='1'?'/pause?s='+pausedur.value:'/resume',{headers:CSRF_HDRS}).then(load);}
async function load(){let s=await(await fetch('/stats.json')).json();
try{qd=await(await fetch('/blocked.json')).json();drawInsights();}catch(_){}
let tot=s.blocked+s.allowed,pct=tot?(s.blocked*100/tot).toFixed(1):'0.0';
host.textContent='@ '+s.ip;
credwarn.style.display=s.defcreds?'block':'none';
let on=s.blocking!==false;blockstate.dataset.on=on?'1':'0';
blockdot.textContent=on?'🛡️':'⏸️';blockbar.style.borderColor=on?'#30363d':'#f0883e';
blockstate.textContent=on?'Blocking active':(s.resumeIn>0?'Paused — resumes in '+s.resumeIn+'s':'Paused');
pausebtn.textContent=on?'Pause':'Resume';pausedur.style.display=on?'':'none';
sys.innerHTML=[['Total blocked',fmt(s.blocked),'b'],['Total allowed',fmt(s.allowed),'a'],['Blocked',pct+' %','b'],['Blocklist',fmt(s.domains)+' domains',''],
['Clients',s.clients.length,''],['WiFi',s.rssi+' dBm',''],['Temp',s.temp+' °C',''],['Free RAM',Math.round(s.heap/1024)+' KB',''],['Uptime',s.uptime,'']]
.map(c=>`<div class=card><div class="v ${c[2]}">${c[1]}</div><div class=l>${c[0]}</div></div>`).join('');
ct.tBodies[0].innerHTML=s.clients.sort((a,b)=>(b.blocked+b.allowed)-(a.blocked+a.allowed)).map(c=>
`<tr><td>${c.ip}${c.banned?' <span class=tag style=color:#f85149>BANNED</span>':''}</td><td>${c.mac}</td>
<td class=b>${fmt(c.blocked)} <span class=muted>(${c.blocked+c.allowed?Math.round(c.blocked*100/(c.blocked+c.allowed)):0}%)</span></td><td class=a>${fmt(c.allowed)}</td>
<td><button class=ban data-ip="${c.ip}">${c.banned?'Unban':'Ban'}</button></td></tr>`).join('');
cl.tBodies[0].innerHTML=s.custom.map(d=>`<tr><td>${esc(d)}</td><td style=text-align:right><button class=rmbtn data-d="${esc(d)}">remove</button></td></tr>`).join('')||'<tr><td style=color:#8b949e>none yet</td></tr>';
if(document.activeElement!=uurl)uurl.value=s.upurl||'';
if(document.activeElement!=uiv)uiv.value=s.upiv||24;
ustat.textContent=s.upstat||'—';}
function addDom(){let d=dom.value.trim();if(d){fetch('/addblock?d='+encodeURIComponent(d),{headers:CSRF_HDRS}).then(()=>{dom.value='';load()})}}
ct.addEventListener('click',e=>{if(e.target.classList.contains('ban'))fetch('/ban?ip='+e.target.dataset.ip,{headers:CSRF_HDRS}).then(load)});
cl.addEventListener('click',e=>{if(e.target.classList.contains('rmbtn'))fetch('/unblock?d='+encodeURIComponent(e.target.dataset.d),{headers:CSRF_HDRS}).then(load)});
function saveUpd(){fetch('/setupdate?u='+encodeURIComponent(uurl.value.trim())+'&h='+(parseInt(uiv.value)||24),{headers:CSRF_HDRS}).then(load)}
function fetchNow(){ustat.textContent='fetching...';fetch('/fetchnow',{headers:CSRF_HDRS}).then(r=>r.text()).then(t=>{ustat.textContent=t;load()})}
function forgetWifi(){fetch('/forgetwifi',{headers:CSRF_HDRS}).then(r=>r.text()).then(t=>alert(t))}
upf.onsubmit=async e=>{e.preventDefault();let f=blf.files[0];if(!f)return;
upmsg.textContent='uploading '+(f.size/1048576).toFixed(2)+' MB...';
let fd=new FormData();fd.append('f',f);
try{let r=await fetch('/upload',{method:'POST',headers:CSRF_HDRS,body:fd});upmsg.textContent=r.ok?'✓ updated':'✗ '+await r.text();}
catch(_){upmsg.textContent='✗ upload failed';}
blf.value='';setTimeout(load,600);};
load();setInterval(load,3000);
</script>
</body>
</html>)HTML";
