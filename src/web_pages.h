// HTML / CSS served by the clock (kept in flash)
#pragma once

#include <Arduino.h>

static const char STYLE_CSS[] PROGMEM = R"CSS(
:root{--bg:#0c0e13;--card:#161920;--line:#252a35;--tx:#e9ebf1;--mut:#8b91a1;--acc:#ff3b3b}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--tx);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
.w{max-width:520px;margin:0 auto;padding:16px}
.hero{border-radius:16px;padding:26px 16px 20px;text-align:center;border:1px solid var(--line);
 background:#050505 radial-gradient(#2a0707 1.2px,transparent 1.3px) 0 0/7px 7px}
.clk{font:700 54px/1 ui-monospace,Consolas,"Courier New",monospace;color:var(--acc);letter-spacing:2px;
 text-shadow:0 0 6px #ff3b3b,0 0 22px #ff3b3b88}
.ap{font-size:18px;margin-left:8px;letter-spacing:0}
.dt{color:var(--mut);margin-top:10px}
.card{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:16px;margin-top:14px}
h2{font-size:12px;text-transform:uppercase;letter-spacing:.09em;color:var(--mut);margin:0 0 10px;font-weight:600}
.row{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:10px 0;border-top:1px solid var(--line)}
.row:first-of-type{border-top:0}
.row small{display:block;color:var(--mut);font-size:12px}
select,input[type=text],input[type=password]{background:#0e1016;color:var(--tx);border:1px solid var(--line);
 border-radius:9px;padding:9px 10px;font:inherit;max-width:62%}
.full{width:100%;max-width:none;margin-bottom:10px}
input[type=range]{width:100%;accent-color:var(--acc);margin:8px 0 4px}
.sw{position:relative;width:46px;height:26px;flex:none}
.sw input{opacity:0;width:0;height:0}
.sw span{position:absolute;inset:0;background:#353a46;border-radius:26px;transition:.2s;cursor:pointer}
.sw span:before{content:"";position:absolute;width:20px;height:20px;left:3px;top:3px;background:#fff;border-radius:50%;transition:.2s}
.sw input:checked+span{background:var(--acc)}
.sw input:checked+span:before{transform:translateX(20px)}
.btns{display:flex;flex-wrap:wrap;gap:8px;margin-top:12px}
button,.btn{background:#2a2f3b;color:var(--tx);border:0;border-radius:10px;padding:10px 14px;font:inherit;cursor:pointer;text-decoration:none;display:inline-block}
button:hover,.btn:hover{filter:brightness(1.2)}
button.p{background:var(--acc);color:#fff}
button.d{background:#3b1d22;color:#ff9a9a}
button:disabled{opacity:.5}
h2 .sm{float:right;padding:3px 10px;font-size:12px;text-transform:none;letter-spacing:0}
.stat{display:grid;grid-template-columns:auto 1fr;gap:6px 14px;font-size:14px;color:var(--mut)}
.stat b{color:var(--tx);font-weight:500;text-align:right;overflow-wrap:anywhere}
.net{display:flex;justify-content:space-between;align-items:center;padding:12px 10px;border-radius:10px;cursor:pointer;gap:10px}
.net:hover{background:#1f2430}
.net.on{background:#2a1418;outline:1px solid var(--acc)}
.sig{display:inline-flex;gap:2px;align-items:flex-end;height:14px;flex:none}
.sig b{width:4px;background:#3a3f4b;border-radius:1px}
.sig b:nth-child(1){height:4px}.sig b:nth-child(2){height:7px}.sig b:nth-child(3){height:10px}.sig b:nth-child(4){height:14px}
.s1 b:nth-child(-n+1),.s2 b:nth-child(-n+2),.s3 b:nth-child(-n+3),.s4 b{background:var(--acc)}
.chk{display:flex;gap:8px;align-items:center;color:var(--mut)}
.mut{color:var(--mut)}
.foot{text-align:center;color:var(--mut);font-size:12px;margin:22px 0 8px}
#toast{position:fixed;left:50%;bottom:22px;transform:translateX(-50%);background:#262b36;padding:10px 18px;border-radius:12px;
 opacity:0;transition:opacity .3s;pointer-events:none}
)CSS";

static const char MAIN_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html lang=en><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Matrix Clock</title>
<link rel=stylesheet href=/style.css></head><body><div class=w>
<div class=hero><div class=clk><span id=tm>--:--</span><span class=ap id=ap></span></div><div class=dt id=dt>&nbsp;</div></div>

<div class=card><h2>Brightness</h2>
<div class=row><span>Brightness</span><b id=brv>-</b></div>
<input type=range id=br min=0 max=15>
<div class=row><span>Night mode<small>Dim automatically at night</small></span><label class=sw><input type=checkbox id=autoDim><span></span></label></div>
<div id=night>
<div class=row><span>From</span><select id=nightFrom class=hr></select></div>
<div class=row><span>Until</span><select id=nightTo class=hr></select></div>
<div class=row><span>Night brightness</span><select id=nightBr></select></div>
</div></div>

<div class=card><h2>Clock</h2>
<div class=row><span>Time format</span><select id=fmt24><option value=0>12 hour</option><option value=1>24 hour</option></select></div>
<div class=row><span>Time zone</span><select id=tz></select></div>
<div class=row><span>Blinking colon<small>Middle dots blink every second</small></span><label class=sw><input type=checkbox id=blinkColon><span></span></label></div>
<div class=row><span>Seconds bar<small>Thin line that fills up each minute</small></span><label class=sw><input type=checkbox id=secondsBar><span></span></label></div>
<div class=row><span>Leading zero<small>07:05 instead of 7:05</small></span><label class=sw><input type=checkbox id=leadingZero><span></span></label></div>
<div class=row><span>Show date<small>Scrolls by once a minute</small></span><label class=sw><input type=checkbox id=showDate><span></span></label></div>
</div>

<div class=card><h2>Animations</h2>
<div class=row><span>Digit change</span><select id=roll><option value=0>Roll down</option><option value=1>Roll up</option><option value=2>Instant</option></select></div>
<div class=row><span>Every hour</span><select id=hourlyAnim class=an></select></div>
<div class=row><span>At power-on</span><select id=bootAnim class=an></select></div>
<div class=btns id=try></div>
</div>

<div class=card><h2>Welcome message</h2>
<input type=text id=welcome maxlength=63 class=full placeholder="Shown at power-on">
<div class=btns style=margin-top:0><button class=p id=wsave>Save</button><button id=wshow>Show on clock</button></div>
</div>

<div class=card><h2>WiFi &amp; system</h2>
<div class=stat><span>Network</span><b id=ssid>-</b><span>Signal</span><b id=rssi>-</b><span>IP address</span><b id=ip>-</b><span>Uptime</span><b id=up>-</b></div>
<div class=btns><a class=btn href=/wifi>Change WiFi</a><button id=sip>Show IP on clock</button><button id=sync>Re-sync time</button>
<button id=rst>Restart</button><button id=fac class=d>Factory reset</button></div>
</div>
<p class=foot>FLASH button: short press shows the IP &middot; hold 5 s for WiFi setup<br>Also reachable at http://matrixclock.local</p>
</div><div id=toast></div>
<script>
const $=i=>document.getElementById(i);
const post=(u,d)=>fetch(u,{method:'POST',body:new URLSearchParams(d||{})});
function toast(t){const e=$('toast');e.textContent=t;e.style.opacity=1;clearTimeout(e.t);e.t=setTimeout(()=>e.style.opacity=0,1600)}
const hr=h=>h==0?'12 AM':h<12?h+' AM':h==12?'12 PM':(h-12)+' PM';
document.querySelectorAll('.hr').forEach(s=>{for(let h=0;h<24;h++)s.add(new Option(hr(h),h))});
for(let b=0;b<=16;b++)$('nightBr').add(new Option(b==16?'Display off':b,b));
const AN=['Off','Sparkle','Wipe','Rain','Boxes','Pac-Man','Random'];
document.querySelectorAll('.an').forEach(s=>AN.forEach((n,i)=>s.add(new Option(n,i))));
AN.slice(1,6).forEach((n,i)=>{const b=document.createElement('button');b.textContent='▶ '+n;b.onclick=()=>post('/api/anim',{n:i+1});$('try').appendChild(b)});
const TZ=[['IST-5:30','India (UTC+5:30)'],['<+0545>-5:45','Nepal (UTC+5:45)'],['<+06>-6','Bangladesh (UTC+6)'],
['<+0530>-5:30','Sri Lanka (UTC+5:30)'],['<+04>-4','UAE / Oman (UTC+4)'],['<+03>-3','Saudi Arabia / Qatar / Kuwait (UTC+3)'],
['<+08>-8','Singapore / Malaysia (UTC+8)'],['GMT0','UTC / GMT'],['GMT0BST,M3.5.0/1,M10.5.0','UK (London)'],
['CET-1CEST,M3.5.0,M10.5.0/3','Central Europe'],['EST5EDT,M3.2.0,M11.1.0','US Eastern'],['CST6CDT,M3.2.0,M11.1.0','US Central'],
['PST8PDT,M3.2.0,M11.1.0','US Pacific'],['AEST-10AEDT,M10.1.0,M4.1.0/3','Australia (Sydney)']];
TZ.forEach(z=>$('tz').add(new Option(z[1],z[0])));
function set(k,v){return post('/api/set',{k,v}).then(r=>toast(r.ok?'Saved':'Could not save'))}
const K=['autoDim','nightFrom','nightTo','nightBr','fmt24','tz','blinkColon','secondsBar','leadingZero','showDate','roll','hourlyAnim','bootAnim'];
fetch('/api/config').then(r=>r.json()).then(c=>{
 $('br').value=c.br;$('brv').textContent=c.br;$('welcome').value=c.welcome;
 if(![...$('tz').options].some(o=>o.value==c.tz))$('tz').add(new Option(c.tz,c.tz));
 K.forEach(k=>{const e=$(k);if(e.type=='checkbox')e.checked=!!c[k];else e.value=c[k]});
 $('night').style.display=c.autoDim?'':'none';
});
K.forEach(k=>$(k).onchange=e=>{const t=e.target;set(k,t.type=='checkbox'?(t.checked?1:0):t.value);
 if(k=='autoDim')$('night').style.display=t.checked?'':'none'});
let bt;$('br').oninput=e=>{$('brv').textContent=e.target.value;clearTimeout(bt);bt=setTimeout(()=>post('/api/set',{k:'br',v:e.target.value,live:1}),60)};
$('br').onchange=e=>{clearTimeout(bt);set('br',e.target.value)};
$('wsave').onclick=()=>set('welcome',$('welcome').value);
$('wshow').onclick=()=>set('welcome',$('welcome').value).then(()=>post('/api/show',{t:'welcome'}));
$('sip').onclick=()=>post('/api/show',{t:'ip'}).then(()=>toast('Watch the clock'));
$('sync').onclick=()=>post('/api/sync').then(()=>toast('Re-syncing time'));
$('rst').onclick=()=>{if(confirm('Restart the clock?'))post('/api/restart').then(()=>toast('Restarting...'))};
$('fac').onclick=()=>{if(confirm('Erase ALL settings including WiFi? The clock goes back to setup mode.'))post('/api/factory').then(()=>toast('Resetting...'))};
const up=s=>{const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return(d?d+'d ':'')+h+'h '+m+'m'};
const q=r=>r>-55?'Excellent':r>-67?'Good':r>-75?'Fair':'Weak';
function poll(){fetch('/api/status').then(r=>r.json()).then(s=>{$('tm').textContent=s.time;$('ap').textContent=s.ampm;
 $('dt').textContent=s.synced?s.date:'Waiting for internet time...';$('ssid').textContent=s.ssid||'-';
 $('rssi').textContent=q(s.rssi)+' ('+s.rssi+' dBm)';$('ip').textContent=s.ip;$('up').textContent=up(s.up)})
 .catch(()=>$('dt').textContent='Clock not reachable').finally(()=>setTimeout(poll,1000))}
poll();
</script></body></html>)HTML";

static const char WIFI_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html lang=en><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Clock WiFi Setup</title>
<link rel=stylesheet href=/style.css></head><body><div class=w>
<div class=hero><div class=clk style=font-size:32px>WiFi Setup</div><div class=dt>Connect the clock to your home WiFi</div></div>
<div class=card><h2>1. Choose your network <button id=rs class=sm>Rescan</button></h2><div id=list class=mut>Scanning...</div></div>
<div class=card><h2>2. Enter the password</h2>
<input type=text id=ssid class=full placeholder="Network name" maxlength=32 autocapitalize=none autocorrect=off spellcheck=false>
<input type=password id=pass class=full placeholder="WiFi password" maxlength=64>
<label class=chk><input type=checkbox id=sp> Show password</label>
<div class=btns><button class=p id=go>Save &amp; connect</button><a class=btn id=back href=/ style=display:none>Back</a></div></div>
<div class=card id=done style=display:none><h2>3. Done</h2>
<p>The clock is restarting and joining <b id=dn></b>.</p>
<p>Once connected, its <b>IP address scrolls across the display</b>. Switch this phone back to your home WiFi and open that
address in the browser (or try <b>http://matrixclock.local</b>) to set brightness, animations and more.</p>
<p class=mut>If the display shows the setup message again, the password was wrong: join the clock's hotspot again and retry.</p></div>
</div><div id=toast></div>
<script>
const $=i=>document.getElementById(i);
function toast(t){const e=$('toast');e.textContent=t;e.style.opacity=1;clearTimeout(e.t);e.t=setTimeout(()=>e.style.opacity=0,2200)}
function scan(){const l=$('list');l.textContent='Scanning...';
 fetch('/api/scan').then(r=>r.json()).then(a=>{a.sort((x,y)=>y.r-x.r);l.innerHTML='';
  if(!a.length){l.textContent='No networks found. Type the name below.';return}
  a.forEach(n=>{const d=document.createElement('div');d.className='net';const s=n.r>-55?4:n.r>-67?3:n.r>-75?2:1;
   const t=document.createElement('span');t.textContent=(n.l?'🔒 ':'')+n.s;d.appendChild(t);
   d.insertAdjacentHTML('beforeend','<i class="sig s'+s+'"><b></b><b></b><b></b><b></b></i>');
   d.onclick=()=>{$('ssid').value=n.s;document.querySelectorAll('.net').forEach(x=>x.classList.remove('on'));
    d.classList.add('on');$('pass').value='';$('pass').focus()};
   l.appendChild(d)})}).catch(()=>l.textContent='Scan failed - tap Rescan')}
$('rs').onclick=scan;
$('sp').onchange=e=>$('pass').type=e.target.checked?'text':'password';
$('go').onclick=()=>{const s=$('ssid').value;if(!s){toast('Choose a network first');return}
 $('go').disabled=true;
 fetch('/api/wifi',{method:'POST',body:new URLSearchParams({ssid:s,pass:$('pass').value})})
 .then(r=>{if(!r.ok)return r.text().then(t=>{throw t});$('dn').textContent=s;$('done').style.display='';$('done').scrollIntoView()})
 .catch(e=>{$('go').disabled=false;toast(typeof e=='string'?e:'Could not save')})};
fetch('/api/status').then(r=>r.json()).then(s=>{if(s.ssid)$('back').style.display=''});
scan();
</script></body></html>)HTML";
