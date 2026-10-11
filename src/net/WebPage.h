#pragma once
#include <Arduino.h>

// The whole web UI: one self-contained page, no external assets, so it works
// on the controller's own access point with no internet. Six sections, shown
// one at a time by the address's #part: home, shots, coffees, recipes,
// grinder, setup. Own data is orange, anything shared is blue.
static const char WEB_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Brew by Weight</title>
<style>
:root{--bg:#0e0f11;--card:#17191c;--up:#1f2226;--line:#2a2e33;--edge:#3a3f46;--fg:#eceef0;--mid:#c3c9d0;--dim:#9aa3ad;--ok:#4cc38a;--okbg:#14301f;--warn:#f5c451;--warnbg:#33290f;--bad:#ff6b5e;--acc:#f08a24;--accbg:#3a2410;--com:#6cb4ff;--combg:#111a24;--comline:#23374d}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif;font-variant-numeric:tabular-nums}
a{color:var(--acc);text-decoration:none}a:hover{color:#ffb061}
.app{display:flex;flex-wrap:wrap;align-items:flex-start}
nav{flex:1 1 190px;padding:20px 14px;display:flex;flex-direction:column;gap:4px}
nav b{font-size:17px;padding:0 12px 14px}
nav a{display:block;padding:11px 12px;border-radius:8px;color:var(--dim)}
nav a.on{background:var(--up);color:var(--fg);font-weight:600}
#link{font-size:12px;color:var(--dim);padding:12px}
main{flex:999 1 560px;min-width:0;max-width:1080px;padding:20px 28px 60px 14px}
.page{display:none;flex-direction:column;gap:16px}.page.on{display:flex}
h1{margin:0;font-size:26px;font-weight:650;letter-spacing:-.02em}
h2{margin:0;font-size:12px;font-weight:600;letter-spacing:.1em;text-transform:uppercase;color:var(--dim)}
.head{display:flex;flex-wrap:wrap;gap:12px;align-items:center;justify-content:space-between}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:18px 22px;display:flex;flex-direction:column;gap:12px;min-width:0}
.card.com{background:var(--combg);border-color:var(--comline)}.card.com h2{color:#8cc4ff}
.cols{display:grid;grid-template-columns:repeat(auto-fit,minmax(290px,1fr));gap:16px}
.split{display:flex;flex-wrap:wrap;gap:16px;align-items:flex-start}.split>.side{flex:1 1 260px;min-width:0}.split>.body{flex:2 1 420px;min-width:0;display:flex;flex-direction:column;gap:16px}
.row{display:flex;flex-wrap:wrap;gap:8px;align-items:center}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:12px}
.big{font-size:48px;font-weight:600;letter-spacing:-.03em}.num{font-size:30px;font-weight:600;letter-spacing:-.02em}
.dim{color:var(--dim)}.mid{color:var(--mid)}.sm{font-size:13px}
.live{display:flex;flex-wrap:wrap;gap:30px;align-items:baseline}
.pill{display:inline-block;padding:5px 12px;border-radius:999px;background:var(--up);font-size:13px;font-weight:600;letter-spacing:.05em}
.pill.ok{background:var(--okbg);color:#7be0aa}.pill.warn{background:var(--warnbg);color:var(--warn)}.pill.bad{background:#3b1512;color:var(--bad)}
.chip{padding:6px 12px;border-radius:8px;background:var(--up);font-size:14px}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:var(--bad);margin-right:6px}.dot.on{background:var(--ok)}.dot.zz{background:#6b7280}.dot.am{background:var(--warn)}
label{display:flex;flex-direction:column;gap:5px;font-size:12px;color:var(--dim)}
label.chk{flex-direction:row;align-items:center;gap:10px;font-size:15px;color:var(--fg);min-height:44px}
input,select,textarea{min-height:44px;padding:0 12px;border-radius:10px;border:1px solid var(--edge);background:var(--bg);color:var(--fg);font:inherit;font-size:15px;width:100%}
input[type=checkbox]{width:20px;height:20px;min-height:0;accent-color:var(--acc);flex:none}
input:disabled,select:disabled{color:var(--dim);background:var(--card);border-color:var(--line)}
button{min-height:44px;padding:0 16px;border-radius:10px;border:1px solid var(--edge);background:var(--up);color:var(--fg);font:inherit;cursor:pointer}
button.pri{background:var(--acc);border-color:var(--acc);color:#1d0f00;font-weight:600}
button.go{background:var(--ok);border-color:var(--ok);color:#06210f;font-weight:600}
button.stop{background:#c6392c;border-color:#c6392c;color:#fff;font-weight:600}
button.del{background:none;border-color:#5c2a25;color:#ff8a80}
button.on{border-color:var(--acc);background:var(--accbg);color:#ffd0a3}
.list{display:flex;flex-direction:column;gap:6px}
.item{display:block;width:100%;text-align:left;min-height:56px;padding:8px 12px;background:none;border:1px solid transparent}
.item.on{background:var(--up);border-color:var(--edge)}.item b{display:block}.item span{display:block;font-size:13px;color:var(--dim)}
.pair{display:flex;align-items:center;gap:12px;min-height:52px;padding:4px 14px;border-radius:10px;background:var(--up);border:1px solid var(--up)}
.pair.on{background:#2a1c0e;border-color:#6b4118}.pair .what{flex:1 1 auto;min-width:0}.pair input[type=number]{width:96px}
.scroll{overflow-x:auto}table{border-collapse:collapse;width:100%;font-size:14px}
th,td{padding:9px 10px;text-align:right;border-bottom:1px solid #22262b;white-space:nowrap}
th{font-size:12px;color:var(--dim);font-weight:500;border-color:var(--line)}
th.l,td.l{text-align:left}tr.on td{background:var(--up)}tr.pick{cursor:pointer}td.off{color:var(--warn);font-weight:600}
td a{color:inherit;text-decoration:underline dotted}
.tiles{display:grid;grid-template-columns:repeat(auto-fit,minmax(125px,1fr));gap:10px}
.tile{background:var(--up);border-radius:10px;padding:10px 14px}.tile b{display:block;font-size:20px;white-space:nowrap}.tile span{font-size:12px;color:var(--dim)}
canvas{width:100%;height:200px;display:block}
fieldset{border:0;padding:0;margin:0;display:flex;flex-direction:column;gap:10px}legend{padding:0;margin-bottom:8px;font-size:12px;font-weight:600;letter-spacing:.1em;text-transform:uppercase;color:var(--dim)}
details summary{cursor:pointer;min-height:44px;display:flex;align-items:center;color:var(--dim)}
#alert{display:none;background:#c6392c;color:#fff;padding:12px 16px;border-radius:10px;font-weight:600}
#toast{position:fixed;left:50%;bottom:20px;transform:translateX(-50%);background:var(--fg);color:#111;padding:10px 18px;border-radius:10px;font-weight:600;display:none;max-width:90vw}
</style></head><body><div class="app">
<nav aria-label="Sections"><b>Brew by Weight</b>
<a href="#home">Home</a><a href="#shots">Shots</a><a href="#coffees">Coffees</a><a href="#recipes">Recipes</a><a href="#grinder">Grinder</a><a href="#setup">Setup</a>
<div id="link"></div></nav>
<main>
<div id="alert" role="alert"></div>

<div class="page" id="p-home">
<div class="head"><h1>Home</h1><span class="pill" id="state">--</span></div>
<section class="card"><div class="head">
<div class="live"><div><span class="big" id="w">--</span> <span class="dim">g</span></div><div><span class="big" id="t">--</span> <span class="dim">s</span></div><div><span class="big" id="f">--</span> <span class="dim">g/s</span></div></div>
<div class="row"><button class="go" id="bstart" onclick="cmd('start')">Start</button><button class="stop" onclick="cmd('stop')">Stop / Cancel</button><button onclick="cmd('tare')">Tare</button><button onclick="cmd('ack')">Ready</button></div></div>
<div class="row sm dim"><span><i class="dot" id="ds"></i>Scale <span id="sbat"></span></span><span><i class="dot" id="db"></i>SwitchBot <span id="bat"></span></span><span id="cut"></span><span id="res"></span></div></section>
<div class="cols">
<section class="card"><div class="head"><h2>Brewing now</h2><a class="sm" href="#coffees">Coffees</a></div>
<label>What the controller will brew<select id="psel" onchange="act('preset_select',+this.value)"></select></label>
<div class="row" id="pchips"></div><div class="sm dim" id="pinfo"></div></section>
<section class="card"><div class="head"><h2>Grinder</h2><a class="sm" href="#grinder">Details</a></div>
<div class="row"><span class="num" id="hg">--</span><span class="dim un">&micro;m</span><span class="pill" id="hgadv"></span></div>
<div class="sm mid" id="hgwhy"></div></section>
</div>
<section class="card"><div class="head"><h2>Most recent shot</h2><a class="sm" href="#shots">All shots</a></div>
<div id="hshot" class="dim">No shot yet.</div><canvas id="hchart" width="900" height="200"></canvas>
<div class="row sm dim" id="hrate"></div></section>
<section class="card com"><div class="head"><h2>Community</h2><a class="sm" href="#coffees" style="color:var(--com)">Coffees</a></div><div id="hcom" class="sm mid"></div></section>
</div>

<div class="page" id="p-shots">
<div class="head"><h1>Shots</h1><div class="row"><a href="/shots.csv"><button>Download CSV</button></a><a href="/api/shots?limit=500"><button>JSON</button></a></div></div>
<section class="card"><div class="head"><div><h2 id="swhen">No shot selected</h2><div style="font-size:19px;font-weight:650" id="sname"></div></div>
<div class="row"><span class="sm dim">For grind advice</span><span class="row" id="svote"></span></div></div>
<div class="tiles" id="stiles"></div><canvas id="schart" width="900" height="200"></canvas>
<div class="row sm dim"><span id="sdata"></span></div><div class="row sm dim" id="srate"></div></section>
<section class="card"><div class="head"><h2>History</h2><div class="row">
<label>Coffee<select id="fcoffee" onchange="drawShots()"></select></label>
<label>Show<select id="fshow" onchange="drawShots()"><option value="0">All shots</option><option value="1">Stopped by weight</option></select></label></div></div>
<div class="scroll"><table id="hist"></table></div>
<div class="head sm dim"><span>Brew times outside the tolerance are marked. Click a row to see the shot; grind and advice can be changed in the row. Every stored value is in the CSV.</span>
<button class="del" onclick="if(confirm('Delete all shot history?'))post('/api/history/clear').then(loadShots)">Clear history</button></div></section>
</div>

<div class="page" id="p-coffees">
<div class="head"><h1>Coffees</h1><button class="pri" onclick="C=0;drawCoffees()">Add a coffee</button></div>
<div class="split"><section class="card side">
<label>Search by name, roaster, origin or process<input type="search" id="csearch" oninput="drawCoffeeList()" placeholder="e.g. Guji, washed"></label>
<div class="list" id="clist"></div><div class="sm dim" id="ccount"></div><div class="list" id="cshared"></div></section>
<div class="body">
<section class="card"><h2 id="ctitle">Coffee</h2><div class="grid">
<label>Name<input id="c_name" maxlength="23" placeholder="e.g. Idjwi Island"></label>
<label>Roaster<input id="c_roaster" maxlength="23" placeholder="e.g. Portrait"></label>
<label>Origin (country)<input id="c_origin" maxlength="23"></label>
<label>Region<input id="c_region" maxlength="31"></label>
<label>Producer or washing station<input id="c_producer" maxlength="31"></label>
<label>Varietal<input id="c_varietal" maxlength="31"></label>
<label>Process<input id="c_process" maxlength="15" placeholder="washed, natural..."></label>
<label>Roast level<select id="c_roastLevel"><option value="0">not set</option><option value="1">light</option><option value="2">medium-light</option><option value="3">medium</option><option value="4">medium-dark</option><option value="5">dark</option></select></label>
<label>Altitude (m)<input id="c_altitudeM" type="number" min="0" max="4000" step="10"></label>
<label>Roast date of the bag in use<input id="c_roastDate" type="date"></label>
</div><label>Tasting notes<input id="c_notes" maxlength="47"></label>
<div class="row"><button class="pri" onclick="saveCoffee()">Save</button><button onclick="newBag()" id="cbag">New bag, roasted today</button><button class="del" id="cdel" onclick="delCoffee()">Delete</button><span class="sm dim" id="cinfo"></span></div>
<div class="sm dim">A new bag keeps the coffee, its recipes and its grind setting, and starts a fresh run of shots for grind advice.</div>
<div class="row" id="cshrow" style="border-top:1px solid var(--line);padding-top:12px"><span class="pill" id="cshpill" style="background:var(--combg);color:#8cc4ff;border:1px solid var(--comline)"></span><span class="sm mid" id="cshare" style="flex:1 1 260px"></span><button id="csend" onclick="sendCoffee()">Send to the shared library</button></div></section>
<section class="card" id="cpairs"><div class="head"><h2>Brew it with</h2><a class="sm" href="#recipes">Recipes</a></div>
<div class="list" id="cplist"></div><div class="sm dim" id="cpinfo"></div></section>
<section class="card com"><h2>Community dial-in</h2><div id="ccom" class="sm mid"></div></section>
</div></div></div>

<div class="page" id="p-recipes">
<div class="head"><h1>Recipes</h1><button class="pri" onclick="R=0;drawRecipes()">New recipe</button></div>
<div class="split"><section class="card side"><div class="list" id="rlist"></div>
<div class="sm dim">A recipe says how to pull the shot. It holds nothing about the coffee or the grind, so one recipe serves every coffee.</div></section>
<div class="body"><section class="card"><div class="head"><label style="flex:1 1 220px">Name<input id="r_name" maxlength="23" style="font-size:17px;font-weight:600"></label>
<div class="row" style="align-self:flex-end"><button onclick="dupRecipe()" id="rdup">Duplicate</button><button class="del" id="rdel" onclick="delRecipe()">Delete</button></div></div>
<fieldset><legend>Dose and yield</legend><div class="grid">
<label>Dose in (g)<input id="r_doseG" type="number" step="0.1" min="5" max="30"></label>
<label>Yield set by<select id="r_useRatio" onchange="recipeForm()"><option value="0">Target weight</option><option value="1">Ratio</option></select></label>
<label>Target out (g)<input id="r_targetWeightG" type="number" step="0.1" min="5" max="200"></label>
<label>Ratio 1:<input id="r_ratio" type="number" step="0.1" min="1" max="5"></label>
<label>Target brew time (s)<input id="r_targetTimeS" type="number" step="0.5" min="5" max="60"></label></div>
<div class="sm dim">The target brew time is what grind advice steers toward.</div></fieldset>
<fieldset><legend>Pre-infusion and bloom</legend><div class="grid">
<label>How the button is pressed<select id="r_preinfusion" onchange="recipeForm()"><option value="0">Tap: the machine's own pre-infusion</option><option value="1">Hold the button for a set time</option></select></label>
<label>Hold time (s)<input id="r_preinfusionHoldS" type="number" step="0.1" min="0.3" max="30"></label>
<label>Bloom pause (s, 0 = none)<input id="r_bloomPauseS" type="number" step="1" min="0" max="45" oninput="recipeForm()"></label></div>
<div class="sm dim" id="rbloom"></div></fieldset>
<fieldset><legend>If the target is never reached</legend><div class="grid">
<label>Who ends the shot<select id="r_timeLimit" onchange="recipeForm()"><option value="0">The machine's own time-out</option><option value="1">The Bot, after a set time</option></select></label>
<label>Stop after (s)<input id="r_maxShotTimeS" type="number" min="5" max="90"></label>
<label>Starting stop offset (g)<input id="r_baselineOffsetG" type="number" step="0.1" min="0" max="5"></label></div></fieldset>
<div class="row"><button class="pri" onclick="saveRecipe()">Save</button><span class="sm dim" id="rinfo"></span></div></section>
<section class="card" id="rused"><div class="head"><h2>Used with</h2><a class="sm" href="#coffees">Add to a coffee</a></div>
<div class="scroll"><table id="rtable"></table></div>
<div class="row"><button id="ralone" onclick="act('preset_add',R,0)">List it on the controller by itself</button></div>
<div class="sm dim">The controller lists coffee and recipe as one line to tap. A recipe listed by itself is for guests and one-off bags.</div></section>
</div></div></div>

<div class="page" id="p-grinder">
<div class="head"><h1>Grinder</h1><span class="sm dim" id="gfor"></span></div>
<div class="cols"><section class="card"><h2>Setting now</h2>
<div><span class="big" id="gcur">--</span> <span class="dim un">&micro;m</span></div>
<div class="row"><label style="flex:1 1 150px"><span>I moved the grinder to (<span class="un">&micro;m</span>)</span><input id="gset" type="number" min="1" max="2000" step="1"></label><button style="align-self:flex-end" onclick="setGrind(+$('gset').value)">Set</button></div>
<div class="sm dim">Shots are only compared with others made at the same setting.</div></section>
<section class="card"><h2>Advice</h2><div><span class="pill" id="gadv" style="font-size:16px"></span></div>
<div class="sm mid" id="gwhy"></div><div class="row"><button class="pri" id="gapply" style="display:none" onclick="setGrind(G.suggested_um)"></button></div></section></div>
<section class="card"><div class="head"><h2>Brew time against grind setting</h2><span class="sm dim" id="gsens"></span></div>
<canvas id="gchart" width="900" height="220" style="height:220px"></canvas><div class="sm mid" id="gnote"></div></section>
<section class="card"><h2>How advice is worked out</h2><div class="grid">
<label>Brew time tolerance (&plusmn; s)<input id="grindToleranceS" type="number" step="0.5" min="0.5" max="10"></label>
<label>Shots averaged<input id="grindWindow" type="number" min="1" max="10"></label>
<label><span>Starting guess (<span class="un">&micro;m</span> per s)</span><input id="grindUmPerS" type="number" step="0.5" min="0.5" max="20"></label>
<label><span>Largest single change (<span class="un">&micro;m</span>)</span><input id="grindMaxStepUm" type="number" min="1" max="100"></label>
<label>Brew time measured<select id="grindTimeBasis"><option value="0">Total: start press to stop (with pre-infusion)</option><option value="1">Pour: first drops to stop</option></select></label></div>
<div class="row"><button class="pri" onclick="saveSettings()">Save</button><span class="sm dim">Changing how brew time is measured starts a fresh comparison: set each recipe's target time to match.</span></div></section>
<section class="card"><h2>My grinder</h2><div class="grid">
<label>Model<select id="gm_model" onchange="grinderForm()"></select></label>
<label>Name, if it is not in the list<input id="gm_name" maxlength="31"></label>
<label>Burrs<input id="gm_burrs" maxlength="23" placeholder="e.g. 64 mm flat"></label>
<label>A setting is read in<input id="gm_unit" maxlength="7" placeholder="um, steps, dial"></label>
<label>Microns of burr gap per unit (blank: not known)<input id="gm_umPerUnit" type="number" min="0" step="0.5"></label>
<label>Last zeroed, or burrs changed<input id="gm_zeroDate" type="date"></label></div>
<div class="row"><button class="pri" onclick="saveGrinder(false)">Save</button><button onclick="saveGrinder(true)">I zeroed it again today</button><button id="gm_addother" onclick="saveGrinder(false,true)">Add what is typed above as my second grinder</button></div>
<div class="sm dim" id="gm_info"></div>
<div class="row" id="gm_shrow" style="border-top:1px solid var(--line);padding-top:12px"><span class="pill" id="gm_shpill" style="background:var(--combg);color:#8cc4ff;border:1px solid var(--comline)"></span><span class="sm mid" id="gm_share" style="flex:1 1 260px"></span><button id="gm_send" onclick="sendGrinder(L.grinder.id)">Send to the shared library</button></div>
<div class="row" id="gm_other" style="border-top:1px solid var(--line);padding-top:12px"></div>
<div class="sm dim">Zeroing the grinder again, other burrs or another model change what a setting means, so shots from before are no longer compared with new ones. Naming the grinder for the first time changes nothing. A second grinder keeps its own setting for every coffee, and its own shots; switching between the two loses neither.</div></section>
<section class="card com"><h2>Shared grinder list</h2><div class="sm mid">The model list above is the firmware's own, plus the models the shared library has published. A grinder that is on neither can be typed in and kept here, or sent to the library for a reviewer to add, so that the next owner of one picks it from the list and settings can be compared among everyone who has it. Settings are translated between grinders through what each is read in and the coffees their owners have in common.</div></section>
</div>

<div class="page" id="p-setup">
<div class="head"><h1>Setup</h1></div>
<section class="card"><h2>Devices</h2><div id="dev" class="mid"></div><div id="scaledata" class="sm dim"></div><div class="scroll"><table id="seen"></table></div>
<div class="row"><button onclick="pair('scale','',0)">Forget scale</button><button onclick="pair('bot','',0)">Forget brew bot</button><button onclick="pair('power','',0)">Forget power bot</button><button onclick="loadDevices()">Refresh</button></div><div class="sm dim">A button pusher in the list can be the <b>brew bot</b> (on the brew button, stops the shot) or the <b>power bot</b> (on the power button). SwitchBot Bot, Tuya Fingerbot and MicroBot Push are supported; the last two need a key, below.</div></section>
<section class="card"><h2>Machine power</h2><div id="pw" class="mid"></div><div class="row"><button onclick="power('on')">Switch on</button><button onclick="power('off')">Switch off</button><button onclick="power('press')">Press the power button anyway</button></div><div class="row sm">It is really: <button onclick="power('believe_on')">on</button><button onclick="power('believe_off')">off</button></div><div class="sm dim">The power button is a toggle and the machine does not say whether it is on, so the controller keeps count. If someone used the button by hand, or the machine switched itself off, put it right here: nothing is pressed.</div><h2 style="margin-top:10px">Schedule</h2><div class="scroll"><table id="pws"></table></div><div class="row"><button onclick="savePowerSchedule()">Save schedule</button></div><div class="sm dim">Times are this browser's local time; saving stores its time zone, so save again after a daylight-saving change. A time is skipped if the machine is already believed to be that way, or while a shot is running.</div></section>
<section class="card"><h2>Pusher keys</h2><div class="grid"><label>For<select id="pk_role" onchange="loadPusher()"><option value="brew">Brew bot</option><option value="power">Power bot</option></select></label><label>Tuya uuid<input id="pk_uuid" maxlength="16"></label><label>Tuya device id<input id="pk_dev" maxlength="22"></label><label>Tuya local key<input id="pk_key" type="password" maxlength="16"></label><label>Tuya model<select id="pk_var"><option value="0">Fingerbot / Fingerbot Plus</option><option value="1">CubeTouch</option></select></label><label>MicroBot token (32 hex digits)<input id="pk_tok" type="password" maxlength="32"></label><label id="pk_pwl">SwitchBot password (power bot)<input id="pk_pw" type="password" maxlength="23"></label></div><div id="pk_state" class="sm dim"></div><div class="row"><button onclick="savePusher()">Save keys</button></div><div class="sm dim">A SwitchBot Bot needs nothing here. A Tuya Fingerbot needs its uuid, device id and local key from Tuya's cloud. A MicroBot Push needs the token from its first pairing (Home Assistant shows it). Keys are stored on the controller and never shown again.</div></section>
<section class="card"><h2>Brew</h2><fieldset><legend>Cup</legend><div class="grid">
<label>Cup workflow<select id="cupMode"><option value="0">Cup on scale before Start</option><option value="1">Tare, then place cup</option></select></label>
<label>Cup on the scale<select id="requireCup"><option value="1">Required before Start</option><option value="0">Not checked</option></select></label>
<label>After tare<select id="autoStartAfterTare"><option value="1">Start automatically</option><option value="0">Wait for a second tap</option></select></label>
<label>Minimum cup weight (g)<input id="cupMinWeightG" type="number" step="0.5"></label></div></fieldset>
<fieldset><legend>Stopping on weight</legend><div class="grid">
<label>Stop offset learning<select id="learningEnabled"><option value="1">On</option><option value="0">Off</option></select></label>
<label>Learning factor (0.01 to 1.00)<input id="learningFactor" type="number" step="0.01" min="0.01" max="1"></label>
<label>Learning factor mode<select id="adaptiveLearning"><option value="0">Fixed: always the value entered</option><option value="1">Adaptive: tuned per controller entry</option></select></label>
<label>Drive the scale's timer<select id="scaleTimer"><option value="1">Yes</option><option value="0">No</option></select></label></div></fieldset>
<details><summary>Limits, timeouts and SwitchBot press timing (rarely changed)</summary><div class="grid">
<label>Minimum valid weight (g)<input id="minValidWeightG" type="number" step="0.5"></label>
<label>Scale timeout (ms)<input id="scaleTimeoutMs" type="number" step="100"></label>
<label>Post-shot drip observation (ms)<input id="dripObserveMs" type="number" step="100"></label>
<label>No weight stop before (ms)<input id="minShotTimeMs" type="number" step="100"></label>
<label>No-flow timeout (s)<input id="noFlowTimeoutS" type="number"></label>
<label>Press style<select id="actuationMode"><option value="0">Bot press (push and return)</option><option value="1">Controller-timed down / up</option></select></label>
<label>Press duration (ms, down / up style)<input id="pressDurationMs" type="number" step="50"></label>
<label>Release settle time (ms)<input id="releaseSettleMs" type="number" step="50"></label>
<label>Bot hold time (s, Bot press style)<input id="botHoldSeconds" type="number" min="0" max="5"></label></div></details>
<div class="row"><button class="pri" onclick="saveSettings()">Save</button></div></section>
<section class="card"><h2>Network</h2><div class="grid">
<label>Join Wi-Fi network (empty: access point only)<input id="staSsid"></label>
<label>Wi-Fi password (blank keeps current)<input id="staPassword" type="password"></label>
<label>Access point password (blank keeps current)<input id="apPassword" type="password" minlength="8"></label>
<label>SwitchBot password (only if set in its app)<input id="botPassword" type="password"></label></div>
<label class="chk"><input type="checkbox" id="webStartEnabled">Allow Start from this page</label>
<div class="row"><button class="pri" onclick="saveNet()">Save and restart</button></div></section>
<section class="card com"><div class="head"><h2>Shared library</h2><span class="pill" id="sh_state"></span></div>
<div class="sm mid" id="sh_key"></div>
<label class="chk"><input type="checkbox" id="shareEnabled" style="accent-color:var(--com)">Share from this controller</label>
<fieldset><legend>What it shares</legend>
<label class="chk"><input type="checkbox" id="shareCoffees" style="accent-color:var(--com)">Coffees I add or change (name, roaster, origin, process, roast level, notes)</label>
<label class="chk"><input type="checkbox" id="shareDialIns" style="accent-color:var(--com)">Settled dial-ins: grind, dose, yield, brew time, grinder, roast date</label>
<label class="chk"><input type="checkbox" id="shareShots" style="accent-color:var(--com)">Every shot that counts for grind advice, with its flow</label>
<label class="chk"><input type="checkbox" id="shareRatings" style="accent-color:var(--com)">My taste ratings, with those shots</label></fieldset>
<div class="row"><button class="pri" style="background:var(--com);border-color:var(--com);color:#06203a" onclick="saveSettings()">Save</button><button onclick="post('/api/share/coffees').then(r=>{done(r,r.queued+' coffees queued');loadShare()})">Queue all my coffees now</button></div>
<div class="sm mid" id="sh_queue"></div>
<div class="sm mid">Every record is signed with this controller's own key and carries the key's short id, never a name, an address or anything about the network. Coffees and grinders wait for a reviewer before they are published; measurements do not.</div></section>
</div>
</main></div><div id="toast" role="status"></div>
<script>
const $=id=>document.getElementById(id);
const esc=s=>String(s==null?'':s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const get=u=>fetch(u).then(r=>r.json());
const post=(u,b)=>fetch(u,{method:'POST',headers:{'Content-Type':'application/json'},body:b?JSON.stringify(b):''}).then(r=>r.json()).catch(()=>({ok:false,error:'controller not reachable'}));
let toastT=0;function say(t){if(!t)return;const e=$('toast');e.textContent=t;e.style.display='block';clearTimeout(toastT);toastT=setTimeout(()=>e.style.display='none',3500)}
const done=(r,good)=>{say(r.ok?good:(r.error||'Not saved'));return r.ok};
const d2i=v=>v?+v.replace(/-/g,''):0,i2d=n=>n?String(n).padStart(8,'0').replace(/(\d{4})(\d\d)(\d\d)/,'$1-$2-$3'):'';
const age=n=>n?Math.floor((Date.now()-new Date(i2d(n)).getTime())/864e5):null;
const f1=v=>(+v).toFixed(1),f2=v=>(+v).toFixed(2);
let U='µm',S=null,L=null,G=null,SH=[],ST=null,C=-1,R=-1,SEL=0,lastShotId=-1;
const F=["learningEnabled","learningFactor","adaptiveLearning","minValidWeightG","scaleTimeoutMs","dripObserveMs","minShotTimeMs","noFlowTimeoutS","cupMode","cupMinWeightG","autoStartAfterTare","scaleTimer","requireCup","actuationMode","pressDurationMs","releaseSettleMs","botHoldSeconds","grindToleranceS","grindWindow","grindUmPerS","grindMaxStepUm","grindTimeBasis"];
const SB=['shareEnabled','shareCoffees','shareDialIns','shareShots','shareRatings'];
async function loadShare(){let s;try{s=await get('/api/share')}catch(e){return}
$('sh_state').textContent=!S.shareEnabled?'off':s.can_send?'sending':'queueing';$('sh_state').className='pill '+(S.shareEnabled?'ok':'');
$('sh_key').textContent=s.key_ready?'This controller\'s key id: '+s.key_id+'. '+(s.real_signatures?'Signatures: ES256.':'This is the PC emulator: it has no real key, and marks what it writes as '+s.algorithm+'. The library will not accept these records; they are for trying the flow.'):'No device key: nothing can be shared.';
$('sh_queue').textContent=s.waiting+' waiting to send (room for '+s.capacity+'), '+s.sent+' sent. Records go to '+s.sends_to+'.'}
const CF=['name','roaster','origin','region','producer','varietal','process','roastLevel','altitudeM','roastDate','notes'];
const RF=['name','doseG','useRatio','targetWeightG','ratio','targetTimeS','preinfusion','preinfusionHoldS','bloomPauseS','timeLimit','maxShotTimeS','baselineOffsetG'];
const coffee=id=>L.coffees.find(c=>c.id==id),recipe=id=>L.recipes.find(r=>r.id==id);
const plabel=p=>{const c=coffee(p.coffeeId),r=recipe(p.recipeId);return (c?c.name+' — ':'')+(r?r.name:'?')};

function show(){const h=(location.hash||'#home').slice(1);document.querySelectorAll('.page').forEach(p=>p.classList.toggle('on',p.id=='p-'+h));
document.querySelectorAll('nav a').forEach(a=>a.classList.toggle('on',a.getAttribute('href')=='#'+h));if(h=='setup')loadDevices();if(h=='coffees')loadCommunityCoffees();if(h=='grinder'){drawScatter();loadCommunityGrinders()}}
window.addEventListener('hashchange',show);

function cmd(c){post('/api/cmd?c='+c).then(r=>{if(!r.ok)say(r.error||'refused')})}
async function tick(){let s;try{s=await get('/api/status')}catch(e){$('state').textContent='offline';$('state').className='pill bad';$('link').textContent='Controller not reachable';return}
ST=s;$('link').textContent='Connected. Changes are saved on the controller.';
$('w').textContent=f1(s.weight_g);$('t').textContent=f1(s.shot_time_s);$('f').textContent=f2(s.flow_g_s);
const fault=/FAILED|DISCONNECTED|STALE|TIMEOUT|INVALID|ERROR/.test(s.state);
$('state').textContent=s.state=='BLOOMING'?'BLOOM '+Math.ceil(s.bloom_left_s||0)+' s':s.state.replace(/_/g,' ');
$('state').className='pill '+(fault?'bad':s.state=='READY'?'ok':'warn');
const nocup=s.state=='READY'&&s.cup_required&&!s.cup_present;
$('ds').className='dot'+(s.scale_connected?(nocup?' am':' on'):'');
$('sbat').textContent=!s.scale_connected?'not connected':(nocup?'no cup':'connected')+(s.scale_battery!=null?', '+s.scale_battery+'%':'');
$('db').className='dot'+(s.bot_connected?' on':(s.bot_paired?' zz':''));
$('bat').textContent=(s.bot_connected?'connected':(s.bot_paired?'asleep until Start':'not paired'))+(s.bot_battery!=null?', '+s.bot_battery+'%':'');
$('cut').textContent='Target '+f1(s.target_weight_g)+' g, stops at '+f1(s.target_weight_g-s.stop_offset_g)+' g';
$('bstart').style.display=s.web_start?'':'none';
const a=$('alert');if(s.manual_stop_needed){a.style.display='block';a.textContent='STOP THE MACHINE BY HAND. The stop press was not delivered or confirmed.'}
else if(s.bot_switch_mode){a.style.display='block';a.textContent='The SwitchBot is in Switch mode. Set it to Press mode in the SwitchBot app.'}else a.style.display='none';
if(lastShotId!==s.last_shot_id){lastShotId=s.last_shot_id;loadShots();loadLibrary();if(S)loadShare()}}

async function loadSettings(){S=await get('/api/settings');F.forEach(k=>{const e=$(k),v=S[k];e.value=typeof v==='boolean'?(v?1:0):v});
SB.forEach(k=>$(k).checked=!!S[k]);loadShare();$('staSsid').value=S.net.staSsid;$('webStartEnabled').checked=S.net.webStartEnabled}
function saveSettings(){const b={};F.forEach(k=>{const e=$(k);b[k]=typeof S[k]==='boolean'?e.value==='1':+e.value});SB.forEach(k=>b[k]=$(k).checked);
post('/api/settings',b).then(r=>{done(r,'Saved');loadSettings();loadGrind()})}
function saveNet(){post('/api/net',{staSsid:$('staSsid').value,staPassword:$('staPassword').value,apPassword:$('apPassword').value,botPassword:$('botPassword').value,webStartEnabled:$('webStartEnabled').checked}).then(r=>done(r,'Saved, restarting'))}

async function loadLibrary(){L=await get('/api/library');if(C<0||(C&&!coffee(C)))C=(L.presets[L.activePreset]||{}).coffeeId||(L.coffees[0]||{}).id||0;
if(R<0||(R&&!recipe(R)))R=(L.presets[L.activePreset]||{}).recipeId||L.recipes[0].id;U=L.grinder.unit=='um'?'µm':L.grinder.unit;document.querySelectorAll('.un').forEach(e=>e.textContent=U);drawHomePreset();drawCoffees();drawRecipes();drawFilter();drawGrinder();drawShots();loadGrind()}
function act(what,a,b){return post('/api/library/do?what='+what+'&a='+(a||0)+'&b='+(b||0)).then(r=>{done(r,'');return loadLibrary()})}

function drawHomePreset(){const ps=L.presets;drawCommunity('hcom',(ps[L.activePreset]||{}).coffeeId||0);$('psel').innerHTML=ps.map((p,i)=>`<option value="${i}" ${i==L.activePreset?'selected':''}>${esc(plabel(p))}</option>`).join('');
const p=ps[L.activePreset],r=recipe(p.recipeId),c=coffee(p.coffeeId);if(!r)return;
const pre=r.preinfusion==1?'hold '+r.preinfusionHoldS+' s'+(r.bloomPauseS?', bloom '+r.bloomPauseS+' s':''):'machine pre-infusion';
$('pchips').innerHTML=[esc(r.name),r.doseG+' g in, '+r.targetWeightG+' g out',r.targetTimeS+' s',pre,p.grindUm?p.grindUm+' '+U+'':'grind not set'].map(x=>`<span class="chip">${x}</span>`).join('');
const d=c?age(c.roastDate):null;$('pinfo').textContent=(c?[c.roaster,c.origin,c.region,c.process].filter(x=>x).join(', ')+(d!=null?(c.roaster||c.origin||c.region||c.process?'. ':'')+'Roasted '+d+' days ago':'')+'. ':'No coffee attached. ')+ps.length+' of '+L.maxPresets+' entries on the controller.'}

function drawCoffeeList(){const q=$('csearch').value.toLowerCase();const m=L.coffees.filter(c=>!q||[c.name,c.roaster,c.origin,c.region,c.process,c.varietal].join(' ').toLowerCase().includes(q));
$('clist').innerHTML=m.map(c=>{const n=L.presets.filter(p=>p.coffeeId==c.id).length,d=age(c.roastDate);return `<button class="item ${c.id==C?'on':''}" onclick="C=${c.id};drawCoffees()"><b>${esc(c.name)}</b><span>${esc([c.roaster,n?n+(n==1?' recipe':' recipes'):'not on the controller',d!=null?d+' days off roast':'',c.share&&c.share!='local'?SHN[c.share]+(c.shareChanged?', changed since':''):''].filter(x=>x).join(' · '))}</span></button>`}).join('')||'<div class="dim sm">No coffee matches.</div>';
$('ccount').textContent=L.coffees.length+' of '+L.maxCoffees+' coffees on the shelf.';
const have=new Set(L.coffees.map(c=>c.libraryId).filter(x=>x)),sm=CC.filter(c=>!have.has(c.id)&&(!q||[c.name,c.roaster,c.origin,c.region,c.process,c.varietal].join(' ').toLowerCase().includes(q))).slice(0,8);
$('cshared').innerHTML=sm.length?'<h2 style="color:#8cc4ff;margin-top:6px">In the shared library</h2>'+sm.map(c=>`<div class="pair" style="background:var(--combg);border-color:var(--comline)"><span class="what"><b>${esc(c.name)}</b> <span class="dim">${esc([c.roaster,c.origin,c.process].filter(x=>x).join(', '))}</span></span><button onclick="adopt('${esc(c.id)}')">Add</button></div>`).join(''):''}
function drawCoffees(){drawCoffeeList();const c=coffee(C)||{roastLevel:0};$('ctitle').textContent=C?'Coffee':'New coffee';
CF.forEach(k=>{$('c_'+k).value=k=='roastDate'?i2d(c[k]):(c[k]==null?'':c[k])});if(!c.altitudeM)$('c_altitudeM').value='';
$('cdel').style.display=$('cbag').style.display=C?'':'none';drawCoffeeShare(c);if(C)drawCommunity('ccom',C);else $('ccom').textContent='';$('cpairs').style.display=C?'':'none';$('cinfo').textContent='';
if(!C)return;
$('cplist').innerHTML=L.recipes.map(r=>{const i=L.presets.findIndex(p=>p.coffeeId==C&&p.recipeId==r.id),on=i>=0,p=on?L.presets[i]:null;
return `<div class="pair ${on?'on':''}"><input type="checkbox" aria-label="Brew ${esc(c.name)} with ${esc(r.name)}" ${on?'checked':''} onchange="${on?`act('preset_delete',${i})`:`act('preset_add',${r.id},${C})`}"><span class="what"><b>${esc(r.name)}</b> <span class="dim">${r.doseG} g in, ${r.targetWeightG} g out, ${r.targetTimeS} s${r.bloomPauseS?', bloom '+r.bloomPauseS+' s':''}</span></span>`+(on?`<input type="number" aria-label="Grind setting in microns" min="0" max="2000" value="${p.grindUm||''}" placeholder="${U}" onchange="act('preset_grind',${i},+this.value||0)"><span class="dim sm">${U}</span>`:'')+'</div>'}).join('');
$('cpinfo').textContent='Each ticked line is one entry on the controller ('+L.presets.length+' of '+L.maxPresets+' used). The grind setting and the learned stop offset belong to the pair.'}
const SHN={local:'on this controller only',queued:'waiting to send',sent:'sent',review:'in review',published:'published',declined:'declined'};
function drawCoffeeShare(c){$('cshrow').style.display=C?'':'none';if(!C)return;const st=c.share||'local',on=S&&S.shareEnabled;
$('cshpill').textContent=SHN[st];
$('cshare').textContent=({local:on?'Not sent to the shared library yet.':'Sharing is switched off (Setup), so this coffee stays here.',queued:'Signed and waiting in the outbox.',sent:'Sent. The library has not answered yet.',review:'The library has it. A reviewer has not decided yet.',published:'Approved by a reviewer'+(c.libraryId?': in the library as '+c.libraryId:'')+'.',declined:'A reviewer did not accept it'+(c.reviewNote?': "'+c.reviewNote+'"':'')+'. What you typed is unchanged.'})[st]+(st!='declined'&&c.reviewNote?' Reviewer: "'+c.reviewNote+'"':'')+(c.shareChanged?' You have changed it since that version was sent.':'');
const can=on&&(st=='local'||c.shareChanged);$('csend').style.display=can?'':'none';$('csend').textContent=st=='local'?'Send to the shared library':'Send the new version'}
function sendCoffee(){post('/api/share/coffee?id='+C).then(r=>{done(r,'Queued for the shared library');loadLibrary();loadShare()})}
const RL=['','light','medium-light','medium','medium-dark','dark'];let CC=[];
async function loadCommunityCoffees(){try{CC=(await get('/api/community/coffees')).coffees||[]}catch(e){}if(L)drawCoffeeList()}
function adopt(id){const c=CC.find(x=>x.id==id);if(!c)return;post('/api/coffee/adopt',{libraryId:c.id,name:c.name,roaster:c.roaster||'',origin:c.origin||'',region:c.region||'',producer:c.producer||'',varietal:c.varietal||'',process:c.process||'',notes:c.notes||'',roastLevel:Math.max(0,RL.indexOf(c.roast_level||'')),altitudeM:c.altitude_m||0}).then(r=>{if(done(r,'Added to your shelf')){C=r.id;loadLibrary()}})}
let comSeq=0;
async function drawCommunity(el,coffeeId){const box=$(el),c=coffee(coffeeId),mine=++comSeq;if(!c){box.textContent=coffeeId?'':'No coffee is attached to what is being brewed, so there is nothing from other people to show.';return}
if(!c.libraryId){box.textContent=c.name+' is not in the shared library yet, so there is nothing from other people for it. Once it has been sent and a reviewer has published it, what others settle on appears here.';return}
const ps=L.presets.map((p,i)=>({p,i})).filter(x=>x.p.coffeeId==coffeeId);if(!ps.length){box.textContent='Tick a recipe for this coffee to see what others settled on for it.';return}
const x=ps.find(x=>x.i==L.activePreset)||ps[0],r=recipe(x.p.recipeId);let m;try{m=await get('/api/community?coffee='+coffeeId+'&recipe='+r.id)}catch(e){return}if(mine!=comSeq&&el=='ccom')return;
if(!m.found){box.textContent='Nobody has shared a settled dial-in for '+c.name+' with a recipe like '+r.name+' ('+r.doseG+' g in, '+r.targetWeightG+' g out) yet.'+(m.reachable?'':' The shared library cannot be reached from this controller yet.');return}
const s=m.start,why={fitted:'translated to your grinder through '+m.coffees_in_common+' coffee'+(m.coffees_in_common==1?'':'s')+' you have in common with others',same_model:'what '+m.same_model+' other'+(m.same_model==1?'':'s')+' with your grinder model set',microns:'the shared scale read as microns; how your own grinder sits against it is not known yet',none:'cannot be translated to your grinder yet'}[s.basis];
box.innerHTML=`<div class="tiles"><div class="tile" style="background:#16263a"><span>Start your grinder at</span><b>${s.setting?s.setting+' \u00b1 '+s.range+' '+esc(U):'not known'}</b><span>${why}</span></div><div class="tile" style="background:#16263a"><span>What a good shot looked like</span><b>${f1(m.time_s)} s</b><span>${r.doseG} g in, ${r.targetWeightG} g out (${esc(r.name)})</span></div><div class="tile" style="background:#16263a"><span>Based on</span><b>${m.contributors} ${m.contributors==1?'person':'people'}</b><span>${m.dialins} settled dial-in${m.dialins==1?'':'s'}, within \u00b1 ${f1(m.spread_um)} \u00b5m of each other</span></div></div>`+(s.setting?`<div class="row" style="margin-top:12px"><button style="background:var(--com);border-color:var(--com);color:#06203a;font-weight:600" onclick="useStart(${x.i},${s.setting})">Use ${s.setting} ${esc(U)} as my starting grind</button><span>${x.p.grindUm?'Your setting for it now: '+x.p.grindUm+' '+esc(U)+'.':'No setting entered for it yet.'} A start only: grind advice takes it from there.</span></div>`:'')}
function useStart(i,um){const p=L.presets[i];if(p.grindUm&&p.grindUm!=um&&!confirm('Replace your setting of '+p.grindUm+' '+U+' with '+um+' '+U+'?'))return;act('preset_grind',i,um)}
function saveCoffee(extra){const b={id:C||0};CF.forEach(k=>{const v=$('c_'+k).value;b[k]=k=='roastDate'?d2i(v):(k=='roastLevel'||k=='altitudeM')?+v||0:v.trim()});Object.assign(b,extra||{});
if(!b.name)return say('A coffee needs a name');
if(!C&&L.coffees.some(c=>c.name.toLowerCase()==b.name.toLowerCase()&&c.roaster.toLowerCase()==b.roaster.toLowerCase())&&!confirm('"'+b.name+'" is already on the shelf. Add it again?'))return;
post('/api/coffee',b).then(r=>{if(done(r,'Coffee saved')){C=r.id;loadLibrary()}})}
function newBag(){const d=today();$('c_roastDate').value=i2d(d);saveCoffee({roastDate:d})}
function delCoffee(){const c=coffee(C);if(!c||!confirm('Delete "'+c.name+'" from the shelf? Its shots stay in the history.'))return;act('coffee_delete',C).then(()=>{C=-1;loadLibrary()})}

function recipeForm(){const bloom=+$('r_bloomPauseS').value>0;if(bloom)$('r_preinfusion').value=1;$('r_preinfusion').disabled=bloom;
$('r_targetWeightG').disabled=$('r_useRatio').value=='1';$('r_ratio').disabled=$('r_useRatio').value!='1';
$('r_preinfusionHoldS').disabled=$('r_preinfusion').value!='1';$('r_maxShotTimeS').disabled=$('r_timeLimit').value!='1';
$('rbloom').textContent=bloom?'Bloom: the button is held for the hold time, then pressed again to switch the pump off. After the pause it is held for one second, which skips the machine\'s own pre-infusion, and the brew runs to the target. The pause counts toward the brew time, and "Stop after" must leave room for it.':'Holding MANUAL on the Breville keeps it in low-pressure pre-infusion for as long as it is held. A bloom pause switches the pump off after the hold and starts it again after the pause; it needs a held pre-infusion.'}
function drawRecipes(){$('rlist').innerHTML=L.recipes.map(r=>{const n=L.presets.filter(p=>p.recipeId==r.id).length;return `<button class="item ${r.id==R?'on':''}" onclick="R=${r.id};drawRecipes()"><b>${esc(r.name)}</b><span>${r.doseG} g in, ${r.targetWeightG} g out · ${n?n+(n==1?' entry':' entries')+' on the controller':'not on the controller'}</span></button>`}).join('');
const r=recipe(R)||{name:'',doseG:18,useRatio:false,targetWeightG:36,ratio:2,targetTimeS:28,preinfusion:0,preinfusionHoldS:3,bloomPauseS:0,timeLimit:0,maxShotTimeS:50,baselineOffsetG:1.5};
RF.forEach(k=>{$('r_'+k).value=k=='useRatio'?(r[k]?1:0):r[k]});recipeForm();
$('rdup').style.display=$('rdel').style.display=R?'':'none';$('rused').style.display=R?'':'none';$('rinfo').textContent=R?'':'New recipe: not saved yet.';
if(!R)return;const use=L.presets.map((p,i)=>({p,i})).filter(x=>x.p.recipeId==R);
$('rtable').innerHTML='<tr><th class="l">Coffee</th><th>Grind '+U+'</th><th>Learned stop offset g</th><th class="l"></th></tr>'+(use.map(x=>{const c=coffee(x.p.coffeeId);
return `<tr><td class="l"><b>${c?esc(c.name):'No coffee attached'}</b>${x.i==L.activePreset?' <span class="dim">in use now</span>':''}</td><td>${x.p.grindUm||'not set'}</td><td>${f2(x.p.stopOffsetG)}</td><td class="l"><a href="#" onclick="post('/api/offset/reset?recipe=${x.i}').then(loadLibrary);return false">reset offset</a> &nbsp; <a href="#" onclick="act('preset_delete',${x.i});return false">take off the controller</a></td></tr>`}).join('')||'<tr><td class="l dim" colspan="4">Not on the controller yet.</td></tr>');
$('ralone').style.display=use.some(x=>!x.p.coffeeId)?'none':''}
function readRecipe(){const b={id:R||0};RF.forEach(k=>{const v=$('r_'+k).value;b[k]=k=='name'?v.trim():k=='useRatio'?v==='1':+v});if(b.useRatio)b.targetWeightG=Math.round(b.doseG*b.ratio*10)/10;return b}
function saveRecipe(){const b=readRecipe();if(!b.name)return say('A recipe needs a name');post('/api/recipe',b).then(r=>{if(done(r,'Recipe saved')){R=r.id;loadLibrary()}})}
function dupRecipe(){const b=readRecipe();b.id=0;b.name=(b.name+' 2').slice(0,23);post('/api/recipe',b).then(r=>{if(done(r,'Copy made: not on the controller yet')){R=r.id;loadLibrary()}})}
function delRecipe(){const r=recipe(R);if(!r||!confirm('Delete the recipe "'+r.name+'"?'))return;act('recipe_delete',R).then(()=>{R=-1;loadLibrary()})}

async function loadGrind(){try{G=await get('/api/grind')}catch(e){return}
const cur=G.current_um||'--';$('gcur').textContent=cur;$('hg').textContent=cur;$('gfor').textContent='Advice for '+G.recipe+(G.coffee?', '+G.coffee:'');
const b=$('gapply');b.style.display='none';let title,cls='',why,short;
const basis=`Average ${G.time_basis=='pour'?'pour':'total'} time ${f1(G.average_time_s)} s over ${G.shots_used} shot${G.shots_used==1?'':'s'} at ${G.current_um} ${U}, target ${G.target_time_s} s ± ${G.tolerance_s} s.`;
const out=G.outliers_ignored?` ${G.outliers_ignored} one-off shot${G.outliers_ignored==1?'':'s'} left out (far from the others at this setting).`:'';
const sens=G.learned?` Sensitivity ${G.um_per_s} ${U} per second, measured from ${G.learn_shots} of your shots.`:` Sensitivity ${G.um_per_s} ${U} per second is the starting guess; it is measured once shots exist at two or more grind settings.`;
if(G.action=='no_grind_set'){title='Enter the grinder’s setting';why=short='Shots are stored with the grind setting entered here. Advice starts with the first valid shot.'}
else if(G.action=='no_data'){title='No shot at this setting yet';why=short=`Pull a shot at ${G.current_um} ${U}. A shot counts if it ends on the weight target, settles, and lasts more than 15 s and less than target + 15 s.`}
else if(G.action=='inconsistent'){title='Brew times vary: no advice';cls='warn';short=`The last ${G.shots_used} shots at ${G.current_um} ${U} ran from ${f1(G.min_time_s)} s to ${f1(G.max_time_s)} s against ${G.target_time_s} s ± ${G.tolerance_s} s. Check dose, distribution and tamp before moving the grind.`;
why=`The last ${G.shots_used} shots at ${G.current_um} ${U} ran from ${f1(G.min_time_s)} s to ${f1(G.max_time_s)} s against a target of ${G.target_time_s} s ± ${G.tolerance_s} s, some fast and some slow. Their average (${f1(G.average_time_s)} s) describes none of them, so it is not used. If the grinder was changed between them, enter its setting: shots are only compared at the same setting. Otherwise look at dose, distribution and tamp. A shot can be left out on the Shots page.`+out}
else if(G.action=='hold'){title='Keep '+G.current_um+' '+U+'';cls='ok';why=basis+' Inside tolerance.'+out;short=basis}
else{const fin=G.action=='finer';title=`Go ${fin?'finer':'coarser'} ${Math.abs(G.delta_um)} ${U}, to ${G.suggested_um} ${U}`;cls='warn';short=basis+` Running ${f1(Math.abs(G.error_s))} s ${fin?'fast':'slow'}.`;
why=short+sens+(G.step_limited?' Limited to the largest single change; expect another step.':'')+out;b.style.display='';b.textContent='I set the grinder to '+G.suggested_um+' '+U+''}
$('gadv').textContent=$('hgadv').textContent=title;$('gadv').className=$('hgadv').className='pill '+cls;$('gwhy').textContent=why;$('hgwhy').textContent=short;
$('gsens').textContent='Sensitivity: '+(G.learned?'measured, ':'starting guess, ')+G.um_per_s+' '+U+' per second';drawScatter()}
const today=()=>{const t=new Date();return t.getFullYear()*10000+(t.getMonth()+1)*100+t.getDate()};
let GM=[];
async function loadCommunityGrinders(){try{GM=(await get('/api/community/grinders')).grinders||[]}catch(e){}if(L)drawGrinder()}
const gmPick=()=>{const v=$('gm_model').value;return v[0]=='L'?{lib:GM.find(x=>x.id==v.slice(2))}:{own:L.grinderModels.find(x=>x.id==+v)}};
function grinderForm(){const p=gmPick(),m=p.own||(p.lib&&{model:p.lib.model,unit:p.lib.unit,umPerUnit:p.lib.um_per_unit,burrs:p.lib.burrs||''});
if(m){$('gm_name').value=m.model;$('gm_unit').value=m.unit;$('gm_umPerUnit').value=m.umPerUnit||'';const known=L.grinderModels.map(x=>x.burrs).concat(GM.map(x=>x.burrs||''));if(!$('gm_burrs').value||known.includes($('gm_burrs').value))$('gm_burrs').value=m.burrs}
['gm_name','gm_unit','gm_umPerUnit'].forEach(k=>$(k).disabled=!!m)}
function drawGrinder(){const g=L.grinder,o=L.otherGrinder,libSel=g.libraryId&&GM.some(x=>x.id==g.libraryId);
$('gm_model').innerHTML=L.grinderModels.map(m=>`<option value="${m.id}" ${m.id==g.modelId?'selected':''}>${esc(m.model)}</option>`).join('')+GM.filter(m=>!L.grinderModels.some(x=>x.model==m.model)).map(m=>`<option value="L:${esc(m.id)}" ${!g.modelId&&g.libraryId==m.id?'selected':''}>${esc(m.model)} (shared library)</option>`).join('')+`<option value="0" ${g.modelId||libSel?'':'selected'}>${g.model||g.modelId?'Another grinder: type it':'Not set yet: pick one, or type it'}</option>`;
$('gm_name').value=g.model;$('gm_burrs').value=g.burrs;$('gm_unit').value=g.unit;$('gm_umPerUnit').value=g.umPerUnit||'';$('gm_zeroDate').value=i2d(g.zeroDate);grinderForm();
if(!gmPick().own&&!gmPick().lib){$('gm_name').value=g.model;$('gm_unit').value=g.unit;$('gm_umPerUnit').value=g.umPerUnit||''}
$('gm_info').textContent=(g.model?'In use: '+g.model+(g.burrs?', '+g.burrs:'')+'. ':'No grinder named yet. ')+'Settings are whole numbers in the unit shown; a dial read in tenths is not supported yet.';
const st=g.share||'local',on=S&&S.shareEnabled,known=g.modelId>0;$('gm_shrow').style.display=g.model?'':'none';$('gm_shpill').textContent=known?'a known model':SHN[st];
$('gm_share').textContent=known?'This model is in the firmware\'s own list: there is nothing to send.':({local:on?'Kept on this controller only. Sending it lets a reviewer add the model to the shared list.':'Kept on this controller only. Sharing is switched off (Setup).',queued:'Signed and waiting in the outbox.',sent:'Sent. The library has not answered yet.',review:'The library has it. A reviewer has not decided yet.',published:'In the shared list'+(g.libraryId?' as '+g.libraryId:'')+'.',declined:'A reviewer did not accept it'+(g.reviewNote?': "'+g.reviewNote+'"':'')+'. What you typed is unchanged.'})[st]+(g.shareChanged?' You have changed it since that version was sent.':'');
$('gm_send').style.display=on&&g.canShare?'':'none';$('gm_send').textContent=st=='local'?'Send to the shared library':'Send the new version';
$('gm_addother').style.display=o?'none':'';
$('gm_other').innerHTML=o?`<span class="sm mid" style="flex:1 1 260px">Your other grinder: <b>${esc(o.model)}</b>${o.burrs?', '+esc(o.burrs):''} (read in ${esc(o.unit=='um'?'µm':o.unit)}).</span><button onclick="switchGrinder()">Use it instead</button>`+(S&&S.shareEnabled&&o.canShare?`<button onclick="sendGrinder(${o.id})">Send it to the shared library</button>`:'')+`<button class="del" onclick="removeOther()">Remove it</button>`:'<span class="sm dim">One grinder. A second can be kept beside it: choose or type it above, then "Add ... as my second grinder".</span>'}
function grinderBody(rezeroed){const p=gmPick();return {modelId:p.own?p.own.id:0,libraryId:p.lib?p.lib.id:'',model:$('gm_name').value.trim(),burrs:$('gm_burrs').value.trim(),unit:$('gm_unit').value.trim()||'um',umPerUnit:+$('gm_umPerUnit').value||0,zeroDate:rezeroed?today():d2i($('gm_zeroDate').value),rezeroed:!!rezeroed}}
function saveGrinder(rezeroed,asOther){const g=L.grinder,b=grinderBody(rezeroed);if(!b.modelId&&!b.model)return say('Pick a model, or type its name');
if(asOther){b.asOther=true;b.rezeroed=false;if(b.model==g.model&&b.burrs==g.burrs)return say('That is the grinder already in use: choose or type the other one first');
post('/api/grinder',b).then(r=>{done(r,'Second grinder added');loadLibrary()});return}
const named=g.model||g.modelId,changes=named&&(b.model!=g.model||b.burrs!=g.burrs||b.unit!=g.unit);
if((rezeroed||changes)&&!confirm('Grind settings from before will no longer be compared with new ones: grind advice starts fresh for every coffee, and if the unit changes the settings entered so far are cleared. To keep both grinders, use "Add ... as my second grinder" instead. Continue?'))return;
post('/api/grinder',b).then(r=>{done(r,'Grinder saved');loadLibrary()})}
function switchGrinder(){act('grinder_switch').then(()=>say('Now using '+L.grinder.model))}
function removeOther(){if(confirm('Remove '+L.otherGrinder.model+'? Its settings for each coffee go with it; its shots stay in the history.'))act('grinder_remove_other')}
function sendGrinder(id){post('/api/share/grinder?id='+id).then(r=>{done(r,'Queued for the shared library');loadLibrary();loadShare()})}
function setGrind(um){if(!(um>=1&&um<=2000))return say('Enter the setting as a number from 1 to 2000');post('/api/grind/set?um='+Math.round(um)).then(r=>{done(r,'Grind setting saved');$('gset').value='';loadLibrary()})}
function drawScatter(){const c=$('gchart'),x=c.getContext('2d');x.clearRect(0,0,c.width,c.height);if(!L||!G||!S)return;const p=L.presets[L.activePreset];
const pts=SH.filter(s=>s.recipe_id==p.recipeId&&s.coffee_id==p.coffeeId&&s.grind_um&&/^weight/.test(s.end_reason)&&s.grind_vote!='ignore'&&(s.grinder_calibration||0)==L.grinder.calibration).map(s=>({u:s.grind_um,t:S.grindTimeBasis==1?s.pour_time_s:s.shot_time_s})).filter(q=>q.t>0);
const tg=G.target_time_s,tol=G.tolerance_s;if(!pts.length){$('gnote').textContent='No shots of this coffee and recipe stopped by weight yet.';return}
const us=pts.map(q=>q.u),lo=Math.min(...us,G.current_um||us[0])-10,hi=Math.max(...us,G.current_um||us[0])+10,ts=pts.map(q=>q.t),t0=Math.min(...ts,tg-tol)-3,t1=Math.max(...ts,tg+tol)+3;
const X=u=>50+(u-lo)/(hi-lo)*(c.width-70),Y=t=>c.height-26-(t-t0)/(t1-t0)*(c.height-40);
x.fillStyle='#14301f';x.fillRect(50,Y(tg+tol),c.width-70,Y(tg-tol)-Y(tg+tol));x.strokeStyle='#4cc38a';x.setLineDash([4,6]);x.beginPath();x.moveTo(50,Y(tg));x.lineTo(c.width-20,Y(tg));x.stroke();x.setLineDash([]);
x.strokeStyle='#2a2e33';x.beginPath();x.moveTo(50,10);x.lineTo(50,c.height-26);x.lineTo(c.width-20,c.height-26);x.stroke();
x.fillStyle='#9aa3ad';x.font='12px sans-serif';x.fillText(t1.toFixed(0)+' s',8,18);x.fillText(t0.toFixed(0)+' s',8,c.height-28);x.fillText(lo+' '+U+'',50,c.height-8);x.fillText(hi+' '+U+'',c.width-70,c.height-8);
pts.forEach((q,i)=>{x.fillStyle=Math.abs(q.t-tg)<=tol?'#7be0aa':'#f5c451';x.beginPath();x.arc(X(q.u)+((i*37)%13-6),Y(q.t),5,0,7);x.fill()});
const set=new Set(us);$('gnote').textContent=pts.length+' shot'+(pts.length==1?'':'s')+' stopped by weight, at '+set.size+' setting'+(set.size==1?'. With one setting there is no slope to measure: once shots exist at two settings at least 4 '+U+' apart, the line through them replaces the starting guess.':'s.')+' Green band: '+tg+' s ± '+tol+' s.'}

function drawFilter(){const cur=$('fcoffee').value;$('fcoffee').innerHTML='<option value="">All coffees</option>'+[...new Set(SH.map(s=>s.coffee).filter(x=>x))].map(n=>`<option ${n==cur?'selected':''}>${esc(n)}</option>`).join('')}
async function loadShots(){try{SH=await get('/api/shots?limit=500')}catch(e){return}drawFilter();drawShots();
if(SH.length){showShot(SH[0].id,'h');if(!SEL||!SH.some(s=>s.id==SEL))SEL=SH[0].id;showShot(SEL,'s')}drawScatter()}
const when=s=>s.timestamp?new Date(s.timestamp).toLocaleString([], {month:'short',day:'numeric',hour:'numeric',minute:'2-digit'}):'#'+s.id;
function drawShots(){const fc=$('fcoffee').value,fw=$('fshow').value=='1';const tol=S?S.grindToleranceS:2;
let h='<tr><th class="l">When</th><th class="l">Coffee, recipe</th><th>Out g</th><th>Time s</th><th>Pour s</th><th>Pour g/s</th><th>Grind '+U+'</th><th class="l">Ended by</th><th class="l">Advice</th><th class="l">Taste</th></tr>';
SH.filter(s=>(!fc||s.coffee==fc)&&(!fw||/^weight/.test(s.end_reason))).forEach(s=>{const t=s.time_basis=='pour'?s.pour_time_s:s.shot_time_s,off=/^weight/.test(s.end_reason)&&s.target_time_s&&Math.abs(t-s.target_time_s)>tol;
h+=`<tr class="pick ${s.id==SEL?'on':''}" onclick="SEL=${s.id};showShot(${s.id},'s');drawShots()"><td class="l">${esc(when(s))}</td><td class="l">${esc((s.coffee?s.coffee+', ':'')+s.recipe)}</td><td>${f1(s.final_weight_g)}</td><td class="${off?'off':''}">${f1(s.shot_time_s)}</td><td>${f1(s.pour_time_s)}</td><td>${f2(s.pour_flow_g_s)}</td><td><a href="#" onclick="event.stopPropagation();editGrind(${s.id},${s.grind_um});return false">${s.grind_um||'set'}</a></td><td class="l mid">${esc(s.end_reason.replace(/_/g,' '))}</td><td class="l"><a href="#" onclick="event.stopPropagation();vote(${s.id},'${s.grind_vote}');return false">${s.grind_vote=='count'?'counted':s.grind_vote=='ignore'?'ignored':'auto'}</a></td><td class="l dim">${s.rating?'★'.repeat(s.rating):''}${s.taste?' '+s.taste:(s.rating?'':'not rated')}</td></tr>`});
$('hist').innerHTML=h}
function editGrind(id,cur){const v=prompt('Grind setting for this shot, in '+U+' (0 = not recorded)',cur||'');if(v===null)return;post('/api/shot/grind?id='+id+'&um='+Math.round(+v||0)).then(r=>{done(r,'');loadShots();loadGrind()})}
function vote(id,cur,next){if(next==null)next=cur==''?2:cur=='count'?1:0;post('/api/shot/vote?id='+id+'&vote='+next).then(r=>{done(r,'');loadShots();loadGrind()})}
function rate(id,rating,taste){post('/api/shot/rate?id='+id+'&rating='+rating+'&taste='+taste).then(r=>{done(r,'');loadShots()})}
const TN=['','sour','balanced','bitter'];
function rateRow(s){const t=TN.indexOf(s.taste||'');return '<span>How did it taste?</span>'+[1,2,3].map(i=>`<button class="${t==i?'on':''}" onclick="rate(${s.id},${s.rating||0},${t==i?0:i})">${TN[i][0].toUpperCase()+TN[i].slice(1)}</button>`).join('')+'<span style="margin-left:8px">Rating</span>'+[1,2,3,4,5].map(i=>`<button class="${s.rating==i?'on':''}" aria-label="${i} of 5" onclick="rate(${s.id},${s.rating==i?0:i},${t<0?0:t})">${i}</button>`).join('')}
async function showShot(id,where){const s=SH.find(x=>x.id==id);if(!s)return;const big=where=='h';
if(big){$('hshot').className='';$('hshot').innerHTML=`<div class="row" style="gap:26px;align-items:baseline"><span><span class="num">${f1(s.final_weight_g)} g</span> <span class="dim">of ${s.target_weight_g}</span></span><span><span class="num">${f1(s.shot_time_s)} s</span>${s.target_time_s?` <span class="dim">of ${s.target_time_s}</span>`:''}</span><span class="dim">${esc((s.coffee?s.coffee+', ':'')+s.recipe)}. First drops ${f1(s.first_drop_s)} s. Pour ${f1(s.pour_time_s)} s at ${f2(s.pour_flow_g_s)} g/s${s.bloom_pause_s?'. Bloom '+s.bloom_pause_s+' s':''}. ${esc(when(s))}.</span></div>`;$('hrate').innerHTML=rateRow(s);
const d=s.final_weight_g-s.target_weight_g;$('res').textContent='Last shot '+(Math.abs(d)<0.05?'on target':(d>0?'over +':'under ')+f1(d)+' g')+(s.final_valid?'':', did not settle')}
else{$('swhen').textContent=when(s);$('sname').textContent=(s.coffee?s.coffee+', ':'')+s.recipe;
$('stiles').innerHTML=[['In the cup',f1(s.final_weight_g)+' g','target '+s.target_weight_g+', drip '+f1(s.drip_weight_g)],['Brew time',f1(s.shot_time_s)+' s',s.target_time_s?'target '+s.target_time_s:''],['Pour',f1(s.pour_time_s)+' s',f2(s.pour_flow_g_s)+' g/s, first drops '+f1(s.first_drop_s)+' s'],['Grind',(s.grind_um||'--')+' '+U+'',s.dose_g+' g dose'],['Flow at 25/50/75 %',[s.flow_25_g_s,s.flow_50_g_s,s.flow_75_g_s].map(f1).join(' / '),'peak '+f2(s.peak_flow_g_s)+' g/s'],['Ended by',esc(s.end_reason.replace(/_/g,' ')),'offset '+f2(s.stop_offset_g)+' → '+f2(s.next_stop_offset_g)+' g'+(s.bloom_pause_s?', bloom '+s.bloom_pause_s+' s':'')]].map(x=>`<div class="tile"><span>${x[0]}</span><b>${x[1]}</b><span>${x[2]}</span></div>`).join('');
$('svote').innerHTML=[['Auto',0,''],['Count',2,'count'],['Ignore',1,'ignore']].map(v=>`<button class="${s.grind_vote==v[2]?'on':''}" onclick="vote(${s.id},'',${v[1]})">${v[0]}</button>`).join('');$('srate').innerHTML=rateRow(s)}
const cv=big?'hchart':'schart';if(!s.has_data){draw(cv,s,null);if(!big)$('sdata').textContent='The readings of this shot are no longer stored; the summary is.';return}
let r;try{r=await get('/api/shot/data?id='+id)}catch(e){return}if(!r.time_s)return;draw(cv,s,r.time_s,r.weight_g,r.flow_g_s);
if(!big){const d=r.data;$('sdata').innerHTML=`White: weight. Orange: flow. ${d.samples} readings over ${d.duration_s} s, one every ${d.mean_period_ms} ms, longest gap ${d.max_gap_ms} ms. `+(d.timer_samples?`Scale timer in ${d.timer_samples} of them`+(d.timer_span_s>0?`: it counted ${d.timer_span_s} s while this controller counted ${d.clock_span_s} s. `:'. '):'The scale sent no timer. ')+`<a href="/shot.csv?id=${id}">Download this shot's readings</a>`}}
function draw(id,s,T,W,F){const c=$(id),x=c.getContext('2d');x.clearRect(0,0,c.width,c.height);if(!T||T.length<2)return;
const fv=F?F.filter(v=>v!=null):[],tm=Math.max(T[T.length-1],1),wm=Math.max(s.target_weight_g*1.1,...W),fm=Math.max(1,...fv),R=52,X=t=>34+t/tm*(c.width-34-R),Y=w=>c.height-20-Math.max(w,0)/wm*(c.height-32),YF=v=>c.height-20-Math.max(v,0)/fm*(c.height-32);
x.lineWidth=1;x.strokeStyle='#6b7280';x.setLineDash([4,6]);x.beginPath();x.moveTo(34,Y(s.target_weight_g));x.lineTo(c.width-R,Y(s.target_weight_g));x.stroke();
x.fillStyle='#9aa3ad';x.font='12px sans-serif';x.strokeStyle='#3a3f46';[[s.first_drop_s,'first drops'],[s.shot_time_s,'stop']].forEach(m=>{if(!(m[0]>0&&m[0]<=tm))return;x.beginPath();x.moveTo(X(m[0]),10);x.lineTo(X(m[0]),c.height-20);x.stroke();x.fillText(m[1],X(m[0])+4,c.height-26)});x.setLineDash([]);
if(F){x.strokeStyle='#f08a24';x.lineWidth=2;x.beginPath();let pen=false;T.forEach((t,i)=>{if(F[i]==null){pen=false;return}pen?x.lineTo(X(t),YF(F[i])):x.moveTo(X(t),YF(F[i]));pen=true});x.stroke()}
x.strokeStyle='#eceef0';x.lineWidth=2.5;x.beginPath();T.forEach((t,i)=>i?x.lineTo(X(t),Y(W[i])):x.moveTo(X(t),Y(W[i])));x.stroke();
x.fillStyle='#eceef0';x.fillText(wm.toFixed(0)+' g',2,16);x.fillStyle='#9aa3ad';x.fillText(tm.toFixed(0)+' s',c.width-R-28,c.height-4);x.fillStyle='#f08a24';x.fillText(fm.toFixed(1)+' g/s',c.width-R+6,16)}

async function loadDevices(){let d;try{d=await get('/api/devices')}catch(e){return}
$('dev').innerHTML=`<b>Scale</b>: ${d.scale_connected?esc(d.scale_name)+' ('+esc(d.scale_protocol)+'), connected':'not connected'} <span class="dim">[${esc(d.pairing.scale||'adopts the first supported scale found')}]</span><br><b>Brew bot</b>${d.bot_label?' ('+esc(d.bot_label)+')':''}: ${d.bot_connected?'connected':(d.pairing.bot?'asleep until Start':'not paired')} <span class="dim">[${esc(d.pairing.bot||'pick one below')}]</span>`+(d.bot_firmware&&d.bot_firmware!='0.0'?`, firmware ${esc(d.bot_firmware)}`:'')+(d.bot_battery!=null?`, battery ${d.bot_battery}%`:'')+(d.bot_problem?` <b style="color:var(--warn)">${esc(d.bot_problem)}</b>`:'')+(d.bot_caps&&d.bot_caps.length&&!d.bot_caps.includes('down_up')?' <span class="dim">This pusher cannot hold the button: held pre-infusion and bloom restarts become plain presses.</span>':'')+`<br><b>Power bot</b>${d.power_label?' ('+esc(d.power_label)+')':''}: ${d.power_connected?'connected':(d.pairing.power?'asleep until needed':'not paired')} <span class="dim">[${esc(d.pairing.power||'optional: pick one below')}]</span>`+(d.power_battery!=null?`, battery ${d.power_battery}%`:'')+(d.power_problem?` <b style="color:var(--warn)">${esc(d.power_problem)}</b>`:'');
const q=d.scale_data,caps=d.scale_caps||[],no=['timer_control','timer','battery','buttons'].filter(c=>!caps.includes(c)).map(c=>({timer_control:'timer control',timer:'timer readings',battery:'battery level',buttons:'button presses'})[c]);$('scaledata').textContent=d.scale_connected&&q?`Scale link: ${q.weight_frames} readings since it connected`+(caps.includes('timer')?`, ${q.timer_frames} with the scale's timer`:'')+`, ${q.split_frames} frames arrived in two parts, ${q.bad_frames} failed their checksum`+(q.battery!=null?`, scale battery ${q.battery}%`:'')+'.'+(no.length?` This scale does not offer: ${no.join(', ')}.`:''):'';
const mm=d.memory;if(mm){const low=mm.internal_lowest<40000;$('scaledata').innerHTML=esc($('scaledata').textContent)+'<br>Memory inside the chip: '+Math.round(mm.internal_free/1024)+' kB free now, '+(low?'<b style="color:var(--warn)">':'')+Math.round(mm.internal_lowest/1024)+' kB at its lowest since start-up'+(low?'</b> (tight: below 40 kB)':'')+', largest single piece '+Math.round(mm.internal_largest/1024)+' kB. '+(mm.psram_total?'PSRAM: '+Math.round(mm.psram_free/1024)+' of '+Math.round(mm.psram_total/1024)+' kB free.':'<b style="color:var(--warn)">No PSRAM found: the large buffers are inside the chip.</b>')}
$('seen').innerHTML='<tr><th class="l">Type</th><th class="l">Name</th><th class="l">Address</th><th>Signal</th><th></th></tr>'+d.seen.map(s=>`<tr><td class="l">${s.kind}</td><td class="l">${esc(s.name)}</td><td class="l">${esc(s.address)}</td><td>${s.rssi}</td><td><button onclick="pair('${s.kind}','${esc(s.address)}',${s.type})">${s.kind=='bot'?'Brew':'Use'}</button>${s.kind=='bot'?`<button onclick="pair('power','${esc(s.address)}',${s.type})">Power</button>`:''}</td></tr>`).join('');loadPower();loadPusher()}
const DAYS=['Su','Mo','Tu','We','Th','Fr','Sa'];
async function loadPower(){let p;try{p=await get('/api/power')}catch(e){return}
$('pw').innerHTML=(p.paired?'':'<b>No power bot paired.</b> Pick a pusher in the list above with "Power". ')+'The machine is believed to be <b>'+esc(p.belief=='unknown'?'unknown: say which below':p.belief)+'</b>.'+(p.working?' Pressing the power button...':'')+(p.last?' Last: '+esc(p.last)+(p.last_by_schedule?' (schedule)':'')+'.':'')+(p.clock_set?'':' The clock is not set: the schedule waits for it.');
$('pws').innerHTML='<tr><th>Use</th><th>Do</th><th>At</th>'+DAYS.map(d=>'<th>'+d+'</th>').join('')+'</tr>'+p.schedule.map((e,i)=>`<tr><td><input type="checkbox" id="pe${i}" ${e.enabled?'checked':''}></td><td><select id="po${i}"><option value="1" ${e.on?'selected':''}>On</option><option value="0" ${e.on?'':'selected'}>Off</option></select></td><td><input type="time" id="pt${i}" value="${String(Math.floor(e.minute/60)).padStart(2,'0')}:${String(e.minute%60).padStart(2,'0')}"></td>`+DAYS.map((d,k)=>`<td><input type="checkbox" id="pd${i}_${k}" ${e.days>>k&1?'checked':''}></td>`).join('')+'</tr>').join('')}
function power(action){post('/api/power',{action}).then(r=>{done(r,action.startsWith('believe')?'Noted':'Power button: on its way');setTimeout(loadPower,600);setTimeout(loadPower,6000)})}
function savePowerSchedule(){const schedule=[];for(let i=0;$('pe'+i);i++){const t=$('pt'+i).value.split(':');let days=0;for(let k=0;k<7;k++)if($('pd'+i+'_'+k).checked)days|=1<<k;schedule.push({enabled:$('pe'+i).checked,on:$('po'+i).value=='1',minute:(+t[0]||0)*60+(+t[1]||0),days})}
post('/api/power',{action:'schedule',schedule,utc_offset_min:-new Date().getTimezoneOffset()}).then(r=>{done(r,'Schedule saved');loadPower()})}
async function loadPusher(){const role=$('pk_role').value;let k;try{k=await get('/api/pusher?role='+role)}catch(e){return}
$('pk_uuid').value=k.tuya_uuid||'';$('pk_dev').value=k.tuya_device_id||'';$('pk_key').value='';$('pk_tok').value='';$('pk_pw').value='';$('pk_var').value=k.tuya_variant||0;$('pk_pwl').style.display=role=='power'?'':'none';
$('pk_state').textContent='Stored: Tuya local key '+(k.tuya_local_key_set?'yes':'no')+', MicroBot token '+(k.microbot_token_set?'yes':'no')+(role=='power'?', SwitchBot password '+(k.password_set?'yes':'no'):'')+'. Empty key fields keep what is stored.'}
function savePusher(){post('/api/pusher',{role:$('pk_role').value,tuya_uuid:$('pk_uuid').value.trim(),tuya_device_id:$('pk_dev').value.trim(),tuya_local_key:$('pk_key').value,tuya_variant:+$('pk_var').value,microbot_token:$('pk_tok').value.trim(),password:$('pk_pw').value}).then(r=>{done(r,'Keys saved');loadPusher()})}
function pair(kind,address,type){post('/api/pair',{kind,address,type}).then(()=>setTimeout(loadDevices,800))}

post('/api/time?epoch='+Math.floor(Date.now()/1000));show();
loadSettings().then(loadLibrary).then(loadShots).then(loadCommunityCoffees).then(loadCommunityGrinders);setInterval(tick,500);tick();
</script></body></html>)HTML";
