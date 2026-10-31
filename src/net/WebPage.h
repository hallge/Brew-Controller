#pragma once
#include <Arduino.h>

// The whole web UI: one self-contained page, no external assets, so it works
// on the controller's own access point with no internet. Six sections, shown
// one at a time by the address's #part: home, shots, coffees, recipes,
// grinder, setup. Own data is orange, anything shared is blue.
static const char WEB_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="bbw-controller" content="1">
<title>Brew by Weight</title>
<script>try{const t=localStorage.getItem('bbwTheme');if(t&&t!='dark')document.documentElement.dataset.theme=t}catch(e){}</script>
<style>
:root{--bg:#0e0f11;--card:#17191c;--up:#1f2226;--line:#2a2e33;--edge:#3a3f46;--fg:#eceef0;--mid:#c3c9d0;--dim:#9aa3ad;--ok:#4cc38a;--okbg:#14301f;--warn:#f5c451;--warnbg:#33290f;--bad:#ff6b5e;--acc:#f08a24;--accbg:#3a2410;--com:#6cb4ff;--combg:#111a24;--comline:#23374d;--grid:#22262b;--gridlo:#1c1f23;--empty:#15171a;--stripe:#121417;--acctext:#ffd0a3;--comtext:#cfe6ff;--comh:#8cc4ff;--oktext:#7be0aa;--hover:#1a1d21;--hover2:#1b1e22;--badbg:#3b1512;--pairbg:#2a1c0e;--pairline:#6b4118;--delline:#5c2a25;--deltext:#ff8a80;--inhover:#5b6672;--acchover:#ffb061;color-scheme:dark}
:root[data-theme="light"]{--bg:#f4f3f0;--card:#ffffff;--up:#eceae6;--line:#dcd9d3;--edge:#b9b5ad;--fg:#1a1a1a;--mid:#3d4249;--dim:#646b73;--ok:#1e8e4e;--okbg:#dff3e7;--warn:#9a6a00;--warnbg:#fbf0d2;--bad:#c0392b;--acc:#c2560c;--accbg:#fbe6d4;--com:#1d6fc4;--combg:#e6f0fb;--comline:#b9d3ef;--grid:#e3e0da;--gridlo:#ecebe7;--empty:#f0eeea;--stripe:#f7f6f3;--acctext:#8a3a05;--comtext:#0d4a8c;--comh:#1d6fc4;--oktext:#156b3a;--hover:#eceae6;--hover2:#f0eee9;--badbg:#fbe1dd;--pairbg:#fbe6d4;--pairline:#e2a46e;--delline:#e8b4ae;--deltext:#b0302a;--inhover:#9a958c;--acchover:#a64708;color-scheme:light}
:root[data-theme="nord"]{--bg:#2e3440;--card:#3b4252;--up:#434c5e;--line:#4c566a;--edge:#5e6a80;--fg:#eceff4;--mid:#d8dee9;--dim:#a3abb9;--ok:#a3be8c;--okbg:#2f3a2f;--warn:#ebcb8b;--warnbg:#3d3a2e;--bad:#bf616a;--acc:#d08770;--accbg:#4a3a33;--com:#88c0d0;--combg:#2f3b45;--comline:#4b6170;--grid:#434c5e;--gridlo:#3e4656;--empty:#353c4a;--stripe:#323946;--acctext:#f1d3c4;--comtext:#d8eef3;--comh:#88c0d0;--oktext:#c4dcb0;--hover:#434c5e;--hover2:#3f4757;--badbg:#4a2e33;--pairbg:#4a3a33;--pairline:#8a6455;--delline:#7a3f45;--deltext:#e8a0a7;--inhover:#7b8394;--acchover:#e3a58d;color-scheme:dark}
:root[data-theme="solarized"]{--bg:#002b36;--card:#073642;--up:#0d3f4c;--line:#1b4d5a;--edge:#2f6170;--fg:#eee8d5;--mid:#d5d0bd;--dim:#93a1a1;--ok:#859900;--okbg:#1d3a1a;--warn:#b58900;--warnbg:#2f3315;--bad:#dc322f;--acc:#cb4b16;--accbg:#3a2a14;--com:#268bd2;--combg:#08394d;--comline:#1c5a78;--grid:#0f4654;--gridlo:#0a3d4a;--empty:#04313c;--stripe:#03303b;--acctext:#f5c39e;--comtext:#cfe6f7;--comh:#6cb4e6;--oktext:#b5c95e;--hover:#0d3f4c;--hover2:#0a3a46;--badbg:#3a1715;--pairbg:#3a2a14;--pairline:#7a4a1e;--delline:#6b2522;--deltext:#f08a86;--inhover:#4f7380;--acchover:#e2733c;color-scheme:dark}
:root[data-theme="dracula"]{--bg:#21222c;--card:#282a36;--up:#343746;--line:#44475a;--edge:#5a5e78;--fg:#f8f8f2;--mid:#e2e2dc;--dim:#a9adc8;--ok:#50fa7b;--okbg:#1f3a2a;--warn:#f1fa8c;--warnbg:#3a3b25;--bad:#ff5555;--acc:#ffb86c;--accbg:#47392b;--com:#8be9fd;--combg:#253a44;--comline:#3b5d6a;--grid:#343746;--gridlo:#2e303e;--empty:#2a2c38;--stripe:#262833;--acctext:#ffe0bd;--comtext:#d5f6fd;--comh:#8be9fd;--oktext:#9bfbb4;--hover:#343746;--hover2:#30323f;--badbg:#4a2328;--pairbg:#47392b;--pairline:#8a6a45;--delline:#7a3038;--deltext:#ff9a9a;--inhover:#6a6e8a;--acchover:#ffd09c;color-scheme:dark}
:root[data-theme="contrast"]{--bg:#000000;--card:#0a0a0a;--up:#1a1a1a;--line:#5a5a5a;--edge:#9a9a9a;--fg:#ffffff;--mid:#f0f0f0;--dim:#d0d0d0;--ok:#00e676;--okbg:#003a1c;--warn:#ffd600;--warnbg:#3a3100;--bad:#ff3b30;--acc:#ffb000;--accbg:#3d2a00;--com:#4fc3f7;--combg:#00243a;--comline:#2a6a90;--grid:#333333;--gridlo:#262626;--empty:#141414;--stripe:#0d0d0d;--acctext:#ffe2a0;--comtext:#e0f5ff;--comh:#4fc3f7;--oktext:#7dffb5;--hover:#1f1f1f;--hover2:#1a1a1a;--badbg:#3a0d0a;--pairbg:#3d2a00;--pairline:#a07000;--delline:#a03028;--deltext:#ff8a80;--inhover:#bdbdbd;--acchover:#ffc940;color-scheme:dark}
*{box-sizing:border-box}
.subh{margin:18px 0 6px;font-size:12px;font-weight:600;letter-spacing:.1em;text-transform:uppercase;color:var(--dim)}
.page .card{min-width:0}.days{display:inline-flex;flex-wrap:wrap;gap:4px}@media(max-width:600px){#pws .days{max-width:150px}#pws td{padding:6px 4px}}.days label{display:inline-block;cursor:pointer}.days input{position:absolute;opacity:0;width:0;height:0}
.days span{display:inline-block;min-width:30px;padding:5px 0;text-align:center;border-radius:8px;border:1px solid var(--line);color:var(--dim);font-size:13px;font-weight:600}
.days input:checked+span{background:var(--accbg);border-color:var(--acc);color:var(--acctext)}.days input:focus-visible+span{outline:2px solid var(--acc)}
.stars{display:inline-flex;gap:2px}.stars button{background:none;border:none;padding:2px 3px;font-size:28px;line-height:1;color:var(--edge);min-width:0}.stars button.on{color:var(--acc)}.stars button:hover{color:var(--acchover)}
.tbtn{background:none;border:none;padding:2px 6px;color:var(--acc);font-size:13px;min-width:0}.tbtn.del{color:var(--deltext)}
.recos{display:grid;grid-template-columns:repeat(auto-fill,minmax(250px,1fr));gap:10px}
.reco{display:block;background:var(--up);border:1px solid var(--line);border-radius:12px;padding:12px 14px;color:var(--fg)}a.reco:hover{border-color:var(--edge)}
.reco .src{float:right;font-size:11px;letter-spacing:.06em;text-transform:uppercase;color:var(--dim)}
.likes{display:flex;flex-wrap:wrap;gap:8px}.likes a{background:var(--up);border:1px solid var(--line);border-radius:999px;padding:5px 12px;font-size:13px;color:var(--fg)}.likes a b{color:var(--acc);font-weight:600;margin-left:6px}
.subs{display:grid;grid-template-columns:repeat(auto-fill,minmax(270px,1fr));gap:14px}
.sub{position:relative;background:var(--card);border:1px solid var(--line);border-radius:16px;overflow:hidden;display:flex;flex-direction:column;min-width:0}.sub.off{opacity:.62}
.sub .ban{position:relative;height:118px;background:var(--up);overflow:hidden}.sub .ban img{width:100%;height:100%;object-fit:cover;display:block}
.sub .ban .ini{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;font-size:44px;font-weight:700;letter-spacing:-.03em;color:rgba(255,255,255,.88)}
.sub .ico{position:absolute;left:14px;top:92px;width:44px;height:44px;border-radius:12px;background:var(--card);border:3px solid var(--card);overflow:hidden;box-shadow:0 2px 8px rgba(0,0,0,.25)}.sub .ico img{width:100%;height:100%;object-fit:cover;display:block}
.sub .tags{position:absolute;right:10px;top:10px;display:flex;gap:6px}.sub .tags .pill{font-size:11px;padding:3px 9px;box-shadow:0 1px 4px rgba(0,0,0,.3)}
.sub .bd{padding:26px 16px 10px;display:flex;flex-direction:column;gap:5px;flex:1}.sub .bd b{font-size:16px}
.sub .acts{display:flex;flex-wrap:wrap;gap:6px;padding:4px 12px 14px}.sub .acts button,.sub .acts a.btn{min-height:36px;padding:0 12px;font-size:14px}
a.btn{display:inline-flex;align-items:center;border-radius:10px;border:1px solid var(--edge);background:var(--up);color:var(--fg)}a.btn.pri{background:var(--acc);border-color:var(--acc);color:#1d0f00;font-weight:600}
.meter{height:6px;border-radius:3px;background:var(--up);overflow:hidden;margin-top:4px}.meter i{display:block;height:100%;background:var(--ok);border-radius:3px}.meter.warn i{background:var(--warn)}
.stat{background:var(--up);border-radius:12px;padding:12px 14px;min-width:0}.stat b{display:block;font-size:22px;font-weight:600;letter-spacing:-.02em;margin-top:2px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.chips{display:flex;flex-wrap:wrap;gap:6px}.chips button{min-height:34px;padding:0 12px;border-radius:999px;font-size:13px}.chips button.on{border-color:var(--acc);background:var(--accbg);color:var(--acctext)}
#fychips{display:flex;flex-wrap:wrap;gap:8px}#fychips a{padding:8px 12px;border-radius:999px;background:var(--accbg);color:var(--acctext);text-decoration:none;font-size:14px;line-height:1.3}.sub .why{display:flex;flex-wrap:wrap;gap:4px}.sub .why span{font-size:12px;padding:2px 8px;border-radius:999px;background:var(--up);color:var(--mid)}
.sub .lib{position:absolute;left:10px;top:10px}.sub .lib .pill{font-size:11px;padding:3px 9px;background:var(--combg);color:var(--comtext);border:1px solid var(--comline)}
.remind{display:flex;flex-wrap:wrap;gap:8px 12px;align-items:center;background:var(--warnbg);border:1px solid var(--line);border-left:4px solid var(--warn);border-radius:12px;padding:10px 14px}
.two{display:grid;grid-template-columns:1fr 1fr;gap:12px}@media(max-width:700px){.two{grid-template-columns:1fr}}
.theme{display:inline-flex;flex-direction:column;align-items:flex-start;gap:6px;min-width:120px}.theme.on{outline:2px solid var(--acc);outline-offset:1px}.theme .strip{display:flex;border-radius:4px;overflow:hidden;border:1px solid var(--edge)}.theme .strip i{display:block;width:22px;height:14px}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif;font-variant-numeric:tabular-nums}
a{color:var(--acc);text-decoration:none}a:hover{color:var(--acchover)}
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
.card.com{background:var(--combg);border-color:var(--comline)}.card.com h2{color:var(--comh)}
.cols{display:grid;grid-template-columns:repeat(auto-fit,minmax(290px,1fr));gap:16px}
.hstk{display:flex;flex-direction:column;gap:16px;min-width:0}.hstk>.card:last-child{flex:1}.card.tight{gap:8px;padding-top:14px;padding-bottom:14px}.card.tight .num{font-size:24px}.card.tight .head .row{gap:10px}.card.tight>.head{flex-wrap:nowrap;align-items:flex-start}.card.tight>.head>:first-child{min-width:0}.card.tight>.head>a{white-space:nowrap}
.split{display:flex;flex-wrap:wrap;gap:16px;align-items:flex-start}.split>.side{flex:1 1 260px;min-width:0}.split>.body{flex:2 1 420px;min-width:0;display:flex;flex-direction:column;gap:16px}
.row{display:flex;flex-wrap:wrap;gap:8px;align-items:center}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:12px}
.big{font-size:48px;font-weight:600;letter-spacing:-.03em}.num{font-size:30px;font-weight:600;letter-spacing:-.02em}
.dim{color:var(--dim)}.mid{color:var(--mid)}.sm{font-size:13px}
.live{display:flex;flex-wrap:wrap;gap:30px;align-items:baseline}
.pill{display:inline-block;padding:5px 12px;border-radius:999px;background:var(--up);font-size:13px;font-weight:600;letter-spacing:.05em}
.pill.ok{background:var(--okbg);color:var(--oktext)}
button.mstate{border:1px solid var(--edge);cursor:pointer;min-height:0;line-height:normal;font-family:inherit;color:var(--dim)}
button.mstate.ok{border-color:#2c6b4a}
button.mstate.warn{border-color:#6b5a24}
button.mstate:active,button.mstate.hit{border-color:var(--acc)}.pill.warn{background:var(--warnbg);color:var(--warn)}.pill.bad{background:var(--badbg);color:var(--bad)}
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
button.del{background:none;border-color:var(--delline);color:var(--deltext)}
button.on{border-color:var(--acc);background:var(--accbg);color:var(--acctext)}
button.as{border:2px solid var(--com);background:var(--combg);color:var(--comtext);font-weight:600}
button.pwr{transition:background .15s,border-color .15s}button.pwr:active,button.pwr.hit{background:var(--accbg);border-color:var(--acc);color:var(--acctext)}
.list{display:flex;flex-direction:column;gap:6px}
.item{display:block;width:100%;text-align:left;min-height:56px;padding:8px 12px;background:none;border:1px solid transparent}
.item.on{background:var(--up);border-color:var(--edge)}.item b{display:block}.item span{display:block;font-size:13px;color:var(--dim)}
.pair{display:flex;align-items:center;gap:12px;min-height:52px;padding:4px 14px;border-radius:10px;background:var(--up);border:1px solid var(--up)}
.pair.on{background:var(--pairbg);border-color:var(--pairline)}.pair .what{flex:1 1 auto;min-width:0}.pair input[type=number]{width:96px}
.scroll{overflow-x:auto}table{border-collapse:collapse;width:100%;font-size:14px}
th,td{padding:9px 10px;text-align:right;border-bottom:1px solid var(--grid);white-space:nowrap}
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

/* ---- polish: every control answers the hand ---- */
button,a,input,select,textarea,.item,.pair,nav a{transition:background-color .12s,border-color .12s,color .12s,transform .08s,box-shadow .12s,opacity .12s,filter .12s}
button:hover:not(:disabled){filter:brightness(1.14)}
button:active:not(:disabled){transform:scale(.96);filter:brightness(.9)}
button:disabled{opacity:.45;cursor:not-allowed}
:focus-visible{outline:2px solid var(--acc);outline-offset:2px}
input:hover:not(:disabled),select:hover:not(:disabled){border-color:var(--inhover)}
input:focus,select:focus,textarea:focus{border-color:var(--acc);outline:none;box-shadow:0 0 0 3px rgba(240,138,36,.22)}
nav a:hover{background:var(--hover);color:var(--fg)}
.item:hover{background:var(--hover)}tr.pick:hover td{background:var(--hover2)}
.pair:hover{border-color:var(--edge)}
/* a request in flight, then how it went */
button.busy{position:relative;color:transparent!important;pointer-events:none}
button.busy::after{content:"";position:absolute;left:50%;top:50%;width:16px;height:16px;margin:-10px 0 0 -10px;border:2px solid var(--fg);border-right-color:transparent;border-radius:50%;animation:spin .7s linear infinite}
button.pri.busy::after,button.go.busy::after{border-color:#1d0f00;border-right-color:transparent}
button.did{box-shadow:0 0 0 2px var(--ok)}
button.didnt{box-shadow:0 0 0 2px var(--bad);animation:shake .28s}
@keyframes spin{to{transform:rotate(360deg)}}
@keyframes shake{25%{transform:translateX(-4px)}75%{transform:translateX(4px)}}
/* something typed and not saved yet */
button.dirty{box-shadow:0 0 0 3px rgba(240,138,36,.38)}
button.dirty::before{content:"";display:inline-block;width:7px;height:7px;border-radius:50%;background:currentColor;margin-right:8px;vertical-align:middle}
.tick{margin-left:8px;font-size:12px;font-weight:600;color:var(--ok);white-space:nowrap}.tick.bad{color:var(--bad)}
#unsaved{display:none;position:fixed;left:50%;transform:translateX(-50%);bottom:18px;z-index:60;max-width:min(640px,calc(100vw - 24px));background:var(--up);border:1px solid var(--acc);border-radius:12px;padding:12px 14px;box-shadow:0 8px 30px rgba(0,0,0,.5);gap:10px;align-items:center;flex-wrap:wrap}
#unsaved.on{display:flex}
#askbox{display:none;position:fixed;z-index:80;background:var(--up);border:1px solid var(--edge);border-radius:12px;padding:14px 16px;max-width:min(420px,calc(100vw - 24px));box-shadow:0 10px 34px rgba(0,0,0,.55);white-space:pre-line}#askbox.on{display:block}#asktxt{margin-bottom:12px;line-height:1.45}
#askyes.del{background:var(--bad);border-color:var(--bad);color:#200}
#ctip{display:none;position:fixed;z-index:70;background:var(--up);border:1px solid var(--edge);border-radius:8px;padding:8px 10px;font-size:13px;line-height:1.45;pointer-events:none;max-width:280px;box-shadow:0 6px 20px rgba(0,0,0,.45)}
#bnav{display:none}#more{display:none;position:fixed;right:8px;bottom:72px;z-index:45;flex-direction:column;min-width:210px;background:var(--up);border:1px solid var(--edge);border-radius:12px;padding:6px;box-shadow:0 8px 30px rgba(0,0,0,.5)}
#more a{padding:13px 14px;border-radius:8px;color:var(--fg)}#more a.on{background:var(--card);font-weight:600}#unsaved span{flex:1 1 220px;font-size:14px}
/* messages */
#toast{left:50%;bottom:24px;transform:translateX(-50%);background:var(--up);color:var(--fg);border:1px solid var(--edge);border-left:4px solid var(--dim);padding:12px 18px;border-radius:12px;font-weight:600;max-width:min(90vw,560px);box-shadow:0 10px 30px rgba(0,0,0,.5);z-index:50;animation:rise .18s ease-out}
#toast.ok{border-left-color:var(--ok)}#toast.bad{border-left-color:var(--bad)}
@keyframes rise{from{opacity:0;transform:translate(-50%,8px)}}
#offline{display:none;background:var(--warnbg);color:var(--warn);padding:12px 16px;border-radius:10px;font-weight:600;margin-bottom:16px}
#alert{margin-bottom:16px}
.page.on{animation:fade .18s ease-out}
@keyframes fade{from{opacity:0;transform:translateY(4px)}}
/* what a page is for, and where things are on it */
.lead{color:var(--mid);margin:-8px 0 0;font-size:14px;max-width:72ch}
.otp{position:sticky;top:10px;z-index:20;align-self:flex-end;height:0;margin-bottom:-16px}
.otpb{min-height:36px;padding:0 14px 0 12px;font-size:13px;border-radius:999px;background:var(--card);color:var(--mid);box-shadow:0 2px 10px rgba(0,0,0,.45)}
.otpb::before{content:"";display:inline-block;width:12px;height:6px;margin-right:8px;border-top:2px solid currentColor;border-bottom:2px solid currentColor;vertical-align:middle}
.otp.show .otpb{color:var(--fg);border-color:var(--acc)}
/* scrolled past the title: it shrinks into the margin, clear of the cards */
.otp.mini{margin-right:-20px}.otp.mini .otpb{width:30px;min-height:30px;padding:0;font-size:0;border-radius:50%}.otp.mini .otpb::before{margin:0}
.jump{display:none;position:absolute;right:0;top:42px;flex-direction:column;gap:2px;min-width:230px;padding:6px;background:var(--card);border:1px solid var(--edge);border-radius:12px;box-shadow:0 12px 32px rgba(0,0,0,.55)}
.otp.show .jump{display:flex;animation:fade .12s ease-out}
.jump button{min-height:40px;padding:0 12px;font-size:14px;text-align:left;border:0;background:none;border-radius:8px;color:var(--dim);white-space:nowrap}
.jump button:hover{background:var(--up);filter:none}.jump button.open{color:var(--fg)}
.page.hasotp>.head{padding-right:150px;min-height:36px}
/* a long note: its first line, the rest on request */
.note{font-size:13px;color:var(--dim)}.card.com .note{color:var(--mid)}
.note summary{display:block;min-height:0;padding:2px 0;color:inherit;list-style:none}.note summary::-webkit-details-marker{display:none}
.note summary::after{content:"More";margin-left:8px;color:var(--acc);font-weight:600}.note[open] summary::after{content:"Less"}
.note summary:hover::after{color:var(--acchover)}.note ul{margin:6px 0 2px;padding-left:18px;display:flex;flex-direction:column;gap:4px}
/* cards that fold away */
.card.fold>:first-child{cursor:pointer;position:relative;padding-right:30px;user-select:none}
.card.fold>:first-child::after{content:"";position:absolute;right:6px;top:50%;width:8px;height:8px;border-right:2px solid var(--dim);border-bottom:2px solid var(--dim);transform:translateY(-70%) rotate(45deg);transition:transform .15s}
.card.fold.closed>:first-child::after{transform:translateY(-40%) rotate(-45deg)}
.card.fold.closed>:not(:first-child){display:none}
.card.fold.closed{padding-top:14px;padding-bottom:14px}
.card.fold>:first-child:hover h2,.card.fold>h2:first-child:hover{color:var(--fg)}
.card.fold.closed:hover{border-color:var(--edge)}
.card.flash{box-shadow:0 0 0 2px var(--acc)}
.card{transition:box-shadow .3s,border-color .12s}
#navstate{margin:0 12px 12px;align-self:flex-start;font-size:12px}
@media(max-width:760px){
.app{display:block}
nav{flex-direction:row;align-items:center;overflow-x:auto;position:sticky;top:0;z-index:20;background:var(--bg);padding:8px 10px;gap:2px;border-bottom:1px solid var(--line)}
nav b,#link{display:none}nav a{white-space:nowrap;padding:9px 12px}
#navstate{margin:0 6px 0 2px;order:-1;white-space:nowrap}
main{padding:14px 12px 96px}.big{font-size:38px}
nav>a{display:none}nav{justify-content:space-between}nav b{display:block;font-size:15px}
.page>.head #state{display:none}
#bnav{display:flex;position:fixed;left:0;right:0;bottom:0;z-index:40;background:var(--bg);border-top:1px solid var(--line);padding:4px 4px calc(4px + env(safe-area-inset-bottom))}
#bnav a,#bnav button{flex:1 1 0;text-align:center;padding:12px 2px;border-radius:10px;color:var(--dim);font-size:14px;background:none;border:0;min-height:48px}
#bnav a.on,#bnav button.on{color:var(--fg);background:var(--up);font-weight:600}
#more.open{display:flex}#unsaved{bottom:84px}.otp{top:62px}.otp.mini{margin-right:-10px}.card{padding:16px}
}
@media(prefers-reduced-motion:reduce){*{animation:none!important;transition:none!important}}

/* ---- shots: a list by day, one shot open at a time ---- */
.shots{display:flex;flex-direction:column}
.srow{display:grid;grid-template-columns:70px minmax(0,1fr) 62px 58px 72px;gap:10px;align-items:center;padding:9px 10px;border-bottom:1px solid var(--grid);font-size:14px}
.srow.shd{font-size:12px;color:var(--dim);border-color:var(--line);padding-top:0}
.srow .r{text-align:right;white-space:nowrap}.srow .off{color:var(--warn);font-weight:600}
.srow .nm{min-width:0}.srow .nm b{display:block;font-weight:600;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.srow .nm>span{display:block;font-size:13px;color:var(--dim);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.srow.pick{cursor:pointer;border-radius:8px;transition:background-color .12s}.srow.pick:hover{background:var(--hover2)}.srow.pick:active{background:var(--grid)}
.srow.pick.on{background:var(--up);border-bottom-color:transparent;border-radius:8px 8px 0 0}
.day{padding:16px 10px 6px;font-size:12px;font-weight:600;letter-spacing:.08em;text-transform:uppercase;color:var(--dim)}
.st{font-style:normal;color:var(--warn)}
.tag{font-style:normal;font-size:11px;padding:1px 7px;border-radius:999px;background:var(--warnbg);color:var(--warn)}.tag.q{background:var(--line);color:var(--mid)}
#sstash{display:none}
#sdetail{display:flex;flex-direction:column;gap:12px;padding:14px 14px 18px;background:var(--up);border-radius:0 0 8px 8px;margin-bottom:10px;animation:fade .18s ease-out}
#sdetail .tile{background:var(--card)}#sdetail .tiles{grid-template-columns:repeat(auto-fit,minmax(150px,1fr))}#sdetail .tile b{font-size:18px}#sdetail .tile a{text-decoration:underline dotted}
@media(max-width:760px){.srow{grid-template-columns:58px minmax(0,1fr) 56px 50px}.srow .g{display:none}#sdetail .tiles{grid-template-columns:1fr 1fr}}
body:not(.pn-insights) [data-panel~="insights"],body:not(.pn-discover) [data-panel~="discover"],body:not(.pn-subscriptions) [data-panel~="subscriptions"],body:not(.pn-community) [data-panel~="community"],body:not(.pn-logs) [data-panel~="logs"],body:not(.pn-hub) [data-panel~="hub"],body:not(.pn-start) [data-panel~="start"]{display:none!important}
.lbl{display:flex;flex-direction:column;gap:8px;margin-top:10px}.lbl .it{display:flex;gap:12px;align-items:center;flex-wrap:wrap;padding:10px 12px;border:1px solid var(--line);border-radius:10px}
.lbl .it .what{flex:1 1 240px;min-width:0}.lbl .it .what b{display:block}.lbl .dn{flex:1 1 100%;display:flex;gap:8px}.lbl .dn input{flex:1 1 auto}
.gsbar{height:6px;background:var(--up);border-radius:3px;overflow:hidden;margin-top:10px}.gsbar i{display:block;height:100%;background:var(--acc);border-radius:3px;transition:width .3s}
.gs{display:flex;flex-direction:column;gap:10px;margin-top:14px}.gs .st{display:flex;gap:14px;align-items:center;padding:12px 14px;border:1px solid var(--line);border-radius:12px}
.gs .st.done{opacity:.72}.gs .n{flex:0 0 32px;height:32px;border-radius:50%;display:flex;align-items:center;justify-content:center;font-weight:700;background:var(--up)}
.gs .st.done .n{background:var(--okbg);color:var(--oktext)}.gs .st.next{border-color:var(--acc)}.gs .tx{flex:1 1 0;min-width:0}.gs .tx b{display:block}.gs .tx .sm{margin-top:2px}
.gs .st button{flex:0 0 auto}@media(max-width:560px){.gs .st{flex-wrap:wrap;align-items:flex-start}.gs .st .tx{flex:1 1 calc(100% - 50px)}.gs .st button{margin-left:46px}}
.panels{display:grid;gap:10px;margin-top:8px}.panels label.chk{align-items:flex-start;padding:10px 12px;border:1px solid var(--line);border-radius:10px;margin:0}.panels label.chk b{display:block}
.empty{padding:14px 16px;border:1px dashed var(--line);border-radius:10px;color:var(--mid);font-size:13px;line-height:1.5}
/* On a phone the scrolled-away "On this page" button floats above the bottom bar instead of over the cards' edge. */
@media(max-width:760px){.otp.mini{position:fixed;top:auto;right:14px;bottom:calc(72px + env(safe-area-inset-bottom));height:auto;margin:0}
.otp.mini .otpb{width:44px;min-height:44px}.otp.mini .jump{top:auto;bottom:54px;max-height:60vh;overflow:auto}}
textarea{resize:vertical;min-height:96px;padding:10px 12px;line-height:1.45}
</style></head><body><div class="app">
<nav aria-label="Sections"><b>Brew by Weight</b><span class="pill" id="navstate" title="What the controller is doing">--</span>
<a href="#start" data-panel="start">Getting started</a><a href="#home">Home</a><a href="#shots">Shots</a><a href="#insights" data-panel="insights">Insights</a><a href="#coffees">Coffees</a><a href="#recipes">Recipes</a><a href="#grinder">Grinder</a><a href="#machine">Espresso machine</a><a href="#subscriptions" data-panel="subscriptions">Subscriptions</a><a href="#discover" data-panel="discover">Discover</a><a href="#community" data-panel="community">Community</a><a href="#setup">Setup</a><a href="#logs" data-panel="logs">Logs</a>
<div id="link"></div></nav>
<div id="bnav" role="navigation" aria-label="Sections"><a href="#home">Home</a><a href="#shots">Shots</a><a href="#coffees">Coffees</a><a href="#grinder">Grinder</a><button type="button" id="bmore" aria-expanded="false" aria-controls="more">More</button></div>
<div id="more" role="menu"><a href="#start" data-panel="start">Getting started</a><a href="#insights" data-panel="insights">Insights</a><a href="#recipes">Recipes</a><a href="#machine">Espresso machine</a><a href="#subscriptions" data-panel="subscriptions">Subscriptions</a><a href="#discover" data-panel="discover">Discover</a><a href="#community" data-panel="community">Community</a><a href="#setup">Setup</a><a href="#logs" data-panel="logs">Logs</a></div>
<main>
<div id="ctip" role="tooltip"></div>
<div id="askbox" role="dialog" aria-modal="true" aria-labelledby="asktxt"><div id="asktxt"></div><div class="row"><button id="askyes" class="pri">Yes</button><button id="askno">Cancel</button></div></div>
<div id="unsaved" role="alertdialog" aria-live="polite"><span id="unsavedtxt"></span><button class="pri" id="unsavedsave">Save</button><button id="unsaveddrop">Discard</button><button id="unsavedstay">Stay</button></div>
<div id="offline" role="status">Lost contact with the controller. Trying again...</div>
<div id="alert" role="alert"></div>

<div class="page" id="p-start" data-panel="start">
<div class="head"><h1>Getting started</h1><span class="pill" id="gsn">--</span></div>
<p class="sm dim">Everything a new controller needs, in order. Each step shows its state; tap a step to go to where it is set.</p>
<section class="card"><div class="head"><h2>Your setup</h2></div><div class="gsbar"><i id="gsbar" style="width:0"></i></div><div class="gs" id="gslist"></div></section>
<section class="card"><div class="head"><h2>When you want more</h2></div><div class="gs" id="gsmore"></div>
<p class="sm dim" style="margin-top:12px">Done with this guide? Hide it in <a href="#setup" onclick="setTimeout(()=>goCard('setup','Web page panels'),50)">Setup › Web page panels</a>; it can be turned back on there.</p></section>
</div>

<div class="page" id="p-home">
<div class="head"><h1>Home</h1><div class="row"><button id="hpower" class="pwr" style="display:none" onclick="homePower()">Espresso machine power</button><button id="hmstate" class="pill mstate" style="display:none" onclick="toggleMachineState()" title="What the controller believes, from the power bot's last press. Tap to correct it: nothing is pressed.">Espresso machine off</button><span class="pill" id="state">--</span></div></div>
<section class="card" id="hstart" data-panel="start" style="display:none"><div class="row" style="justify-content:space-between;margin:0"><span><b>Getting started</b> <span class="dim sm" id="hstartn"></span></span><button class="pri" onclick="location.hash='#start'">Continue</button></div><div class="gsbar"><i id="hstartbar" style="width:0"></i></div></section>
<section class="card"><div class="head">
<div class="live"><div><span class="big" id="w">--</span> <span class="dim">g</span></div><div><span class="big" id="t">--</span> <span class="dim">s</span></div><div><span class="big" id="f">--</span> <span class="dim">g/s</span></div></div>
<div class="row"><span class="mid" id="ihint"></span><button class="go" id="bstart" onclick="cmd('start')">Start</button><button class="stop" id="bstop" style="display:none" onclick="cmd('stop')">Stop</button><button id="btare" onclick="cmd('tare')">Tare</button><button id="back" style="display:none" onclick="cmd('ack')">Done with this shot</button></div></div>
<div class="row sm dim"><span><i class="dot" id="ds"></i>Scale <span id="sbat"></span></span><span><i class="dot" id="db"></i>Brew bot <span id="bat"></span></span><span id="cut"></span><span id="res"></span></div></section>
<div class="cols">
<section class="card"><div class="head"><h2>Brewing now</h2><a class="sm" href="#coffees">Coffees</a></div>
<label>What the controller will brew<select id="psel" onchange="act('preset_select',+this.value)"></select></label>
<div class="row" id="pchips"></div><div class="sm dim" id="pinfo"></div></section>
<div class="hstk"><section class="card tight"><div class="head"><div class="row"><h2>Grinder</h2><span><span class="num" id="hg">--</span> <span class="dim un">&micro;m</span></span><span class="pill" id="hgadv"></span></div><a class="sm" href="#grinder">Details</a></div>
<div class="sm mid"><span id="hgwhy"></span> <span class="dim" id="hgage"></span></div>
<div id="hgrec" class="row" style="display:none;border-top:1px solid var(--line);padding-top:10px;margin-top:8px"><span class="sm" id="hgrect" style="flex:1 1 220px"></span><button class="pri" id="hgrecu" onclick="answerRec(1)">Use</button><button id="hgreck" onclick="answerRec(0)">Keep</button></div></section>
<section class="card tight"><div class="head"><h2>Previous grinder recommendation</h2><a class="sm" href="#shots">All shots</a></div>
<div id="hprev" class="sm mid"><span class="dim">No shot yet.</span></div></section></div>
</div>
<section class="card"><div class="head"><h2>Most recent shot</h2><a class="sm" href="#shots">All shots</a></div>
<div id="hshot" class="dim">No shot yet.</div><canvas id="hchart" width="900" height="200"></canvas>
<div class="row sm dim" id="hrate"></div></section>
<section class="card" id="foryou" data-panel="discover" style="display:none"><div class="head"><h2>For you</h2><a class="sm" href="#discover">All recommendations</a></div><div class="why" id="fychips"></div></section>
<section class="card com" data-panel="community"><div class="head"><h2>Community</h2><a class="sm" href="#coffees" style="color:var(--com)">Coffees</a></div><div id="hcom" class="sm mid"></div></section>
</div>

<div class="page" id="p-shots">
<div class="head"><h1>Shots</h1><div class="row"><a href="/shots.csv"><button>Download CSV</button></a><a href="/api/shots?limit=500"><button>JSON</button></a></div></div>
<div class="tiles" id="ssum"></div>
<section class="card" id="cmpcard" style="display:none"><div class="head"><h2>Compare shots</h2><div class="row"><label class="chk"><input type="checkbox" id="cmpflow" onchange="drawCompare()">Flow</label><label class="chk"><input type="checkbox" id="cmpalign" onchange="drawCompare()">Line up at first drops</label><button onclick="CMP=[];drawCompare();cmpButton()">Clear</button></div></div><canvas id="cmpchart" width="900" height="260" style="height:260px"></canvas><div class="row sm" id="cmpleg"></div><div class="sm dim">Solid: weight in the cup. Dashed: flow (right-hand scale). Open a shot below and press "Add to comparison" to add it; up to 8.</div></section>
<section class="card"><div class="head"><h2>History</h2><div class="row">
<label>Coffee<select id="fcoffee" onchange="SHOWN=20;drawShots()"></select></label>
<label>Show<select id="fshow" onchange="SHOWN=20;drawShots()"><option value="0">All shots</option><option value="1">Stopped by weight</option><option value="2">Brew time off target</option><option value="3">Not rated yet</option></select></label></div></div>
<div class="shots" id="hist"></div>
<div class="row" id="smore"></div>
<div class="head sm dim"><span>Click a shot to open it. Brew times outside the tolerance are marked; every stored value is in the CSV.</span>
<button class="del" onclick="ask('Delete all shot history?').then(ok=>{if(ok){post('/api/history/clear').then(loadShots)}})">Clear history</button></div></section>
<div id="sstash"><div id="sdetail"><div class="head"><div><h2 id="swhen">No shot selected</h2><div style="font-size:19px;font-weight:650" id="sname"></div></div>
<div class="row"><button id="scmp" onclick="cmpToggle(SEL)">Add to comparison</button><button data-panel="community" onclick="toReddit(SEL)">Share to Reddit</button><span class="sm dim">For grind advice</span><span class="row" id="svote"></span></div></div>
<div class="tiles" id="stiles"></div><canvas id="schart" width="900" height="200"></canvas>
<div class="row sm dim"><span id="sdata"></span></div><div class="row sm dim" id="srate"></div></div></div>
</div>

<div class="page" id="p-coffees">
<div class="head"><h1>Coffees</h1><button class="pri" onclick="C=0;drawCoffees()">Add a coffee</button></div>
<div class="split"><section class="card side">
<label>Search by name, roaster, origin or process<input type="search" id="csearch" oninput="drawCoffeeList()" placeholder="e.g. Guji, washed"></label>
<div class="list" id="clist"></div><div class="sm dim" id="ccount"></div><div class="list" id="cshared"></div></section>
<div class="body">
<section class="card"><h2 id="ctitle">Coffee</h2>
<fieldset><legend>The coffee</legend><div class="grid">
<label>Name<input id="c_name" maxlength="23" placeholder="e.g. Idjwi Island"></label>
<label>Roaster<input id="c_roaster" maxlength="23" placeholder="e.g. Portrait"></label>
<label>Roast level<select id="c_roastLevel"><option value="0">not set</option><option value="1">light</option><option value="2">medium-light</option><option value="3">medium</option><option value="4">medium-dark</option><option value="5">dark</option></select></label>
<label style="grid-column:1/-1">Tasting notes<input id="c_notes" maxlength="47" placeholder="e.g. blueberry, cocoa"></label></div></fieldset>
<fieldset><legend>The bag in use</legend><div class="grid">
<label>Roast date printed on the bag<input id="c_roastDate" type="date"></label>
<label>Put in the freezer<input id="c_frozenDate" type="date"></label>
<label>Taken out (leave empty if still frozen)<input id="c_thawDate" type="date"></label></div></fieldset>
<fieldset><legend>Where it is from (optional)</legend><div class="grid">
<label>Origin (country)<input id="c_origin" maxlength="23"></label>
<label>Region<input id="c_region" maxlength="31"></label>
<label>Producer or washing station<input id="c_producer" maxlength="31"></label>
<label>Varietal<input id="c_varietal" maxlength="31"></label>
<label>Process<input id="c_process" maxlength="15" placeholder="washed, natural..."></label>
<label>Altitude (m)<input id="c_altitudeM" type="number" min="0" max="4000" step="10"></label></div></fieldset>
<div class="row"><button class="pri" data-save onclick="saveCoffee()">Save</button><button onclick="newBag()" id="cbag">New bag...</button><button id="cfin" onclick="finishBag()">Bag finished</button><span class="sm dim" id="cinfo"></span><button class="del" id="cdel" style="margin-left:auto" onclick="delCoffee()">Delete</button></div>
<div class="row" id="cbagrow" style="display:none;background:var(--up);border-radius:10px;padding:12px"><label style="flex:1 1 180px">Roast date printed on the new bag<input id="cbagdate" type="date"></label><label style="flex:1 1 160px">Opened (optional)<input id="cbagopen" type="date"></label><button class="pri" style="align-self:flex-end" onclick="startBag()">Start this bag</button><button style="align-self:flex-end" onclick="$('cbagrow').style.display='none'">Cancel</button></div>
<div id="cbags"></div>
<details class="note"><summary>A new bag keeps the coffee, recipes and grind setting, and starts a fresh run for grind advice.</summary><ul><li>Earlier bags are kept below: put one back in use if you go back to it.</li><li>The dates above are those of the bag in use; correcting them corrects that bag.</li><li>Days in the freezer are not counted as age: enter the day the bag went in and the day it came out.</li></ul></details>
<div class="row" id="cshrow" style="border-top:1px solid var(--line);padding-top:12px"><span class="pill" id="cshpill" style="background:var(--combg);color:#8cc4ff;border:1px solid var(--comline)"></span><span class="sm mid" id="cshare" style="flex:1 1 260px"></span><button id="csend" onclick="sendCoffee()">Send to the shared library</button></div></section>
<section class="card" id="crate" style="display:none"><div class="head"><h2>How you liked it</h2><span class="sm dim" id="crinfo"></span></div>
<div class="row" id="crwho"></div>
<div class="row" id="crstars" role="group" aria-label="Stars"></div>
<div class="grid" id="crattr"></div>
<div class="subh">Flavours you tasted</div><div class="row" id="crfl"></div>
<div class="row"><button class="pri" onclick="saveRating()">Save rating</button><button onclick="RS=null;drawRating()">Clear</button><span class="sm dim" id="crnote"></span></div>
<div class="list" id="crlist"></div>
<details class="note"><summary>For the coffee, not one shot: how you liked this bag. Rate again to change it.</summary><ul><li>Stars are needed; sliders and flavours are optional (0 means not given).</li><li>Ratings build your taste on Insights and the coffees recommended to you.</li><li>With named drinkers on (Setup), each rating says who gave it.</li></ul></details></section>
<section class="card" id="cpairs"><div class="head"><h2>Brew it with</h2><a class="sm" href="#recipes">Recipes</a></div>
<div class="list" id="cplist"></div><div class="sm dim" id="cpinfo"></div></section>
<section class="card"><h2>Add coffees from elsewhere</h2><div class="row"><label style="flex:1 1 240px">Look up a coffee on visualizer.coffee<input id="vzq" placeholder="coffee or roaster" onkeydown="if(event.key==='Enter')vzLookup()"></label><button style="align-self:flex-end" onclick="vzLookup()">Search</button></div><div id="vzres" class="sm mid"></div><details class="note"><summary>Fills the form above with what that shared database knows about the coffee.</summary><ul><li>You add the roast date and your own notes, then Save.</li><li>Needs a phone that is online.</li><li>The data is community-contributed and has its own terms.</li></ul></details><div class="row" style="margin-top:12px"><label style="flex:1 1 240px">Bring in coffees from a Beanconqueror backup<input type="file" accept=".json,application/json" onchange="importBeanconqueror(this)"></label><label class="chk" style="align-self:flex-end"><input type="checkbox" id="bcfin">include finished beans</label></div><details class="note"><summary>Adds the beans in a Beanconqueror export file that you do not have yet.</summary><ul><li>They come with their roast dates, opening dates, weights and any time in the freezer.</li><li>The file stays in your browser.</li><li>Shots are not imported: Beanconqueror does not know this machine's timing, so they would only confuse the grind advice.</li></ul></details></section>
<section class="card com" id="dircard" data-panel="community"><div class="head"><h2>Coffee directory</h2><span class="sm dim" id="dircount"></span></div>
<div class="sm mid">Every coffee published in the shared library, kept on the controller, so it works without the internet too.</div>
<div class="row" style="margin-top:10px"><label style="flex:2 1 220px">Search<input type="search" id="dirq" placeholder="name, roaster, origin, notes" oninput="clearTimeout(DIRT);DIRT=setTimeout(()=>{DIRP=0;loadDirectory()},250)"></label>
<label style="flex:1 1 130px">Origin<select id="diro" onchange="DIRP=0;loadDirectory()"></select></label>
<label style="flex:1 1 130px">Process<select id="dirp" onchange="DIRP=0;loadDirectory()"></select></label>
<label style="flex:1 1 130px">Roast<select id="dirr" onchange="DIRP=0;loadDirectory()"></select></label></div>
<div id="dirlist" style="margin-top:10px"></div><div class="row" id="dirpager"></div></section>
<section class="card com" data-panel="community"><h2>Community dial-in</h2><div id="ccom" class="sm mid"></div></section>
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
<label title="A new coffee on this recipe starts from what the recipe has learned on its other coffees; this is used until it has learned one">Starting stop offset, until learned (g)<input id="r_baselineOffsetG" type="number" step="0.1" min="0" max="10"></label></div></fieldset>
<div class="row"><button class="pri" data-save onclick="saveRecipe()">Save</button><button id="rshare" onclick="shareRecipe()">Share to the recipe book</button><span class="sm dim" id="rinfo"></span></div></section>
<section class="card" id="rused"><div class="head"><h2>Used with</h2><a class="sm" href="#coffees">Add to a coffee</a></div>
<div class="scroll"><table id="rtable"></table></div>
<div class="row"><button id="ralone" onclick="act('preset_add',R,0)">List it on the controller by itself</button></div>
<div class="sm dim">The controller lists coffee and recipe as one line to tap. A recipe listed by itself is for guests and one-off bags.</div></section>
</div></div></div>

<div class="page" id="p-grinder">
<div class="head"><h1>Grinder</h1><span class="sm dim" id="gfor"></span></div>
<div class="cols"><section class="card"><h2>Setting now</h2>
<div><span class="big" id="gcur">--</span> <span class="dim un">&micro;m</span></div>
<div class="row"><label style="flex:1 1 150px"><span>I moved the grinder to (<span class="un">&micro;m</span>)</span><input id="gset" inputmode="decimal" autocomplete="off" placeholder="as the dial shows"></label><button style="align-self:flex-end" onclick="setGrind(ps($('gset').value))">Set</button></div>
<div class="sm dim">Shots are only compared with others made at the same setting.</div></section>
<section class="card"><h2>Advice</h2><div><span class="pill" id="gadv" style="font-size:16px"></span></div>
<div class="sm mid" id="gwhy"></div><div class="sm mid" id="gage"></div><div class="sm mid" id="grate"></div><div class="row"><button class="pri" id="gapply" style="display:none" onclick="setGrind(G.suggested_um)"></button></div></section></div>
<section class="card"><div class="head"><h2>Brew time against grind setting</h2><div class="row"><span class="sm dim" id="gsens"></span><label class="chk sm"><input type="checkbox" id="gall" onchange="drawScatter()">All coffees</label></div></div>
<canvas id="gchart" width="900" height="220" style="height:220px"></canvas><div class="row sm" id="gleg"></div><div class="sm mid" id="gnote"></div></section>
<section class="card"><div class="head"><h2>Grind setting, shot by shot</h2><span class="sm dim" id="cnote"></span></div><canvas id="cchart" width="900" height="220" style="height:220px"></canvas><div class="row sm" id="cleg"></div><details class="note"><summary>Each shot's setting, oldest first: how each coffee was dialled in and drifted as it aged.</summary><ul><li>A line per coffee, in its colour (named below the chart); the one being brewed now is orange.</li><li>Dots: green on time, yellow fast, red slow, each against that shot's own target and the tolerance.</li><li>Dashed lines: a new bag of that coffee. Dates along the bottom mark the first shot of each day.</li><li>White rings: shots you rated 4 or 5. Hover or tap a dot to see the shot; click it to open it.</li></ul></details></section>
<section class="card"><h2>How advice is worked out</h2><div class="grid">
<label>Brew time tolerance (&plusmn; s)<input id="grindToleranceS" type="number" step="0.5" min="0.5" max="10"></label>
<label>Shots averaged<input id="grindWindow" type="number" min="1" max="10"></label>
<label>Ratings<select id="grindRatedTarget"><option value="1">Refine the advice towards the shots I rated 4 or 5 stars, inside its tolerance</option><option value="0">Off: time advice only</option></select></label>
<label>Confidence<select id="grindConfidence"><option value="1">Size each change by how well past advice landed</option><option value="0">Off: the same size every time</option></select></label>
<label><span>Starting guess (<span class="un">&micro;m</span> per s)</span><input id="grindUmPerS" type="number" step="0.5" min="0.5" max="20"></label>
<label><span>Largest single change (<span class="un">&micro;m</span>)</span><input id="grindMaxStepUm" type="number" min="1" max="100"></label>
<label>Brew time measured<select id="grindTimeBasis"><option value="0">Total: start press to stop (with pre-infusion)</option><option value="1">Pour: first drops to stop</option></select></label></div>
<div class="row"><button class="pri" data-save onclick="saveSettings(this.closest('.card'))">Save</button><span class="sm dim">Changing how brew time is measured starts a fresh comparison: set each recipe's target time to match.</span></div></section>
<section class="card"><div class="head"><h2>Grinder upkeep</h2><span class="sm dim" id="gu_total"></span></div><div id="gu_rows"></div><details class="note"><summary>Cleaning is counted in shots, like the machine's upkeep. Zeroing starts a fresh comparison of settings.</summary><ul><li>After zeroing the grinder again (or changing burrs), settings from before are no longer compared with new ones: grind advice starts fresh for every coffee.</li><li>The date can be corrected under My grinder.</li></ul></details></section>
<section class="card"><h2>My grinder</h2><div class="grid">
<label>Model<select id="gm_model" onchange="grinderForm()"></select></label>
<label id="gm_namel">Name<input id="gm_name" maxlength="31" placeholder="e.g. My hand grinder" oninput="gmSummary()"></label>
<label><span id="gm_kindl">How it adjusts</span><select id="gm_kind" onchange="gmSummary()"><option value="steps">Steps or clicks</option><option value="dial">Stepless dial</option><option value="um">Microns (display)</option></select></label>
<label id="gm_divl">Read it to the nearest<select id="gm_divisor" onchange="gmSummary()"><option value="1">Whole number (12)</option><option value="2">0.5 (12.5)</option><option value="4">0.25 (12.25)</option><option value="5">0.2 (12.2)</option><option value="10">0.1 (12.1)</option><option value="20">0.05 (12.05)</option><option value="25">0.04 (12.04)</option><option value="50">0.02 (12.02)</option><option value="100">0.01 (12.01)</option></select></label>
<label id="gm_unitl">A setting is called (optional)<input id="gm_unit" maxlength="7" list="gm_units" placeholder="step" oninput="gmSummary()"><datalist id="gm_units"><option value="step"><option value="click"><option value="setting"><option value="number"><option value="dial"></datalist></label>
<label><span id="gm_minl">Finest setting</span><input id="gm_min" inputmode="decimal" placeholder="1" oninput="gmSummary()"></label>
<label><span id="gm_maxl">Coarsest setting</span><input id="gm_max" inputmode="decimal" placeholder="e.g. 60" oninput="gmSummary()"></label>
<label>The range covers<select id="gm_covers" onchange="gmSummary()"><option value="0">Not said</option><option value="2">Espresso to filter</option><option value="1">Espresso only</option></select></label>
<label>Last zeroed, recalibrated or burrs changed<input id="gm_zeroDate" type="date"></label></div>
<div id="gm_micro" class="grid" style="display:none;border-top:1px solid var(--line);padding-top:12px">
<label>Micro adjustment: how it adjusts<select id="gm_mkind" onchange="gmSummary()"><option value="steps">Steps or clicks</option><option value="dial">Stepless</option></select></label>
<label><span id="gm_mlab">Steps per macro step</span><input id="gm_subSteps" type="number" min="2" max="99" step="1" placeholder="e.g. 9" oninput="gmSummary()"></label>
<div class="row" style="align-self:end"><button class="del" onclick="gmMicro(false)">Remove the micro adjustment</button></div></div>
<div class="row"><button id="gm_addmicro" onclick="gmMicro(true)">+ Add a micro adjustment</button><span class="sm dim" id="gm_microhint">For a second ring or lever (macro and micro, like a Sette or Vario). An inner burr you set once, like the Smart Grinder Pro's top burr, is not one: when you move it, press "Zeroed today" under Grinder upkeep.</span></div>
<div class="sm mid" id="gm_sum" style="margin:6px 0"></div>
<div class="row"><button class="pri" data-save onclick="saveGrinder(false)">Save</button><button id="gm_addother" onclick="saveGrinder(false,true)">Add what is typed above as my second grinder</button></div>
<div class="sm dim" id="gm_info"></div>
<div class="row" id="gm_shrow" style="border-top:1px solid var(--line);padding-top:12px"><span class="pill" id="gm_shpill" style="background:var(--combg);color:#8cc4ff;border:1px solid var(--comline)"></span><span class="sm mid" id="gm_share" style="flex:1 1 260px"></span><button id="gm_send" onclick="sendGrinder(L.grinder.id)">Send to the shared library</button></div>
<div class="row" id="gm_other" style="border-top:1px solid var(--line);padding-top:12px"></div>
<details class="note"><summary>Only what grind advice needs is asked for.</summary><ul><li>How far one step moves the shot is learned from your own shots, so burrs and microns per step are not needed.</li><li>The range keeps advice inside the dial's ends; what it covers gives a first guess when nothing else is known.</li><li>Zeroing again, other burrs or another model start grind advice afresh: they change what a setting means, so shots from before are no longer compared with new ones.</li><li>Naming the grinder for the first time changes nothing.</li><li>A second grinder keeps its own setting for every coffee, and its own shots; switching between the two loses neither.</li></ul></details></section>
<section class="card com"><h2>Shared grinder list</h2><details class="note"><summary>The model list holds only grinders with a known profile: the firmware's own, and those the shared library has published.</summary><ul><li>A listed model can be changed and saved: it is then kept as your own version.</li><li>A grinder on neither list can be typed in and kept here, or sent to the library for a reviewer to add.</li><li>The next owner of one then picks it from the list, and settings can be compared among everyone who has it.</li><li>Settings are translated between grinders through what each is read in and the coffees their owners have in common.</li></ul></details></section>
</div>

<div class="page" id="p-insights">
<div class="head"><h1 id="ititle">Insights</h1><label class="sm" data-panel="insights">Coffee<select id="icoffee" onchange="drawInsights()"></select></label></div>
<section class="card" id="itaste" data-panel="insights"><div class="head"><h2>Your coffee taste</h2><div class="row" id="itwho" style="margin:0"></div></div>
<div class="two"><div><div class="sm dim">The cup</div><canvas id="itcup" width="440" height="340" style="height:auto"></canvas></div><div><div class="sm dim">Flavours</div><canvas id="itfl" width="440" height="340" style="height:auto"></canvas></div></div>
<div class="sm" id="itnote"></div><div id="itbest"></div>
<details class="note"><summary>From your coffee ratings: coffees you loved pull it their way, ones you disliked push it away.</summary><ul><li>The middle ring is no lean either way; further out is more of it, further in less.</li><li>A pale point says little is known about it yet. The profile shows from 3 rated coffees and is settled from 8.</li><li>Values you did not give are filled in from each coffee's tasting notes, process and roast level, with less weight.</li></ul></details></section>
<section class="card" data-panel="insights"><div class="head"><h2>Roast level against grind setting</h2><span class="sm dim" id="irnote"></span></div><canvas id="irchart" width="900" height="240" style="height:240px"></canvas><div class="row sm" id="irleg"></div><details class="note"><summary>One dot per shot on this grinder: its setting and roast level, coloured by the coffee's age.</summary><ul><li>Blue is fresh, red is old; grey: no roast date. A larger dot poured faster.</li><li>The green band on each roast level: the settings whose shots ran on time (within the tolerance of their target time); the line marks the middle one.</li><li>Hover or tap a dot to see the shot; click it to open it.</li></ul></details></section>
<section class="card" data-panel="insights"><div class="head"><h2>Coffee age against brew time</h2><span class="sm dim" id="ianote"></span></div><canvas id="iachart" width="900" height="260" style="height:260px"></canvas><div class="row sm" id="ialeg"></div><details class="note"><summary>Brew time against target, by days off roast. Darker squares hold more shots.</summary><ul><li>Rows above the outlined band ran slow, rows below ran fast.</li><li>Shots without a roast date or a target time are left out.</li></ul></details></section>
<section class="card" data-panel="insights"><div class="head"><h2>Brewing calendar</h2><div class="row" id="icmode" role="group" aria-label="What the calendar shows"><button data-m="shots" onclick="CALMODE='shots';drawCalendar()">Shots</button><button data-m="brew" onclick="CALMODE='brew';drawCalendar()">Coffee &amp; recipe</button><button data-m="drift" onclick="CALMODE='drift';drawCalendar()">Drift</button><button data-m="hours" onclick="CALMODE='hours';drawCalendar()">Time of day</button></div></div><div class="scroll"><canvas id="icchart" width="900" height="180" style="height:180px;min-width:620px"></canvas></div><div class="row sm" id="icleg"></div></section>
<section class="card" data-panel="insights"><div class="head"><h2>Dial-in shots per bag</h2><span class="sm dim" id="idnote"></span></div><canvas id="idchart" width="900" height="240" style="height:240px"></canvas><div class="row sm" id="idleg"></div><details class="note"><summary>Per bag: shots to the first on-time shot, then how the rest ran.</summary><ul><li>On time: within the tolerance of that shot's target. Only shots stopped by weight count.</li><li>The shots after the first on-time one are coloured by how they ran: green on time, yellow fast, red slow. A dashed bar: no shot on time yet.</li></ul></details></section>
<section class="card" data-panel="insights"><div class="head"><h2>Grind advice against target</h2><span class="sm dim" id="ignote"></span></div><div class="tiles" id="igtiles" style="margin-bottom:10px"></div><canvas id="igchart" width="900" height="240" style="height:240px"></canvas><div class="row sm" id="igleg"></div><details class="note"><summary>Each dot is a change the advice asked for, placed by how the next shot ran against its target.</summary><ul><li>Inside the green band the change landed within tolerance; above it the shot still ran slow, below it fast.</li><li>Grey rings: the grinder went somewhere other than advised; they do not count.</li><li>The orange line is the confidence (right axis), from the followed changes before each one. Low confidence makes the next change smaller with a wider \u00b1 window; high confidence allows a bigger change, past the usual largest step, with a tighter window.</li></ul></details></section>
</div>
<div class="page" id="p-machine">
<div class="head"><h1>Espresso machine</h1></div>
<section class="card" id="pwcard"><div class="head"><h2>Machine power</h2><span class="pill" id="pwstate"></span></div>
<div id="pw" class="sm mid"></div>
<div class="row"><button class="pri" onclick="power('on')">Switch on</button><button onclick="power('off')">Switch off</button></div>
<div class="row sm dim" style="gap:6px;align-items:center">Not right? It is really<button class="sm" onclick="power('believe_on')">on</button><button class="sm" onclick="power('believe_off')">off</button><span>·</span><button class="sm" onclick="power('press')">Press the button anyway</button></div>
<details class="note"><summary>Switched by hand, or it switched itself off? Say how it is: nothing is pressed.</summary><ul><li>The power button is a toggle and the machine does not say whether it is on, so the controller keeps count.</li><li>"Press the button anyway" presses it whatever the controller believes.</li></ul></details>
<div class="subh">Schedule</div><div class="scroll"><table id="pws"></table></div>
<div class="row"><button class="sm" id="pwadd" onclick="addPowerTime()">Add a time</button><button data-save onclick="savePowerSchedule()">Save schedule</button><span class="sm dim">This browser's local time</span></div>
<details class="note"><summary>A time is skipped if the machine is already that way, or a shot is running.</summary><ul><li>Saving stores this browser's time zone: save again after a daylight-saving change.</li></ul></details>
<div class="subh">Options</div>
<label class="chk"><input type="checkbox" id="keepPowerBotConnected" onchange="post('/api/settings',{keepPowerBotConnected:this.checked}).then(r=>done(r,'Saved'))"><span>Keep the power bot connected while the scale is off <span class="dim">(presses at once; uses more battery)</span></span></label>
<label class="chk"><input type="checkbox" id="powerOffWithScale" onchange="post('/api/settings',{powerOffWithScale:this.checked}).then(r=>done(r,'Saved'))"><span>Switch the machine off when the scale switches off <span class="dim">(only when it is known to be on)</span></span></label>
<div class="row" style="align-items:center;gap:8px;margin:6px 0 0"><span>Let the scale sleep after</span><input id="scaleRestMin" aria-label="Minutes idle before the scale may sleep" type="number" min="0" max="60" step="1" style="width:80px" onchange="post('/api/settings',{scaleRestMin:+this.value}).then(r=>{done(r,'Saved');loadSettings()})"><span>minutes idle</span><span class="sm dim">(0: keep it connected)</span></div>
<details class="note"><summary>An Acaia scale only counts down to sleep while nothing is connected to it, so the controller lets go of it.</summary><ul><li>The brew bot is let go with it; the power bot connects once the scale is off.</li><li>Touching the screen, the touch key or this page keeps it connected. The touch key while it rests takes it back and starts the shot once it is there.</li></ul></details></section>
<section class="card"><div class="head"><h2>Upkeep</h2><span class="sm dim" id="mt_total"></span></div><div id="mt_rows"></div><details class="note"><summary>Shots counted since each job was last done.</summary><ul><li>A shot counts from 12 s and 2 g.</li><li>The controller cannot tell from the cup when a machine needs descaling or a grinder cleaning, so it counts.</li><li>You choose how often to be reminded; no interval is set until you set one.</li></ul></details></section>
<section class="card"><div class="head"><h2>Make and model</h2></div><div id="mc" class="mid"></div><div class="grid"><label>Machine<select id="mc_pick" onchange="pickMachine()"></select></label><label>Maker<input id="mc_maker" maxlength="23"></label><label>Model<input id="mc_model" maxlength="31"></label><label>Control<input id="mc_control" maxlength="31"></label><label>How a shot is run<select id="mc_style"><option value="toggle_button">One button: press to start, press to stop</option><option value="start_button">A button starts; the machine stops itself</option><option value="start_stop_buttons">Two buttons: start and stop (adds a stop bot)</option><option value="hold_button">One button held for the whole shot (SwitchBot)</option><option value="manual">By hand: lever, knob or paddle</option></select></label><label>By hand, the clock starts<select id="mc_start"><option value="tap">when Start is tapped</option><option value="first_drip">with the first coffee on the scale</option></select></label><label>Holding the button pre-infuses<select id="mc_hold"><option value="0">No</option><option value="1">Yes</option></select></label><label>A tap uses its own pre-infusion<select id="mc_tap"><option value="0">No</option><option value="1">Yes</option></select></label><label>Power button toggles<select id="mc_pow"><option value="0">No</option><option value="1">Yes</option></select></label><label>Stops a shot itself after (s, 0 never)<input id="mc_self" type="number" min="0" max="600"></label><label title="The controller cannot see the machine. After this long with nothing done on it, it counts the machine as off, so a scheduled or asked-for 'on' presses the button again. Breville Dual Boiler: 240 (it switches itself off after 4 hours unused), or 0 if Auto Off was turned off in its menu (A OF).">Switches itself off after (min idle, 0 never)<input id="mc_aoff" type="number" min="0" max="1440"></label></div><div class="row"><button class="pri" data-save onclick="saveMachine()">Save</button><button id="mc_send" onclick="shareMachine()">Send to the shared library</button></div><details class="note"><summary>Change anything and Save; a changed listed machine becomes your own version.</summary><ul><li>Set a machine up once and send it: the next owner of the same machine picks it from the list.</li><li>The same machine worked by another button is another entry.</li><li>Where the bot does not stop the machine, the target weight is a cue: the screen says STOP NOW (and the scale sounds, if it can be told to) and the shot ends when the pour does.</li><li>Held pre-infusion and bloom in a recipe are only used on a machine whose button holds pre-infusion.</li></ul></details></section>
</div>
<div class="page" id="p-subscriptions">
<div class="head"><h1>Subscriptions</h1><button class="pri" onclick="subEdit(0)">Add a subscription</button></div>
<div class="grid" id="subsum"></div>
<div class="list" id="subrem"></div>
<section class="card" id="subext"><div class="head"><h2>Update from the shops</h2><span class="sm dim" id="sxstate"></span></div><div id="sxbody"></div></section>
<section class="card" id="subimp" style="display:none"><div class="head"><h2>From the shops: check and apply</h2><button class="tbtn" onclick="IMP={};drawImport()">Dismiss</button></div><div id="sibody"></div>
<div class="row"><button class="pri" onclick="applyImport()">Apply what is ticked</button><span class="sm dim">Nothing changes until you apply.</span></div></section>
<div class="subs" id="sublist"></div>
<section class="card com" id="sublib"><div class="head"><h2>From the shared library</h2><span class="sm" id="slcount"></span></div>
<label>Search services<input type="search" id="slq" oninput="drawLib()" placeholder="name, style, sourcing"></label>
<div class="subs" id="sllist"></div></section>
<section class="card" id="subform" style="display:none"><div class="head"><h2 id="sftitle">Add a subscription</h2><button class="tbtn" onclick="subClose()">Close</button></div>
<div class="row"><label style="flex:1 1 280px">Link to the shop or the subscription<input id="sf_url" type="url" placeholder="https://" onkeydown="if(event.key==='Enter')subRead()"></label><button style="align-self:flex-end" onclick="subRead()">Read the page</button></div>
<div class="sm dim" id="sfread"></div>
<div class="two"><div class="subs" id="sfprev" style="grid-template-columns:1fr"></div><div class="grid" style="align-content:start">
<label>Name<input id="sf_name" maxlength="47" oninput="subPrev()"></label>
<label>What it is<select id="sf_kind" onchange="subKind();subPrev()"><option value="coffee">Coffee</option><option value="supplies">Supplies (filters...)</option><option value="other">Other</option></select></label>
<label>Status<select id="sf_status" onchange="subPrev()"><option value="active">Active</option><option value="paused">Paused</option><option value="cancelled">Cancelled</option></select></label>
<label>Every<span class="row" style="flex-wrap:nowrap"><input id="sf_every" type="number" min="0" max="366" style="width:90px" oninput="subPrev()"><select id="sf_unit" onchange="subPrev()"><option value="7">weeks</option><option value="1">days</option><option value="30.44">months</option></select></span></label>
<label>Next charge or delivery<input id="sf_next" type="date" onchange="subPrev()"></label>
<label>Price each time ($)<input id="sf_price" type="number" min="0" step="0.01" oninput="subPrev()"></label></div></div>
<div class="grid" id="sfsup"><label>Item<input id="sf_item" maxlength="39" placeholder="e.g. paper filters"></label><label>In stock now<input id="sf_stock" type="number" min="0"></label><label>Used per shot<input id="sf_pershot" type="number" min="0" max="10" placeholder="1"></label><label>Each delivery brings<input id="sf_perdel" type="number" min="0"></label></div>
<label>Where you manage it (the shop's subscription page)<input id="sf_manage" type="url" placeholder="https://"></label>
<details class="note" id="sfshared"><summary>For the shared library: what it offers, where it ships, how it sources</summary><div class="grid" style="margin-top:8px">
<label>Ships to (country codes)<input id="sf_ships" placeholder="e.g. US, CA"></label><label>Deliveries offered (every N weeks)<input id="sf_cad" placeholder="e.g. 1, 2, 4"></label>
<label>Typical price each delivery<input id="sf_pfrom" type="number" min="0" step="0.01"></label><label>Currency<input id="sf_cur" maxlength="3" placeholder="USD"></label></div>
<div class="subh">Style</div><div class="chips" id="sf_style"></div><div class="subh">Sourcing it claims</div><div class="chips" id="sf_sourcing"></div>
<div class="sm dim" style="margin-top:6px">Claims are checked against the shop's site by the library's reviewer before it is listed. Your own price, dates, stock and notes are never sent.</div></details>
<div class="row" id="sfsharerow" style="border-top:1px solid var(--line);padding-top:12px"><span class="pill" id="sfsharepill" style="background:var(--combg);color:var(--comtext);border:1px solid var(--comline)"></span><span class="sm mid" id="sfsharetxt" style="flex:1 1 240px"></span><button id="sfsend" onclick="subShare()">Send to the shared library</button></div>
<div id="sfreview" style="border-top:1px solid var(--line);padding-top:12px;display:flex;flex-direction:column;gap:10px"><div class="head"><h2>Your review</h2><span class="sm dim" id="svinfo"></span></div>
<div class="row" id="svwho"></div><div class="row" id="svstars"></div><div class="grid" id="svasp"></div>
<label>Note (stays on this controller)<input id="sv_note" maxlength="39"></label>
<div class="row"><button class="pri" onclick="subReviewSave()">Save review</button><button class="del" id="svdel" onclick="subReviewDel()">Delete review</button></div><div class="list sm" id="svlist"></div></div>
<label>Note<input id="sf_note" maxlength="95" placeholder="e.g. 2 bags, whole bean"></label>
<details class="note"><summary>Card look</summary><div class="grid" style="margin-top:8px"><label>Picture<input id="sf_image" type="url" oninput="subPrev()"></label><label>Icon<input id="sf_icon" type="url" oninput="subPrev()"></label><label>Colour<input id="sf_color" type="color" value="#5b6470" oninput="subPrev()"></label></div></details>
<div class="row"><button class="pri" data-save onclick="subSave()">Save</button><button onclick="subClose()">Cancel</button><button class="del" id="sfdel" style="margin-left:auto" onclick="subDel()">Delete</button></div></section>
<section class="card" id="subarr" style="display:none"><div class="head"><h2 id="satitle">A delivery arrived</h2><button class="tbtn" onclick="$('subarr').style.display='none'">Close</button></div>
<div class="grid"><label>Which coffee<select id="sa_coffee" onchange="subArrKind()"></select></label>
<label>Roast date printed on the bag<input id="sa_roast" type="date"></label><label>Weight (g, optional)<input id="sa_weight" type="number" min="0" max="5000"></label></div>
<div class="grid" id="sanew"><label>Name<input id="sa_name" maxlength="23"></label><label>Roaster<input id="sa_roaster" maxlength="23"></label><label>Tasting notes<input id="sa_notes" maxlength="47"></label></div>
<div class="row"><button class="pri" data-save onclick="subArrSave()">Put it on the shelf</button><span class="sm dim">Unopened: nothing is tracked until you open it, or it comes up next by roast date when a bag is finished.</span></div></section>
<details class="note"><summary>Why skip and pause happen on the shop's page.</summary><ul><li>Shops run subscriptions on systems (Recharge, Ordergroove and the like) that only the shop can reach: there is no way in for your controller.</li><li>So the card links to where you manage it, and keeps its own track here: when it comes, what it costs, how much is left.</li><li>Arrived and Skip move the date on here. Skipping at the shop is still yours to do.</li><li>The picture and name come from the shop's page, read once by the controller when you add the link.</li></ul></details>
</div>
<div class="page" id="p-discover" data-panel="discover">
<div class="head"><h1>Discover</h1><div class="row" id="dwho" style="margin:0"></div></div>
<section class="card" id="ireco"><div class="head"><h2>Coffees you might like</h2><span class="sm dim" id="irwho"></span></div>
<div class="recos" id="irlist"></div><div class="sm" id="iragain" style="margin-top:10px"></div>
<details class="note"><summary>Coffees you have not rated, ranked by how well they match your taste.</summary><ul><li>A coffee's taste comes from the library's ratings when there are enough, else from its tasting notes, process and roast (less sure).</li><li>With ratings shared, drinkers whose stars agree with yours count too.</li><li>Shown once 3 coffees are rated. Fixed rules on the controller; never touches the grind advice.</li></ul></details></section>
<section class="card" id="subreco"><div class="head"><h2>Subscriptions you might like</h2><span class="sm dim" id="srbasis"></span></div>
<div class="row" style="align-items:flex-end"><label style="flex:0 0 130px">Your country<input id="sr_country" maxlength="2" placeholder="e.g. US" onchange="subPrefs()"></label><div style="flex:1 1 300px"><div class="sm dim" style="margin-bottom:6px">What matters to you</div><div class="chips" id="srmatters"></div></div></div>
<div class="subs" id="srlist"></div></section>
<section class="card com" id="lbcoffee"><div class="head"><h2>Coffee library</h2><div class="row" role="group" aria-label="Which coffees" style="margin:0"><button id="lbc_t0" class="on" onclick="lbTab('c',0)">Published <span class="dim" id="cmcount"></span></button><button id="lbc_t1" onclick="lbTab('c',1)">Waiting for review <span class="dim" id="lbc_n"></span></button></div></div>
<div id="lbc_0"><label>Search<input type="search" id="cmq" oninput="drawCommunityCoffees()" placeholder="name, roaster, origin, process"></label><div id="cmlist" style="margin-top:10px"></div></div>
<div id="lbc_1" style="display:none"><div class="sm mid" id="lbc_why"></div><div id="lbc_list" class="lbl"></div></div></section>
<section class="card com" id="lbrecipe"><div class="head"><h2>Recipe book</h2><div class="row" role="group" aria-label="Which recipes" style="margin:0"><button id="lbr_t0" class="on" onclick="lbTab('r',0)">Published <span class="dim" id="lbr_c"></span></button><button id="lbr_t1" onclick="lbTab('r',1)">Waiting for review <span class="dim" id="lbr_n"></span></button></div></div>
<div id="lbr_0"><label>Search<input type="search" id="lbrq" oninput="drawRecipeBook()" placeholder="name, dose, yield"></label><div id="lbr_list" class="lbl" style="margin-top:10px"></div></div>
<div id="lbr_1" style="display:none"><div class="sm mid" id="lbr_why"></div><div id="lbr_pend" class="lbl"></div></div>
<details class="note"><summary>Owners review what other owners send.</summary><ul><li>An item is published once two owners approve it; one decline holds it for the library's maintainer.</li><li>Your reviews count once something you shared has been published. You never review your own.</li><li>Share one of your recipes from the Recipes page.</li></ul></details></section>
</div>
<div class="page" id="p-community" data-panel="community">
<div class="head"><h1>Community</h1><a class="sm" href="#setup" onclick="setTimeout(()=>{const e=$('sh_state');if(e)e.closest('.card').scrollIntoView({behavior:'smooth'})},300)" style="color:var(--com)">Sharing settings</a></div>
<section class="card com" id="cmfeedcard"><div class="head"><h2>Community feed</h2><button class="tbtn" onclick="lkToggle()">Suggest a link</button></div>
<div class="sm mid">Videos, posts and guides about espresso that owners suggested and a reviewer approved.</div>
<div class="chips" id="cmtopics" style="margin-top:10px"></div>
<div class="recos" id="cmfeed" style="margin-top:10px"></div>
<div id="lkform" style="display:none;margin-top:14px;border-top:1px solid var(--line);padding-top:14px">
<div class="subh" style="margin-top:0">Suggest a link</div>
<div class="row"><label style="flex:1 1 300px">Link<input id="lk_url" type="url" placeholder="https://www.youtube.com/watch?v=..." onkeydown="if(event.key==='Enter')lkRead()"></label><button style="align-self:flex-end" onclick="lkRead()">Read the page</button></div>
<div class="sm dim" id="lk_read"></div>
<div class="grid"><label>Title<input id="lk_title" maxlength="79"></label><label>What it is<select id="lk_kind"></select></label></div>
<div class="sm dim" style="margin:8px 0 6px">What it is about (up to three)</div><div class="chips" id="lk_topics"></div>
<label style="margin-top:10px">Why it is worth it (optional)<textarea id="lk_why" maxlength="160" rows="2" placeholder="What someone gets from it, in a sentence."></textarea></label>
<input type="hidden" id="lk_image">
<div class="row"><button class="pri" onclick="lkSend()">Send for review</button><span class="sm dim">A reviewer checks it first; once approved it shows on every controller. It goes with this controller's key: no account, nothing personal.</span></div>
</div>
<div id="lkmine"></div></section>
<section class="card com" id="rdcard"><div class="head"><h2>Share a shot to Reddit</h2><span class="sm dim">Nothing is sent from the controller</span></div>
<div class="sm mid">A picture of the shot (its curve, the recipe and the coffee, and your photo if you add one) and the post's text, ready for r/espresso.</div>
<div class="grid" style="margin-top:10px"><label>Shot<select id="rd_shot" onchange="rdBuild()"></select></label>
<label>Your photo (optional)<input type="file" id="rd_photo" accept="image/*" capture="environment" onchange="rdPhoto(this)"></label>
<label>Community<select id="rd_sub" onchange="rdBuild()"><option value="espresso">r/espresso</option><option value="Coffee">r/Coffee</option><option value="BrevilleCoffee">r/BrevilleCoffee</option></select></label></div>
<label>Title<input id="rd_title" maxlength="300"></label>
<div class="two" style="margin-top:10px"><div><canvas id="rd_img" width="1200" height="1200" style="width:100%;height:auto;border-radius:10px;border:1px solid var(--line)"></canvas></div>
<div><label>Post text<textarea id="rd_text" rows="14"></textarea></label>
<div class="row"><button class="pri" onclick="rdDownload()">Download image</button><button onclick="rdCopy()">Copy text</button><button onclick="rdOpen()">Open Reddit</button></div>
<div class="sm dim">Reddit takes no picture through a link: download the image, then on Reddit attach it to the post and paste the text.</div></div></div>
<canvas id="rdcurve" width="560" height="235" style="display:none"></canvas></section>
<section class="card com"><div class="head"><h2>Trending recipes</h2><span class="sm dim" id="cmtrendnote"></span></div><div id="cmtrend" class="sm mid"></div></section>
<section class="card com"><div class="head"><h2>For what you are brewing</h2><span class="sm dim" id="cmcoffee"></span></div><div id="cmnow" class="sm mid"></div></section>
<section class="card com"><div class="head"><h2>Coffees and recipes in the shared library</h2></div><div class="sm mid">Browse them, add them, and review what other owners sent on <a href="#discover" style="color:var(--com)">Discover</a>.</div></section>
<section class="card com"><div class="head"><h2>Grinders in the shared library</h2><a class="sm" href="#grinder" style="color:var(--com)">Your grinder</a></div><div id="cmgrinders" class="sm mid"></div></section>
<section class="card com"><div class="head"><h2>What you share</h2><span class="pill" id="cmstate"></span></div><div id="cmshare" class="sm mid"></div></section>
</div>
<div class="page" id="p-setup">
<div class="head"><h1>Setup</h1></div>
<section class="card"><h2>Appearance</h2><p class="sm dim">For this page and the controller's screen; every browser follows it.</p><div class="row" id="themes"></div></section>
<section class="card" id="panelcard"><div class="head"><h2>Web page panels</h2><span class="sm dim" id="pn_note"></span></div><p class="sm dim">Extra pages and cards. Saved on the controller, so every browser shows the same.</p><div class="panels" id="panels"></div></section>
<section class="card"><h2>Drinkers</h2><label class="chk"><input type="checkbox" id="namedDrinkers" onchange="post('/api/settings',{namedDrinkers:this.checked}).then(r=>{done(r,'Saved');loadRatings()})"><span>Named drinkers <span class="dim">(each rating says who gave it; each drinker gets their own taste and recommendations. Off: one household profile)</span></span></label>
<div class="grid" id="drnames"></div><div class="row"><button onclick="saveDrinkers()">Save names</button><span class="sm dim">Clearing a name keeps that drinker's ratings.</span></div></section>
<section class="card"><h2>Devices</h2><div id="dev" class="mid"></div><div id="scaledata" class="sm dim"></div><div class="scroll"><table id="seen"></table></div>
<div class="row"><button onclick="loadDevices()">Refresh</button><button onclick="testPress('brew')">Test press: brew bot</button><button onclick="testPress('power')">Test press: power bot</button><button onclick="swapBots()">Swap brew and power bots</button></div>
<div class="row"><span class="sm dim">Forget a paired device:</span><button class="del" onclick="forget('scale','the scale')">Scale</button><button class="del" onclick="forget('bot','the brew bot')">Brew bot</button><button class="del" onclick="forget('power','the power bot')">Power bot</button><button class="del" onclick="forget('stop','the stop bot')">Stop bot</button></div>
<details class="note"><summary>A test press moves one pusher once, so you can see which button it is on. <b>It really presses.</b></summary><ul><li>Two pushers of the same make look the same in the list.</li><li>The brew bot starts the pump if the machine is on (press again to stop it).</li><li>The power bot switches the machine on or off.</li></ul></details>
<details class="note"><summary>A pusher in the list can be the brew bot, the power bot or the stop bot.</summary><ul><li><b>Brew bot</b>: on the brew button; it stops the shot.</li><li><b>Power bot</b>: on the power button.</li><li><b>Stop bot</b>: on the stop button of a machine with separate start and stop buttons.</li><li>SwitchBot Bot, Tuya Fingerbot and MicroBot Push are supported; the last two need a key, under Pusher keys.</li></ul></details></section>
<section class="card"><h2>Controller screen</h2><label class="chk"><input type="checkbox" id="displayFlipped" onchange="post('/api/settings',{displayFlipped:this.checked}).then(r=>done(r,'Saved'))">Turned upside down, for a mounting with the USB-C connector and the touch key on the right</label></section>
<section class="card"><h2>Pusher keys</h2><div class="grid"><label>For<select id="pk_role" onchange="loadPusher()"><option value="brew">Brew bot</option><option value="power">Power bot</option><option value="stop">Stop bot</option></select></label><label>Tuya uuid<input id="pk_uuid" maxlength="16"></label><label>Tuya device id<input id="pk_dev" maxlength="22"></label><label>Tuya local key<input id="pk_key" type="password" maxlength="16"></label><label>Tuya model<select id="pk_var"><option value="0">Fingerbot / Fingerbot Plus</option><option value="1">CubeTouch</option></select></label><label>MicroBot token (32 hex digits)<input id="pk_tok" type="password" maxlength="32"></label><label id="pk_pwl">SwitchBot password (this bot)<input id="pk_pw" type="password" maxlength="23"></label></div><div id="pk_state" class="sm dim"></div><div class="row"><button class="pri" data-save onclick="savePusher()">Save keys</button></div><details class="note"><summary>A SwitchBot Bot needs nothing here; the other pushers need a key.</summary><ul><li>Tuya Fingerbot: its uuid, device id and local key from Tuya's cloud.</li><li>MicroBot Push: the token from its first pairing (Home Assistant shows it).</li><li>Keys are stored on the controller and never shown again.</li></ul></details></section>
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
<div class="row"><button class="pri" data-save onclick="saveSettings(this.closest('.card'))">Save</button></div></section>
<section class="card"><h2>Network</h2><div class="grid">
<label>Join Wi-Fi network (empty: access point only)<input id="staSsid"></label>
<label>Wi-Fi password (blank keeps current)<input id="staPassword" type="password"></label>
<label>Access point password (blank keeps current)<input id="apPassword" type="password" minlength="8"></label>
<label>SwitchBot password (only if set in its app)<input id="botPassword" type="password"></label></div>
<label class="chk"><input type="checkbox" id="webStartEnabled">Allow Start from this page</label>
<div class="row"><button class="pri" data-save onclick="saveNet()">Save and restart</button></div></section>
<section class="card"><h2>Firmware</h2><div id="fw" class="mid"></div>
<div class="scroll"><table id="fwslots"></table></div>
<h2 style="margin-top:10px">From GitHub</h2><div id="fwgh" class="mid"></div>
<div class="row"><button onclick="fwCheck()">Check for an update</button><button class="pri" id="fwinst" style="display:none" onclick="fwInstall()"></button></div>
<div class="grid"><label>Releases from (owner/name)<input id="fwrepo" maxlength="63"></label><label>Token (private repository only; blank keeps it, - forgets it)<input id="fwtok" type="password" maxlength="127"></label></div>
<label class="chk"><input type="checkbox" id="fwauto">Look for a new release once a day (installing is always your choice)</label>
<div class="row"><button data-save onclick="fwSaveCfg()">Save</button><button onclick="fwBoot('previous')">Go back to the previous version</button><button onclick="fwBoot('factory')">Start the safe (factory) version</button></div>
<h2 style="margin-top:10px">From a file</h2>
<div class="row"><input type="file" id="fwfile" accept=".bin" style="flex:1 1 220px"><button onclick="uploadFirmware()">Install this file</button></div>
<div id="fwprog" class="sm mid"></div>
<details class="note"><summary>Download <b>firmware.bin</b> for the version you want from the <a id="fwrel" href="#" target="_blank" rel="noopener">releases page</a>, then choose it here.</summary><ul><li>Not while a shot is in hand.</li><li>The new firmware goes into the spare slot and is on trial for its first 45 seconds: if it crashes or hangs in that time the controller goes back to this one by itself.</li><li>Settings, recipes and shot history are kept.</li></ul></details></section>
<section class="card com"><div class="head"><h2>Shared library</h2><span class="pill" id="sh_state"></span></div>
<div class="sm mid" id="sh_key"></div>
<div class="sm mid">Sharing goes through the Brew Controller relay: no GitHub account needed.</div>
<div class="sm mid" id="sh_link"></div>
<details class="note" id="sh_own"><summary>Use your own library repository (optional)</summary>
<div class="grid"><label>Library repository (owner/name)<input id="sh_repo" maxlength="63" placeholder="e.g. hallge/Community-Coffee" autocomplete="off"></label>
<label>Token for that repository only (blank keeps the one stored)<input id="sh_token" type="password" maxlength="127" autocomplete="new-password"></label></div>
<div class="sm mid">For running another library, or testing. The token should be a fine-grained one limited to this repository, with Contents: read and write and nothing else. Save below stores it on the controller; it is never shown again. Both filled in, they are used instead of the relay; clear the repository to go back to the relay.</div></details>
<label class="chk"><input type="checkbox" id="shareEnabled" style="accent-color:var(--com)">Share from this controller</label>
<fieldset><legend>What it shares</legend>
<label class="chk"><input type="checkbox" id="shareCoffees" style="accent-color:var(--com)">Coffees I add or change (name, roaster, origin, process, roast level, notes)</label>
<label class="chk"><input type="checkbox" id="shareDialIns" style="accent-color:var(--com)">Settled dial-ins: grind, dose, yield, brew time, grinder, roast date</label>
<label class="chk"><input type="checkbox" id="shareShots" style="accent-color:var(--com)">Every shot that counts for grind advice, with its flow</label>
<label class="chk"><input type="checkbox" id="shareRatings" style="accent-color:var(--com)">My taste ratings, with those shots</label></fieldset>
<div class="row"><button class="pri" style="background:var(--com);border-color:var(--com);color:#06203a" data-save onclick="saveShare()">Save</button><button onclick="post('/api/share/coffees').then(r=>{done(r,r.queued+' coffees queued');loadLibrary();loadShare()})">Queue coffees not sent yet</button><button onclick="ask('Forget what was sent before and send every coffee and grinder you have described again?').then(ok=>{if(ok){post('/api/share/again').then(r=>{done(r,r.queued+' queued');loadLibrary();loadShare()})}})">Send everything again</button></div>
<div class="sm mid" id="sh_queue"></div>
<details class="note"><summary>Every record is signed with this controller's own key.</summary><ul><li>It carries the key's short id, never a name, an address or anything about the network.</li><li>Coffees and grinders wait for a reviewer before they are published; measurements do not.</li></ul></details></section>
<section class="card" id="diagcard"><div class="head"><h2>Diagnostics</h2><span class="pill" id="dg_state"></span></div>
<div class="sm mid">A short technical report once a day, and after a crash, so problems in other homes get found and fixed.</div>
<label class="chk"><input type="checkbox" id="diagnosticsOn" onchange="post('/api/settings',{diagnosticsOn:this.checked}).then(r=>{done(r,this.checked?'Diagnostics on':'Diagnostics off: nothing is sent');loadDiag()})">Send diagnostics reports</label>
<div class="sm mid" id="dg_line"></div><div class="sm" id="dg_crash"></div>
<div class="row"><button onclick="diagPreview()">What would be sent</button><button id="dg_send" onclick="post('/api/diagnostics/send').then(r=>{done(r,'Sending a report');setTimeout(loadDiag,8000)})">Send now</button></div>
<pre id="dg_pre" class="sm" style="display:none;white-space:pre-wrap;word-break:break-word;max-height:360px;overflow:auto"></pre>
<details class="note"><summary>What is in a report, and what never is.</summary><ul><li><b>Sent:</b> the firmware version and restarts; which scale, pushers and machine type are in use; how their Bluetooth connections went; how shots and power presses ended; memory and Wi-Fi strength; warnings from the log, with names and addresses taken out.</li><li><b>Never sent:</b> Bluetooth or IP addresses, the Wi-Fi name, passwords, tokens, coffee, roaster or drinker names, notes, subscriptions or where you are.</li><li>Reports are signed with this controller's own key (its short id is all that identifies it) and go through a relay to a private repository only the maintainer reads. No GitHub account is needed.</li><li>Off: nothing is built or sent, and reports waiting are dropped.</li></ul></details>
<div class="subh" id="bugrep">Report a problem</div>
<div class="sm mid">Something not working? Say what happened: it goes to the maintainer with the log attached.</div>
<div class="grid"><label>What it is about<select id="bg_kind"><option value="brewing">Brewing: starting, stopping, weights</option><option value="grind_advice">Grind advice or a starting point</option><option value="scale">The scale</option><option value="pushers">Button pushers (bots)</option><option value="machine_power">Switching the machine on or off</option><option value="web_page">This web page</option><option value="sharing">Sharing and the community library</option><option value="firmware_update">Firmware updates</option><option value="other" selected>Something else</option></select></label>
<label>How to reach you (optional)<input id="bg_contact" maxlength="64" placeholder="a GitHub name or an email"></label></div>
<label>What happened<textarea id="bg_text" rows="4" maxlength="1000" placeholder="What you did, what you expected, and what happened instead. When, if you know." oninput="$('bg_n').textContent=this.value.length+' / 1000'"></textarea></label>
<div class="row"><button class="pri" id="bg_send" onclick="sendBug()">Send the report</button><span class="sm dim" id="bg_n">0 / 1000</span><span class="sm" id="bg_state"></span></div>
<div class="sm dim">Addresses, network and device names, coffee and drinker names are taken out before it is sent, as for every report. The contact goes as you type it.</div>
<details class="note"><summary>Relay address (for testing; diagnostics and the shared library both use it)</summary><div class="row"><input id="dg_relay" maxlength="95" placeholder="blank: the built-in relay" style="flex:1 1 260px"><button onclick="post('/api/diagnostics/relay',{url:$('dg_relay').value.trim()}).then(r=>{done(r,'Saved');loadDiag()})">Save</button></div></details></section>
</div>
<div class="page" id="p-logs">
<div class="head"><h1>Logs</h1></div>
<section class="card"><h2>What is recorded</h2><div class="grid">
<label>Detail<select id="lg_level"><option value="0">Errors only</option><option value="1">Warnings and errors</option><option value="2">Normal: every event</option><option value="3">Debug: also once a second during a shot</option></select></label>
<label class="chk"><input type="checkbox" id="lg_keep">Keep on flash, so it survives a restart</label></div>
<div class="row"><button class="pri" data-save onclick="saveLog()">Save</button><a href="/log.txt"><button>Download the whole log</button></a><button class="del" style="margin-left:auto" onclick="ask('Delete the stored log?').then(ok=>{if(ok){post('/api/log/clear').then(r=>{done(r,'Log cleared');loadLog()})}})">Clear stored log</button></div>
<div id="lg_state" class="sm dim"></div>
<details class="note"><summary>What is logged, and when.</summary><ul><li>Each start-up: the firmware version and why the controller started (power on, restart, crash, watchdog).</li><li>Every ten minutes: free memory and how much stack each task has left.</li><li>Lines are written to flash only between shots.</li></ul></details></section>
<section class="card"><div class="head"><h2>Live</h2><div class="row"><label class="chk"><input type="checkbox" id="lg_follow" checked>Follow</label><input id="lg_filter" placeholder="filter, e.g. scale or W" style="width:170px"><button onclick="LOGT='';drawLog()">Clear view</button></div></div>
<pre id="lg_view" style="height:60vh;overflow:auto;margin:0;font:12px/1.45 ui-monospace,Consolas,monospace;white-space:pre-wrap;word-break:break-all"></pre></section>
</div>
</main></div><div id="toast" role="status" aria-live="polite"></div>
<script>
// Chart colours: the scheme's, read from its CSS variables (readTC on a change of scheme).
let TC={};function readTC(){const st=getComputedStyle(document.documentElement),v=n=>st.getPropertyValue(n).trim(),c=v('--card');
TC={dim:v('--dim'),mid:v('--mid'),fg:v('--fg'),edge:v('--edge'),grid:v('--grid'),gridLo:v('--gridlo'),empty:v('--empty'),stripe:v('--stripe'),
cardRgb:c.length==7?[1,3,5].map(k=>parseInt(c.slice(k,k+2),16)):[38,42,47]}}
readTC();
const $=id=>document.getElementById(id);
const esc=s=>String(s==null?'':s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const get=u=>fetch(u).then(r=>r.json());
let lastBtn=null,lastAt=0;
const post=(u,b)=>{const btn=lastBtn&&Date.now()-lastAt<700&&document.contains(lastBtn)?lastBtn:null;lastBtn=null;if(btn)btn.classList.add('busy');
const inp=!btn&&lastIn&&Date.now()-lastInAt<1500&&document.contains(lastIn)?lastIn:null;lastIn=null;
return fetch(u,{method:'POST',headers:{'Content-Type':'application/json'},body:b?JSON.stringify(b):''}).then(r=>r.json()).catch(()=>({ok:false,error:'The controller could not be reached'})).then(r=>{if(btn){btn.classList.remove('busy');const how=r&&r.ok===false?'didnt':'did';btn.classList.add(how);setTimeout(()=>btn.classList.remove(how),1000);
if(how=='did'){const card=btn.closest('.card');if(card)card.querySelectorAll('button.dirty').forEach(x=>x.classList.remove('dirty'))}}if(inp)tick(inp,!(r&&r.ok===false));return r})};
// "Saved" beside a control that saves itself, where the eye already is.
// A question asked in the page, beside the button that asked it (the browser's own pop-up does not fit
// the page and is clumsy on a phone). Resolves true for Yes; Esc, Cancel or a click elsewhere is no.
function ask(msg){return new Promise(res=>{const box=$('askbox'),from=lastBtn&&Date.now()-lastAt<2000&&document.contains(lastBtn)?lastBtn:null;
$('asktxt').textContent=msg;$('askyes').classList.toggle('del',/^(Delete|Forget|Remove|Clear)/.test(msg));box.classList.add('on');
const r=from?from.getBoundingClientRect():null,w=box.offsetWidth,h=box.offsetHeight;
if(r){box.style.left=Math.max(12,Math.min(innerWidth-w-12,r.left))+'px';box.style.top=(r.bottom+8+h<innerHeight?r.bottom+8:Math.max(12,r.top-h-8))+'px'}
else{box.style.left=Math.max(12,(innerWidth-w)/2)+'px';box.style.top=Math.max(12,(innerHeight-h)/3)+'px'}
const end=v=>{box.classList.remove('on');document.removeEventListener('keydown',key,true);document.removeEventListener('pointerdown',away,true);if(v&&from){lastBtn=from;lastAt=Date.now()}res(v)};
const key=e=>{if(e.key=='Escape'){e.preventDefault();end(false)}else if(e.key=='Enter'){e.preventDefault();end(true)}};
const away=e=>{if(!e.target.closest('#askbox'))end(false)};
document.addEventListener('keydown',key,true);setTimeout(()=>document.addEventListener('pointerdown',away,true),0);
$('askyes').onclick=()=>end(true);$('askno').onclick=()=>end(false);$('askyes').focus()})}
let lastIn=null,lastInAt=0;function tick(el,ok){const host=el.closest('label')||el.parentElement;if(!host)return;let t=host.querySelector(':scope>.tick');if(!t){t=document.createElement('span');t.className='tick';host.appendChild(t)}
t.textContent=ok?'Saved ✓':'Not saved';t.classList.toggle('bad',!ok);clearTimeout(t._t);t._t=setTimeout(()=>t.remove(),2500)}
let SH_=null;const lib=()=>!SH_||SH_.to_library;let toastT=0;function say(t,how){if(!t)return;const e=$('toast');e.textContent=t;e.className=how===true?'ok':how===false?'bad':'';e.style.display='none';void e.offsetWidth;e.style.display='block';clearTimeout(toastT);toastT=setTimeout(()=>e.style.display='none',how===false?6000:3500)}
const done=(r,good)=>{say(r.ok?good:(r.error||'Not saved'),!!r.ok);return r.ok};
const DK=['roastDate','frozenDate','thawDate'];
const d2i=v=>v?+v.replace(/-/g,''):0,i2d=n=>n?String(n).padStart(8,'0').replace(/(\d{4})(\d\d)(\d\d)/,'$1-$2-$3'):'';
const age=n=>n?Math.floor((Date.now()-new Date(i2d(n)).getTime())/864e5):null;
const frozenFor=c=>{if(!c||!c.frozenDate)return 0;const a=new Date(i2d(c.frozenDate)).getTime(),z=c.thawDate?new Date(i2d(c.thawDate)).getTime():Date.now();return Math.max(0,Math.floor((Math.min(z,Date.now())-a)/864e5))};
const ageNote=c=>{const d=c?age(c.roastDate):null;if(d==null)return '';const f=frozenFor(c);return 'Roasted '+d+' days ago'+(f?' ('+f+' in the freezer'+(c.frozenDate&&!c.thawDate?', still there':'')+', so about '+Math.max(0,d-Math.round(f*0.9))+' days of ageing)':'')};
const f1=v=>(+v).toFixed(1),f2=v=>(+v).toFixed(2);
let GRD={divisor:1,subSteps:0};
const dpl=d=>d==2||d==5||d==10?1:2;
const fsn=v=>{if(!v)return '';const d=GRD.divisor||1,r=GRD.subSteps||0;if(r>=2)return (Math.floor((v-1)/r)+1)+':'+((v-1)%r+1);if(d>1){const p=dpl(d),sc=p==1?10:100;return Math.floor(v/d)+'.'+String((v%d)*(sc/d)).padStart(p,'0')}return String(v)};
const fdn=v=>{v=Math.abs(Math.round(v));const d=GRD.divisor||1;if((GRD.subSteps||0)<2&&d>1){const p=dpl(d),sc=p==1?10:100;return Math.floor(v/d)+'.'+String((v%d)*(sc/d)).padStart(p,'0')}return String(v)};
const fdf=x=>{const d=(GRD.subSteps||0)>=2?1:(GRD.divisor||1);return (+x/d).toFixed(2).replace(/\.?0+$/,'')};
const fdu=()=>(GRD.subSteps||0)>=2?'steps':U;
const ps=t=>{t=String(t==null?'':t).trim();if(!t)return null;const d=GRD.divisor||1,r=GRD.subSteps||0;let v;
if(r>=2){const m=t.match(/^(\d+)(?:\s*[:\/-]\s*(\d+))?$/);if(!m)return null;const a=+m[1],b=m[2]===undefined?1:+m[2];if(a<1||b<1||b>r)return null;v=(a-1)*r+b}
else{const x=parseFloat(t.replace(',','.'));if(!isFinite(x)||!/^[\d.,]+$/.test(t))return null;v=Math.floor(x*d+0.5)}
return v>=1&&v<=2000?v:null};
const fs=v=>fsn(v)+' '+U;
let U='µm',S=null,L=null,G=null,SH=[],ST=null,C=-1,R=-1,SEL=0,lastShotId=-1;
const F=["learningEnabled","learningFactor","adaptiveLearning","minValidWeightG","scaleTimeoutMs","dripObserveMs","minShotTimeMs","noFlowTimeoutS","cupMode","cupMinWeightG","autoStartAfterTare","scaleTimer","requireCup","actuationMode","pressDurationMs","releaseSettleMs","botHoldSeconds","grindToleranceS","grindWindow","grindUmPerS","grindMaxStepUm","grindTimeBasis","grindRatedTarget","grindConfidence","scaleRestMin"];
const SB=['shareEnabled','shareCoffees','shareDialIns','shareShots','shareRatings','keepPowerBotConnected','displayFlipped','powerOffWithScale','namedDrinkers'];
// Diagnostics reports (docs/TELEMETRY.md).
const ago=s=>{if(!s)return 'never';const d=Math.round(Date.now()/1000-s);return d<90?'just now':d<5400?Math.round(d/60)+' min ago':d<129600?Math.round(d/3600)+' h ago':Math.round(d/86400)+' days ago'};
async function loadDiag(){let d;try{d=await get('/api/diagnostics/report')}catch(e){return}
$('diagnosticsOn').checked=!!d.on;$('dg_state').textContent=d.on?'on':'off';$('dg_send').disabled=!d.on;$('bg_send').disabled=!d.on;
$('bg_state').textContent=!d.on?'Turn diagnostics on to send a report.':d.bug_waiting?'Your report goes with the next one, in a moment.':d.bug_sent?'Last report sent '+new Date(d.bug_sent*1000).toLocaleString()+'.':'';
if(document.activeElement!==$('dg_relay'))$('dg_relay').value=d.relay_default?'':d.relay;
$('dg_line').textContent=d.on?['Last sent: '+ago(d.last_sent),d.waiting?d.waiting+' waiting':'',d.last_problem?'last try: '+d.last_problem:'',d.key_id?'key '+d.key_id:''].filter(x=>x).join(' · '):'Nothing is sent.';
let c;try{c=await get('/api/crash')}catch(e){c={}}const dm=c&&c.dump;
$('dg_crash').innerHTML=c&&c.t?`<b style="color:var(--warn)">Last crash:</b> ${esc(c.fw||'')} ${esc(c.reset||'')}${dm?`, in ${esc(dm.task)} at ${esc(dm.pc)}${dm.cause?' ('+esc(dm.cause)+')':''}`:''}${c.at?' · '+esc(ago(c.at)):''}`:''}
// Once, after the update that brought them: diagnostics are on, and where to turn them off.
function diagNotice(){if(!S||!S.diagnosticsOn)return;let seen=null;try{seen=localStorage.getItem('bbwDiagSeen')}catch(e){}if(seen)return;
try{localStorage.setItem('bbwDiagSeen','1')}catch(e){}say('Diagnostics reports are on: a short technical report a day helps fix problems. Setup, Diagnostics shows what is sent and turns it off.',true)}
function sendBug(){const t=$('bg_text').value.trim();if(!t)return say('Say what happened first',false);
post('/api/diagnostics/bug',{kind:$('bg_kind').value,text:t,contact:$('bg_contact').value.trim()}).then(r=>{done(r,'Thank you: the report is on its way');if(r.ok){$('bg_text').value='';$('bg_n').textContent='0 / 1000';setTimeout(loadDiag,8000)}})}
async function diagPreview(){const p=$('dg_pre');if(p.style.display!='none'){p.style.display='none';return}let t;try{t=await get('/api/diagnostics/preview')}catch(e){return}p.textContent=JSON.stringify(t,null,1);p.style.display=''}
async function loadShare(){let s;try{s=await get('/api/share')}catch(e){return}SH_=s;if(L){drawCoffees();drawGrinder()}
$('sh_state').textContent=!S.shareEnabled?'off':s.can_send?'sending':'queueing';$('sh_state').className='pill '+(S.shareEnabled?'ok':'');
$('sh_key').textContent=s.key_ready?'This controller\'s key id: '+s.key_id+'. '+(s.real_signatures?'Signatures: ES256. Records travel by '+(s.via||'HTTPS')+'.':'This is the PC emulator: it has no real key, and marks what it writes as '+s.algorithm+'. The library will not accept these records; they are for trying the flow.'):'No device key: nothing can be shared.';
$('sh_queue').textContent=s.waiting+' waiting to send (room for '+s.capacity+'), '+s.sent+' sent.';
if(document.activeElement!=$('sh_repo'))$('sh_repo').value=s.repository||'';
if(s.repository&&!$('sh_own').open)$('sh_own').open=true;
$('sh_link').textContent=(s.real_signatures&&s.via_relay?'Through the relay ('+s.relay+'). ':'')+(s.real_signatures?(!s.configured?(s.repository?'No token stored yet.':'Not connected: records wait on the controller.'):s.problem?'Not reaching the library: '+s.problem+(s.last_answer?' (answer '+s.last_answer+')':'')+'.':s.can_send?'Connected'+(s.last_ok?', last reached '+new Date(s.last_ok*1000).toLocaleString():'')+'.':'Waiting to try again.')+(s.token_set?' A token is stored.':''):'This PC has no OpenSSL, so the emulator cannot sign: records go to a folder beside its data and no further.')+(s.real_signatures&&!s.to_library?' Not reaching the library: set the repository above, and log the GitHub CLI in on this PC (gh auth login).':'')}
// The Shared library card has one Save: the connection (repository, token) when either was changed,
// then the sharing switches. A token typed here is never left behind by a Save that says Saved.
function saveShare(){const repo=$('sh_repo').value.trim(),tok=$('sh_token').value.trim(),linkChanged=!!tok||!SH_||repo!==(SH_.repository||'');
(linkChanged?post('/api/share/link',{repository:repo,token:tok}):Promise.resolve({ok:true})).then(r=>{if(!r.ok){done(r,'');return}
if(linkChanged)$('sh_token').value='';saveSettings($('sh_repo').closest('.card'));loadShare()})}
const CF=['name','roaster','origin','region','producer','varietal','process','roastLevel','altitudeM','roastDate','frozenDate','thawDate','notes'];
const RF=['name','doseG','useRatio','targetWeightG','ratio','targetTimeS','preinfusion','preinfusionHoldS','bloomPauseS','timeLimit','maxShotTimeS','baselineOffsetG'];
const coffee=id=>L.coffees.find(c=>c.id==id),recipe=id=>L.recipes.find(r=>r.id==id);
const plabel=p=>{const c=coffee(p.coffeeId),r=recipe(p.recipeId);return (c?c.name+' — ':'')+(r?r.name:'?')};

function show(){let h=(location.hash||'#home').slice(1);
// A hidden panel's page goes Home.
if(['insights','discover','subscriptions','community','logs','start'].includes(h)&&!panelOn(h)){if(location.hash!='#home')history.replaceState(null,'','#home');h='home'}
const disc=false;document.querySelectorAll('.page').forEach(p=>p.classList.toggle('on',p.id=='p-'+h));
document.querySelectorAll('nav a,#bnav a,#more a').forEach(a=>a.classList.toggle('on',a.getAttribute('href')=='#'+(disc?'discover':h)));
$('bmore').classList.toggle('on',['start','recipes','insights','machine','subscriptions','discover','community','setup','logs'].includes(h));if(h=='start'||h=='home')loadStart();$('more').classList.remove('open');$('bmore').setAttribute('aria-expanded','false');if(h=='setup'){loadDevices();loadRatings();loadFirmware()}if(h=='machine'){loadUpkeep();loadMachine();loadPower()}if(h=='logs')loadLog();if(h=='coffees'){loadCommunityCoffees();loadRatings();if(panelOn('community'))loadDirectory()}if(h=='grinder'){drawScatter();loadCommunityGrinders();loadUpkeep()}if(h=='insights')drawInsights();if(h=='discover')loadDiscover();if(h=='community')loadCommunityPage();if(h=='home'&&panelOn('discover'))loadForYou();if(h=='subscriptions')loadSubs();}
// The optional panels (bbw::WebPanel bits): key, name, what it is, bit.
const PANELS=[['insights','Insights','Your taste profile from your ratings, and charts: roast level against grind, coffee age against brew time, a brewing calendar, dial-in shots per bag.',1],
['subscriptions','Subscriptions','Coffee subscriptions: when the next delivery comes, skipping one, what is on its way.',4],
['discover','Discover','Coffees and subscriptions that match your taste, the shared coffee library and recipe book (browse, add, review), and the For you card on Home.',2],
['community','Community','What other owners share: where to start your grinder for what you are brewing, links, trending recipes, grinders, and what you share. Also the Community cards on Home and on each coffee.',8],
['logs','Logs','The controller\'s log, live, for when something needs looking into. It is also always at /log.txt.',16],
['start','Getting started','A step-by-step guide for a new controller: Wi-Fi, scale, bots, machine, grinder, recipe, coffee and the first shot.',64,1]];
let PN=0;try{PN=+(localStorage.getItem('bbwPanels')||0)}catch(e){}
// A panel's bit means shown, except the guide's (p[4]), which means hidden.
const pOn=p=>p[4]?!(PN&p[3]):!!(PN&p[3]);
const panelOn=k=>{const p=PANELS.find(x=>x[0]==k);return !p||pOn(p)};
function applyPanels(mask,keep){PN=mask|0;PANELS.forEach(p=>document.body.classList.toggle('pn-'+p[0],pOn(p)));document.body.classList.toggle('pn-hub',!!(PN&32));
try{localStorage.setItem('bbwPanels',String(PN))}catch(e){}if(!keep)show()}
function renderPanels(){const el=$('panels');if(!el)return;el.innerHTML=PANELS.map(p=>{const[k,n,d,b,inv]=p;return `<label class="chk"><input type="checkbox" ${pOn(p)?'checked':''} onchange="setPanel(${b},${inv?'!':''}this.checked,${inv?1:0})"><span><b>${n}</b><span class="sm dim">${d}</span></span></label>`}).join('');
$('pn_note').textContent=S&&!S.webPanelsChosen?'showing what is in use':''}
// Open a page and the card titled so (unfolded, scrolled to).
function goCard(page,title){if(location.hash!='#'+page)location.hash='#'+page;setTimeout(()=>{const c=[...document.querySelectorAll('#p-'+page+' .card')].find(c=>{const h=c.querySelector('h2');return h&&h.textContent.trim().toLowerCase().startsWith(title.toLowerCase())});
if(!c)return;c.classList.remove('closed');c.scrollIntoView({behavior:'smooth',block:'start'})},80)}
// Getting started: each step's state from /api/start. first: a new controller opens on the guide.
const GS=[
['Join your Wi-Fi',x=>x.wifi.connected,x=>x.wifi.connected?'Connected to your home network.':x.wifi.joined?'Saved, not connected yet: check the name and password.':'Running on its own access point. Join your home network for the clock, updates and the shared library.',()=>goCard('setup','Network')],
['Pair your scale',x=>x.scale.paired,x=>x.scale.connected?'Connected.':x.scale.paired?'Paired. Switch it on to connect.':'Switch the scale on near the controller: a supported scale is adopted by itself.',()=>goCard('setup','Devices')],
['Pair the brew bot',x=>x.brew_bot.paired,x=>x.brew_bot.paired?'Paired. It wakes when you press Start.':'Put the bot on the brew button, then pick it in the list of pushers.',()=>goCard('setup','Devices')],
['Check your espresso machine',x=>!!x.machine.maker,x=>x.machine.maker?esc(x.machine.maker+' '+x.machine.model)+'. Change it if yours is different.':'Choose your make and model: it says how the bot presses.',()=>goCard('machine','Make and model')],
['Describe your grinder',x=>x.grinder.set&&x.grinder.range,x=>x.grinder.set?esc(x.grinder.model||'Your grinder')+(x.grinder.range?'.':': add its range of settings for a first guess.'):'Pick your grinder and how it is set: advice is given in its own settings.',()=>goCard('grinder','My grinder')],
['Choose a recipe',x=>x.recipes>0,x=>x.recipes?x.recipes+' recipe'+(x.recipes==1?'':'s')+' ready. Adjust dose, yield and time to taste.':'Add a recipe: dose, yield and brew time.',()=>location.hash='#recipes'],
['Add your coffee',x=>x.coffees_on_recipe>0,x=>x.coffees_on_recipe?'On the controller, ready to pick.':'Add the coffee you are brewing and tick a recipe for it.',()=>location.hash='#coffees'],
['Pull your first shot',x=>x.shots,x=>x.shots?'Done. Grind advice learns from every shot.':'Cup on the scale, then Start. Grind advice starts from here.',()=>location.hash='#home']];
const GSMORE=[['Power bot','Switch the machine on and off, on a schedule too.',()=>goCard('setup','Devices')],['Insights and Discover','Charts of your shots and coffees picked for your taste.',()=>goCard('setup','Web page panels')],['Shared library','Share coffees and dial-ins, and start from what other owners settled on.',()=>goCard('setup','Shared library')]];
let GSX=null;
async function loadStart(first){if(!panelOn('start'))return;let x;try{x=await get('/api/start')}catch(e){return}GSX=x;
const done=GS.map(g=>!!g[1](x)),n=done.filter(Boolean).length,next=done.indexOf(false),pc=Math.round(100*n/GS.length)+'%';
$('gsn').textContent=n+' of '+GS.length+' done';$('gsn').className='pill'+(n==GS.length?' ok':'');$('gsbar').style.width=pc;
$('gslist').innerHTML=GS.map((g,i)=>`<div class="st${done[i]?' done':''}${i==next?' next':''}"><span class="n">${done[i]?'\u2713':i+1}</span><div class="tx"><b>${g[0]}</b><div class="sm mid">${g[2](x)}</div></div><button class="${i==next?'pri':''}" onclick="GS[${i}][3]()">${done[i]?'Change':'Set it up'}</button></div>`).join('');
$('gsmore').innerHTML=GSMORE.map((g,i)=>`<div class="st"><span class="n">+</span><div class="tx"><b>${g[0]}</b><div class="sm mid">${g[1]}</div></div><button onclick="GSMORE[${i}][2]()">Open</button></div>`).join('');
$('hstart').style.display=n<GS.length?'':'none';$('hstartn').textContent=n+' of '+GS.length+' done';$('hstartbar').style.width=pc;
if(first&&!x.shots&&n<GS.length&&!location.hash)location.hash='#start'}
function setPanel(bit,on,inv){const m=on?(PN|bit):(PN&~bit);applyPanels(m,true);post('/api/settings',{webPanels:m}).then(r=>{if(r&&r.ok===false)return done(r,'');if(S){S.webPanels=m;S.webPanelsChosen=true}renderPanels();say(on!=!!inv?'Shown':'Hidden')})}
applyPanels(PN,true);
// Colour schemes, in the controller's order (bbw::UiTheme): name, and a strip of its colours.
const THEMES=[['dark','Dark',['#0e0f11','#17191c','#f08a24','#4cc38a']],['light','Light',['#f4f3f0','#ffffff','#c2560c','#1e8e4e']],
['nord','Nord',['#2e3440','#3b4252','#d08770','#a3be8c']],['solarized','Solarized Dark',['#002b36','#073642','#cb4b16','#859900']],
['dracula','Dracula',['#21222c','#282a36','#ffb86c','#50fa7b']],['contrast','High contrast',['#000000','#1a1a1a','#ffb000','#00e676']]];
function themeNow(){const t=document.documentElement.dataset.theme||'dark';return Math.max(0,THEMES.findIndex(x=>x[0]==t))}
function setTheme(n,save){const t=(THEMES[n]||THEMES[0])[0];if(t=='dark')delete document.documentElement.dataset.theme;else document.documentElement.dataset.theme=t;
try{localStorage.setItem('bbwTheme',t)}catch(e){}readTC();renderThemes();show();
if(save)post('/api/settings',{uiTheme:n}).then(r=>{if(r&&r.ok===false)return;if(S)S.uiTheme=n})}
function renderThemes(){const el=$('themes');if(!el)return;const cur=themeNow();el.innerHTML=THEMES.map(([k,name,cols],i)=>`<button class="theme${i==cur?' on':''}" onclick="setTheme(${i},true)" aria-pressed="${i==cur}"><span class="strip">${cols.map(c=>`<i style="background:${c}"></i>`).join('')}</span>${name}</button>`).join('')}
window.addEventListener('hashchange',show);loadStart(true);loadHomePower();if(panelOn('subscriptions'))loadSubs();
let LOGT='',LOGN=0,LOGB=false;
function drawLog(){const f=$('lg_filter').value.toLowerCase(),v=$('lg_view');v.innerHTML=LOGT.split('\n').filter(l=>l&&(!f||l.toLowerCase().includes(f))).map(l=>{const k=l.charAt(11);return '<span'+(k=='E'||k=='W'?' style="color:var(--warn)"':k=='D'?' class="dim"':'')+'>'+esc(l)+'</span>'}).join('\n');if($('lg_follow').checked)v.scrollTop=v.scrollHeight}
async function pollLog(){if(LOGB||!$('p-logs').classList.contains('on'))return;LOGB=true;try{for(let i=0;i<8;i++){const r=await fetch('/api/log?from='+LOGN),t=await r.text(),n=+r.headers.get('X-Log-Next');if(n<LOGN)LOGT='';LOGN=n;if(!t)break;LOGT=(LOGT+t).slice(-200000)}drawLog()}catch(e){}LOGB=false}
async function loadLog(){let c;try{c=await get('/api/log/config')}catch(e){return}$('lg_level').value=c.level;$('lg_keep').checked=c.keep;
$('lg_state').textContent=(c.keep?'On flash: '+Math.round(c.flash_bytes/1024)+' kB of at most '+Math.round(c.flash_limit/1024)+' kB (the oldest half is dropped when it is full). ':'Not kept on flash: only the last '+Math.round(c.memory_bytes/1024)+' kB, in memory, lost at a restart. ');pollLog()}
function saveLog(){post('/api/log/config',{level:+$('lg_level').value,keep:$('lg_keep').checked}).then(r=>{done(r,'Saved');loadLog()})}
setInterval(pollLog,2000);
async function testPress(role){const brew=role=='brew';if(!await ask(brew?'Press the brew bot once?\n\nIf it is on the machine and the machine is on, the pump STARTS. Press again to stop it.':'Press the power bot once?\n\nThe machine switches on or off.'))return;
fetch('/api/bot/test?role='+role,{method:'POST'}).then(r=>r.json()).then(r=>{if(!r.ok)return say(r.error||'Not pressed');if(!brew){say('Power bot: pressing. Watch which one moves.');return setTimeout(loadDevices,3000)}
say('Brew bot: waking it up...');let n=0;const t=setInterval(async()=>{let s;try{s=(await get('/api/bot/test')).brew}catch(e){return}if(s=='pressed'||s=='failed'||++n>45){clearInterval(t);say(s=='pressed'?'Brew bot pressed. That is the one that moved.':'The brew bot did not answer: is it in range and awake?');loadDevices()}},1000)}).catch(()=>say('controller not reachable',false))}
async function swapBots(){let d;try{d=await get('/api/devices')}catch(e){return}const b=d.pairing.bot,p=d.pairing.power;if(!b||!p)return say('Both a brew bot and a power bot must be paired first',false);if(!await ask('Swap them?\n\nBrew bot becomes '+p+'\nPower bot becomes '+b))return;
const ty=a=>{const s=d.seen.find(x=>x.address==a);return s?s.type:1};await post('/api/pair',{kind:'bot',address:'',type:0});await post('/api/pair',{kind:'power',address:b,type:ty(b)});await post('/api/pair',{kind:'bot',address:p,type:ty(p)});say('Swapped');setTimeout(loadDevices,800)}
let FW=null,FWT=0;
async function loadFirmware(){let f;try{f=await get('/api/update')}catch(e){return}FW=f;
$('fw').innerHTML='Running <b>'+esc(f.version)+'</b>, built '+esc(f.built)+'.'+(f.can_update===false?' <span class="dim">This is the PC emulator: it is updated by rebuilding it.</span>':'');
$('fwslots').innerHTML=(f.slots||[]).length?'<tr><th class="l">Slot</th><th class="l">Holds</th><th class="l">State</th><th></th></tr>'+f.slots.map(s=>`<tr><td class="l">${esc(s.label)}</td><td class="l">${esc(s.version||'?')}</td><td class="l">${esc(s.state)}</td><td>${s.running?'<span class="pill ok">running</span>':''}${s.boot_next&&!s.running?'<span class="pill">starts next</span>':''}</td></tr>`).join(''):'';
const busy=f.state=='checking'||f.state=='downloading';
$('fwgh').innerHTML=esc(f.repo)+(f.token_set?' (with a token)':'')+'. '+(f.state=='downloading'?'<b>Downloading: '+f.percent+' %</b> ':'')+esc(f.message||'Not checked since start-up.')+(f.checked_epoch?' <span class="dim">Checked '+new Date(f.checked_epoch*1000).toLocaleString()+'.</span>':'');
const ib=$('fwinst');ib.style.display=f.available&&f.latest&&!busy?'':'none';if(f.latest)ib.textContent='Install '+f.latest.tag+' ('+Math.round(f.latest.bytes/1024)+' kB)';
if(document.activeElement!=$('fwrepo'))$('fwrepo').value=f.repo||'';$('fwauto').checked=!!f.auto_check;$('fwrel').href=f.releases;
clearTimeout(FWT);if(busy||f.state=='installed')FWT=setTimeout(loadFirmware,1500);if(f.state=='installed')setTimeout(()=>location.reload(),25000)}
function fwCheck(){post('/api/update/check').then(r=>{done(r,'Asking GitHub');setTimeout(loadFirmware,800)})}
async function fwInstall(){if(!FW||!FW.latest)return;if(!await ask('Download and install '+FW.latest.tag+'?\n\nThe controller restarts into it when it is done. If it does not run properly in its first 45 seconds, the controller goes back to '+FW.version+' by itself.'))return;post('/api/update/install').then(r=>{done(r,'Downloading '+FW.latest.tag);setTimeout(loadFirmware,800)})}
function fwSaveCfg(){post('/api/update/config',{repo:$('fwrepo').value.trim(),token:$('fwtok').value.trim(),auto_check:$('fwauto').checked}).then(r=>{done(r,'Saved');$('fwtok').value='';loadFirmware()})}
async function fwBoot(slot){const t=slot=='factory'?'Restart into the safe (factory) version?\n\nThat is the firmware flashed over USB. Updates can be installed again from there.':'Restart into the version before this one?';if(!await ask(t))return;post('/api/update/boot?slot='+slot).then(r=>{done(r,r.message||'Restarting');if(r.ok)setTimeout(()=>location.reload(),20000)})}
async function uploadFirmware(){const f=$('fwfile').files[0];if(!f)return say('Choose firmware.bin first',false);if(!/\.bin$/i.test(f.name)||f.size<200000||f.size>6291456)return say('That does not look like a firmware.bin',false);if(!await ask('Install '+f.name+' ('+Math.round(f.size/1024)+' kB) and restart the controller?'))return;
const x=new XMLHttpRequest(),d=new FormData();d.append('firmware',f,f.name);x.upload.onprogress=e=>{if(e.lengthComputable)$('fwprog').textContent='Sending: '+Math.round(e.loaded/e.total*100)+' %'};
x.onload=()=>{let r={};try{r=JSON.parse(x.responseText)}catch(e){}if(r.ok){$('fwprog').textContent='Installed. The controller is restarting; this page reloads in 20 seconds.';setTimeout(()=>location.reload(),20000)}else $('fwprog').textContent='Not installed: '+(r.error||'the controller refused the file')};
x.onerror=()=>{$('fwprog').textContent='The connection was lost while sending. The running firmware is unchanged.'};x.open('POST','/api/update');x.send(d)}

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
// What the live keys offer follows the state (core/Types.h State).
const prep=/^(TARING|WAITING_FOR_CUP|READY_TO_BREW)$/.test(s.state),run=/^(STARTING_SHOT|BREWING|BLOOMING|STOPPING_SHOT|POST_SHOT_DRIP)$/.test(s.state),idle=s.state=='READY';
const result=s.state=='SHOT_COMPLETE'||(!prep&&!run&&!idle&&s.state!='CONNECTING_SCALE'&&s.state!='BOOT'&&s.state!='INITIALIZING');
$('bstart').style.display=s.web_start&&idle?'':'none';
$('bstop').style.display=prep||run?'':'none';$('bstop').textContent=prep?'Cancel':'Stop';
$('btare').style.display=s.scale_connected&&(idle||prep)?'':'none';
$('back').style.display=result?'':'none';$('back').textContent=s.state=='SHOT_COMPLETE'?'Done with this shot':'Clear the warning';
$('ihint').textContent=run||prep||result?'':s.scale_resting?'Scale resting: the touch key wakes it and brews':!s.scale_connected?'Switch on the scale':nocup?'Waiting for a cup':'Ready: press the touch key to brew';
const a=$('alert');if(s.manual_stop_needed){a.style.display='block';a.textContent='STOP THE MACHINE BY HAND. The stop press was not delivered or confirmed.'}
else if(s.bot_switch_mode){a.style.display='block';a.textContent='The SwitchBot is in Switch mode. Set it to Press mode in the SwitchBot app.'}else a.style.display='none';
if(lastShotId!==s.last_shot_id){lastShotId=s.last_shot_id;loadShots();loadLibrary();if(S)loadShare()}}

async function loadSettings(){S=await get('/api/settings');if(S&&typeof S.uiTheme=='number'&&S.uiTheme!=themeNow())setTheme(S.uiTheme,false);renderThemes();if(S&&typeof S.webPanels=='number'&&S.webPanels!=PN)applyPanels(S.webPanels);renderPanels();F.forEach(k=>{const e=$(k),v=S[k];e.value=typeof v==='boolean'?(v?1:0):v});
SB.forEach(k=>$(k).checked=!!S[k]);loadShare();loadDiag();diagNotice();$('staSsid').value=S.net.staSsid;$('webStartEnabled').checked=S.net.webStartEnabled}
// Only what was changed on this page: a page left open since a setting was
// changed elsewhere (the power card, the API, another browser) must not put
// the old value back. Seen on the board: the press style and the power bot
// setting were undone by a Save of an older page.
// Only the settings in the card whose Save was pressed (scope); without one, all of them.
function saveSettings(scope){const inS=k=>!scope||scope.contains($(k));const b={};F.filter(inS).forEach(k=>{const e=$(k);b[k]=typeof S[k]==='boolean'?e.value==='1':+e.value});SB.filter(inS).forEach(k=>b[k]=$(k).checked);
Object.keys(b).forEach(k=>{if(S&&k in S&&b[k]===S[k])delete b[k]});
post('/api/settings',b).then(r=>{done(r,'Saved');loadSettings();loadGrind()})}
function saveNet(){post('/api/net',{staSsid:$('staSsid').value,staPassword:$('staPassword').value,apPassword:$('apPassword').value,botPassword:$('botPassword').value,webStartEnabled:$('webStartEnabled').checked}).then(r=>done(r,'Saved, restarting'))}

async function loadLibrary(){L=await get('/api/library');if(C<0||(C&&!coffee(C)))C=(L.presets[L.activePreset]||{}).coffeeId||(L.coffees[0]||{}).id||0;
if(R<0||(R&&!recipe(R)))R=(L.presets[L.activePreset]||{}).recipeId||L.recipes[0].id;U=L.grinder.unit=='um'?'µm':L.grinder.unit;GRD={divisor:L.grinder.divisor||1,subSteps:L.grinder.subSteps||0};document.querySelectorAll('.un').forEach(e=>e.textContent=U);drawHomePreset();drawCoffees();drawRecipes();drawFilter();drawGrinder();drawShots();loadGrind()}
function act(what,a,b){return post('/api/library/do?what='+what+'&a='+(a||0)+'&b='+(b||0)).then(r=>{done(r,'');return loadLibrary()})}

function drawHomePreset(){const ps=L.presets;drawCommunity('hcom',(ps[L.activePreset]||{}).coffeeId||0);$('psel').innerHTML=ps.map((p,i)=>p.hidden?'':`<option value="${i}" ${i==L.activePreset?'selected':''}>${esc(plabel(p))}</option>`).join('');
const p=ps[L.activePreset],r=recipe(p.recipeId),c=coffee(p.coffeeId);if(!r)return;
const pre=r.preinfusion==1?'hold '+r.preinfusionHoldS+' s'+(r.bloomPauseS?', bloom '+r.bloomPauseS+' s':''):'machine pre-infusion';
$('pchips').innerHTML=[esc(r.name),r.doseG+' g in, '+r.targetWeightG+' g out',r.targetTimeS+' s',pre,p.grindUm?fs(p.grindUm):'grind not set'].map(x=>`<span class="chip">${x}</span>`).join('');
const d=c?age(c.roastDate):null;$('pinfo').textContent=(c?[c.roaster,c.origin,c.region,c.process].filter(x=>x).join(', ')+(d!=null?(c.roaster||c.origin||c.region||c.process?'. ':'')+ageNote(c):'')+'. ':'No coffee attached. ')+ps.filter(p=>!p.hidden).length+' entries on the controller'+(ps.some(p=>p.hidden)?' ('+ps.filter(p=>p.hidden).length+' more wait for a new bag)':'')+', room for '+L.maxPresets+'.'}

// Coffees on the shelf first; those whose bag was finished below, folded away but kept with all
// their data (a new bag puts one back on the shelf and on the controller).
function drawCoffeeList(){const q=$('csearch').value.toLowerCase();const hit=c=>!q||[c.name,c.roaster,c.origin,c.region,c.process,c.varietal].join(' ').toLowerCase().includes(q);
const m=L.coffees.filter(c=>(!c.finished||waitingBag(c))&&hit(c)),fin=L.coffees.filter(c=>c.finished&&!waitingBag(c)&&hit(c));
$('clist').innerHTML=m.map(c=>{const n=L.presets.filter(p=>p.coffeeId==c.id).length,w=waitingBag(c),u=L.bags.filter(b=>b.coffeeId==c.id&&b.unopened).length,otw=L.bags.filter(b=>b.coffeeId==c.id&&b.onTheWay),d=c.finished||!c.bagId?null:age(c.roastDate);return `<button class="item ${c.id==C?'on':''}" onclick="C=${c.id};drawCoffees()"><b>${esc(c.name)}</b><span>${esc([c.roaster,n?n+(n==1?' recipe':' recipes'):'not on the controller',d!=null?d+' days off roast':'',u?(u==1?'1 unopened bag':u+' unopened bags')+(w&&w.roastDate&&(c.finished||!c.bagId)?', roasted '+fd(w.roastDate):''):'',otw.length?'on its way'+(otw[0].expectedDate?', expected '+fd(otw[0].expectedDate):''):'',c.share&&c.share!='local'?(c.share=='sent'&&!lib()?'not sent':SHN[c.share])+(c.shareChanged?', changed since':''):''].filter(x=>x).join(' · '))}</span></button>`}).join('')||'<div class="dim sm">'+(q?'No coffee on the shelf matches.':'No coffee on the shelf.')+'</div>';
const fo=$('cfinished')&&$('cfinished').open;
$('clist').innerHTML+=fin.length?`<details id="cfinished" ${fo||fin.some(c=>c.id==C)||q?'open':''} style="margin-top:10px"><summary class="sm mid">Finished bags (${fin.length})</summary>`+fin.map(c=>`<button class="item ${c.id==C?'on':''}" style="opacity:.8" onclick="C=${c.id};drawCoffees()"><b>${esc(c.name)}</b><span>${esc([c.roaster,'finished '+fd(c.finished)].filter(x=>x).join(' · '))}</span></button>`).join('')+'</details>':'';
const onShelf=L.coffees.filter(c=>!c.finished).length,nf=L.coffees.length-onShelf;
$('ccount').textContent=onShelf+(onShelf==1?' coffee':' coffees')+' on the shelf'+(nf?', '+nf+' finished':'')+' (room for '+L.maxCoffees+' in all).';
const have=new Set(L.coffees.map(c=>c.libraryId).filter(x=>x)),sm=CC.filter(c=>!have.has(c.id)&&(!q||[c.name,c.roaster,c.origin,c.region,c.process,c.varietal].join(' ').toLowerCase().includes(q))).slice(0,8);
$('cshared').innerHTML=sm.length?'<h2 style="color:#8cc4ff;margin-top:6px">In the shared library</h2>'+sm.map(c=>`<div class="pair" style="background:var(--combg);border-color:var(--comline)"><span class="what"><b>${esc(c.name)}</b> <span class="dim">${esc([c.roaster,c.origin,c.process].filter(x=>x).join(', '))}</span></span><button onclick="adopt('${esc(c.id)}')">Add</button></div>`).join(''):''}
// ---- Coffee taste (docs/COFFEE-TASTE.md): ratings of coffees, by the household or named drinkers
let RT=null,RS=null;
const ATTR_LABEL=['Acidity','Sweetness','Body','Bitterness','Finish'],FAM_LABEL=['Fruity','Floral','Sweet / caramel','Chocolate / nutty','Spicy','Roasty / smoky','Fermented / winey','Earthy / green'];
const starTxt=n=>'★'.repeat(n)+'☆'.repeat(5-n);
// "2 more coffees": what is still needed before recommendations appear (3 rated coffees).
const moreTxt=n=>{const k=Math.max(1,3-(n||0));return k+' more coffee'+(k==1?'':'s')};
function drinkerName(id){return !id?'Household':(RT&&RT.drinkers[id-1])||('Drinker '+id)}
async function loadRatings(){try{RT=await get('/api/coffee/ratings')}catch(e){RT=null}drawRating();drawDrinkers();if(location.hash=='#insights'&&typeof drawTaste=='function')drawTaste()}
function drawDrinkers(){const el=$('drnames');if(!el||!RT)return;el.innerHTML=RT.drinkers.map((n,i)=>`<label>Drinker ${i+1}<input id="drn${i}" maxlength="16" value="${esc(n)}"></label>`).join('')}
function saveDrinkers(){post('/api/drinkers',{names:[...Array(8)].map((_,i)=>($('drn'+i)||{}).value||'')}).then(r=>{done(r,'Names saved');loadRatings()})}
// The rating being edited: the drinker's rating of the bag in use if there is one.
function ratingFor(cid,bag,dr){return RT?RT.ratings.find(r=>r.coffee==cid&&r.bag==bag&&r.drinker==dr):null}
function drawRating(){const el=$('crate');if(!el||!L)return;const c=C&&coffee(C);if(!c||!RT){el.style.display='none';return}el.style.display='';
const named=RT.named,people=named?RT.drinkers.map((n,i)=>[i+1,n]).filter(x=>x[1]):[];
if(!RS||RS.coffee!=C||(RS.bag==null&&RS.forBag!=(c.bagId||0))){const dr=named&&people.length?(RS&&RS.drinker&&RT.drinkers[RS.drinker-1]?RS.drinker:people[0][0]):0,old=ratingFor(C,c.bagId||0,dr);
RS=old?{coffee:C,id:old.id,drinker:dr,stars:old.stars,attr:[...old.attr],fl:old.flavours}:{coffee:C,id:0,drinker:dr,stars:0,attr:[0,0,0,0,0],fl:0};RS.forBag=c.bagId||0}
$('crwho').innerHTML=named?(people.length?'<span class="sm dim">Who is rating:</span>'+people.map(([id,n])=>`<button class="${id==RS.drinker?'on':''}" onclick="pickDrinker(${id})">${esc(n)}</button>`).join(''):'<span class="sm dim">Named drinkers is on, but no names are set: add them in Setup.</span>'):'';
$('crstars').innerHTML='<span class="stars">'+[1,2,3,4,5].map(n=>`<button class="${n<=RS.stars?'on':''}" aria-label="${n} star${n>1?'s':''}" onclick="RS.stars=${n};drawRating()">★</button>`).join('')+'</span>'+(RS.stars?'':'<span class="sm dim">tap to rate</span>');
$('crattr').innerHTML=ATTR_LABEL.map((l,i)=>`<label>${l} <span class="dim">${RS.attr[i]?RS.attr[i]+' / 5':'not given'}</span><input type="range" min="0" max="5" step="1" value="${RS.attr[i]}" oninput="RS.attr[${i}]=+this.value;this.previousElementSibling.textContent=+this.value?this.value+' / 5':'not given'"></label>`).join('');
$('crfl').innerHTML=FAM_LABEL.map((l,i)=>`<button class="${RS.fl&(1<<i)?'on':''}" aria-pressed="${!!(RS.fl&(1<<i))}" onclick="RS.fl^=${1<<i};drawRating()">${l}</button>`).join('');
const bag=(L.bags||[]).find(b=>b.id==c.bagId);$('crinfo').textContent=bag&&bag.roastDate?'the bag roasted '+fd(bag.roastDate):'';
$('crnote').textContent=RS.id?'Changing your rating of this bag'+(named?' ('+drinkerName(RS.drinker)+')':''):'';
const mine=RT.ratings.filter(r=>r.coffee==C);
$('crlist').innerHTML=mine.length?mine.map(r=>{const b=(L.bags||[]).find(x=>x.id==r.bag),attrs=r.attr.map((v,i)=>v?ATTR_LABEL[i].toLowerCase()+' '+v:'').filter(Boolean).join(', '),fl=FAM_LABEL.filter((_,i)=>r.flavours&(1<<i)).map(x=>x.toLowerCase()).join(', ');
return `<div class="item" style="cursor:default"><div><b style="color:var(--acc)">${starTxt(r.stars)}</b> <span class="sm">${named||r.drinker?esc(drinkerName(r.drinker))+' · ':''}${r.t?new Date(r.t*1000).toLocaleDateString():''}${b&&b.roastDate?' · bag roasted '+fd(b.roastDate):''}</span><div class="sm dim">${[attrs,fl].filter(Boolean).join(' · ')||'stars only'}${r.share&&r.share!='local'?' · shared: '+esc(r.share):''}</div></div><div style="white-space:nowrap"><button class="tbtn" onclick="editRating(${r.id})">Edit</button><button class="tbtn del" onclick="delRating(${r.id})">Delete</button></div></div>`}).join(''):'<div class="sm dim">Not rated yet.</div>'}
function pickDrinker(id){RS=null;const c=coffee(C),old=ratingFor(C,(c&&c.bagId)||0,id);RS=old?{coffee:C,id:old.id,drinker:id,stars:old.stars,attr:[...old.attr],fl:old.flavours}:{coffee:C,id:0,drinker:id,stars:0,attr:[0,0,0,0,0],fl:0};RS.forBag=(c&&c.bagId)||0;drawRating()}
function editRating(id){const r=RT.ratings.find(x=>x.id==id);if(!r)return;RS={coffee:C,id:r.id,drinker:r.drinker,stars:r.stars,attr:[...r.attr],fl:r.flavours,bag:r.bag};drawRating();$('crate').scrollIntoView({behavior:'smooth'})}
function saveRating(){const c=coffee(C);if(!c||!RS)return;if(!RS.stars)return say('Give it 1 to 5 stars first',false);
post('/api/coffee/rate',{id:RS.id||0,coffee:C,bag:RS.bag!=null?RS.bag:(c.bagId||0),drinker:RS.drinker,stars:RS.stars,attr:RS.attr,flavours:RS.fl}).then(r=>{done(r,'Rating saved');if(r.ok){RS.id=r.id;loadRatings()}})}
async function delRating(id){if(!await ask('Delete this rating?','Delete'))return;post('/api/coffee/rate/delete?id='+id).then(r=>{done(r,'Deleted');if(RS&&RS.id==id)RS=null;loadRatings()})}

// ---- Insights: the taste profile as two radar charts
let TPD=0,TPBUSY=false;
function radar(c,labels,vals,known){const x=c.getContext('2d'),W=c.width,H=c.height,cx=W/2,cy=H/2+4,R=Math.min(W,H)/2-46,n=labels.length;x.clearRect(0,0,W,H);x.font='13px sans-serif';
const P=(i,r)=>[cx+r*Math.sin(2*Math.PI*i/n),cy-r*Math.cos(2*Math.PI*i/n)];
[.25,.5,.75,1].forEach(f=>{x.beginPath();for(let i=0;i<=n;i++){const[a,b]=P(i%n,R*f);i?x.lineTo(a,b):x.moveTo(a,b)}x.strokeStyle=f==.5?TC.edge:TC.grid;x.lineWidth=f==.5?1.5:1;x.stroke()});x.lineWidth=1;
labels.forEach((l,i)=>{const[a,b]=P(i,R);x.strokeStyle=TC.grid;x.beginPath();x.moveTo(cx,cy);x.lineTo(a,b);x.stroke();const[lx,ly]=P(i,R+22);x.fillStyle=known[i]>.05?TC.mid:TC.dim;const w=x.measureText(l).width;x.fillText(l,Math.max(2,Math.min(W-w-2,lx-w/2)),ly+5)});
x.beginPath();vals.forEach((v,i)=>{const[a,b]=P(i,R*v);i?x.lineTo(a,b):x.moveTo(a,b)});x.closePath();x.fillStyle='rgba(240,138,36,.22)';x.fill();x.strokeStyle='#f08a24';x.lineWidth=2;x.stroke();x.lineWidth=1;
vals.forEach((v,i)=>{const[a,b]=P(i,R*v);x.globalAlpha=.35+.65*Math.min(1,known[i]);x.fillStyle='#f08a24';x.beginPath();x.arc(a,b,4,0,7);x.fill();x.globalAlpha=1})}
async function drawTaste(){const el=$('itaste');if(!el||TPBUSY)return;TPBUSY=true;try{if(!RT){try{RT=await get('/api/coffee/ratings')}catch(e){}}
const named=RT&&RT.named,people=named?RT.drinkers.map((n,i)=>[i+1,n]).filter(x=>x[1]):[];if(!named)TPD=0;
$('itwho').innerHTML=named&&people.length?[[0,'Household'],...people].map(([id,n])=>`<button class="${id==TPD?'on':''}" onclick="TPD=${id};drawTaste()">${esc(n)}</button>`).join(''):'';
let t;try{t=await get('/api/taste/profile?drinker='+TPD)}catch(e){el.style.display='none';return}el.style.display='';
const p=t.profile,lab=t.dims.map(d=>d[0].toUpperCase()+d.slice(1));radar($('itcup'),lab.slice(0,5),p.v.slice(0,5),p.known.slice(0,5));radar($('itfl'),lab.slice(5),p.v.slice(5),p.known.slice(5));
const lean=t.dims.map((d,i)=>({d,i,s:(p.v[i]-.5)*Math.min(1,p.known[i]*1.5)})).filter(o=>Math.abs(o.s)>.06),name=o=>o.i<5?(o.s>0?'more ':'less ')+o.d:o.d;
const to=lean.filter(o=>o.s>0).sort((a,b)=>b.s-a.s).slice(0,4).map(name),from=lean.filter(o=>o.s<0&&o.i>=5).sort((a,b)=>a.s-b.s).slice(0,3).map(name);
$('itnote').innerHTML=!p.coffees?'<span class="dim">No coffees rated yet. Rate the coffees you drink on the Coffees page (How you liked it) and your taste shows here.</span>':
(!p.shown?`<span class="dim">Early days: ${p.coffees} coffee${p.coffees==1?'':'s'} rated; the profile shows from 3.</span> `:'')+(to.length?`You lean towards <b>${to.join(', ')}</b>`:'')+(from.length?`${to.length?'; ':'You lean '}away from <b>${from.join(', ')}</b>`:'')+(to.length||from.length?'.':'')+(p.shown&&!p.settled?` <span class="dim">(${p.coffees} coffees rated: settles from 8)</span>`:'');
drawReco();$('itbest').innerHTML=(t.best||[]).length?'<div class="subh">Liked best</div><div class="likes">'+t.best.map(b=>{const c=coffee(b.coffee);return c?`<a href="#coffees" onclick="C=${c.id}" title="${esc(c.roaster||'')}">${esc(c.name)}<b>${'★'.repeat(b.stars)}</b></a>`:''}).join('')+'</div>':''}finally{TPBUSY=false}}

// Home, For you: what there is to discover, as chips that lead to it. Hidden when there is nothing.
async function loadForYou(){const box=$('foryou');if(!box)return;const chips=[];
const tr=await get('/api/taste/recommend?drinker='+(typeof TPD!='undefined'?TPD:0)).catch(()=>null);
if(tr&&tr.shown&&tr.items&&tr.items.length)chips.push(`<a href="#discover">Coffees you might like: <b>${esc(tr.items[0].name||'')}</b>${tr.items.length>1?' and '+(tr.items.length-1)+' more':''}</a>`);
else if(tr&&!tr.shown)chips.push(`<a href="#coffees">Rate ${moreTxt(tr.coffees)} to get coffee recommendations</a>`);
const sr=await get('/api/subscriptions/recommend?drinker='+(typeof TPD!='undefined'?TPD:0)).catch(()=>null);
if(sr&&sr.items&&sr.items.length)chips.push(`<a href="#discover" onclick="setTimeout(()=>{const e=$('subreco');if(e)e.scrollIntoView({behavior:'smooth'})},300)">Subscriptions you might like: <b>${esc(sr.items[0].service.name)}</b></a>`);
if(G&&G.has_number&&(G.action=='finer'||G.action=='coarser')&&G.suggested_um)chips.push(`<a href="#grinder">Next shot: ${G.action} ${fdn(G.delta_um)} ${fdu()}, to ${fs(G.suggested_um)}</a>`);
$('fychips').innerHTML=chips.join('');box.style.display=chips.length?'':'none'}
// Discover: who it is for (named drinkers), the coffees and the subscriptions.
async function loadDiscover(){loadLibraryPanels();if(!RT&&typeof loadRatings=='function')await loadRatings();
const named=RT&&RT.named,people=named?RT.drinkers.map((n,i)=>[i+1,n]).filter(x=>x[1]):[];
$('dwho').innerHTML=people.length?[[0,'Household'],...people].map(([id,n])=>`<button class="${id==TPD?'on':''}" onclick="TPD=${id};loadDiscover()">${esc(n)}</button>`).join(''):'';
drawReco();if(!SUBS)await loadSubs();loadSubReco()}
// Community: what others settled on for what is being brewed, the library's coffees and grinders, and
// what this controller shares.
async function loadCommunityPage(){if(!L)return;const ps=L.presets[L.activePreset]||{},c=coffee(ps.coffeeId);
$('cmcoffee').textContent=c?c.name:'';drawCommunity('cmnow',ps.coffeeId||0);
loadFeed();loadTrending();rdShots();
if(!CC.length)await loadCommunityCoffees();drawCommunityCoffees();
if(!GM.length)await loadCommunityGrinders();const g=GM.filter(x=>x.id);
$('cmgrinders').innerHTML=g.length?g.length+' grinder models, each with how its setting reads and its range. '+'<span class="dim">'+g.slice(0,12).map(x=>esc(x.model)).join(' · ')+(g.length>12?' and '+(g.length-12)+' more':'')+'</span>. Pick yours on the <a href="#grinder">Grinder</a> page; owners of the same model pool what they learn.':'<div class="empty">The shared grinder list cannot be read yet.</div>';
let sh;try{sh=await get('/api/share')}catch(e){sh=null}
$('cmstate').textContent=!sh?'':S&&S.shareEnabled?'sharing':'not sharing';
$('cmshare').innerHTML=!sh?'':!(S&&S.shareEnabled)?'<div class="empty">You are not sharing yet. Turn it on in <a href="#setup">Setup › Shared library</a> to send your coffees, dial-ins and (if you choose) ratings. Nothing personal leaves the controller, and no account is needed.</div>':
`${sh.sent} record${sh.sent==1?'':'s'} sent, ${sh.waiting} waiting. ${sh.problem?'<span style="color:var(--warn)">Not reaching the library: '+esc(sh.problem)+'.</span> ':sh.last_ok?'Last reached '+new Date(sh.last_ok*1000).toLocaleString()+'. ':''}What each coffee shares, and what reviewers said, is on its page under <a href="#coffees">Coffees</a>.`}
// ---- The Community Hub: the feed of reviewed links, suggestions, trending recipes, the Reddit packager
const LK_KINDS=[['video','Video'],['playlist','Playlist'],['post','Post'],['guide','Guide'],['article','Article'],['podcast','Podcast']];
const LK_TOPICS=[['preparation','Preparation'],['dialing_in','Dialling in'],['brewing','Brewing'],['beans','Beans'],['gear','Gear'],['milk','Milk'],['maintenance','Upkeep'],['science','Science']];
const LK_STATE={queued:'waiting to send',sent:'sent',review:'with a reviewer',published:'approved: in the feed',declined:'declined',refused:'refused by the library\'s checks',local:'not sent'};
let LK=null,LKT='',LKSEL=[];
const ytId=u=>{const m=/(?:youtube\.com\/watch\?(?:.*&)?v=|youtu\.be\/|youtube\.com\/shorts\/)([\w-]{11})/.exec(u||'');return m?m[1]:''};
const lkThumb=l=>l.image||(ytId(l.url)?'https://i.ytimg.com/vi/'+ytId(l.url)+'/hqdefault.jpg':'');
const lkLabel=(list,k)=>(list.find(x=>x[0]==k)||[k,k])[1];
async function loadFeed(){try{LK=await get('/api/links')}catch(e){LK=null}drawFeed()}
function drawFeed(){if(!$('cmfeed'))return;const listed=(LK&&LK.listed)||[];
$('cmtopics').innerHTML=[['','All'],...LK_TOPICS].filter(t=>!t[0]||listed.some(l=>(l.topics||[]).includes(t[0]))).map(([k,n])=>`<button class="${k==LKT?'on':''}" onclick="LKT='${k}';drawFeed()">${n}</button>`).join('');
const shown=listed.filter(l=>!LKT||(l.topics||[]).includes(LKT));
$('cmfeed').innerHTML=!LK?'<div class="empty">The feed cannot be read from the controller.</div>':!listed.length?'<div class="empty">Nothing in the feed yet. It fills as owners suggest links and a reviewer approves them: suggest the first one.</div>':
shown.map(l=>{const th=lkThumb(l);return `<a class="reco" href="${esc(l.url)}" target="_blank" rel="noopener noreferrer">${th?`<img src="${esc(th)}" alt="" loading="lazy" referrerpolicy="no-referrer" style="width:100%;aspect-ratio:16/9;object-fit:cover;border-radius:8px;margin-bottom:8px;background:var(--up)">`:''}<span class="src">${esc(lkLabel(LK_KINDS,l.kind))} · ${esc(l.site||'')}</span><b>${esc(l.title)}</b>${l.why?'<div class="sm" style="margin-top:6px">'+esc(l.why)+'</div>':''}<div class="sm dim" style="margin-top:6px">${(l.topics||[]).map(t=>esc(lkLabel(LK_TOPICS,t))).join(' · ')}${l.added?' · added '+esc(l.added):''}</div></a>`}).join('');
const mine=(LK&&LK.mine)||[];
$('lkmine').innerHTML=mine.length?'<div class="subh">Your suggestions</div>'+mine.map(m=>`<div class="pair"><span class="what"><b>${esc(m.title)}</b> <span class="dim">${esc(LK_STATE[m.state]||m.state)}${m.note?': '+esc(m.note):''}</span></span><button class="tbtn del" onclick="lkDelete(${m.id})">Delete</button></div>`).join(''):''}
function lkToggle(){const f=$('lkform'),open=f.style.display=='none';f.style.display=open?'':'none';if(!open)return;
$('lk_kind').innerHTML=LK_KINDS.map(([k,n])=>`<option value="${k}">${n}</option>`).join('');LKSEL=[];lkTopics();$('lk_url').focus()}
function lkTopics(){$('lk_topics').innerHTML=LK_TOPICS.map(([k,n])=>`<button class="${LKSEL.includes(k)?'on':''}" aria-pressed="${LKSEL.includes(k)}" onclick="lkTopic('${k}')">${n}</button>`).join('')}
function lkTopic(k){const i=LKSEL.indexOf(k);if(i>=0)LKSEL.splice(i,1);else if(LKSEL.length<3)LKSEL.push(k);else return say('Three topics at most',false);lkTopics()}
async function lkRead(){const u=$('lk_url').value.trim();if(!/^https?:\/\//i.test(u))return say('Paste a link starting https://',false);
const yt=ytId(u);if(yt){$('lk_kind').value=/list=/.test(u)&&!/v=/.test(u)?'playlist':'video';$('lk_image').value='https://i.ytimg.com/vi/'+yt+'/hqdefault.jpg'}else if(/list=/.test(u))$('lk_kind').value='playlist';
$('lk_read').textContent='Reading the page...';const r=await post('/api/subscriptions/preview',{url:u});if(r&&r.ok===false){$('lk_read').textContent=r.error||'Could not read it: type the title.';return}
for(let i=0;i<40;i++){await new Promise(x=>setTimeout(x,750));let g;try{g=await get('/api/subscriptions/preview')}catch(e){continue}
if(g.state=='done'){if(g.title&&!$('lk_title').value)$('lk_title').value=g.title.slice(0,79);if(g.image&&!$('lk_image').value)$('lk_image').value=g.image;$('lk_read').textContent=g.site?'Read from '+g.site+'. Check the title.':'Read. Check the title.';return}
if(g.state=='failed'){$('lk_read').textContent='Could not read it ('+(g.problem||'no answer')+'): type the title.';return}}
$('lk_read').textContent='No answer from the page: type the title.'}
function lkSend(){const u=$('lk_url').value.trim(),t=$('lk_title').value.trim();if(!u)return say('Paste the link first',false);if(!t)return say('Give it a title',false);if(!LKSEL.length)return say('Pick at least one topic',false);
post('/api/links/suggest',{url:u,title:t,kind:$('lk_kind').value,topics:LKSEL,why:$('lk_why').value.trim(),image:$('lk_image').value}).then(r=>{done(r,'Sent for review: it shows in the feed once approved');if(r.ok){['lk_url','lk_title','lk_why','lk_image'].forEach(k=>$(k).value='');$('lk_read').textContent='';LKSEL=[];$('lkform').style.display='none';loadFeed()}})}
async function lkDelete(id){if(!await ask('Delete this suggestion from the list here? What a reviewer already approved stays in the feed.','Delete'))return;post('/api/links/delete?id='+id).then(r=>{done(r,'Deleted');loadFeed()})}
const METHOD={straight:'straight through',preinfusion:'held pre-infusion',bloom:'bloom'};
async function loadTrending(){let t;try{t=await get('/api/trending')}catch(e){t=null}const el=$('cmtrend');if(!el)return;const rs=(t&&t.recipes)||[];
$('cmtrendnote').textContent=t&&t.days?'the last '+t.days+' days':'';
el.innerHTML=rs.length?'<div class="recos">'+rs.map((r,i)=>`<div class="reco"><span class="src">#${i+1}</span><b>${r.dose_g} g in, ${r.yield_g} g out</b><div class="sm">1:${r.ratio} in about ${r.time_s} s, ${esc(METHOD[r.method]||r.method)}</div><div class="sm dim" style="margin-top:6px">${r.contributors} people, ${r.dialins} dial-in${r.dialins==1?'':'s'}, ${r.coffees} coffee${r.coffees==1?'':'s'}</div><button style="margin-top:8px" onclick="trendRecipe(${r.dose_g},${r.yield_g},${r.time_s},'${esc(r.method)}')">Make it a recipe</button></div>`).join('')+'</div>':
'<div class="empty">Coming as more people share: the recipes owners settled on lately, once at least two people settled on the same one.</div>'}
function trendRecipe(dose,out,time,method){location.hash='#recipes';setTimeout(()=>{R=0;drawRecipes();const set=(k,v)=>{const e=$('r_'+k);if(e){e.value=v;e.dispatchEvent(new Event('input'))}};set('name',dose+' g in, '+out+' g out');set('doseG',dose);set('targetWeightG',out);set('targetTimeS',Math.round(time));say('Filled in: check it and save')},400)}
// The Reddit packager: a 1200 x 1200 picture and the post's text, made here in the browser.
let RDP=null;
function rdShots(){const sel=$('rd_shot');if(!sel||typeof SH=='undefined'||!SH)return;const cur=+sel.value||0;
sel.innerHTML=SH.slice(0,40).map(s=>`<option value="${s.id}">${esc(when(s))} · ${esc(s.coffee||s.recipe)} · ${f1(s.final_weight_g)} g in ${f1(s.shot_time_s)} s</option>`).join('');if(cur&&SH.some(s=>s.id==cur))sel.value=cur;if(SH.length)rdBuild()}
function toReddit(id){location.hash='#community';setTimeout(()=>{rdShots();$('rd_shot').value=id;rdBuild();$('rdcard').scrollIntoView({behavior:'smooth'})},500)}
function rdPhoto(inp){const f=inp.files&&inp.files[0];if(!f){RDP=null;return rdBuild()}const img=new Image();img.onload=()=>{RDP=img;rdBuild()};img.src=URL.createObjectURL(f)}
async function rdBuild(){const id=+$('rd_shot').value,s=(SH||[]).find(x=>x.id==id);if(!s)return;
let d=null;if(s.has_data){try{d=await get('/api/shot/data?id='+id)}catch(e){}}if(+$('rd_shot').value!=id)return;
const c=L&&L.coffees.find(x=>x.id==s.coffee_id),g=L?L.grinder:{},cs=getComputedStyle(document.documentElement),col=k=>cs.getPropertyValue(k).trim()||'#888';
const dose=s.dose_g>0?s.dose_g:0,out=s.final_weight_g,ratio=dose?(out/dose).toFixed(1):'',days=c&&c.roastDate&&s.timestamp?Math.round((Date.parse(s.timestamp)-Date.parse(dateIso(c.roastDate)))/864e5):null;
const name=c?c.name:(s.coffee||'Espresso');
if(!$('rd_title').dataset.edited||$('rd_title').dataset.for!=id){$('rd_title').value=`${name}${c&&c.roaster?' ('+c.roaster+')':''}: ${dose?f1(dose)+' g in, ':''}${f1(out)} g out in ${f1(s.shot_time_s)} s`;$('rd_title').dataset.for=id;$('rd_title').oninput=()=>$('rd_title').dataset.edited=1}
const cv=$('rd_img'),x=cv.getContext('2d'),W=1200;x.fillStyle=col('--bg');x.fillRect(0,0,W,W);
x.fillStyle=col('--fg');x.font='600 52px sans-serif';x.fillText(name.slice(0,34),48,96);x.fillStyle=col('--dim');x.font='28px sans-serif';
x.fillText([c&&c.roaster,c&&c.origin,c&&c.process].filter(Boolean).join(' · ').slice(0,70),48,140);
if(d&&d.time_s&&d.time_s.length>1){draw('rdcurve',s,d.time_s,d.weight_g,d.flow_g_s);x.fillStyle=col('--card');x.fillRect(40,170,W-80,500);x.drawImage($('rdcurve'),40,185,W-80,470)}
else{x.fillStyle=col('--card');x.fillRect(40,170,W-80,500);x.fillStyle=col('--dim');x.font='28px sans-serif';x.fillText('No curve stored for this shot',80,430)}
const lines=[['Dose',dose?f1(dose)+' g':'not set'],['Yield',f1(out)+' g'+(ratio?' (1:'+ratio+')':'')],['Time',f1(s.shot_time_s)+' s'+(s.first_drop_s>0?', first drops '+f1(s.first_drop_s)+' s':'')],['Flow',f2(s.pour_flow_g_s)+' g/s, peak '+f2(s.peak_flow_g_s)],
['Grind',(s.grind_um?fs(s.grind_um):'not recorded')+(g&&g.model?' on '+g.model:'')],['Start',s.bloom_pause_s?'bloom '+s.bloom_pause_s+' s':s.preinfusion_hold_s?'held pre-infusion':'machine pre-infusion'],['Roasted',c&&c.roastDate?fd(c.roastDate)+(days!=null&&days>=0?' ('+days+' days)':''):'']].filter(l=>l[1]);
const px=RDP?600:W-80;x.fillStyle=col('--card');x.fillRect(40,700,px-40+(RDP?0:40),440);x.font='30px sans-serif';
lines.forEach((l,i)=>{x.fillStyle=col('--dim');x.fillText(l[0],70,756+i*56);x.fillStyle=col('--fg');x.fillText(String(l[1]).slice(0,RDP?26:52),230,756+i*56)});
if(RDP){const sz=460,sx=640,sy=700,r=Math.max(sz/RDP.width,(440)/RDP.height),w=RDP.width*r,h=RDP.height*r;x.save();x.beginPath();x.rect(sx,sy,W-40-sx,440);x.clip();x.drawImage(RDP,sx+(W-40-sx-w)/2,sy+(440-h)/2,w,h);x.restore()}
x.fillStyle=col('--dim');x.font='22px sans-serif';x.fillText('Brew by Weight controller · github.com/hallge/Brew-Controller',48,1180);
const md=[`**Coffee:** ${name}${c&&c.roaster?' from '+c.roaster:''}${c&&(c.origin||c.process)?' ('+[c.origin,c.process].filter(Boolean).join(', ')+')':''}${c&&c.roastDate?', roasted '+fd(c.roastDate)+(days!=null&&days>=0?' ('+days+' days before this shot)':''):''}`,
`**Recipe:** ${dose?f1(dose)+' g in, ':''}${f1(out)} g out${ratio?' (1:'+ratio+')':''} in ${f1(s.shot_time_s)} s${s.first_drop_s>0?'; first drops at '+f1(s.first_drop_s)+' s':''}${s.bloom_pause_s?'; bloom '+s.bloom_pause_s+' s':''}`,
`**Grind:** ${s.grind_um?fs(s.grind_um):'not recorded'}${g&&g.model?' on a '+g.model:''}`,
`**Flow:** ${f2(s.pour_flow_g_s)} g/s on average while pouring, peak ${f2(s.peak_flow_g_s)} g/s`,
s.taste?`**Taste:** ${s.taste}${s.rating?', '+s.rating+'/5':''}`:'',c&&c.notes?`**Tasting notes on the bag:** ${c.notes}`:'',
'','*Stopped by weight with an open-source brew-by-weight controller: https://github.com/hallge/Brew-Controller*'].filter((l,i,a)=>l||a[i+1]);
$('rd_text').value=md.join('\n\n').replace(/\n\n\n+/g,'\n\n')}
function rdDownload(){const id=$('rd_shot').value;$('rd_img').toBlob(b=>{const a=document.createElement('a');a.href=URL.createObjectURL(b);a.download='espresso-shot-'+id+'.png';a.click();setTimeout(()=>URL.revokeObjectURL(a.href),4000)},'image/png')}
async function rdCopy(){try{await navigator.clipboard.writeText($('rd_text').value);say('Text copied')}catch(e){$('rd_text').select();say('Select-all done: copy it with Ctrl+C',false)}}
function rdOpen(){window.open('https://www.reddit.com/r/'+$('rd_sub').value+'/submit?title='+encodeURIComponent($('rd_title').value),'_blank','noopener')}
// ---- The coffee directory: the library's coffees as kept on the controller (/api/directory)
let DIRP=0,DIRT=0,DIRF=null;
async function loadDirectory(){const el=$('dirlist');if(!el)return;const q=new URLSearchParams({q:$('dirq').value.trim(),origin:$('diro').value||'',process:$('dirp').value||'',roast:$('dirr').value||'',page:DIRP});
let d;try{d=await get('/api/directory?'+q)}catch(e){el.innerHTML='<div class="empty">The directory cannot be read from the controller.</div>';return}
if(d.facets&&!DIRF){DIRF=d.facets;const opt=(id,f,all)=>{$(id).innerHTML=`<option value="">${all}</option>`+Object.entries(f||{}).map(([k,n])=>`<option value="${esc(k)}">${esc(k)} (${n})</option>`).join('')};opt('diro',DIRF.origin,'Any origin');opt('dirp',DIRF.process,'Any process');opt('dirr',DIRF.roast_level,'Any roast')}
$('dircount').textContent=d.count?d.count+' coffees':'';
el.innerHTML=!d.kept_pages?'<div class="empty">The directory has not been read from the shared library yet. It comes with the library\'s next read (within the hour, while the controller is idle).</div>':!d.coffees.length?'<div class="empty">No coffee in the directory matches that.</div>':
d.coffees.map(c=>`<div class="pair" style="background:var(--combg);border-color:var(--comline)"><span class="what"><b>${esc(c.name)}</b> <span class="dim">${esc([c.roaster,c.origin,c.process,c.roast_level].filter(Boolean).join(' · '))}</span>${c.stars?` <span style="color:var(--acc)">★${c.stars}</span> <span class="dim">(${c.n})</span>`:''}${c.notes?'<div class="sm dim">'+esc(c.notes)+'</div>':''}</span>${c.on_shelf?'<span class="sm dim">on your shelf</span>':`<button onclick="dirAdopt('${esc(c.id)}')">Add to my coffees</button>`}</div>`).join('');
const pages=Math.ceil(d.matched/d.page_size);$('dirpager').innerHTML=pages>1?`<button ${DIRP?'':'disabled'} onclick="DIRP--;loadDirectory()">Previous</button><span class="sm dim">${d.matched} found · page ${DIRP+1} of ${pages}</span><button ${DIRP+1<pages?'':'disabled'} onclick="DIRP++;loadDirectory()">Next</button>`:(d.matched?`<span class="sm dim">${d.matched} found</span>`:'');
DIRC=d.coffees}
let DIRC=[];
function dirAdopt(id){const c=DIRC.find(x=>x.id==id);if(!c)return;if(!CC.find(x=>x.id==id))CC.push(c);adopt(id);setTimeout(loadDirectory,1500)}
// ---- The shared library on Discover: published coffees and recipes, and the ones waiting for review.
let LP=null,LR=[];const lbTab=(k,i)=>{[0,1].forEach(j=>{$('lb'+k+'_'+j).style.display=j==i?'':'none';$('lb'+k+'_t'+j).classList.toggle('on',j==i)})};
async function loadLibraryPanels(){try{LP=await get('/api/library/pending')}catch(e){LP=null}try{LR=((await get('/api/community/recipes')).items)||[]}catch(e){LR=[]}
loadCommunityCoffees().then(drawCommunityCoffees);drawPending();drawRecipeBook()}
const lbNum=v=>v==null?'':String(Math.round(v*10)/10);
const recipeLine=r=>`${lbNum(r.dose_g)} g in, ${lbNum(r.yield_g)} g out`+(r.target_time_s?`, ${lbNum(r.target_time_s)} s`:'')+(r.preinfusion_hold_s?`, hold ${lbNum(r.preinfusion_hold_s)} s`:'')+(r.bloom_pause_s?`, bloom ${lbNum(r.bloom_pause_s)} s`:'');
function drawRecipeBook(){const el=$('lbr_list');if(!el)return;const q=($('lbrq').value||'').toLowerCase(),hit=LR.filter(x=>!q||[(x.recipe||{}).name,recipeLine(x.recipe||{})].join(' ').toLowerCase().includes(q));
$('lbr_c').textContent=LR.length?LR.length:'';
el.innerHTML=!LR.length?'<div class="empty">No recipes published yet. Share one of yours from the Recipes page.</div>':!hit.length?'<div class="empty">No recipe matches that.</div>':hit.map(x=>{const r=x.recipe||{},i=LR.indexOf(x);
return `<div class="it"><div class="what"><b>${esc(r.name||'Recipe')}</b><span class="sm mid">${esc(recipeLine(r))}</span>${x.contributors>1?`<span class="sm dim"> \u00b7 ${x.contributors} owners</span>`:''}</div><button onclick="bookRecipe(${i})">Make it a recipe</button></div>`}).join('')}
function bookRecipe(i){const r=(LR[i]||{}).recipe;if(!r)return;location.hash='#recipes';setTimeout(()=>{R=0;drawRecipes();const set=(k,v)=>{const e=$('r_'+k);if(e){e.value=v;e.dispatchEvent(new Event('input'))}};
set('name',(r.name||'').slice(0,23));set('doseG',r.dose_g);set('useRatio',0);set('targetWeightG',r.yield_g);if(r.target_time_s)set('targetTimeS',r.target_time_s);
if(r.preinfusion_hold_s){set('preinfusion',1);set('preinfusionHoldS',r.preinfusion_hold_s)}if(r.bloom_pause_s)set('bloomPauseS',r.bloom_pause_s);recipeForm();say('Filled in: check it and save')},400)}
function drawPending(){if(!$('lbc_list'))return;const idx=(LP&&LP.index&&LP.index.items)||[],mine=new Map(((LP&&LP.mine)||[]).map(m=>[m.target,m.approve]));
const why=!LP?'The controller cannot be reached.':!LP.linked?'Link the shared library in Setup to see what is waiting.':LP.can_review?(k=>k=='c'?'Check the details against the bag, then approve or decline.':'Pull it once if you can, then approve or decline.'):'You can review once something you shared has been published.';
['c','r'].forEach(k=>{const list=idx.filter(x=>x.type==(k=='c'?'coffee':'recipe')),el=$(k=='c'?'lbc_list':'lbr_pend');$('lb'+k+'_why').textContent=typeof why=='function'?why(k):why;$('lb'+k+'_n').textContent=list.length?list.length:'';
el.innerHTML=!list.length?'<div class="empty">Nothing waiting for review.</div>':list.map(x=>{const own=LP.kid&&x.from==LP.kid,done=mine.has(x.id),c=x.coffee||{},r=x.recipe||{};
const title=k=='c'?esc(c.name||'Coffee'):esc(r.name||'Recipe'),line=k=='c'?esc([c.roaster,c.origin,c.region,c.process,c.roast_level].filter(Boolean).join(' \u00b7 ')):esc(recipeLine(r));
const notes=k=='c'&&c.notes?`<span class="sm dim" style="display:block">${esc(c.notes)}</span>`:'',count=`<span class="sm dim">${x.approvals||0} of 2 approvals${x.declines?', '+x.declines+' declined':''}</span>`;
const act=own?'<span class="pill">yours, waiting</span>':done?`<span class="pill ${mine.get(x.id)?'ok':''}">you ${mine.get(x.id)?'approved':'declined'}</span>`:LP.can_review?`<button class="pri" onclick="reviewItem('${esc(x.id)}',1)">Approve</button><button onclick="declineOpen(this)">Decline\u2026</button>`:'';
return `<div class="it"><div class="what"><b>${title}</b><span class="sm mid" style="display:block">${line}</span>${notes}${count}</div>${act}<div class="dn" style="display:none"><input maxlength="119" placeholder="Why? The sender sees this." aria-label="Why it is declined"><button onclick="reviewItem('${esc(x.id)}',0,this)">Send</button></div></div>`}).join('')})}
function declineOpen(b){const d=b.parentElement.querySelector('.dn');d.style.display=d.style.display?'':'none';if(!d.style.display)d.querySelector('input').focus()}
async function reviewItem(id,approve,b){const note=b?b.parentElement.querySelector('input').value.trim():'';if(!approve&&!note){say('Say why it is declined');return}
const r=await post('/api/library/review',{target:id,verdict:approve?'approve':'decline',note});if(done(r,approve?'Approved: sent with your key':'Declined: sent with your note')){if(LP){LP.mine=(LP.mine||[]).concat([{target:id,approve:!!approve}])}drawPending()}}
// Recipes page: where this recipe stands with the recipe book.
const SHARE_WORDS={queued:'waiting to be sent',sent:'sent',review:'in review',published:'published in the recipe book',declined:'declined',refused:'refused'};
async function drawRecipeShare(){const b=$('rshare');if(!b||!R)return;if(!LP){try{LP=await get('/api/library/pending')}catch(e){return}}
const st=((LP.recipes||[]).find(x=>x.id==R)||{});b.textContent=st.state&&st.state!='local'?'Share again':'Share to the recipe book';b.disabled=!LP.linked;b.title=LP.linked?'':'Link the shared library in Setup first';
if(st.state&&st.state!='local')$('rinfo').textContent='Recipe book: '+(SHARE_WORDS[st.state]||st.state)+(st.note?': '+st.note:'')}
async function shareRecipe(){if(!R)return;const r=await post('/api/recipes/share?id='+R);if(done(r,'Queued for the recipe book')){LP=null;setTimeout(drawRecipeShare,1500)}}
function drawCommunityCoffees(){const el=$('cmlist');if(!el)return;const q=($('cmq').value||'').toLowerCase(),have=new Set((L?L.coffees:[]).map(c=>c.libraryId).filter(x=>x));
const hit=CC.filter(c=>!q||[c.name,c.roaster,c.origin,c.region,c.process,c.varietal].join(' ').toLowerCase().includes(q));
$('cmcount').textContent=CC.length?CC.length+' coffee'+(CC.length==1?'':'s'):'';
el.innerHTML=!CC.length?'<div class="empty">No coffees published yet. Published coffees appear here, ready to add to your shelf.</div>':!hit.length?'<div class="empty">No shared coffee matches that.</div>':
hit.slice(0,40).map(c=>`<div class="pair" style="background:var(--combg);border-color:var(--comline)"><span class="what"><b>${esc(c.name)}</b> <span class="dim">${esc([c.roaster,c.origin,c.process].filter(x=>x).join(', '))}</span></span>${have.has(c.id)?'<span class="sm dim">on your shelf</span>':`<button onclick="adopt('${esc(c.id)}')">Add to my coffees</button>`}</div>`).join('')+(hit.length>40?'<div class="sm dim">and '+(hit.length-40)+' more: search to narrow it down.</div>':'')}
async function drawReco(){const el=$('ireco');if(!el)return;let r;try{r=await get('/api/taste/recommend?drinker='+TPD)}catch(e){el.style.display='none';return}el.style.display='';
$('irwho').textContent=TPD&&RT?drinkerName(TPD):'';
const fromTxt={shelf:'Your shelf',library:'Library',community:'Library ratings'};
if(!r.shown){$('irlist').innerHTML=`<div class="empty">Rate ${moreTxt(r.coffees)} on the <a href="#coffees">Coffees</a> page and recommendations appear here.</div>`;$('iragain').innerHTML='';return}
$('irlist').innerHTML=r.items.length?r.items.map(it=>{const like=it.like&&coffee(it.like);const why=it.why.length?'<b>'+esc(it.why.join(', '))+'</b>':'',likeTxt=like?`Like the ${esc(like.name)} you gave <span style="color:var(--acc)">${starTxt(it.like_stars)}</span>`:'';
const extra=[it.people_stars?`drinkers like you: ★${it.people_stars}`:'',it.community_n?`library: ★${it.community_stars} from ${it.community_n}`:'',it.sure<.4?'a guess from its description':''].filter(Boolean).join(' · ');
const desc=[it.roaster,it.origin,it.process].filter(Boolean).map(esc).join(' · ');
return `<${it.coffee?'a href="#coffees" onclick="C='+it.coffee+'"':'div'} class="reco"><span class="src">${fromTxt[it.from]||''}</span><b>${esc(it.name)}</b>${desc?'<div class="sm dim">'+desc+'</div>':''}${why?'<div class="sm" style="margin-top:6px">'+why+'</div>':''}${likeTxt?'<div class="sm dim">'+likeTxt+'</div>':''}${extra?'<div class="sm dim">'+extra+'</div>':''}</${it.coffee?'a':'div'}>`}).join(''):'<div class="empty">Nothing new to recommend yet. Coffees on your shelf that you have not rated, and coffees in the shared library that match your taste, appear here as they come.</div>';
$('iragain').innerHTML=(r.buy_again||[]).length?'<span class="dim">Buy again:</span> '+r.buy_again.map(b=>`${esc(b.name)} <span style="color:var(--acc)">${starTxt(b.stars)}</span>`).join(', '):''}

function drawCoffees(){drawCoffeeList();drawRating();const c=coffee(C)||{roastLevel:0};$('ctitle').textContent=C?'Coffee':'New coffee';
CF.forEach(k=>{$('c_'+k).value=DK.includes(k)?i2d(c[k]):(c[k]==null?'':c[k])});drawBags(c);if(!c.altitudeM)$('c_altitudeM').value='';
$('cdel').style.display=$('cbag').style.display=C?'':'none';$('cbagrow').style.display='none';
$('cfin').style.display=C&&!c.finished&&c.bagId?'':'none';$('cbag').textContent=c.finished?'New bag: back on the shelf...':'New bag...';drawCoffeeShare(c);if(C)drawCommunity('ccom',C);else $('ccom').textContent='';$('cpairs').style.display=C?'':'none';
const wb=C?waitingBag(c):null;$('cinfo').textContent=wb&&(c.finished||!c.bagId)?'Waiting unopened: open it below when you start it.':c.finished?'Bag finished '+fd(c.finished)+'. Off the controller until a new bag is started.':'';
if(!C)return;
$('cplist').innerHTML=L.recipes.map(r=>{const i=L.presets.findIndex(p=>p.coffeeId==C&&p.recipeId==r.id),on=i>=0,p=on?L.presets[i]:null;
return `<div class="pair ${on?'on':''}"><input type="checkbox" aria-label="Brew ${esc(c.name)} with ${esc(r.name)}" ${on?'checked':''} ${c.finished?'disabled title="Bag finished: start a new bag first"':''} onchange="${on?`act('preset_delete',${i})`:`act('preset_add',${r.id},${C})`}"><span class="what"><b>${esc(r.name)}</b> <span class="dim">${r.doseG} g in, ${r.targetWeightG} g out, ${r.targetTimeS} s${r.bloomPauseS?', bloom '+r.bloomPauseS+' s':''}</span></span>`+(on?`<span class="dim sm" title="Set on the controller's recipe screen or the Grinder page; a recommendation is offered when the coffee is put in use">grinder ${p.grindUm?esc(fs(p.grindUm)):'not set yet'}</span>`:'')+'</div>'}).join('');
$('cpinfo').textContent='Each ticked line is one entry on the controller ('+L.presets.length+' of '+L.maxPresets+' used). The grind setting and the learned stop offset belong to the pair.'}
const SHN={local:'on this controller only',queued:'waiting to send',sent:'sent',review:'in review',published:'published',declined:'declined',refused:'refused by the library'};
function drawCoffeeShare(c){$('cshrow').style.display=C?'':'none';if(!C)return;const st=c.share||'local',on=S&&S.shareEnabled;
$('cshpill').textContent=st=='sent'&&!lib()?'not sent':SHN[st];
$('cshare').textContent=({local:on?'Not sent to the shared library yet.':'Sharing is switched off (Setup), so this coffee stays here.',queued:'Signed and waiting in the outbox.',sent:lib()?'Sent to the library. Its intake runs within a minute or two and puts it up for review.':'Written to this PC only ('+(SH_.via||'a folder')+'). It has not gone to the library.',review:'The library has it. A reviewer has not decided yet.',published:'Approved by a reviewer'+(c.libraryId?': in the library as '+c.libraryId:'')+'.',declined:'A reviewer did not accept it'+(c.reviewNote?': "'+c.reviewNote+'"':'')+'. What you typed is unchanged.',refused:'The library\'s checks turned this record away'+(c.reviewNote?': "'+c.reviewNote+'"':'')+'. Correct it if needed, then send it again.'})[st]+(st!='declined'&&st!='refused'&&c.reviewNote?' Reviewer: "'+c.reviewNote+'"':'')+(c.shareChanged?' You have changed it since that version was sent.':'');
const can=on&&(st=='local'||st=='refused'||c.shareChanged);$('csend').style.display=can?'':'none';$('csend').textContent=st=='local'?'Send to the shared library':st=='refused'?'Send again':'Send the new version'}
function sendCoffee(){post('/api/share/coffee?id='+C).then(r=>{done(r,'Queued for the shared library');loadLibrary();loadShare()})}
const RL=['','light','medium-light','medium','medium-dark','dark'];let CC=[];
async function loadCommunityCoffees(){try{CC=(await get('/api/community/coffees')).coffees||[]}catch(e){}if(L)drawCoffeeList()}
function adopt(id){const c=CC.find(x=>x.id==id);if(!c)return;post('/api/coffee/adopt',{libraryId:c.id,name:c.name,roaster:c.roaster||'',origin:c.origin||'',region:c.region||'',producer:c.producer||'',varietal:c.varietal||'',process:c.process||'',notes:c.notes||'',roastLevel:Math.max(0,RL.indexOf(c.roast_level||'')),altitudeM:c.altitude_m||0}).then(r=>{if(done(r,'Added to your shelf')){C=r.id;loadLibrary()}})}
let comSeq=0;
const simHtml=(m,x,r)=>{const s=m.similar;if(!s||!s.found)return '';
const hint=[];if(s.hint_roast)hint.push('roasted '+(s.hint_roast>0?'darker':'lighter')+' than it ('+(s.hint_roast>0?'usually ground coarser':'usually ground finer')+')');
if(s.hint_altitude)hint.push('grown '+(s.hint_altitude<0?'higher':'lower')+' ('+(s.hint_altitude<0?'denser, usually ground finer':'usually ground coarser')+')');
if(s.hint_process)hint.push('processed '+(s.hint_process>0?'more like a natural':'more like a washed')+' (advice differs and nothing measured supports it)');
const fac=[(s.applied&1)?'roast level':'',(s.applied&2)?'altitude':'',(s.applied&4)?'process':''].filter(v=>v).join(', ');
const how=s.basis=='fitted'?'Moved '+fdn(s.shift)+' '+esc(fdu())+(s.shift<0?' finer':' coarser')+' from it because your own '+s.samples+' coffees show that '+fac+' changes your setting.':'Not moved: no published figure says how far to move for roast, altitude or process, and your own coffees do not show it yet.';
const spread=s.samples>=2?' The range comes from how far your own settings are spread ('+s.samples+' coffees).':' With one coffee to go on the range is wide on purpose.';
return `<div class="tiles" style="margin-top:12px"><div class="tile" style="background:#2a2a1a"><span>A guess from your own coffees</span><b>${fsn(s.setting)} \u00b1 ${fdn(s.range)} ${esc(U)}</b><span>From ${esc(s.from_coffee)} (${fs(s.from_setting)}), your nearest coffee${s.near_recipe?' on a close recipe':''}. Grind advice takes over from your first shot.</span></div></div><details class="note"><summary>How this guess is made</summary><ul><li>${how}</li>${hint.length&&s.basis!='fitted'?'<li>This coffee is '+hint.join(' and ')+'.</li>':''}<li>${spread.trim()}</li></ul></details><div class="row" style="margin-top:12px"><button onclick="useStart(${x.i},${s.setting})">Use ${fs(s.setting)} as my starting grind</button><span>${x.p.grindUm?'Your setting for it now: '+esc(fs(x.p.grindUm))+'.':'No setting entered for it yet.'}</span></div>`};
async function drawCommunity(el,coffeeId){const box=$(el),c=coffee(coffeeId),mine=++comSeq;if(!c){box.textContent=coffeeId?'':'No coffee is attached to what is being brewed, so there is nothing from other people to show.';return}
const ps=L.presets.map((p,i)=>({p,i})).filter(x=>x.p.coffeeId==coffeeId);if(!ps.length){box.textContent=c.libraryId?'Tick a recipe for this coffee to see what others settled on for it.':c.name+' is not in the shared library yet. Once it is published, where other owners settle shows here.';return}
const x=ps.find(x=>x.i==L.activePreset)||ps[0],r=recipe(x.p.recipeId);let m;try{m=await get('/api/community?coffee='+coffeeId+'&recipe='+r.id)}catch(e){return}if(mine!=comSeq&&el=='ccom')return;
const sim=simHtml(m,x,r);
if(!c.libraryId){box.innerHTML=esc(c.name+' is not in the shared library yet. Once it is published, where other owners settle shows here.')+sim;return}
if(!m.found){box.innerHTML=esc('Nobody has shared a settled dial-in for '+c.name+' with a recipe like '+r.name+' ('+r.doseG+' g in, '+r.targetWeightG+' g out) yet.'+(m.reachable?'':' The shared library cannot be reached from this controller yet.'))+sim;return}
const s=m.start,why={fitted:'translated to your grinder through '+m.coffees_in_common+' coffee'+(m.coffees_in_common==1?'':'s')+' you have in common with others',same_model:'what '+m.same_model+' other'+(m.same_model==1?'':'s')+' with your grinder model set',model:'translated to your grinder through what the library has learned from other owners of your grinder model',microns:'the shared scale read as microns; how your own grinder sits against it is not known yet',none:'cannot be translated to your grinder yet'}[s.basis]+(s.age_shift?'; includes '+fdn(s.age_shift)+' '+esc(fdu())+' '+(s.age_shift<0?'finer':'coarser')+' because your bag is '+m.my_age_days+' days off roast and theirs were about '+m.their_age_days:(m.their_age_days>=0&&m.my_age_days>=0&&Math.abs(m.my_age_days-m.their_age_days)>=7&&!m.age_rate_known?'; your bag is '+m.my_age_days+' days off roast, theirs were about '+m.their_age_days+', and the library has not yet measured what that difference is worth':''))+(s.flow_shift?'; includes '+fdn(s.flow_shift)+' '+esc(fdu())+' '+(s.flow_shift<0?'finer':'coarser')+' because your recipe aims for a '+(s.flow_shift<0?'slower':'faster')+' flow than theirs':'')+(m.recipe_match=='near'?'; nobody has settled this exact recipe, so this comes from '+m.their_dose_g+' g in, '+m.their_yield_g+' g out, with a wider range':'');
box.innerHTML=`<div class="tiles"><div class="tile" style="background:#16263a"><span>Start your grinder at</span><b>${s.setting?fsn(s.setting)+' \u00b1 '+fdn(s.range)+' '+esc(U):'not known'}</b><span>${why}</span></div><div class="tile" style="background:#16263a"><span>What a good shot looked like</span><b>${f1(m.time_s)} s</b><span>${r.doseG} g in, ${r.targetWeightG} g out (${esc(r.name)})</span></div><div class="tile" style="background:#16263a"><span>Based on</span><b>${m.contributors} ${m.contributors==1?'person':'people'}</b><span>${m.dialins} settled dial-in${m.dialins==1?'':'s'}, within \u00b1 ${f1(m.spread_um)} \u00b5m of each other</span></div></div>`+(s.setting?`<div class="row" style="margin-top:12px"><button style="background:var(--com);border-color:var(--com);color:#06203a;font-weight:600" onclick="useStart(${x.i},${s.setting})">Use ${fs(s.setting)} as my starting grind</button><span>${x.p.grindUm?'Your setting for it now: '+esc(fs(x.p.grindUm))+'.':'No setting entered for it yet.'} A start only: grind advice takes it from there.</span></div>`:'');box.insertAdjacentHTML('beforeend',sim)}
async function useStart(i,um){const p=L.presets[i];if(p.grindUm&&p.grindUm!=um&&!await ask('Replace your setting of '+fs(p.grindUm)+' with '+fs(um)+'?'))return;act('preset_grind',i,um)}
async function saveCoffee(extra){const b={id:C||0};CF.forEach(k=>{const v=$('c_'+k).value;b[k]=DK.includes(k)?d2i(v):(k=='roastLevel'||k=='altitudeM')?+v||0:v.trim()});Object.assign(b,extra||{});
if(!b.name)return say('A coffee needs a name',false);
if(!C&&L.coffees.some(c=>c.name.toLowerCase()==b.name.toLowerCase()&&c.roaster.toLowerCase()==b.roaster.toLowerCase())&&!await ask('"'+b.name+'" is already on the shelf. Add it again?'))return;
post('/api/coffee',b).then(r=>{if(done(r,'Coffee saved')){C=r.id;loadLibrary()}})}
// The unopened bag a coffee would open next (oldest roast first), or null.
const waitingBag=c=>L.bags.filter(b=>b.coffeeId==c.id&&b.unopened).sort((a,b)=>(a.roastDate||9e9)-(b.roastDate||9e9))[0]||null;
function drawBags(c){const el=$('cbags');if(!el)return;const bs=c?L.bags.filter(b=>b.coffeeId==c.id).sort((a,b)=>b.id-a.id):[];if(!bs.length){el.innerHTML='';return}
el.innerHTML='<h3 style="margin:14px 0 6px">Bags of '+esc(c.name)+'</h3>'+bs.map(b=>{const f=frozenFor({frozenDate:b.frozenDate,thawDate:b.thawDate});const bits=[b.roastDate?'roasted '+i2d(b.roastDate):'roast date not entered',b.openedDate?'opened '+i2d(b.openedDate):'',f?f+' days frozen'+(b.frozenDate&&!b.thawDate?' (still there)':''):'',b.weightG?b.weightG+' g':''].filter(x=>x).join(', ');
const from=b.subscriptionId&&SUBS?SUBS.subscriptions.find(x=>x.id==b.subscriptionId):null;
if(b.onTheWay)return `<div class="row" style="align-items:center"><span style="flex:1 1 220px">${b.expectedDate?'expected '+i2d(b.expectedDate):'not come yet'}${b.weightG?', '+b.weightG+' g':''} <span class="pill" style="background:var(--combg);color:var(--comtext)">on its way</span>${from?' <span class="sm dim">from '+esc(from.name)+'</span>':''}</span><button class="pri" onclick="act('bag_arrived',${b.id}).then(()=>say('On the shelf, unopened: add its roast date',true))">It arrived</button><button class="del" onclick="ask('Forget this bag?').then(ok=>{if(ok){act('bag_delete',${b.id})}})">Forget</button></div>`;
if(b.unopened)return `<div class="row" style="align-items:center"><span style="flex:1 1 220px">${esc(bits)} <span class="pill warn">unopened</span>${from?' <span class="sm dim">from '+esc(from.name)+'</span>':''}</span><button class="pri" onclick="act('bag_open',${b.id}).then(()=>say('Bag opened: tracking starts now',true))">Open this bag</button><button class="del" onclick="ask('Forget this bag?').then(ok=>{if(ok){act('bag_delete',${b.id})}})">Forget</button></div>`;
return `<div class="row" style="align-items:center"><span style="flex:1 1 220px">${esc(bits)}${b.current?' <span class="pill">in use</span>':''}</span>${b.current?'':`<button onclick="act('bag_use',${c.id},${b.id})">Use this bag</button><button class="del" onclick="ask('Forget this bag? Its shots stay in the history.').then(ok=>{if(ok){act('bag_delete',${b.id})}})">Forget</button>`}</div>`}).join('')}
// ---- Subscriptions (core/Subscriptions.h): cards built from the shop's page, tracked here.
let SUBS=null,SF=null,SA=null;
const money=c=>'$'+(c/100).toFixed(2),perDay=v=>{const n=Math.round(v*10)/10;return n+(n==1?' shot':' shots')+' a day'},cap=t=>t?t.charAt(0).toUpperCase()+t.slice(1):t;
const subEvery=d=>!d?'not regular':d%7==0?(d==7?'every week':'every '+d/7+' weeks'):d>=28&&d<=31?'every month':d>=58&&d<=62?'every 2 months':'every '+d+' days';
const subWhen=s=>{if(s.days_until==null)return '';const d=s.days_until,t=fd(s.next_date);return d<0?t+' · '+(-d)+(d==-1?' day':' days')+' overdue':d==0?'today':d==1?'tomorrow':t+' · in '+d+' days'};
const imgTag=(u,cls)=>u?`<img src="${esc(u)}" alt="" loading="lazy" referrerpolicy="no-referrer" onerror="this.remove()">`:'';
const kindTxt={coffee:'Coffee',supplies:'Supplies',other:'Subscription'};
const platTxt={recharge:'Recharge',ordergroove:'Ordergroove',skio:'Skio',loop:'Loop',appstle:'Appstle',bold:'Bold',shopify:'Shopify'};
// One card, from a subscription (saved) or the form (preview).
function subCard(s,preview){const col=/^#[0-9a-f]{6}$/i.test(s.color||'')?s.color:'#5b6470',name=s.name||s.title||s.site||'Subscription';
const off=s.status!='active',bits=[kindTxt[s.kind]||'',subEvery(s.every_days),s.price_cents?money(s.price_cents):''].filter(x=>x).join(' · ');
const tags=(off?`<span class="pill">${s.status=='paused'?'Paused':'Cancelled'}</span>`:'')+(!off&&s.due_soon?`<span class="pill warn">${s.days_until<0?'Overdue':'Soon'}</span>`:'');
let sup='';if(s.kind=='supplies'&&(s.stock||s.per_delivery)){const full=Math.max(s.per_delivery||0,s.stock||0,1),pct=Math.min(100,Math.round(100*(s.stock||0)/full));
sup=`<div class="sm">${s.stock||0} ${esc(s.item||'left')}${s.stock_days_left!=null?' · about '+s.stock_days_left+' days'+(SUBS&&SUBS.shots_per_day?' at '+perDay(SUBS.shots_per_day)+(s.per_shot>1?' × '+s.per_shot:''):''):''}</div><div class="meter${s.stock_short?' warn':''}"><i style="width:${pct}%"></i></div>${s.stock_short?'<div class="sm" style="color:var(--warn)">Runs out before the next delivery</div>':''}`}
const acts=preview?'':`<div class="acts">${s.manage_url?`<a class="btn pri" href="${esc(s.manage_url)}" target="_blank" rel="noopener noreferrer">Manage ↗</a>`:s.url?`<a class="btn" href="${esc(s.url)}" target="_blank" rel="noopener noreferrer">Open ↗</a>`:''}${off?'':`<button onclick="subArrived(${s.id})">Arrived</button><button onclick="subSkip(${s.id})">Skip one</button>`}${EXT&&/^https:/.test(s.url||s.manage_url||'')?`<button class="tbtn" onclick="subUpdate([${s.id}])">Update</button>`:''}<button class="tbtn" onclick="subEdit(${s.id})">Edit</button></div>`;
return `<article class="sub${off?' off':''}"><div class="ban" style="background:linear-gradient(135deg,${col},${col}99 55%,var(--up))">${s.image?imgTag(s.image):''}${s.image?'':`<div class="ini">${esc(name.trim().charAt(0).toUpperCase())}</div>`}</div>
<div class="ico">${s.icon?imgTag(s.icon):`<div style="width:100%;height:100%;background:${col}"></div>`}</div><div class="tags">${tags}</div>${s.share&&s.share!='local'?`<div class="lib"><span class="pill">${s.share=='published'?'Library':esc(SHARE_TXT[s.share]||s.share)}</span></div>`:''}
<div class="bd"><b>${esc(name)}</b><span class="sm dim">${esc([s.site&&s.site!=name?s.site:'',platTxt[s.platform]?'via '+platTxt[s.platform]:''].filter(x=>x).join(' · '))}</span><span class="sm mid">${esc(bits)}</span>
${s.next_date?`<span class="sm">Next: ${esc(subWhen(s)||fd(s.next_date))}</span>`:''}${!preview&&typeof L!='undefined'&&L&&L.bags?L.bags.filter(b=>b.onTheWay&&b.subscriptionId==s.id).map(b=>{const c=coffee(b.coffeeId);return `<span class="sm" style="color:var(--com)">On its way: ${esc(c?c.name:'a bag')}${b.expectedDate?', expected '+fd(b.expectedDate):''}</span>`}).join(''):''}${sup}${s.note?`<span class="sm dim">${esc(s.note)}</span>`:''}${s.last_arrived?`<span class="sm dim">Last arrived ${fd(s.last_arrived)}</span>`:''}</div>${acts}</article>`}
// ---- The Brew Controller helper (browser-extension/): updates from the shops' pages, in this browser.
let EXT=(document.documentElement.dataset.bbwExtension||'').replace('unpaired',''),EXTSEEN=document.documentElement.dataset.bbwExtension=='unpaired',IMP={},SXS={};
window.addEventListener('message',e=>{if(e.source!==window||e.origin!==location.origin||!e.data||typeof e.data!='object')return;const m=e.data;
if(m.bbw=='stale'){say('The helper was reloaded or updated: reload this page to use it',false);return}
if(m.bbw=='hello'){EXT=m.paired===false?'':m.version||'1';EXTSEEN=m.paired===false;drawExt();drawSubs();return}
if(m.bbw!='result')return;SXS[m.subscription]={state:m.state,message:m.message};
if(m.state=='done'&&m.data&&typeof m.data=='object')IMP[m.subscription]=m.data;drawExt();drawImport()});
function drawExt(){const el=$('sxbody');if(!el)return;const list=SUBS&&SUBS.subscriptions||[],can=list.filter(s=>/^https:/.test(s.url||s.manage_url||'')&&(s.status||'active')=='active');
if(!EXT&&EXTSEEN){$('sxstate').textContent='helper installed';el.innerHTML=`<p class="sm mid" style="margin:0">The helper is installed but not connected to this controller. Click its icon in Chrome's toolbar and choose <b>Connect to this controller</b>; <b>Update</b> appears here as soon as it is.</p>`;return}
if(!EXT){$('sxstate').textContent='';el.innerHTML=`<p class="sm mid" style="margin:0">The <b>Brew Controller helper</b> for Chrome brings each subscription's next delivery and the coffee coming from the shop's own pages, where you are signed in, and offers them here to apply. Your shop logins never leave your browser.</p>
<ol class="sm mid" style="margin:8px 0 0;padding-left:20px;line-height:1.7"><li><a href="/extension.zip" download>Download the helper</a> and unzip it: you get a folder, <code>brew-controller-helper</code>.</li><li>In Chrome open <code>chrome://extensions</code>, turn on <b>Developer mode</b> (top right), click <b>Load unpacked</b> and choose that folder.</li><li>Click the puzzle piece in Chrome's toolbar and pin <b>Brew Controller helper</b>. Click it, then <b>Connect to this controller</b> (this page's address is filled in).</li><li>Reload this page. Each card then has <b>Update</b>.</li></ol>
<details class="note"><summary>What it reads, and where it works</summary><ul><li>Only the next delivery date, how often, the items and coffees, and prices. Never your address, phone, email or payment details.</li><li>Chrome asks once for each shop it may read. It sends what it read only to this controller's page.</li><li>Desktop Chrome, Edge or Brave. Phones do not run extensions.</li><li>Shops that need you signed in: the helper opens the shop, you sign in as usual, and it carries on.</li></ul></details>`;return}
$('sxstate').textContent='helper '+EXT;
el.innerHTML=`<div class="row"><button class="pri" onclick="subUpdate(${JSON.stringify(can.map(s=>s.id))})" ${can.length?'':'disabled'}>Update all</button><span class="sm dim">Opens each active subscription's shop in turn; sign in if it asks. A paused one: <b>Update</b> on its card.</span></div>`+
(Object.keys(SXS).length?'<div class="list sm">'+Object.entries(SXS).map(([id,x])=>{const s=sub(+id);return `<div>${esc(s?s.name:'#'+id)}: <span class="${x.state=='error'?'':'dim'}" ${x.state=='error'?'style="color:var(--warn)"':''}>${esc(x.state=='done'?'read: check below':x.message||x.state)}</span></div>`}).join('')+'</div>':'')}
function subUpdate(ids){const list=(SUBS.subscriptions||[]).filter(s=>ids.includes(s.id));if(!list.length)return;
list.forEach(s=>SXS[s.id]={state:'working',message:'waiting'});drawExt();
window.postMessage({bbw:'update',requestId:String(Date.now()),subscriptions:list.map(s=>({id:s.id,name:s.name,url:s.url,manage_url:s.manage_url}))},location.origin)}
// What came back, turned into ticked suggestions per subscription.
const norm=t=>String(t||'').toLowerCase().replace(/[^a-z0-9]+/g,' ').trim();
const shelfCoffee=name=>L.coffees.find(c=>norm(c.name)==norm(name))||null;
function importRows(){const rows=[];const list=SUBS&&SUBS.subscriptions||[];
for(const [sid,d] of Object.entries(IMP)){const s=sub(+sid);if(!s||!d)continue;
// Which of the subscriptions on this website each dated item belongs to: by its variant or name against the note and item.
const mates=list.filter(x=>hostOf(x.url)&&hostOf(x.url)==hostOf(s.url));
const owner=it=>{if(mates.length<2)return s;const w=norm(it.variant||it.name).split(' ').filter(t=>t.length>0);
let best=null,bs=0;for(const m of mates){const hay=' '+norm([m.note,m.item,m.name].join(' '))+' ';const sc=w.filter(t=>/[0-9]/.test(t)||t.length==1?hay.includes(' '+t+' '):false).length*2+w.filter(t=>t.length>3&&hay.includes(t)).length;if(sc>bs){bs=sc;best=m}}return best||s};
const dated=[];if(d.next&&d.next.date)(d.next.items&&d.next.items.length?d.next.items:[{}]).forEach(it=>dated.push({date:d.next.date,it}));
(d.later||[]).forEach(g=>(g.items||[]).forEach(it=>{if(!it.coffee)dated.push({date:g.date,it})}));
const seen=new Set();for(const {date,it} of dated){const m=owner(it);if(seen.has(m.id))continue;seen.add(m.id);
if(date&&date>=today()&&date!=m.next_date)rows.push({key:'d'+m.id,sub:m,kind:'date',value:date,text:`${m.name}${m.note?' ('+m.note+')':''}: next ${fd(date)}`+(m.next_date?` (was ${fd(m.next_date)})`:''),on:true});
const every=it.every_days||(d.plan&&d.plan.every_days)||0;if(every&&every!=m.every_days)rows.push({key:'e'+m.id,sub:m,kind:'every',value:every,text:`${m.name}: ${subEvery(every)} (was ${subEvery(m.every_days)})`,on:true});
if(it.price_cents&&it.price_cents!=m.price_cents)rows.push({key:'p'+m.id,sub:m,kind:'price',value:it.price_cents,text:`${m.name}: ${money(it.price_cents)} each time (was ${money(m.price_cents||0)})`,on:false})}
if(!seen.size&&d.plan&&d.plan.every_days&&d.plan.every_days!=s.every_days)rows.push({key:'e'+s.id,sub:s,kind:'every',value:d.plan.every_days,text:`${s.name}: ${subEvery(d.plan.every_days)} (was ${subEvery(s.every_days)})`,on:true});
// Coffees: coming, on the way, or come.
const coffees=[...(d.later||[]).flatMap(g=>(g.items||[]).filter(it=>it.coffee).map(it=>({coffee:it.name,eta:g.date,coming:true}))),...(d.shipments||[]).map(x=>({...x,coming:!x.delivered}))];
const done=new Set();for(const x of coffees){if(!x.coffee||done.has(norm(x.coffee)))continue;done.add(norm(x.coffee));const c=shelfCoffee(x.coffee);
const otw=c?L.bags.find(b=>b.coffeeId==c.id&&b.onTheWay):null,on=c?L.bags.find(b=>b.coffeeId==c.id&&(b.unopened||b.current)):null;
if(x.coming){if(otw)rows.push({key:'k'+norm(x.coffee),kind:'known',text:`${x.coffee}: already on its way`});
else rows.push({key:'w'+norm(x.coffee),sub:s,kind:'otw',coffee:x,existing:c,text:`${x.coffee}${x.roaster?' ('+x.roaster+')':''}: add as on its way`+(x.eta?`, expected ${fd(x.eta)}`:''),on:true})}
else if(otw)rows.push({key:'a'+norm(x.coffee),kind:'arrived',bag:otw,text:`${x.coffee}: arrived ${fd(x.delivered)}, mark its bag arrived`,on:true});
else if(on)rows.push({key:'k'+norm(x.coffee),kind:'known',text:`${x.coffee}: on the shelf already`});
else if(x.delivered&&(Date.now()-new Date(i2d(x.delivered)))/864e5>60)rows.push({key:'k'+norm(x.coffee),kind:'known',text:`${x.coffee}: delivered ${fd(x.delivered)}, long ago: not added`});
else{const recent=x.delivered&&(Date.now()-new Date(i2d(x.delivered)))/864e5<10;rows.push({key:'u'+norm(x.coffee),sub:s,kind:'unopened',coffee:x,existing:c,text:`${x.coffee}${x.roaster?' ('+x.roaster+')':''}: delivered ${fd(x.delivered)}, add to the shelf unopened`,on:recent})}}}
return rows}
let IROWS=[];
function drawImport(){const box=$('subimp');if(!box)return;IROWS=importRows();if(!Object.keys(IMP).length){box.style.display='none';return}box.style.display='';
$('sibody').innerHTML=IROWS.length?IROWS.map((r,i)=>r.kind=='known'?`<div class="sm dim" style="padding:4px 0">${esc(r.text)}</div>`:`<label class="chk"><input type="checkbox" ${r.on?'checked':''} onchange="IROWS[${i}].on=this.checked">${esc(r.text)}</label>`).join(''):'<div class="sm mid">Everything already matches what the shops say.</div>'}
async function applyImport(){let n=0;for(const r of IROWS){if(!r.on||r.kind=='known')continue;
if(r.kind=='date'||r.kind=='every'||r.kind=='price'){const cur=sub(r.sub.id);if(!cur)continue;const f=Object.assign({},cur);
if(r.kind=='date')f.next_date=r.value;if(r.kind=='every')f.every_days=r.value;if(r.kind=='price')f.price_cents=r.value;const x=await post('/api/subscriptions',f);if(x.ok){n++;await loadSubs()}}
else if(r.kind=='arrived'){const x=await post('/api/library/do?what=bag_arrived&a='+r.bag.id+'&b=0');if(x.ok)n++}
else if(r.kind=='otw'||r.kind=='unopened'){let cid=r.existing&&r.existing.id;
if(!cid){const name=r.coffee.coffee.split(' ').map(w=>w.charAt(0)+w.slice(1).toLowerCase()).join(' ').slice(0,23);
const c=await post('/api/coffee',{name,roaster:(r.coffee.roaster||r.sub.site||r.sub.name||'').slice(0,23),notes:(r.coffee.notes||'').slice(0,47)});if(!c.ok){done(c,'');continue}cid=c.id}
const b=await post('/api/bag',r.kind=='otw'?{coffeeId:cid,onTheWay:true,expectedDate:r.coffee.eta||0,subscriptionId:r.sub.id}:{coffeeId:cid,roastDate:0,unopened:true,subscriptionId:r.sub.id});if(b.ok)n++;else done(b,'')}}
say(n?n+(n==1?' change applied':' changes applied'):'Nothing applied',!!n);IMP={};await loadLibrary();await loadSubs();drawImport()}
async function loadSubs(){try{SUBS=await get('/api/subscriptions')}catch(e){SUBS=null}drawSubs();drawExt();
if(location.hash=='#subscriptions'){loadLib();loadReviews();if(!RT&&typeof loadRatings=='function')loadRatings()}}
// ---- The shared library's services, reviews and recommendations (docs/SUBSCRIPTIONS.md).
let LIBS=[],SREV=[],SV=null;
const hostOf=u=>{try{return new URL(u).hostname.replace(/^www\./,'')}catch(e){return ''}};
const STYLE_TXT={single_roaster:'Single roaster',multi_roaster:'Many roasters',curated:'Curated',matched:'Matched to your taste',single_origin:'Single origin',blends:'Blends',decaf:'Decaf',espresso:'For espresso',filters:'Filters',cleaning:'Cleaning',equipment:'Equipment'};
const SRC_TXT={fair_trade:'Fair trade',direct_trade:'Direct trade',organic:'Organic',farm_named:'Farms named',prices_published:'Prices published',sustainable_packaging:'Sustainable packaging',carbon_neutral:'Carbon neutral',b_corp:'B Corp'};
const ASP=[['value','Value'],['quality','Quality'],['consistency','Consistency'],['reliability','Reliability'],['flexibility','Flexibility'],['transparency','Transparency']];
const SHARE_TXT={local:'On this controller only',queued:'Waiting to be sent',sent:'Sent: waiting for the library',review:'In review',published:'In the shared library',declined:'Declined',refused:'Refused by the library\'s checks'};
async function loadLib(){try{const r=await get('/api/subscriptions/library');LIBS=r.services||[]}catch(e){LIBS=[]}drawLib()}
async function loadReviews(){try{SREV=(await get('/api/subscriptions/reviews')).reviews||[]}catch(e){SREV=[]}if(SF&&SF.id)drawReview()}
// A library entry as a card: what it is, where it ships, its claims and reviews, and Add.
function libCard(e,why){const col=/^#[0-9a-f]{6}$/i.test(e.color||'')?e.color:'#3b6e9e',rv=e.reviews,have=SUBS&&(SUBS.subscriptions||[]).some(x=>x.library_id==e.id||(hostOf(x.url)&&hostOf(x.url)==hostOf(e.url)));
const bits=[kindTxt[e.kind]||'',(e.cadences||[]).length?'every '+e.cadences.map(d=>d%7?d+' d':d/7+' wk').join(' / '):'',e.price_from_cents?'from '+(e.currency=='USD'?'$':'')+(e.price_from_cents/100).toFixed(2)+(e.currency&&e.currency!='USD'?' '+e.currency:''):''].filter(x=>x).join(' · ');
const tags=[...(e.style||[]).map(k=>STYLE_TXT[k]),...(e.sourcing||[]).map(k=>SRC_TXT[k])].filter(x=>x);
return `<article class="sub"><div class="ban" style="background:linear-gradient(135deg,${col},${col}99 55%,var(--up))">${e.image?imgTag(e.image):`<div class="ini">${esc((e.name||'?').charAt(0).toUpperCase())}</div>`}</div>
<div class="ico">${e.icon?imgTag(e.icon):`<div style="width:100%;height:100%;background:${col}"></div>`}</div><div class="lib"><span class="pill">Library</span></div>
<div class="bd"><b>${esc(e.name)}</b><span class="sm mid">${esc(bits)}</span>${e.ships_to?`<span class="sm dim">Ships to ${esc(e.ships_to.join(', '))}</span>`:''}
${rv?`<span class="sm">${'★'.repeat(Math.round(rv.stars))}<span class="dim">${'★'.repeat(5-Math.round(rv.stars))}</span> ${rv.stars.toFixed(1)} from ${rv.n} reviews</span>`:'<span class="sm dim">Not enough reviews yet</span>'}
${e.about?`<span class="sm dim">${esc(e.about)}</span>`:''}${tags.length?`<div class="why">${tags.map(t=>`<span>${esc(t)}</span>`).join('')}</div>`:''}${why||''}</div>
<div class="acts">${have?'<span class="sm dim" style="padding:8px 4px">You have this one</span>':`<button class="pri" onclick="libAdd('${esc(e.id)}')">Add to my subscriptions</button>`}<a class="btn" href="${esc(e.url)}" target="_blank" rel="noopener noreferrer">Visit ↗</a></div></article>`}
function drawLib(){const el=$('sllist');if(!el)return;const q=($('slq').value||'').toLowerCase();
const hit=e=>!q||[e.name,e.about,e.kind,...(e.style||[]).map(k=>STYLE_TXT[k]),...(e.sourcing||[]).map(k=>SRC_TXT[k])].join(' ').toLowerCase().includes(q);
const list=LIBS.filter(hit);$('slcount').textContent=LIBS.length?LIBS.length+(LIBS.length==1?' service':' services'):'';
el.innerHTML=list.length?list.map(e=>libCard(e)).join(''):`<div class="sm mid" style="grid-column:1/-1">${LIBS.length?'No service matches.':'The shared library has no services yet. Add yours above, then open it and send it to the library: once a reviewer lists it, everyone can add it in one tap.'}</div>`}
function libAdd(id){const e=LIBS.find(x=>x.id==id);if(!e)return;
const f={id:0,library_id:e.id,name:e.name,url:e.url,manage_url:e.manage_url||'',kind:e.kind||'coffee',status:'active',platform:e.platform||'',image:e.image||'',icon:e.icon||'',color:e.color||'',
every_days:(e.cadences||[])[0]||0,ships_to:(e.ships_to||[]).join(','),cadences:e.cadences||[],price_from_cents:e.price_from_cents||0,currency:e.currency||'',style:e.style||[],sourcing:e.sourcing||[],description:e.about||''};
post('/api/subscriptions',f).then(r=>{if(done(r,e.name+' added: set its next date and price')){loadSubs().then(()=>subEdit(r.id))}})}
// The owner's say: country and what matters.
function drawPrefs(){if(!SUBS)return;const m=SUBS.matters||[];$('sr_country').value=SUBS.country||'';
$('srmatters').innerHTML=Object.keys(SRC_TXT).map(k=>`<button class="${m.includes(k)?'on':''}" aria-pressed="${m.includes(k)}" onclick="toggleMatter('${k}')">${SRC_TXT[k]}</button>`).join('')}
function toggleMatter(k){SUBS.country=$('sr_country').value.trim().toUpperCase();const m=new Set(SUBS.matters||[]);m.has(k)?m.delete(k):m.add(k);SUBS.matters=[...m];drawPrefs();subPrefs()}
function subPrefs(){SUBS.country=$('sr_country').value.trim().toUpperCase();post('/api/subscriptions/prefs',{country:SUBS.country,matters:SUBS.matters||[]}).then(r=>{if(r.ok)loadSubReco()})}
async function loadSubReco(){drawPrefs();let r;try{r=await get('/api/subscriptions/recommend?drinker='+(typeof TPD!='undefined'?TPD:0))}catch(e){return}
const el=$('srlist');$('srbasis').textContent=r.basis=='taste'?'By how their coffees taste to you':r.basis=='reviews'?'By reviews and what matters to you, until you have rated '+moreTxt(r.coffees):'';
el.innerHTML=(r.items||[]).length?r.items.map(it=>libCard(it.service,`${it.why&&it.why.length?`<div class="why">${it.why.map(w=>`<span style="background:var(--accbg);color:var(--acctext)">${esc(w)}</span>`).join('')}</div>`:''}${it.people?`<span class="sm mid">${esc(it.people)}</span>`:''}`)).join(''):
`<div class="sm mid" style="grid-column:1/-1">${LIBS.length?'Nothing to suggest yet: services you already have are left out, and so are those that do not ship to your country.':'Suggestions come from the shared library\'s services, how their coffees taste to people like you, and their reviews. The library has none listed yet.'}</div>`}
// Sharing, on the form.
function drawShare(){const row=$('sfsharerow');if(!SF||!SF.id||!SUBS){row.style.display='none';return}row.style.display='';
const st=SF.share||'local',fromLib=st=='published'&&SF.library_id;$('sfsharepill').textContent=SHARE_TXT[st]||st;
$('sfsharetxt').textContent=fromLib?'Listed as '+SF.library_id+(SF.share_changed?'. You have changed it since: send the change for review.':'.'):st=='declined'||st=='refused'?(SF.review_note||''):!SUBS.sharing?'Sharing is off (Setup, Shared library).':!SF.can_share?'It needs a name and an https link to be shared.':st=='local'?'Send it so other people can add it in one tap. A reviewer checks it first.':'';
$('sfsend').style.display=SUBS.sharing&&SF.can_share&&(st=='local'||st=='declined'||st=='refused'||SF.share_changed)?'':'none'}
async function subShare(){if(!SF||!SF.id)return;if(!await ask('Send "'+SF.name+'" to the shared library?\n\nIts name, links, kind, where it ships, deliveries offered, typical price, style and sourcing claims are sent. Your own price, dates, stock and notes are not.'))return;
post('/api/subscriptions/share',{id:SF.id}).then(r=>{if(done(r,'Sent for review')){loadSubs().then(()=>{SF=Object.assign(SF,sub(SF.id)||{});drawShare()})}})}
// Reviews, on the form: one per drinker.
function revFor(d){return SREV.find(r=>r.subscription==SF.id&&r.drinker==d)}
function drawReview(){const box=$('sfreview');if(!SF||!SF.id){box.style.display='none';return}box.style.display='flex';
const named=RT&&RT.named,people=named?RT.drinkers.map((n,i)=>[i+1,n]).filter(x=>x[1]):[];
if(!SV||SV.sub!=SF.id){const d=named&&people.length?people[0][0]:0,o=revFor(d);SV={sub:SF.id,drinker:d,stars:o?o.stars:0,asp:ASP.map(([k])=>o&&o.aspects[k]||0),note:o?o.note:''};$('sv_note').value=SV.note}
$('svwho').innerHTML=named&&people.length?'<span class="sm dim">Who is reviewing:</span>'+people.map(([id,n])=>`<button class="${id==SV.drinker?'on':''}" onclick="SV=null;svPick(${id})">${esc(n)}</button>`).join(''):'';
$('svstars').innerHTML='<span class="stars">'+[1,2,3,4,5].map(n=>`<button class="${n<=SV.stars?'on':''}" aria-label="${n} stars" onclick="SV.stars=${n};drawReview()">★</button>`).join('')+'</span>'+(SV.stars?'':'<span class="sm dim">tap to rate</span>');
$('svasp').innerHTML=ASP.map(([k,l],i)=>`<label>${l} <span class="dim">${SV.asp[i]?SV.asp[i]+' / 5':'not given'}</span><input type="range" min="0" max="5" step="1" value="${SV.asp[i]}" oninput="SV.asp[${i}]=+this.value;this.previousElementSibling.textContent=+this.value?this.value+' / 5':'not given'"></label>`).join('');
const mine=revFor(SV.drinker);$('svdel').style.display=mine?'':'none';
$('svinfo').textContent=SUBS&&SUBS.share_ratings?(SF.share=='published'?'Shared anonymously with the library':'Shared once the service is in the library'):'Kept on this controller';
const all=SREV.filter(r=>r.subscription==SF.id);$('svlist').innerHTML=all.length>1||(all.length&&named)?all.map(r=>`<div>${esc(drinkerName(r.drinker))}: ${'★'.repeat(r.stars)}${r.note?' <span class="dim">'+esc(r.note)+'</span>':''}</div>`).join(''):''}
function svPick(d){const o=revFor(d);SV={sub:SF.id,drinker:d,stars:o?o.stars:0,asp:ASP.map(([k])=>o&&o.aspects[k]||0),note:o?o.note:''};$('sv_note').value=SV.note;drawReview()}
function subReviewSave(){if(!SV||!SV.stars)return say('Give it 1 to 5 stars',false);const aspects={};ASP.forEach(([k],i)=>{if(SV.asp[i])aspects[k]=SV.asp[i]});
post('/api/subscriptions/review',{subscription:SF.id,drinker:SV.drinker,stars:SV.stars,aspects,note:$('sv_note').value.trim()}).then(r=>{if(done(r,'Review saved'))loadReviews()})}
async function subReviewDel(){const o=SV&&revFor(SV.drinker);if(!o||!await ask('Delete this review?'))return;post('/api/subscriptions/review/delete',{id:o.id}).then(r=>{if(done(r,'Deleted')){SV=null;loadReviews()}})}
function drawSubs(){const el=$('sublist');if(!el)return;if(!SUBS){el.innerHTML='<div class="dim sm">Could not load the subscriptions.</div>';return}
const list=SUBS.subscriptions||[],act=list.filter(s=>s.status=='active');
const order=(a,b)=>(a.status!='active')-(b.status!='active')||(a.days_until==null)-(b.days_until==null)||(a.days_until||0)-(b.days_until||0);
el.innerHTML=list.length?list.slice().sort(order).map(s=>subCard(s)).join(''):`<section class="card" style="grid-column:1/-1;align-items:flex-start"><b>Keep your subscriptions in one place.</b><span class="sm mid">Coffee, paper filters, anything that comes by post: add the link and the controller builds a card, tracks when the next one comes, what it costs, and how many filters you have left.</span><button class="pri" onclick="subEdit(0)">Add a subscription</button></section>`;
const next=act.filter(s=>s.days_until!=null).sort((a,b)=>a.days_until-b.days_until)[0],sup=list.filter(s=>s.kind=='supplies'&&s.per_shot>0);
$('subsum').innerHTML=list.length?[`<div class="stat"><span class="sm dim">Each month</span><b>${money(SUBS.monthly_cents||0)}</b><span class="sm dim">${act.length} active</span></div>`,
next?`<div class="stat"><span class="sm dim">Next up</span><b>${esc(next.name||next.site)}</b><span class="sm dim">${esc(subWhen(next))}</span></div>`:'',
...sup.map(s=>`<div class="stat"><span class="sm dim">${esc(cap(s.item||s.name))}${s.note?' · '+esc(s.note):''}</span><b>${s.stock} left</b><span class="sm ${s.stock_short?'':'dim'}" ${s.stock_short?'style="color:var(--warn)"':''}>${s.stock_days_left!=null?'about '+s.stock_days_left+' days at '+perDay(SUBS.shots_per_day):'not used up yet'}</span></div>`)].join(''):'';
$('subrem').innerHTML=act.filter(s=>s.due_soon||s.stock_short).map(s=>`<div class="remind"><span style="flex:1 1 240px">${s.due_soon?`<b>${esc(s.name)}</b> ${s.days_until<0?'was due '+fd(s.next_date)+': did it come?':s.days_until==0?'charges or ships today':'charges or ships '+(s.days_until==1?'tomorrow':'in '+s.days_until+' days')+'. Skip it at the shop if you do not need it.'}`:`<b>${esc(cap(s.item||s.name))}</b> will run out before the next delivery.`}</span>${s.manage_url?`<a class="btn" href="${esc(s.manage_url)}" target="_blank" rel="noopener noreferrer">Manage ↗</a>`:''}${s.due_soon&&s.days_until<=0?`<button onclick="subArrived(${s.id})">Arrived</button>`:''}</div>`).join('')}
const sub=id=>SUBS&&(SUBS.subscriptions||[]).find(s=>s.id==id);
function subKind(){$('sfsup').style.display=$('sf_kind').value=='supplies'?'':'none'}
function subEdit(id){const s=id?sub(id):null;SF=s?Object.assign({},s):{id:0,kind:'coffee',status:'active',color:''};
$('sftitle').textContent=s?'Edit '+(s.name||'subscription'):'Add a subscription';$('sfread').textContent='';$('sfdel').style.display=s?'':'none';
$('sf_url').value=SF.url||'';$('sf_name').value=SF.name||'';$('sf_kind').value=SF.kind;$('sf_status').value=SF.status;
const d=SF.every_days||0,u=d&&d%7==0?7:d>=28&&d<=31&&d!=28?30.44:1;$('sf_unit').value=String(d?u:7);$('sf_every').value=d?(u==30.44?Math.round(d/30.44):d/u):'';
$('sf_next').value=i2d(SF.next_date||0);$('sf_price').value=SF.price_cents?(SF.price_cents/100).toFixed(2):'';$('sf_manage').value=SF.manage_url||'';$('sf_note').value=SF.note||'';
$('sf_item').value=SF.item||'';$('sf_stock').value=SF.stock||'';$('sf_pershot').value=SF.per_shot||'';$('sf_perdel').value=SF.per_delivery||'';
$('sf_ships').value=(SF.ships_to||'').split(',').filter(x=>x).join(', ');$('sf_cad').value=(SF.cadences||[]).map(d=>Math.round(d/7*10)/10).join(', ');
$('sf_pfrom').value=SF.price_from_cents?(SF.price_from_cents/100).toFixed(2):'';$('sf_cur').value=SF.currency||'';
SF.style=SF.style||[];SF.sourcing=SF.sourcing||[];drawSfChips();SV=null;drawShare();drawReview();
$('sf_image').value=SF.image||'';$('sf_icon').value=SF.icon||'';$('sf_color').value=/^#[0-9a-f]{6}$/i.test(SF.color||'')?SF.color:'#5b6470';
subKind();subPrev();$('subform').style.display='';$('subarr').style.display='none';$('subform').scrollIntoView({behavior:'smooth',block:'start'});if(!s)$('sf_url').focus()}
function subClose(){$('subform').style.display='none';SF=null;SV=null}
function drawSfChips(){const chip=(list,key,txt)=>Object.keys(txt).map(k=>`<button class="${(SF[key]||[]).includes(k)?'on':''}" aria-pressed="${(SF[key]||[]).includes(k)}" onclick="sfToggle('${key}','${k}')">${txt[k]}</button>`).join('');
$('sf_style').innerHTML=chip(SF.style,'style',STYLE_TXT);$('sf_sourcing').innerHTML=chip(SF.sourcing,'sourcing',SRC_TXT)}
function sfToggle(key,k){const m=new Set(SF[key]||[]);m.has(k)?m.delete(k):m.add(k);SF[key]=[...m];drawSfChips()}
function subForm(){const every=+$('sf_every').value||0,unit=+$('sf_unit').value;return Object.assign({},SF,{url:$('sf_url').value.trim(),name:$('sf_name').value.trim(),kind:$('sf_kind').value,status:$('sf_status').value,
every_days:Math.min(366,Math.round(every*unit)),next_date:d2i($('sf_next').value),price_cents:Math.round((+$('sf_price').value||0)*100),manage_url:$('sf_manage').value.trim(),note:$('sf_note').value.trim(),
item:$('sf_item').value.trim(),stock:Math.max(0,Math.round(+$('sf_stock').value||0)),per_shot:$('sf_kind').value=='supplies'?Math.round(+$('sf_pershot').value||0):0,per_delivery:Math.round(+$('sf_perdel').value||0),
ships_to:$('sf_ships').value.toUpperCase().split(/[^A-Z]+/).filter(x=>x.length==2).join(','),cadences:$('sf_cad').value.split(/[^0-9.]+/).map(x=>Math.round(+x*7)).filter(d=>d>0&&d<=366).slice(0,6),
price_from_cents:Math.round((+$('sf_pfrom').value||0)*100),currency:$('sf_cur').value.trim().toUpperCase(),style:SF.style||[],sourcing:SF.sourcing||[],
image:$('sf_image').value.trim(),icon:$('sf_icon').value.trim(),color:$('sf_color').value=='#5b6470'&&!SF.color?'':$('sf_color').value})}
function subPrev(){if(!SF)return;const f=subForm(),t=today(),n=f.next_date?Math.round((new Date(i2d(f.next_date))-new Date(i2d(t)))/864e5):null;
$('sfprev').innerHTML=subCard(Object.assign(f,{days_until:n,due_soon:f.status=='active'&&n!=null&&n<=3}),true)}
async function subRead(){const u=$('sf_url').value.trim();if(!/^https?:\/\//i.test(u))return say('Paste a link starting https://',false);
$('sfread').textContent='Reading the page…';const r=await post('/api/subscriptions/preview',{url:u});if(!r.ok){$('sfread').textContent='';return done(r,'')}
let p=null;for(let i=0;i<20;i++){await new Promise(z=>setTimeout(z,1000));try{p=await get('/api/subscriptions/preview')}catch(e){continue}if(p.url==u&&p.state!='waiting')break}
if(!p||p.state=='waiting'){$('sfread').textContent='Still reading: try again in a moment.';return}
if(p.state=='failed'){$('sfread').textContent=(p.problem||'The page could not be read')+'. Fill in the name; the card uses its initial.';if(!$('sf_name').value){try{$('sf_name').value=new URL(u).hostname.replace(/^www\./,'')}catch(e){}}subPrev();return}
const keep=(id,v)=>{if(v&&!$(id).value)$(id).value=v};
SF.title=p.title;SF.site=p.site;SF.description=p.description;SF.platform=p.platform;
keep('sf_name',p.site||p.title);keep('sf_image',p.image);keep('sf_icon',p.icon);keep('sf_manage',p.manage_url);if(p.color&&!SF.color&&!/^#(fff|ffffff|000|000000)$/i.test(p.color)){$('sf_color').value=p.color;SF.color=p.color}
$('sfread').textContent='Read: '+[p.title,platTxt[p.platform]?'subscriptions by '+platTxt[p.platform]:''].filter(x=>x).join(' · ')+(p.manage_url?'. Check the Manage link below.':'');subPrev()}
function subSave(){const f=subForm();if(!f.name&&!f.url)return say('Give it a name or a link',false);if(f.manage_url&&!/^https?:\/\//i.test(f.manage_url))return say('The manage link must start https://',false);
post('/api/subscriptions',f).then(r=>{if(done(r,'Subscription saved')){subClose();loadSubs()}})}
async function subDel(){if(!SF||!SF.id||!await ask('Delete "'+(SF.name||'this subscription')+'"? Bags it brought stay on the shelf.'))return;post('/api/subscriptions/delete',{id:SF.id}).then(r=>{if(done(r,'Deleted')){subClose();loadSubs()}})}
async function subSkip(id){const s=sub(id);if(!s)return;if(!await ask('Skip the next one of "'+s.name+'"?\n\nThe date moves on here only: skip it on the shop\'s page too.'))return;
post('/api/subscriptions/skip',{id}).then(r=>{if(done(r,'Moved on to the next date')){loadSubs();if(s.manage_url)setTimeout(()=>window.open(s.manage_url,'_blank','noopener'),300)}})}
async function subArrived(id){const s=sub(id);if(!s)return;
if(s.kind=='coffee'){SA=s;$('satitle').textContent=(s.name||'Coffee')+' arrived';const shelf=L.coffees.filter(c=>!c.finished||waitingBag(c));
const otw=L.bags.filter(b=>b.onTheWay&&(b.subscriptionId==s.id||!b.subscriptionId));
$('sa_coffee').innerHTML=otw.map(b=>{const c=coffee(b.coffeeId);return `<option value="otw:${b.id}">On its way: ${esc(c?c.name:'a bag')}</option>`}).join('')+'<option value="0">A new coffee</option>'+shelf.map(c=>`<option value="${c.id}">${esc(c.name)}${c.roaster?' ('+esc(c.roaster)+')':''}</option>`).join('');
const same=shelf.find(c=>c.roaster&&s.name&&c.roaster.toLowerCase()==s.name.toLowerCase());$('sa_coffee').value=otw.length?'otw:'+otw[0].id:same?same.id:0;
$('sa_roast').value='';$('sa_weight').value='';$('sa_name').value='';$('sa_roaster').value=(s.site||s.name||'').slice(0,23);$('sa_notes').value='';subArrKind();
$('subarr').style.display='';$('subform').style.display='none';$('subarr').scrollIntoView({behavior:'smooth',block:'start'});return}
const msg=s.kind=='supplies'&&s.per_delivery?'Add '+s.per_delivery+' '+(s.item||'')+' to the stock and move the next date on?':'Mark "'+s.name+'" arrived and move the next date on?';
if(!await ask(msg))return;post('/api/subscriptions/arrived',{id}).then(r=>{if(done(r,'Arrived'))loadSubs()})}
function subArrKind(){const v=$('sa_coffee').value;$('sanew').style.display=v=='0'?'':'none'}
async function subArrSave(){if(!SA)return;const pick=$('sa_coffee').value;
if(pick.startsWith('otw:')){const id=+pick.slice(4),roast=d2i($('sa_roast').value);if(!roast)return say('Enter the roast date printed on the bag',false);if(roast>today())return say('That date has not come yet',false);
const r=await post('/api/library/do?what=bag_arrived&a='+id+'&b=0');if(!done(r,''))return;const bag=L.bags.find(b=>b.id==id)||{};
await post('/api/bag',{id,roastDate:roast,openedDate:0,frozenDate:0,thawDate:0,weightG:Math.round(+$('sa_weight').value||0)||bag.weightG||0});
const a=await post('/api/subscriptions/arrived',{id:SA.id});done(a,'On the shelf, unopened');$('subarr').style.display='none';SA=null;loadLibrary();loadSubs();return}
let cid=+pick;const roast=d2i($('sa_roast').value);if(!roast)return say('Enter the roast date printed on the bag',false);if(roast>today())return say('That date has not come yet',false);
if(!cid){const name=$('sa_name').value.trim();if(!name)return say('Give the new coffee a name',false);
const r=await post('/api/coffee',{name,roaster:$('sa_roaster').value.trim(),notes:$('sa_notes').value.trim()});if(!done(r,''))return;cid=r.id}
const b=await post('/api/bag',{coffeeId:cid,roastDate:roast,weightG:Math.round(+$('sa_weight').value||0),unopened:true,subscriptionId:SA.id});if(!done(b,''))return;
const a=await post('/api/subscriptions/arrived',{id:SA.id});done(a,'On the shelf, unopened');$('subarr').style.display='none';SA=null;loadLibrary();loadSubs()}
function newBag(){$('cbagdate').value='';$('cbagrow').style.display='';$('cbagdate').focus()}
// The bag is used up: the coffee goes to Finished bags and its entries leave the controller, which
// moves on to the coffee with the oldest roast if this was the one it brews. A new bag brings it all back.
async function finishBag(){const c=coffee(C);if(!c)return;if(!await ask('Is the bag of "'+c.name+'" finished?\n\nIt moves to Finished bags with everything kept, and leaves the controller until you start a new bag.'))return;
act('coffee_finish',C).then(()=>say(c.name+' moved to Finished bags',true))}
// YYYYMMDD as a short date.
const fd=n=>n&&n>20000101?new Date(Math.floor(n/10000),Math.floor(n/100)%100-1,n%100).toLocaleDateString([], {month:'short',day:'numeric',year:'numeric'}):'(date not known)';
async function startBag(){const d=d2i($('cbagdate').value),o=d2i($('cbagopen').value),c=coffee(C);if(!c)return say('Save the coffee first',false);if(!d)return say('Enter the roast date printed on the bag',false);if(d>today())return say('That date has not come yet',false);if(c.roastDate==d&&!await ask('The bag in use has this roast date. Start another bag anyway?'))return;
post('/api/bag',{coffeeId:c.id,roastDate:d,openedDate:o}).then(r=>{if(done(r,'New bag started')){$('cbagrow').style.display='none';loadLibrary()}})}
async function delCoffee(){const c=coffee(C);if(!c||!await ask('Delete "'+c.name+'" from the shelf? Its shots stay in the history.'))return;act('coffee_delete',C).then(()=>{C=-1;loadLibrary()})}

function recipeForm(){const bloom=+$('r_bloomPauseS').value>0;if(bloom)$('r_preinfusion').value=1;$('r_preinfusion').disabled=bloom;
$('r_targetWeightG').disabled=$('r_useRatio').value=='1';$('r_ratio').disabled=$('r_useRatio').value!='1';
$('r_preinfusionHoldS').disabled=$('r_preinfusion').value!='1';$('r_maxShotTimeS').disabled=$('r_timeLimit').value!='1';
$('rbloom').textContent=bloom?'Bloom: the button is held for the hold time, then pressed again to switch the pump off. After the pause it is held for one second, which skips the machine\'s own pre-infusion, and the brew runs to the target. The pause counts toward the brew time, and "Stop after" must leave room for it.':'Holding MANUAL on the Breville keeps it in low-pressure pre-infusion for as long as it is held. A bloom pause switches the pump off after the hold and starts it again after the pause; it needs a held pre-infusion.'}
function drawRecipes(){$('rlist').innerHTML=L.recipes.map(r=>{const n=L.presets.filter(p=>p.recipeId==r.id).length;return `<button class="item ${r.id==R?'on':''}" onclick="R=${r.id};drawRecipes()"><b>${esc(r.name)}</b><span>${r.doseG} g in, ${r.targetWeightG} g out · ${n?n+(n==1?' entry':' entries')+' on the controller':'not on the controller'}</span></button>`}).join('');
const r=recipe(R)||{name:'',doseG:18,useRatio:false,targetWeightG:36,ratio:2,targetTimeS:28,preinfusion:0,preinfusionHoldS:3,bloomPauseS:0,timeLimit:0,maxShotTimeS:50,baselineOffsetG:1.5};
RF.forEach(k=>{$('r_'+k).value=k=='useRatio'?(r[k]?1:0):r[k]});recipeForm();
$('rdup').style.display=$('rdel').style.display=$('rshare').style.display=R?'':'none';drawRecipeShare();$('rused').style.display=R?'':'none';$('rinfo').textContent=R?'':'New recipe: not saved yet.';
if(!R)return;const use=L.presets.map((p,i)=>({p,i})).filter(x=>x.p.recipeId==R);
$('rtable').innerHTML='<tr><th class="l">Coffee</th><th>Grind '+U+'</th><th>Learned stop offset g</th><th class="l"></th></tr>'+(use.map(x=>{const c=coffee(x.p.coffeeId);
return `<tr><td class="l"><b>${c?esc(c.name):'No coffee attached'}</b>${x.i==L.activePreset?' <span class="dim">in use now</span>':''}${x.p.hidden?' <span class="dim">bag finished</span>':''}</td><td>${fsn(x.p.grindUm)||'not set'}</td><td>${f2(x.p.stopOffsetG)}</td><td class="l"><a href="#" onclick="post('/api/offset/reset?recipe=${x.i}').then(loadLibrary);return false">reset offset</a> &nbsp; <a href="#" onclick="act('preset_delete',${x.i});return false">take off the controller</a></td></tr>`}).join('')||'<tr><td class="l dim" colspan="4">Not on the controller yet.</td></tr>');
$('ralone').style.display=use.some(x=>!x.p.coffeeId)?'none':''}
function readRecipe(){const b={id:R||0};RF.forEach(k=>{const v=$('r_'+k).value;b[k]=k=='name'?v.trim():k=='useRatio'?v==='1':+v});if(b.useRatio)b.targetWeightG=Math.round(b.doseG*b.ratio*10)/10;return b}
function saveRecipe(){const b=readRecipe();if(!b.name)return say('A recipe needs a name',false);post('/api/recipe',b).then(r=>{if(done(r,'Recipe saved')){R=r.id;loadLibrary()}})}
function dupRecipe(){const b=readRecipe();b.id=0;b.name=(b.name+' 2').slice(0,23);post('/api/recipe',b).then(r=>{if(done(r,'Copy made: not on the controller yet')){R=r.id;loadLibrary()}})}
async function delRecipe(){const r=recipe(R);if(!r||!await ask('Delete the recipe "'+r.name+'"?'))return;act('recipe_delete',R).then(()=>{R=-1;loadLibrary()})}

// A coffee or bag just put in use: a grind recommendation to use or not (asked once, here and on the controller).
function drawRec(){const r=G&&G.recommendation,box=$('hgrec');if(!box)return;if(!r){box.style.display='none';return}
const why=r.source=='community'?'from the community ('+r.people+' '+(r.people==1?'person':'people')+')':r.source=='similar'?'from your similar coffee '+r.from:'a first guess from your grinder\'s range';
$('hgrect').innerHTML='<b>New grind recommendation for '+esc(G.coffee||'this bag')+':</b> change the grinder to '+esc(fs(r.setting))+' (\u00b1 '+fdn(r.range)+'), '+esc(why)+'. '+(r.current?'It is at '+esc(fs(r.current))+' now.':'No setting yet.')+' Grind advice takes over from the first shot.';
$('hgrecu').textContent='Use '+fs(r.setting);$('hgreck').textContent=r.current?'Keep '+fs(r.current):'Not now';box.style.display=''}
async function answerRec(use){await post('/api/grind/recommendation?use='+use);await loadGrind();if(use)loadLibrary()}
async function loadGrind(){try{G=await get('/api/grind')}catch(e){return}
const cur=fsn(G.current_um)||'--';$('gcur').textContent=cur;$('hg').textContent=cur;$('gfor').textContent='Advice for '+G.recipe+(G.coffee?', '+G.coffee:'');
const b=$('gapply');b.style.display='none';let title,cls='',why,short;
const basis=`Average ${G.time_basis=='pour'?'pour':'total'} time ${f1(G.average_time_s)} s over ${G.shots_used} shot${G.shots_used==1?'':'s'} at ${fs(G.current_um)}, target ${G.target_time_s} s ± ${G.tolerance_s} s.`;
const out=G.outliers_ignored?` ${G.outliers_ignored} one-off shot${G.outliers_ignored==1?'':'s'} left out (far from the others at this setting).`:'';
const sens=G.sensitivity_source==='measured'?` Sensitivity ${fdf(G.um_per_s)} ${fdu()} per second, measured from ${G.learn_shots} of your shots${G.data_weight<0.99?' ('+Math.round(100*G.data_weight)+'% from this coffee, the rest from what this grinder has shown)':''}.`:G.sensitivity_source==='grinder'?` Sensitivity ${fdf(G.um_per_s)} ${fdu()} per second, from what this grinder has shown on ${G.grinder_coffees} other coffee(s) (${G.grinder_shots} shots); this coffee's own shots take over as they come.`:` Sensitivity ${fdf(G.um_per_s)} ${fdu()} per second is the starting guess; it is measured once shots exist at two or more grind settings.`;
if(G.action=='no_grind_set'){title='Enter the grinder’s setting';why=short='Shots are stored with the grind setting entered here. Advice starts with the first valid shot.'}
else if(G.action=='no_data'){title='No shot at this setting yet';short=`Pull a shot at ${fs(G.current_um)}.`;why=short+` It counts when it stops on the weight and runs from 15 s to the target time + 15 s.`}
else if(G.action=='inconsistent'){title='Brew times vary: no advice';cls='warn';short=`The last ${G.shots_used} shots at ${fs(G.current_um)} ran from ${f1(G.min_time_s)} s to ${f1(G.max_time_s)} s against ${G.target_time_s} s ± ${G.tolerance_s} s. Check dose, distribution and tamp before moving the grind.`;
why=`The last ${G.shots_used} shots at ${fs(G.current_um)} ran from ${f1(G.min_time_s)} s to ${f1(G.max_time_s)} s against a target of ${G.target_time_s} s ± ${G.tolerance_s} s, some fast and some slow. Their average (${f1(G.average_time_s)} s) describes none of them, so it is not used. If the grinder was changed between them, enter its setting: shots are only compared at the same setting. Otherwise look at dose, distribution and tamp. A shot can be left out on the Shots page.`+out}
else if(G.out_of_range){const fin=G.action=='finer';title=`Go ${fin?'finer':'coarser'} ${fdn(G.delta_um)} ${fdu()}, to ${fs(G.suggested_um)}`;cls='warn';why=short=`The last shot ran ${fin?'too short (under 15 s)':'more than 15 s over the target'} to be counted, so its time cannot size the change. The direction is clear: this is the largest single change. The next shot gets the usual advice.`}
else if(G.action=='hold'&&G.between_steps){title='Keep '+fs(G.current_um)+' (closest step)';cls='ok';why=basis+' The target lies between this setting and the next one, which ran on the other side of it; there is no setting between them, so the closer one is kept.'+out;short=basis}
else if(G.action=='hold'&&G.at_limit){title='Keep '+fs(G.current_um)+' (end of the range)';cls='warn';why=basis+' The shots ask for a change past the end of this grinder’s range, so there is nothing left to turn: try a different dose, or a fresher or older coffee.'+out;short=basis}
else if(G.action=='hold'){title='Keep '+fs(G.current_um);cls='ok';why=basis+' Inside tolerance.'+out;short=basis}
else{const fin=G.action=='finer';title=`Go ${fin?'finer':'coarser'} ${fdn(G.delta_um)} ${fdu()}, to ${fs(G.suggested_um)}${G.refined?' (to your liked time)':''}`;cls='warn';short=basis+` Running ${f1(Math.abs(G.error_s))} s ${fin?'fast':'slow'}.`;
why=short+sens+(G.step_limited?' Limited to the largest single change; expect another step.':'')+out;b.style.display='';b.textContent='I set the grinder to '+fs(G.suggested_um)}
{const c=G.confidence;if(c&&(G.action=='finer'||G.action=='coarser')&&!G.out_of_range){const pc=Math.round(100*c.score),lv=pc>=60?'high':pc>=35?'medium':'low';
const ct=` \u00b1 ${fdn(c.window)} ${fdu()} \u00b7 confidence ${lv} (${pc}%)`;short+=ct.replace(' \u00b7',',').replace(/^ /,' Window ')+'.';
why+=` Confidence ${pc}% from ${c.followed} change${c.followed==1?'':'s'} that ${c.followed==1?'was':'were'} followed`+(c.followed?`, landing ${f1(c.rms_s)} s from the target, typically`:'')+'.'+(c.on?(c.scaled?(pc<50?' The step is made smaller while advice is still being proved.':' The step is allowed to be larger because past advice landed close.'):''):' Sizing by confidence is off.')}}
$('gadv').textContent=$('hgadv').textContent=title;$('gadv').className=$('hgadv').className='pill '+cls;$('gwhy').textContent=why;$('hgwhy').textContent=short;
const ag=G.bean_age_days,ds=G.days_since_brewed,sh=G.age_shift;let at='',as='';
if(!G.coffee){at=''}else if(ag==null||ag<0){at='Bean age: no roast date is entered for this bag, so its age cannot be taken into account.'}
else{as='Bag '+ag+' day'+(ag==1?'':'s')+' off roast';at='Bean age: this bag is '+ag+' day'+(ag==1?'':'s')+' off roast.';
if(ag<5)at+=' Very fresh coffee is still giving off gas: expect shots to vary for a few days, and settings found now to need revisiting.';
if(ds>=3){at+=' It is '+ds+' days since a shot of it last counted here. Coffee runs faster as it ages, so the grind usually has to go finer.';
if(sh){at+=' From what contributors to the shared library have measured, expect about '+fdn(sh)+' '+fdu()+' '+(sh<0?'finer':'coarser')+' than last time'+(G.current_um?' (around '+fs(G.current_um+sh)+')':'')+'. A guide only: the advice above follows from your next shot.';as+=', '+ds+' days since last brewed: about '+fdn(sh)+' '+fdu()+' '+(sh<0?'finer':'coarser')+' expected'}
else{at+=G.age_rate_known?' Not enough is known about this grinder to put a number on it.':' The shared library has not measured how fast yet, so no number is offered: the advice above follows from your next shot.';as+=', '+ds+' days since last brewed'}}}
$('gage').textContent=at;$('hgage').textContent=as?as+'.':'';drawRec();
let rt='';if(G.coffee&&G.rated_target_on){const n=G.rated_shots,rs=G.rated_state,T=f1(G.rated_time_s),tol=G.tolerance_s;
if(rs=='on_liked')rt='Your '+n+' best-rated shots (4 or 5 stars) took about '+T+' s, and the average is on that time (within \u00b1 '+f1(G.rated_band_s)+' s).';
else if(rs=='refining')rt='The time advice says keep, but your '+n+' best-rated shots took about '+T+' s and the average is '+f1(G.average_time_s)+' s, so a small move towards the time you liked is suggested. It stays inside the \u00b1 '+tol+' s the time advice allows.';
else if(rs=='base_moving')rt='Your '+n+' best-rated shots took about '+T+' s. Ratings only refine the advice once the time advice says keep.';
else if(rs=='outside_band')rt='Your '+n+' best-rated shots took about '+T+' s, outside the \u00b1 '+tol+' s around the recipe\u2019s '+f1(G.target_time_s)+' s, so the time advice stands. If '+T+' s is what you want, change it in the recipe.';
else if(n>0)rt=n+' of the 3 shots rated 4 or 5 stars that are needed before ratings can refine the advice.'}
if(G.taste_hint&&G.coffee){const sour=G.taste_hint<0,agrees=(G.action=='finer'&&sour)||(G.action=='coarser'&&!sour);
rt+=(rt?' ':'')+'The last '+G.taste_shots+' shots at this setting tasted '+(sour?'sour':'bitter')+'. '+(G.action=='hold'?'The time is on target, so try a little '+(sour?'finer':'coarser')+' only if you agree.':agrees?'That agrees with the advice.':'That disagrees with the time, so something other than the grind may be the cause: dose, distribution, temperature, or the age of the coffee.')}
$('grate').textContent=rt;
$('gsens').textContent='Sensitivity: '+(G.sensitivity_source==='measured'?'measured, ':G.sensitivity_source==='grinder'?'from this grinder, ':'starting guess, ')+fdf(G.um_per_s)+' '+fdu()+' per second'+(G.between_steps?'; the closer of two steps is kept':'');drawScatter();drawPrev()}
const today=()=>{const t=new Date();return t.getFullYear()*10000+(t.getMonth()+1)*100+t.getDate()};
let GM=[];
async function loadCommunityGrinders(){try{GM=(await get('/api/community/grinders')).grinders||[]}catch(e){}if(L)drawGrinder()}
const gmPick=()=>{const v=$('gm_model').value;return v[0]=='L'?{lib:GM.find(x=>x.id==v.slice(2))}:{own:L.grinderModels.find(x=>x.id==+v)}};
// The grinder form. A setting is stored in whole steps; fmt {divisor, subSteps} says how it is written.
const gmWith=(fmt,f)=>{const o=GRD;GRD=fmt;try{return f()}finally{GRD=o}};
const gmFmt=()=>{const k=$('gm_kind').value,r=$('gm_micro').style.display!='none'?Math.max(0,+$('gm_subSteps').value||0):0;return {divisor:k=='dial'&&r<2?(+$('gm_divisor').value||1):1,subSteps:r>=2?r:0}};
const gmKindOf=g=>g.unit=='um'?'um':((g.divisor||1)>1||g.unit=='dial')?'dial':'steps';
// Fills the form from a grinder or a model: {model, unit, divisor, subSteps, min, max}.
function gmFill(m,keepRange){$('gm_name').value=m.model||'';const k=gmKindOf(m);$('gm_kind').value=k;$('gm_unit').value=k=='um'||m.unit==({steps:'step',dial:'dial'})[k]?'':m.unit||'';
$('gm_divisor').value=m.divisor||1;const r=m.subSteps||0;$('gm_micro').style.display=r>=2?'':'none';$('gm_subSteps').value=r>=2?r:'';
if(!keepRange){const fmt={divisor:r>=2?1:(m.divisor||1),subSteps:r>=2?r:0};gmWith(fmt,()=>{$('gm_min').value=m.max?(r>=2?Math.floor(((m.min||1)-1)/r)+1:fsn(m.min||1)):'';$('gm_max').value=m.max?(r>=2?Math.ceil(m.max/r):fsn(m.max)):''})}
gmSummary()}
function gmMicro(on){$('gm_micro').style.display=on?'':'none';if(on&&!$('gm_subSteps').value)$('gm_subSteps').focus();gmSummary()}
// The range typed, in stored steps: [min, max], or [0, 0] when not given or not sensible.
function gmRange(){const fmt=gmFmt(),r=fmt.subSteps,a=$('gm_min').value.trim(),b=$('gm_max').value.trim();if(!b)return [0,0];
let lo,hi;if(r>=2){lo=a?((parseInt(a)||1)-1)*r+1:1;hi=(parseInt(b)||0)*r}else gmWith(fmt,()=>{lo=a?ps(a):1;hi=ps(b)});
if(!lo&&a&&/^0+([.,]0*)?$/.test(a))lo=1;return lo&&hi&&hi>lo&&hi<=2000?[lo,hi]:[0,0]}
// One line saying what the form means, as the controller will read it.
function gmSummary(){const k=$('gm_kind').value,micro=$('gm_micro').style.display!='none',fmt=gmFmt(),p=gmPick(),src=p.own||(p.lib&&{umPerUnit:p.lib.um_per_unit,burrs:p.lib.burrs});
$('gm_namel').style.display=p.own||p.lib?'none':'';$('gm_divl').style.display=k=='dial'&&!micro?'':'none';$('gm_unitl').style.display=k=='um'?'none':'';
$('gm_unit').placeholder=k=='dial'?'dial':'step';$('gm_addmicro').style.display=micro||k=='um'?'none':'';$('gm_microhint').style.display=micro||k=='um'?'none':'';
$('gm_kindl').textContent=micro?'Macro adjustment: how it adjusts':'How it adjusts';$('gm_mlab').textContent=$('gm_mkind').value=='dial'?'Marks you read it to, per macro step':'Steps per macro step';
$('gm_minl').textContent=micro?'Finest macro setting':'Finest setting';$('gm_maxl').textContent=micro?'Coarsest macro setting':'Coarsest setting';
const word=k=='um'?'µm':($('gm_unit').value.trim()||(k=='dial'?'dial':'step')),[lo,hi]=gmRange(),bits=[];
if(micro&&fmt.subSteps<2)bits.push('Enter how many steps the micro adjustment has per macro step (2 to 99).');
else{const ex=k=='um'?246:fmt.subSteps>=2?(12-1)*fmt.subSteps+3:fmt.divisor>1?Math.round(12.5*fmt.divisor):12;bits.push('Settings look like '+gmWith(fmt,()=>fsn(ex))+(k=='um'?' µm of burr gap':' ('+word+')'));
if(hi)bits.push('range '+gmWith(fmt,()=>fmt.subSteps>=2?Math.floor((lo-1)/fmt.subSteps+1)+'–'+Math.ceil(hi/fmt.subSteps)+' on the macro':fsn(lo)+'–'+fsn(hi)));else if($('gm_max').value.trim())bits.push('the range is not readable: finest must be below coarsest');
bits.push(k=='um'?'one step is one micron':src&&src.umPerUnit>0?'the maker says one step is '+src.umPerUnit+' µm':'how far one step moves the shot is learned from your shots');
const cv=+$('gm_covers').value;if(hi&&cv)bits.push('first guess for espresso: '+gmWith(fmt,()=>fsn(Math.round(lo+(cv==2?0.2:0.5)*(hi-lo)))))}
const burrs=src&&src.burrs?' Burrs: '+src.burrs+'.':'';$('gm_sum').textContent=bits.join(' · ')+'.'+burrs}
function grinderForm(){const p=gmPick(),m=p.own?{model:p.own.model,unit:p.own.unit,divisor:p.own.divisor,subSteps:p.own.subSteps,min:p.own.min,max:p.own.max}:p.lib?{model:p.lib.model,unit:p.lib.unit,divisor:p.lib.divisor||1,subSteps:p.lib.sub_steps||0,min:p.lib.min,max:p.lib.max}:null;
if(m)gmFill(m);else{if(L.grinder.modelId||L.grinder.libraryId)$('gm_name').value='';gmSummary()}}
// The maker a model belongs to, for the picker's groups.
function grinderMaker(n){const m=[['Barista Express','Breville'],['Breville','Breville'],['Baratza','Baratza'],['Eureka','Eureka'],['Niche','Niche'],['DF','Turin (DF)'],['1Zpresso','1Zpresso'],['Comandante','Comandante'],['Timemore','Timemore'],['Fellow','Fellow'],['Mahlk','Mahlkönig']].find(x=>n.startsWith(x[0]));return m?m[1]:(n.split(' ')[0]||'Other')}
function drawGrinder(){const g=L.grinder,o=L.otherGrinder,libSel=g.libraryId&&GM.some(x=>x.id==g.libraryId);
// The picker, grouped by maker: the firmware's models and the shared library's others.
const pick=[...L.grinderModels.map(m=>({v:m.id,name:m.model,sel:m.id==g.modelId,note:m.umPerUnit>0?'':' (steps learned from your shots)'})),
...GM.filter(m=>!L.grinderModels.some(x=>x.model==m.model||x.libraryId==m.id)).map(m=>({v:'L:'+m.id,name:m.model,sel:!g.modelId&&g.libraryId==m.id,note:' (shared library)'}))];
const groups={};pick.forEach(m=>{(groups[grinderMaker(m.name)]=groups[grinderMaker(m.name)]||[]).push(m)});
$('gm_model').innerHTML=Object.keys(groups).sort((a,b)=>a.localeCompare(b)).map(k=>`<optgroup label="${esc(k)}">`+groups[k].sort((a,b)=>a.name.localeCompare(b.name)).map(m=>`<option value="${esc(String(m.v))}" ${m.sel?'selected':''}>${esc(m.name)}${m.note}</option>`).join('')+'</optgroup>').join('')+`<option value="0" ${g.modelId||libSel?'':'selected'}>${g.model&&!g.modelId&&!libSel?esc(g.model)+' (yours)':g.model||g.modelId?'Another grinder: type it below':'Not set yet: pick one, or type it below'}</option>`;
$('gm_zeroDate').value=i2d(g.zeroDate);$('gm_covers').value=g.covers||0;gmFill(g);
$('gm_info').textContent=(g.model?'In use: '+g.model+'. ':'No grinder named yet. ')+'Changing how a listed model adjusts makes it your own version, which you can send to the shared list for review.';
const st=g.share||'local',on=S&&S.shareEnabled,known=g.modelId>0;$('gm_shrow').style.display=g.model?'':'none';$('gm_shpill').textContent=known?'a known model':SHN[st];
$('gm_share').textContent=known?'This model is in the firmware\'s own list: there is nothing to send.':({local:on?'Kept on this controller only. Sending it lets a reviewer add the model to the shared list.':'Kept on this controller only. Sharing is switched off (Setup).',queued:'Signed and waiting in the outbox.',sent:'Sent. The library has not answered yet.',review:'The library has it. A reviewer has not decided yet.',published:'In the shared list'+(g.libraryId?' as '+g.libraryId:'')+'.',declined:'A reviewer did not accept it'+(g.reviewNote?': "'+g.reviewNote+'"':'')+'. What you typed is unchanged.',refused:'The library\'s checks turned this record away'+(g.reviewNote?': "'+g.reviewNote+'"':'')+'. Correct it if needed, then send it again.'})[st]+(g.shareChanged?' You have changed it since that version was sent.':'');
$('gm_send').style.display=on&&g.canShare?'':'none';$('gm_send').textContent=st=='local'?'Send to the shared library':st=='refused'?'Send again':'Send the new version';
$('gm_addother').style.display=o?'none':'';
$('gm_other').innerHTML=o?`<span class="sm mid" style="flex:1 1 260px">Your other grinder: <b>${esc(o.model)}</b> (read in ${esc(o.unit=='um'?'µm':o.unit)}).</span><button onclick="switchGrinder()">Use it instead</button>`+(S&&S.shareEnabled&&o.canShare?`<button onclick="sendGrinder(${o.id})">Send it to the shared library</button>`:'')+`<button class="del" onclick="removeOther()">Remove it</button>`:'<span class="sm dim">One grinder. A second can be kept beside it: choose or type it above, then "Add ... as my second grinder".</span>'}
function grinderBody(rezeroed){const p=gmPick(),g=L.grinder,k=$('gm_kind').value,fmt=gmFmt(),[lo,hi]=gmRange(),
src=p.own?{model:p.own.model,unit:p.own.unit,umPerUnit:p.own.umPerUnit,divisor:p.own.divisor||1,subSteps:p.own.subSteps||0}:p.lib?{model:p.lib.model,unit:p.lib.unit,umPerUnit:p.lib.um_per_unit,divisor:p.lib.divisor||1,subSteps:p.lib.sub_steps||0}:null,
model=src?src.model:$('gm_name').value.trim(),unit=k=='um'?'um':($('gm_unit').value.trim()||(k=='dial'?'dial':'step')),
// changed from the listed model in how it adjusts: it is saved as the owner's own version
same=src&&unit==src.unit&&fmt.subSteps==src.subSteps&&fmt.divisor==src.divisor,
umPerUnit=k=='um'?1:same?(src.umPerUnit||0):(!src&&model==g.model&&unit==g.unit?(g.umPerUnit||0):0);
return {modelId:p.own&&same?p.own.id:0,libraryId:p.lib&&same?p.lib.id:'',model,unit,umPerUnit,divisor:fmt.divisor,subSteps:fmt.subSteps,min:lo,max:hi,covers:+$('gm_covers').value||0,zeroDate:rezeroed?today():d2i($('gm_zeroDate').value),rezeroed:!!rezeroed}}
async function saveGrinder(rezeroed,asOther){const g=L.grinder,b=grinderBody(rezeroed);if(!b.modelId&&!b.model)return say('Pick a model, or type its name',false);
if(asOther){b.asOther=true;b.rezeroed=false;if(b.model==g.model)return say('That is the grinder already in use: choose or type the other one first',false);
post('/api/grinder',b).then(r=>{done(r,'Second grinder added');loadLibrary()});return}
if($('gm_micro').style.display!='none'&&b.subSteps<2)return say('Enter how many steps the micro adjustment has per macro step, or remove it',false);
if($('gm_max').value.trim()&&!b.max)return say('The range is not readable: the finest setting must be below the coarsest',false);
const named=g.model||g.modelId,changes=named&&(b.model!=g.model||b.unit!=g.unit||b.divisor!=(g.divisor||1)||b.subSteps!=(g.subSteps||0));
if((rezeroed||changes)&&!await ask('Grind settings from before will no longer be compared with new ones: grind advice starts fresh for every coffee, and if the unit changes the settings entered so far are cleared. To keep both grinders, use "Add ... as my second grinder" instead. Continue?'))return;
post('/api/grinder',b).then(r=>{done(r,'Grinder saved');loadLibrary()})}
function switchGrinder(){act('grinder_switch').then(()=>say('Now using '+L.grinder.model))}
async function removeOther(){if(await ask('Remove '+L.otherGrinder.model+'? Its settings for each coffee go with it; its shots stay in the history.'))act('grinder_remove_other')}
function sendGrinder(id){post('/api/share/grinder?id='+id).then(r=>{done(r,'Queued for the shared library');loadLibrary();loadShare()})}
function setGrind(um){if(um==null||!(um>=1&&um<=2000))return say('That is not a setting this grinder can have'+((GRD.subSteps||0)>=2?' (outer ring : inner ring, for example 12:3)':(GRD.divisor||1)>1?' (a number such as 3.5)':' (a whole number)'));post('/api/grind/set?um='+Math.round(um)).then(r=>{done(r,'Grind setting saved');$('gset').value='';loadLibrary()})}
// Both grinder charts show every coffee and recipe brewed on this grinder, each coffee in its own colour;
// the one being brewed now is orange and drawn on top. The legend under each chart names them. The
// green band is the target of the recipe being brewed now.
const CCOL=['#f59e0b','#38bdf8','#a78bfa','#f472b6','#34d399','#facc15','#fb7185','#60a5fa','#2dd4bf','#c084fc'];
function grindShots(){return SH.filter(s=>(s.grinder_calibration||0)==L.grinder.calibration&&/^weight/.test(s.end_reason)&&s.grind_vote!='ignore').map(s=>({id:s.id,t:S.grindTimeBasis==1?s.pour_time_s:s.shot_time_s,u:s.grind_um,r:s.rating,c:s.coffee_id||0,n:s.coffee||'no coffee',rc:s.recipe||'',w:when(s),tt:s.target_time_s||0,rd:s.roast_date||0,day:s.timestamp?new Date(s.timestamp).toDateString():''})).filter(q=>q.t>0)}
// A coffee keeps its colour on both charts: the one being brewed gets the first, the others follow in
// the order of their ids among all shots on this grinder. The legend counts the shots on its own chart.
function coffeeColours(pts){const cur=(L.presets[L.activePreset]||{}).coffeeId||0,ids=[...new Set(grindShots().map(q=>q.c))].sort((a,b)=>(b==cur)-(a==cur)||a-b),off=ids.includes(cur)?0:1,col=new Map(ids.map((id,i)=>[id,CCOL[(i+off)%CCOL.length]])),m=new Map();
[...ids].forEach(id=>{const q=pts.find(q=>q.c==id);if(q)m.set(id,{col:col.get(id),n:q.n,k:pts.filter(p=>p.c==id).length})});return {cur,m}}
function drawLegend(el,cc){$(el).innerHTML=[...cc.m].map(([id,v])=>`<span class="chk" style="gap:6px;margin-right:14px"><span style="display:inline-block;width:11px;height:11px;border-radius:50%;background:${v.col}"></span>${esc(v.n)}${id==cc.cur?' <span class="dim">(brewing now)</span>':''} <span class="dim">${v.k}</span></span>`).join('')}
// Round values for an axis from lo to hi: 1, 2 or 5 times a power of ten, about n of them, no finer than minStep.
function niceTicks(lo,hi,n,minStep){const span=hi-lo;if(!(span>0))return [lo];const raw=span/n,p=Math.pow(10,Math.floor(Math.log10(raw))),f=raw/p;
const step=Math.max(minStep||0,(f<1.5?1:f<3?2:f<7?5:10)*p),out=[];for(let v=Math.ceil(lo/step)*step;v<=hi+1e-9;v+=step)out.push(+v.toFixed(6));return out}
// Horizontal gridlines with the time beside each, from left to right.
function gridTimes(x,Y,t0,t1,left,right){x.font='12px sans-serif';x.lineWidth=1;niceTicks(t0,t1,4,1).forEach(v=>{const y=Math.round(Y(v))+0.5;x.strokeStyle=TC.grid;x.beginPath();x.moveTo(left,y);x.lineTo(right,y);x.stroke();x.fillStyle=TC.dim;x.fillText(v+' s',6,y+4)})}
// The dots a chart drew (canvas pixels): hovering one shows what shot it is, clicking opens it.
// On a touch screen the first tap shows it and a second tap on the same dot opens it.
function chartDots(c,dots){c._dots=dots;if(c._wired)return;c._wired=true;const tip=$('ctip');
const near=e=>{const r=c.getBoundingClientRect(),px=(e.clientX-r.left)*c.width/r.width,py=(e.clientY-r.top)*c.height/r.height;let best=null,bd=196;(c._dots||[]).forEach(d=>{const dd=(d.x-px)**2+(d.y-py)**2;if(dd<bd){bd=dd;best=d}});return best};
const show=(d,e)=>{if(!d){tip.style.display='none';c.style.cursor='';return}const q=d.q;
tip.innerHTML=`<b>${esc(q.n)}</b>${q.rc?', '+esc(q.rc):''}<br>${esc(q.w)} · ${q.u?esc(fs(q.u)):'no setting'} · ${f1(q.t)} s${q.r?' · '+q.r+' ★':''}<br><span class="dim">${e.pointerType=='mouse'?'Click':'Tap again'} to open the shot</span>`;
tip.style.display='block';tip.style.left=Math.max(8,Math.min(innerWidth-tip.offsetWidth-8,e.clientX+12))+'px';tip.style.top=Math.min(innerHeight-tip.offsetHeight-8,e.clientY+14)+'px';c.style.cursor='pointer'};
c.addEventListener('pointermove',e=>{if(e.pointerType=='mouse')show(near(e),e)});
c.addEventListener('pointerleave',e=>{if(e.pointerType=='mouse')tip.style.display='none'});
c.addEventListener('click',e=>{const d=near(e);if(!d){tip._for=null;return show(null)}
if(e.pointerType!=='mouse'&&tip._for!==d.q.id){tip._for=d.q.id;show(d,e);return}
tip._for=null;tip.style.display='none';openShot(d.q.id)})}
// A shot from a chart, opened on the Shots page with nothing filtered away.
function openShot(id){$('fcoffee').value='';$('fshow').selectedIndex=0;const i=SH.findIndex(s=>s.id==id);if(i<0)return;SHOWN=Math.max(SHOWN,i+1);
location.hash='#shots';SEL=id;SOPEN=true;showShot(id,'s');drawShots();setTimeout(()=>{const d=$('sdetail');if(d)d.scrollIntoView({behavior:'smooth',block:'center'})},250)}
// Chart 2: the setting each shot was pulled at, in order. One step line per coffee, dots coloured by how
// that shot ran against its own target; new bags and new days marked.
const STC={ok:'#4cc38a',fast:'#f5c451',slow:'#ff6b5e'};
function drawControl(){const c=$('cchart');if(!c)return;const x=c.getContext('2d');x.clearRect(0,0,c.width,c.height);$('cnote').textContent='';$('cleg').innerHTML='';c._dots=[];if(!L||!G||!S)return;
// This coffee only, as the chart above, unless All coffees is ticked there (settings of different
// coffees can be far apart, which flattens every line).
const cur=(L.presets[L.activePreset]||{}).coffeeId||0,allP=grindShots().filter(q=>q.u),mine=allP.filter(q=>q.c==cur);
const pts=($('gall').checked||mine.length<2?allP:mine).reverse().slice(-100);
if(pts.length<2){$('cnote').textContent='needs two shots';return}
const cc=coffeeColours(pts),tol=S.grindToleranceS||2,st=q=>!q.tt?'ok':q.t<q.tt-tol?'fast':q.t>q.tt+tol?'slow':'ok';
const us=pts.map(q=>q.u),lo=Math.min(...us),hi=Math.max(...us),pad=Math.max(2,(hi-lo)*0.12),u0=lo-pad,u1=hi+pad;
const L0=50,R0=c.width-20,B0=c.height-24,X=i=>L0+(i+0.5)/pts.length*(R0-L0),Y=u=>B0-(u-u0)/(u1-u0)*(B0-12);
x.font='12px sans-serif';x.lineWidth=1;
niceTicks(u0,u1,4,1).forEach(v=>{const y=Math.round(Y(v))+0.5;x.strokeStyle=TC.grid;x.beginPath();x.moveTo(L0,y);x.lineTo(R0,y);x.stroke();x.fillStyle=TC.dim;x.fillText(fsn(Math.round(v)),6,y+4)});
// new days along the bottom, new bags as dashed lines
let lastLab=-99;const lastRd=new Map();pts.forEach((q,i)=>{const px=X(i);
if(i==0||q.day!=pts[i-1].day){const lab=q.day?new Date(q.day).toLocaleDateString([], {month:'short',day:'numeric'}):'';if(lab&&px-lastLab>x.measureText(lab).width+10){x.fillStyle=TC.dim;x.fillText(lab,Math.min(R0-x.measureText(lab).width,px-4),c.height-6);lastLab=px}}
const prev=lastRd.get(q.c);if(prev!=null&&q.rd&&prev&&q.rd!=prev){x.strokeStyle=cc.m.get(q.c).col;x.globalAlpha=.5;x.setLineDash([4,5]);x.beginPath();x.moveTo(px-0.5*(R0-L0)/pts.length,10);x.lineTo(px-0.5*(R0-L0)/pts.length,B0);x.stroke();x.setLineDash([]);x.globalAlpha=1;
x.fillStyle=TC.dim;x.fillText('new bag',px-0.5*(R0-L0)/pts.length+3,22)}lastRd.set(q.c,q.rd)});
x.strokeStyle=TC.edge;x.beginPath();x.moveTo(L0,10);x.lineTo(L0,B0);x.lineTo(R0,B0);x.stroke();
// a step line per coffee: the setting holds until it is changed
[...cc.m.keys()].forEach(id=>{const idx=pts.map((q,i)=>i).filter(i=>pts[i].c==id);if(idx.length<2)return;x.strokeStyle=cc.m.get(id).col;x.lineWidth=id==cc.cur?2:1.5;x.globalAlpha=id==cc.cur?.9:.6;x.beginPath();
idx.forEach((i,k)=>{const px=X(i),py=Y(pts[i].u);if(!k)x.moveTo(px,py);else{x.lineTo(px,Y(pts[idx[k-1]].u));x.lineTo(px,py)}});x.stroke();x.globalAlpha=1;x.lineWidth=1});
const dots=[],n={ok:0,fast:0,slow:0};pts.forEach((q,i)=>{const px=X(i),py=Y(q.u),k=st(q);n[k]++;x.fillStyle=STC[k];x.beginPath();x.arc(px,py,q.c==cc.cur?4.5:3.5,0,7);x.fill();
if(q.r>=4){x.strokeStyle='#ffffff';x.lineWidth=2;x.beginPath();x.arc(px,py,7,0,7);x.stroke();x.lineWidth=1}dots.push({x:px,y:py,q})});
chartDots(c,dots);drawLegend('cleg',cc);
$('cleg').innerHTML+=`<span class="dim" style="margin-left:auto">${['ok','fast','slow'].map(k=>`<span style="display:inline-block;width:9px;height:9px;border-radius:50%;background:${STC[k]};margin:0 4px 0 10px"></span>${k=='ok'?'on time':k}`).join('')}</span>`;
$('cnote').textContent=pts.length+' shots: '+n.ok+' on time, '+n.fast+' fast, '+n.slow+' slow'}
function drawScatter(){drawControl();drawGrinderUpkeep();const c=$('gchart'),x=c.getContext('2d');x.clearRect(0,0,c.width,c.height);$('gleg').innerHTML='';c._dots=[];if(!L||!G||!S)return;
const cur=(L.presets[L.activePreset]||{}).coffeeId||0,allP=grindShots().filter(q=>q.u),mine=allP.filter(q=>q.c==cur),pts=$('gall').checked||!mine.length?allP:mine;
const tg=G.target_time_s,tol=G.tolerance_s;if(!pts.length){$('gnote').textContent='No shots stopped by weight on this grinder yet.';return}
const cc=coffeeColours(pts);drawLegend('gleg',cc);
const adv=G.has_number&&(G.action=='finer'||G.action=='coarser')&&G.suggested_um?G.suggested_um:0;
const us=pts.map(q=>q.u),lo=Math.min(...us,G.current_um||us[0],adv||us[0])-10,hi=Math.max(...us,G.current_um||us[0],adv||us[0])+10,ts=pts.map(q=>q.t),t0=Math.min(...ts,tg-tol)-3,t1=Math.max(...ts,tg+tol)+3;
const L0=50,R0=c.width-20,B0=c.height-26,X=u=>L0+(u-lo)/(hi-lo)*(R0-L0),Y=t=>B0-(t-t0)/(t1-t0)*(c.height-40);
gridTimes(x,Y,t0,t1,L0,R0);
// Vertical gridlines at round settings, labelled as the dial reads.
x.font='12px sans-serif';niceTicks(lo,hi,6,1).forEach(v=>{const gx=Math.round(X(v))+0.5;x.strokeStyle=TC.grid;x.beginPath();x.moveTo(gx,10);x.lineTo(gx,B0);x.stroke();const lab=fsn(Math.round(v));x.fillStyle=TC.dim;x.fillText(lab,Math.min(R0-x.measureText(lab).width,gx-x.measureText(lab).width/2),c.height-8)});
x.fillStyle='rgba(76,195,138,.16)';x.fillRect(L0,Y(tg+tol),R0-L0,Y(tg-tol)-Y(tg+tol));x.strokeStyle='#4cc38a';x.setLineDash([4,6]);x.beginPath();x.moveTo(L0,Y(tg));x.lineTo(R0,Y(tg));x.stroke();x.setLineDash([]);
x.strokeStyle=TC.edge;x.beginPath();x.moveTo(L0,10);x.lineTo(L0,B0);x.lineTo(R0,B0);x.stroke();
// The advisor's line: the average time at the setting now, and how many seconds a step moves it (its
// sensitivity); where it meets the target is where the grind should go. The advice itself is marked.
const ups=G.um_per_s,tc=G.average_time_s,uc=G.current_um,inT=t=>t>=t0&&t<=t1;
if(ups>0&&tc>0&&uc){const tAt=u=>tc-(u-uc)/ups;let ua=lo,ub=hi;const ta=tAt(ua),tb=tAt(ub);
if(!inT(ta))ua=uc+(tc-(ta>t1?t1:t0))*ups;if(!inT(tb))ub=uc+(tc-(tb<t0?t0:t1))*ups;
x.save();x.strokeStyle=TC.fg;x.globalAlpha=.55;x.setLineDash([6,5]);x.lineWidth=1.5;x.beginPath();x.moveTo(X(ua),Y(tAt(ua)));x.lineTo(X(ub),Y(tAt(ub)));x.stroke();x.restore();
x.strokeStyle=TC.edge;x.beginPath();x.moveTo(X(uc),10);x.lineTo(X(uc),B0);x.stroke();x.fillStyle=TC.dim;x.fillText('now '+fsn(uc),X(uc)+4,22)}
if(adv){const ax=X(adv),ay=Y(tg);x.fillStyle='#f08a24';x.beginPath();x.moveTo(ax,ay-8);x.lineTo(ax+8,ay);x.lineTo(ax,ay+8);x.lineTo(ax-8,ay);x.closePath();x.fill();
const lab='advice '+fs(adv);x.font='bold 12px sans-serif';x.fillText(lab,Math.min(R0-x.measureText(lab).width,ax+10),ay-10);x.font='12px sans-serif'}
const dots=[];pts.map((q,i)=>i).sort((a,b)=>(pts[a].c==cc.cur)-(pts[b].c==cc.cur)).forEach(i=>{const q=pts[i],px=X(q.u)+((i*37)%13-6),py=Y(q.t);x.fillStyle=cc.m.get(q.c).col;x.globalAlpha=q.c==cc.cur?1:0.85;x.beginPath();x.arc(px,py,q.c==cc.cur?5.5:4.5,0,7);x.fill();x.globalAlpha=1;dots.push({x:px,y:py,q})});
chartDots(c,dots);
const set=new Set(us);$('gnote').textContent=pts.length+' shot'+(pts.length==1?'':'s')+' stopped by weight, '+cc.m.size+' coffee'+(cc.m.size==1?'':'s')+', at '+set.size+' setting'+(set.size==1?'. With one setting there is no slope to measure: once shots exist at two settings at least 4 '+U+' apart, the line through them replaces the starting guess.':'s.')+' Green band, the recipe being brewed now: '+tg+' s ± '+tol+' s.'+(ups>0&&tc>0?' Dashed: the advisor\'s line, '+fdf(ups)+' '+fdu()+' per second.':'')+(adv?' Orange diamond: the advice.':'')+(pts===allP||$('gall').checked?'':' This coffee only; tick All coffees for the rest.')}
// Several shots' curves on one chart, each in its own colour: weight solid, flow dashed on its own scale.
// Readings are fetched once per shot and kept while the page is open.
let CMP=[];const CMPD=new Map();
function cmpButton(){const b=$('scmp');if(b){const on=CMP.includes(SEL);b.textContent=on?'Remove from comparison':'Add to comparison';b.classList.toggle('on',on)}}
function cmpToggle(id){if(!id)return;const i=CMP.indexOf(id);if(i>=0)CMP.splice(i,1);else{if(CMP.length>=8)return say('Up to 8 shots can be compared',false);CMP.push(id)}cmpButton();drawCompare();
if(i<0)setTimeout(()=>$('cmpcard').scrollIntoView({behavior:'smooth',block:'nearest'}),100)}
async function drawCompare(){const card=$('cmpcard');card.style.display=CMP.length?'':'none';if(!CMP.length)return;
for(const id of CMP){if(!CMPD.has(id)){try{const r=await get('/api/shot/data?id='+id);if(r.time_s)CMPD.set(id,{T:r.time_s,W:r.weight_g,F:r.flow_g_s})}catch(e){}}}
const c=$('cmpchart'),x=c.getContext('2d');x.clearRect(0,0,c.width,c.height);
const align=$('cmpalign').checked,flow=$('cmpflow').checked,rows=CMP.map((id,k)=>({id,k,s:SH.find(q=>q.id==id),d:CMPD.get(id)})).filter(r=>r.s);
const off=r=>align&&r.s.first_drop_s>0?r.s.first_drop_s:0;
let t0=0,t1=1,wm=1,fm=1;rows.forEach(r=>{if(!r.d)return;const o=off(r);t0=Math.min(t0,-o);t1=Math.max(t1,r.d.T[r.d.T.length-1]-o);wm=Math.max(wm,...r.d.W);if(r.d.F)fm=Math.max(fm,...r.d.F.filter(v=>v!=null))});
wm*=1.05;fm*=1.1;const L0=50,R0=c.width-(flow?50:20),B0=c.height-24,X=t=>L0+(t-t0)/(t1-t0)*(R0-L0),Y=w=>B0-Math.max(w,0)/wm*(B0-10),YF=v=>B0-Math.max(v,0)/fm*(B0-10);
x.font='12px sans-serif';x.lineWidth=1;
niceTicks(0,wm,4,1).forEach(v=>{const y=Math.round(Y(v))+0.5;x.strokeStyle=TC.grid;x.beginPath();x.moveTo(L0,y);x.lineTo(R0,y);x.stroke();x.fillStyle=TC.dim;x.fillText(v+' g',6,y+4)});
niceTicks(t0,t1,6,1).forEach(v=>{const gx=Math.round(X(v))+0.5;x.strokeStyle=TC.grid;x.beginPath();x.moveTo(gx,10);x.lineTo(gx,B0);x.stroke();const lab=v+' s';x.fillStyle=TC.dim;x.fillText(lab,Math.min(R0-x.measureText(lab).width,Math.max(L0,gx-x.measureText(lab).width/2)),c.height-6)});
if(flow)niceTicks(0,fm,4,0.5).forEach(v=>{x.fillStyle=TC.dim;x.fillText(v+' g/s',R0+6,Math.round(YF(v))+4)});
if(align){x.strokeStyle=TC.edge;x.setLineDash([3,5]);x.beginPath();x.moveTo(X(0),10);x.lineTo(X(0),B0);x.stroke();x.setLineDash([]);x.fillStyle=TC.dim;x.fillText('first drops',X(0)+4,22)}
x.strokeStyle=TC.edge;x.beginPath();x.moveTo(L0,10);x.lineTo(L0,B0);x.lineTo(R0,B0);x.stroke();
const line=(T,V,o,f)=>{x.beginPath();let on=false;T.forEach((t,i)=>{const v=V[i];if(v==null){on=false;return}const px=X(t-o),py=f(v);if(on)x.lineTo(px,py);else{x.moveTo(px,py);on=true}});x.stroke()};
rows.forEach(r=>{if(!r.d)return;const col=CCOL[r.k%CCOL.length],o=off(r);x.strokeStyle=col;x.lineWidth=2;x.setLineDash([]);line(r.d.T,r.d.W,o,Y);
if(flow&&r.d.F){x.lineWidth=1.5;x.setLineDash([5,4]);line(r.d.T,r.d.F,o,YF);x.setLineDash([])}});x.lineWidth=1;
$('cmpleg').innerHTML=rows.map(r=>`<span class="chk" style="gap:6px;margin-right:12px"><span style="display:inline-block;width:18px;height:3px;border-radius:2px;background:${CCOL[r.k%CCOL.length]}"></span>${esc(r.s.coffee||r.s.recipe)} <span class="dim">${esc(when(r.s))}, ${f1(r.s.final_weight_g)} g in ${f1(r.s.shot_time_s)} s${r.s.grind_um?', '+esc(fs(r.s.grind_um)):''}${r.d?'':', no readings stored'}</span> <a href="#" onclick="cmpToggle(${r.id});return false" aria-label="Remove from the comparison">✕</a></span>`).join('')}
// ---- Insights: patterns across the shots (all from /api/shots; nothing new from the controller) ----
const ymd=n=>n&&n>19000101?new Date(Math.floor(n/10000),Math.floor(n/100)%100-1,n%100):null;
// Days the coffee was off roast when the shot was pulled; null when either date is not known.
function ageDays(s){const r=ymd(s.roast_date);if(!r||!s.timestamp)return null;const d=Math.floor((new Date(s.timestamp)-r)/864e5);return d>=0&&d<1000?d:null}
const btime=s=>s.time_basis=='pour'?s.pour_time_s:s.shot_time_s,byWeight=s=>/^weight/.test(s.end_reason);
function insShots(){const f=$('icoffee').value;return SH.filter(s=>!f||String(s.coffee_id||0)==f)}
// Fresh (blue) to old (red); and a count, from the card colour to the accent.
function ramp(t){t=Math.max(0,Math.min(1,t));const a=[[56,189,248],[52,211,153],[250,204,21],[249,115,22],[239,68,68]],v=t*(a.length-1),i=Math.min(a.length-2,Math.floor(v)),f=v-i;return 'rgb('+a[i].map((c,k)=>Math.round(c+(a[i+1][k]-c)*f))+')'}
function heat(t){const a=TC.cardRgb,b=[240,138,36];t=Math.max(0,Math.min(1,t));return 'rgb('+a.map((c,k)=>Math.round(c+(b[k]-c)*t))+')'}
// Rectangles a chart drew: hovering one says what it holds, clicking it may open something.
function chartCells(c,cells,text,click){c._cells=cells;c._text=text;c._click=click;if(c._cwired)return;c._cwired=true;const tip=$('ctip');
const at=e=>{const r=c.getBoundingClientRect(),px=(e.clientX-r.left)*c.width/r.width,py=(e.clientY-r.top)*c.height/r.height;return (c._cells||[]).find(q=>px>=q.x&&px<q.x+q.w&&py>=q.y&&py<q.y+q.h)};
const show=(q,e)=>{const t=q&&c._text(q);if(!t){tip.style.display='none';c.style.cursor='';return}tip.innerHTML=t;tip.style.display='block';
tip.style.left=Math.max(8,Math.min(innerWidth-tip.offsetWidth-8,e.clientX+12))+'px';tip.style.top=Math.min(innerHeight-tip.offsetHeight-8,e.clientY+14)+'px';c.style.cursor=c._click&&q.click?'pointer':''};
c.addEventListener('pointermove',e=>{if(e.pointerType=='mouse')show(at(e),e)});c.addEventListener('pointerleave',e=>{if(e.pointerType=='mouse')tip.style.display='none'});
c.addEventListener('click',e=>{const q=at(e);if(!q)return show(null);if(e.pointerType!=='mouse'&&tip._for!==q){tip._for=q;show(q,e);return}tip._for=null;tip.style.display='none';if(c._click&&q.click)c._click(q)})}
let PWR=null,PWRAT=0;
function loadPowerEvents(){if(Date.now()-PWRAT<60000)return;PWRAT=Date.now();get('/api/power/events?days=90').then(a=>{PWR=Array.isArray(a)?a:null;if(CALMODE=='hours'&&location.hash=='#insights')drawCalendar()}).catch(()=>{PWR=null})}
function drawInsights(){if(!L||!Array.isArray(SH))return;loadPowerEvents();drawGrindAcc();drawTaste();const sel=$('icoffee'),cur=sel.value,ids=[...new Map(SH.filter(s=>s.coffee).map(s=>[String(s.coffee_id||0),s.coffee])).entries()];
sel.innerHTML='<option value="">All coffees</option>'+ids.map(([id,n])=>`<option value="${id}" ${id==cur?'selected':''}>${esc(n)}</option>`).join('');
drawRoastGrind();drawAgeHeat();drawCalendar();drawDialIn()}
function emptyNote(c,id,text){const x=c.getContext('2d');x.clearRect(0,0,c.width,c.height);c._dots=[];c._cells=[];x.fillStyle=TC.dim;x.font='14px sans-serif';x.fillText(text,20,40);$(id).textContent=''}

// Roast level (rows) against the grind setting; colour by days off roast, size by pour flow.
function drawRoastGrind(){const c=$('irchart'),x=c.getContext('2d');x.clearRect(0,0,c.width,c.height);c._dots=[];$('irleg').innerHTML='';
const pts=insShots().filter(s=>byWeight(s)&&s.grind_um&&RL.indexOf(s.roast_level)>0&&(s.grinder_calibration||0)==L.grinder.calibration);
if(!pts.length)return emptyNote(c,'irnote','Needs shots with a roast level and a grind setting on this grinder.');
const lv=[...new Set(pts.map(s=>RL.indexOf(s.roast_level)))].sort((a,b)=>a-b),us=pts.map(s=>s.grind_um),lo=Math.min(...us),hi=Math.max(...us),pad=Math.max(2,(hi-lo)*.1),u0=lo-pad,u1=hi+pad;
const ages=pts.map(ageDays).filter(a=>a!=null),amax=Math.max(30,...ages),fmax=Math.max(.1,...pts.map(s=>s.pour_flow_g_s||0));
const L0=110,R0=c.width-20,T0=12,B0=c.height-26,X=u=>L0+(u-u0)/(u1-u0)*(R0-L0),rowH=(B0-T0)/lv.length,Y=l=>T0+rowH*(lv.indexOf(l)+.5);
x.font='12px sans-serif';x.lineWidth=1;
lv.forEach(l=>{const y=Math.round(Y(l))+.5;x.strokeStyle=TC.grid;x.beginPath();x.moveTo(L0,y);x.lineTo(R0,y);x.stroke();x.fillStyle=TC.dim;x.fillText(RL[l],6,y+4)});
niceTicks(u0,u1,6,1).forEach(v=>{const gx=Math.round(X(v))+.5;x.strokeStyle=TC.gridLo;x.beginPath();x.moveTo(gx,T0);x.lineTo(gx,B0);x.stroke();const lab=fsn(Math.round(v));x.fillStyle=TC.dim;x.fillText(lab,Math.min(R0-x.measureText(lab).width,gx-x.measureText(lab).width/2),c.height-8)});
x.strokeStyle=TC.edge;x.beginPath();x.moveTo(L0,T0);x.lineTo(L0,B0);x.lineTo(R0,B0);x.stroke();
// On each roast level, the settings whose shots ran on time (brew time within the tolerance of its
// target): a band from the lowest to the highest (10th to 90th percentile from five shots on), and
// a line at the middle one.
const tol=S?S.grindToleranceS:2,bands=[];lv.forEach(l=>{const g=pts.filter(s=>RL.indexOf(s.roast_level)==l&&s.target_time_s>0&&btime(s)>0&&Math.abs(btime(s)-s.target_time_s)<=tol).map(s=>s.grind_um).sort((a,b)=>a-b);if(!g.length)return;
const q=f=>g[Math.min(g.length-1,Math.max(0,Math.round(f*(g.length-1))))],lo=g.length>=5?q(.1):g[0],hi=g.length>=5?q(.9):g[g.length-1],md=q(.5),y=Y(l),bx=X(lo)-4,bw=Math.max(8,X(hi)-X(lo)+8);
x.fillStyle='rgba(76,195,138,.16)';x.fillRect(bx,y-rowH*.38,bw,rowH*.76);x.strokeStyle='rgba(76,195,138,.55)';x.strokeRect(bx+.5,y-rowH*.38+.5,bw-1,rowH*.76-1);
x.strokeStyle=STC.ok;x.lineWidth=2;x.beginPath();x.moveTo(X(md),y-rowH*.38);x.lineTo(X(md),y+rowH*.38);x.stroke();x.lineWidth=1;bands.push({l,lo,hi,md,n:g.length})});
const dots=[];pts.forEach((s,i)=>{const a=ageDays(s),px=X(s.grind_um),py=Y(RL.indexOf(s.roast_level))+(((i*37)%17)-8)*rowH/40,rad=3+4*Math.min(1,(s.pour_flow_g_s||0)/fmax);
x.fillStyle=a==null?'#6b7280':ramp(a/amax);x.globalAlpha=.85;x.beginPath();x.arc(px,py,rad,0,7);x.fill();x.globalAlpha=1;
dots.push({x:px,y:py,q:{id:s.id,n:s.coffee||'no coffee',rc:(a!=null?a+' days off roast, ':'')+s.roast_level,w:when(s),u:s.grind_um,t:btime(s)||0,r:s.rating}})});
chartDots(c,dots);
$('irleg').innerHTML='<span class="dim">Days off roast:</span>'+[0,.25,.5,.75,1].map(t=>`<span style="display:inline-flex;align-items:center;gap:4px;margin-left:8px"><span style="display:inline-block;width:18px;height:10px;border-radius:2px;background:${ramp(t)}"></span>${Math.round(t*amax)}</span>`).join('')+'<span class="dim" style="margin-left:14px">larger dot: faster pour</span>'+(bands.length?'<br><span style="display:inline-block;width:18px;height:10px;border-radius:2px;background:rgba(76,195,138,.3);border:1px solid rgba(76,195,138,.7);margin-right:6px"></span><span class="dim">ran on time at </span>'+bands.map(b=>`${RL[b.l]} ${b.lo==b.hi?fsn(b.lo):fsn(b.lo)+'–'+fsn(b.hi)} <span class="dim">(middle ${fsn(b.md)}, ${b.n} shot${b.n==1?'':'s'})</span>`).join(', '):'<br><span class="dim">No shot on this grinder ran on time yet: no band.</span>');
$('irnote').textContent=pts.length+' shots, '+lv.length+' roast level'+(lv.length==1?'':'s')}

// Days off roast (columns, a week each) against brew time minus target (rows, 2 s each): shot counts.
function drawAgeHeat(){const c=$('iachart'),x=c.getContext('2d');x.clearRect(0,0,c.width,c.height);c._cells=[];$('ialeg').innerHTML='';
const tol=S?S.grindToleranceS:2,all=insShots().filter(byWeight),pts=all.filter(s=>s.target_time_s&&ageDays(s)!=null&&btime(s)>0);
if(!pts.length)return emptyNote(c,'ianote','Needs shots with a roast date and a target time.');
const amax=Math.max(...pts.map(ageDays)),nx=Math.min(26,Math.floor(amax/7)+1),edges=[-Infinity,-6,-4,-2,0,2,4,6,Infinity],ny=edges.length-1;
const lab=k=>k==0?'≤ −6 s':k==ny-1?'≥ +6 s':(edges[k]>0?'+':'')+edges[k]+' to '+(edges[k+1]>0?'+':'')+edges[k+1]+' s';
const cnt=[...Array(nx)].map(()=>Array(ny).fill(0));pts.forEach(s=>{const a=Math.min(nx-1,Math.floor(ageDays(s)/7)),e=btime(s)-s.target_time_s;let k=0;while(k<ny-1&&e>=edges[k+1])k++;cnt[a][k]++});
const mx=Math.max(1,...cnt.flat()),L0=96,R0=c.width-12,T0=8,B0=c.height-26,cw=(R0-L0)/nx,ch=(B0-T0)/ny,cells=[];
x.font='12px sans-serif';
for(let k=0;k<ny;k++){const y=B0-(k+1)*ch;x.fillStyle=TC.dim;x.fillText(lab(k),4,y+ch/2+4)}
for(let a=0;a<nx;a++){if(nx<=10||a%2==0){const l=(a*7)+'-'+(a*7+6)+' d';x.fillStyle=TC.dim;x.fillText(l,L0+a*cw+cw/2-x.measureText(l).width/2,c.height-8)}
for(let k=0;k<ny;k++){const v=cnt[a][k],cx=L0+a*cw,cy=B0-(k+1)*ch;x.fillStyle=v?heat(.2+.8*v/mx):TC.empty;x.fillRect(cx+1,cy+1,cw-2,ch-2);
if(v&&cw>18){const t=String(v);x.fillStyle=v/mx>.55?'#1d0f00':TC.fg;x.fillText(t,cx+cw/2-x.measureText(t).width/2,cy+ch/2+4)}
cells.push({x:cx,y:cy,w:cw,h:ch,txt:`<b>${v} shot${v==1?'':'s'}</b><br>${a*7} to ${a*7+6} days off roast<br>brew time ${lab(k).replace(' s','')} s from target`})}}
// the rows within the tolerance: on time
const rows=[];for(let k=0;k<ny;k++)if(edges[k]>=-tol-1e-9&&edges[k+1]<=tol+1e-9)rows.push(k);
if(rows.length){const top=B0-(Math.max(...rows)+1)*ch,bot=B0-Math.min(...rows)*ch;x.strokeStyle='#4cc38a';x.lineWidth=2;x.strokeRect(L0+.5,top+.5,R0-L0-1,bot-top-1);x.lineWidth=1;x.fillStyle='#4cc38a';x.fillText('on time',R0-52,top-3)}
chartCells(c,cells,q=>q.txt);
$('ialeg').innerHTML='<span class="dim">Shots:</span>'+[1,Math.ceil(mx/2),mx].filter((v,i,a)=>a.indexOf(v)==i).map(v=>`<span style="display:inline-flex;align-items:center;gap:4px;margin-left:8px"><span style="display:inline-block;width:14px;height:14px;border-radius:3px;background:${heat(.2+.8*v/mx)}"></span>${v}</span>`).join('');
$('ianote').textContent=pts.length+' shots'+(all.length>pts.length?', '+(all.length-pts.length)+' without a roast date or target left out':'')}

// The last 26 weeks, a column a week: shots per day, or each day's drift from the target; or a
// weekday by hour grid of when shots are pulled.
let CALMODE='shots';
function recipeTags(sh){const k=new Map();sh.forEach(s=>{const n=s.recipe||'';k.set(n,(k.get(n)||0)+1)});const used=new Set(),m=new Map();
[...k.keys()].sort((a,b)=>k.get(b)-k.get(a)).forEach(n=>{const w=n.replace(/[^A-Za-z0-9]/g,'').toUpperCase()||'?';let t=[w[0],w.slice(0,2),w.slice(0,3)].find(c=>c&&!used.has(c));for(let i=2;!t;i++)if(!used.has(w[0]+i))t=w[0]+i;used.add(t);m.set(n,t)});return m}
const WD=['Mon','Tue','Wed','Thu','Fri','Sat','Sun'],dkey=d=>d.getFullYear()+'-'+(d.getMonth()+1)+'-'+d.getDate();
function drawCalendar(){const c=$('icchart');if(CALMODE!='hours'&&c.height!=180){c.height=180;c.style.height='180px'}const x=c.getContext('2d');x.clearRect(0,0,c.width,c.height);c._cells=[];$('icleg').innerHTML='';
document.querySelectorAll('#icmode button').forEach(b=>b.classList.toggle('on',b.dataset.m==CALMODE));
const tol=S?S.grindToleranceS:2,sh=insShots().filter(s=>s.timestamp);if(!sh.length)return emptyNote(c,'icleg','No dated shots yet.');
x.font='12px sans-serif';const L0=40,T0=22,cells=[];
if(CALMODE=='hours')return drawDayTimes(c,x,sh);
const today=new Date();today.setHours(0,0,0,0);const start=new Date(today);start.setDate(start.getDate()-((today.getDay()+6)%7)-25*7);
const days=new Map();sh.forEach(s=>{const d=new Date(s.timestamp);if(d<start)return;const k=dkey(d);let o=days.get(k);if(!o){o={n:0,es:0,en:0,ids:[],sh:[]};days.set(k,o)}o.n++;o.ids.push(s.id);o.sh.push(s);
if(byWeight(s)&&s.target_time_s&&btime(s)>0){o.es+=btime(s)-s.target_time_s;o.en++}});
const mx=Math.max(1,...[...days.values()].map(o=>o.n)),cw=(c.width-L0-8)/26,ch=(c.height-T0-6)/7;let lastM=-1;
// Coffee & recipe: the coffee being brewed now keeps the first colour, as on the grinder charts.
const inR=[...days.values()].flatMap(o=>o.sh),cur=(L.presets[L.activePreset]||{}).coffeeId||0,cids=[...new Set(inR.map(s=>s.coffee_id||0))].sort((a,b)=>(b==cur)-(a==cur)||a-b),
cc=new Map(cids.map((id,i)=>[id,CCOL[i%CCOL.length]])),tg=recipeTags(inR),hm=s=>new Date(s.timestamp).toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'});
for(let w=0;w<26;w++){const first=new Date(start);first.setDate(first.getDate()+w*7);if(first.getMonth()!=lastM){lastM=first.getMonth();x.fillStyle=TC.dim;x.fillText(first.toLocaleDateString([], {month:'short'}),L0+w*cw,14)}
for(let r=0;r<7;r++){const d=new Date(first);d.setDate(d.getDate()+r);if(d>today)continue;const o=days.get(dkey(d)),cx=L0+w*cw,cy=T0+r*ch;let col=TC.empty,what='no shots';
if(o){if(CALMODE=='brew'){what=o.sh.map(s=>hm(s)+' '+esc(s.coffee||'no coffee')+' · '+esc(s.recipe||'no recipe')).join('<br>')}else if(CALMODE=='shots'){col=heat(.2+.8*o.n/mx);what=o.n+' shot'+(o.n==1?'':'s')}else if(o.en){const e=o.es/o.en;col=Math.abs(e)<=tol?STC.ok:e<0?STC.fast:STC.slow;x.globalAlpha=.35+.65*Math.min(1,Math.abs(e)/(3*tol)+(Math.abs(e)<=tol?.6:0));what=o.n+' shot'+(o.n==1?'':'s')+', brew time '+(e>0?'+':'')+f1(e)+' s from target on average'}else{col=TC.edge;what=o.n+' shot'+(o.n==1?'':'s')+', none timed against a target'}}
if(CALMODE=='brew'&&o){const l=o.sh.slice(0,4).reverse(),sw=(cw-2)/l.length;l.forEach((s,i)=>{x.fillStyle=cc.get(s.coffee_id||0);x.fillRect(cx+1+i*sw,cy+1,sw,ch-2)});
const t=[...new Set(o.sh.map(s=>tg.get(s.recipe||'')))].join(' ');x.font='bold 11px sans-serif';x.fillStyle='#111';x.fillText(t,cx+cw/2-x.measureText(t).width/2,cy+ch/2+4);x.font='12px sans-serif'}
else{x.fillStyle=col;x.fillRect(cx+1,cy+1,cw-2,ch-2)}x.globalAlpha=1;
cells.push({x:cx,y:cy,w:cw,h:ch,click:o?o.ids[0]:0,txt:`<b>${d.toLocaleDateString([], {weekday:'short',month:'short',day:'numeric'})}</b><br>${what}${o?'<br><span class="dim">'+(o.n>1?'Click to open the last of them':'Click to open it')+'</span>':''}`})}}
for(let r=0;r<7;r+=2){x.fillStyle=TC.dim;x.fillText(WD[r],4,T0+r*ch+ch/2+4)}
chartCells(c,cells,q=>q.txt,q=>openShot(q.click));c.parentElement.scrollLeft=c.parentElement.scrollWidth;
const sw=v=>`<span style="display:inline-block;width:12px;height:12px;border-radius:3px;background:${v};margin:0 4px 0 10px;vertical-align:middle"></span>`;
if(CALMODE=='brew'){$('icleg').innerHTML=cids.map(id=>{const q=inR.find(s=>(s.coffee_id||0)==id);return sw(cc.get(id))+esc(q.coffee||'no coffee')+(id==cur&&id?' <span class="dim">(brewing now)</span>':'')}).join('')+
'<span class="dim" style="margin-left:14px">Recipes:</span>'+[...tg].map(([n,t])=>` <b>${esc(t)}</b> ${esc(n||'no recipe')}`).join(',')+'<span class="dim" style="margin-left:10px">one stripe per shot, up to 4 a day</span>';return}
$('icleg').innerHTML=CALMODE=='shots'?'<span class="dim">Shots a day:</span>'+sw(heat(.2+.8/mx))+'1'+sw(heat(1))+mx:'<span class="dim">Average brew time against target:</span>'+sw(STC.ok)+'on time'+sw(STC.fast)+'fast'+sw(STC.slow)+'slow'+sw(TC.edge)+'not timed'}

// Bags in order: shots until the first one on time (dial-in), then the rest.
// Time of day: a row a day, newest at the top, across the hours brewed in. A dot a shot in its
// coffee's colour with its recipe's letter; the machine's power presses as markers, with a band
// while it was on.
let DAYSPAN=14;
function drawDayTimes(c,x,sh){
const rows=DAYSPAN,RH=rows<=14?24:rows<=30?19:12,T0=24,L0=92,R0=c.width-12,H=T0+rows*RH+6;
if(c.height!=H){c.height=H;c.style.height='auto'}x.clearRect(0,0,c.width,c.height);x.font='12px sans-serif';
const today=new Date();today.setHours(0,0,0,0);const first=new Date(today);first.setDate(first.getDate()-rows+1);
const inR=sh.filter(s=>new Date(s.timestamp)>=first),pw=(PWR||[]).filter(e=>e.t*1000>=+first);
const hrs=[...inR.map(s=>new Date(s.timestamp)),...pw.map(e=>new Date(e.t*1000))].map(d=>d.getHours()+d.getMinutes()/60);
let h0=hrs.length?Math.floor(Math.min(...hrs)):6,h1=hrs.length?Math.ceil(Math.max(...hrs)+.01):22;if(h1-h0<6){const m=(h0+h1)/2;h0=Math.max(0,Math.floor(m-3));h1=Math.min(24,h0+6)}
const X=h=>L0+(h-h0)/(h1-h0)*(R0-L0),hod=d=>d.getHours()+d.getMinutes()/60+d.getSeconds()/3600,rowOf=d=>{const k=new Date(d);k.setHours(0,0,0,0);return Math.round((today-k)/864e5)};
const cur=(L.presets[L.activePreset]||{}).coffeeId||0,cids=[...new Set(inR.map(s=>s.coffee_id||0))].sort((a,b)=>(b==cur)-(a==cur)||a-b),cc=new Map(cids.map((id,i)=>[id,CCOL[i%CCOL.length]])),tg=recipeTags(inR);
const hm=d=>d.toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'}),cells=[];
// hour lines and labels
const step=h1-h0>12?2:1;for(let h=h0;h<=h1;h+=step){const gx=Math.round(X(h))+.5;x.strokeStyle=TC.gridLo;x.beginPath();x.moveTo(gx,T0-4);x.lineTo(gx,T0+rows*RH);x.stroke();
const t=String(h%24).padStart(2,'0')+':00';x.fillStyle=TC.dim;x.fillText(t,Math.max(L0,Math.min(R0-x.measureText(t).width,gx-x.measureText(t).width/2)),14)}
for(let r=0;r<rows;r++){const d=new Date(today);d.setDate(d.getDate()-r);const y=T0+r*RH;if(r%2==0){x.fillStyle=TC.stripe;x.fillRect(L0,y,R0-L0,RH)}
if(RH>=19||r%2==0){x.fillStyle=d.getDay()==0||d.getDay()==6?TC.mid:TC.dim;x.fillText(WD[(d.getDay()+6)%7]+' '+d.toLocaleDateString([], {day:'numeric',month:'short'}),4,y+RH/2+4)}}
// machine on: from an on press to the next off press the same day (to the end of the hours if none)
const byRow=new Map();pw.forEach(e=>{const r=rowOf(new Date(e.t*1000));if(r<0||r>=rows)return;if(!byRow.has(r))byRow.set(r,[]);byRow.get(r).push(e)});
byRow.forEach((es,r)=>{es.sort((a,b)=>a.t-b.t);let from=null;es.forEach(e=>{if(!e.ok)return;const h=hod(new Date(e.t*1000));if(e.on)from=from??h;else if(from!=null){x.fillStyle='rgba(76,195,138,.13)';x.fillRect(X(from),T0+r*RH+2,X(h)-X(from),RH-4);from=null}});
if(from!=null){const endH=r==0?Math.min(h1,hod(new Date())):h1;if(endH>from){x.fillStyle='rgba(76,195,138,.13)';x.fillRect(X(from),T0+r*RH+2,X(endH)-X(from),RH-4)}}});
pw.forEach(e=>{const d=new Date(e.t*1000),r=rowOf(d);if(r<0||r>=rows)return;const px=X(hod(d)),cy=T0+r*RH+RH/2,z=Math.min(6,RH/2-2);
x.beginPath();if(e.on){x.moveTo(px,cy-z);x.lineTo(px-z,cy+z);x.lineTo(px+z,cy+z)}else{x.moveTo(px,cy+z);x.lineTo(px-z,cy-z);x.lineTo(px+z,cy-z)}x.closePath();
const col=e.on?STC.ok:TC.dim;if(e.ok){x.fillStyle=col;x.fill()}else{x.strokeStyle=col;x.lineWidth=1.5;x.stroke();x.lineWidth=1}
cells.push({x:px-z-2,y:cy-z-2,w:2*z+4,h:2*z+4,txt:`<b>Machine ${e.on?'on':'off'}${e.ok?'':': not done'}</b><br>${hm(d)}, ${e.by=='schedule'?'by the schedule':e.by=='scale'?'when the scale switched off':'by you'}${e.ok?'':'<br>'+esc(e.outcome)}${e.sure===false?'<br>pressed while on/off was not known':''}`})});
// shots: dots in time order on each row, nudged apart when they would touch
const rad=Math.max(3,Math.min(9,RH/2-2)),gap=2*rad+1,lastX=new Map();
inR.map(s=>({s,d:new Date(s.timestamp)})).sort((a,b)=>a.d-b.d).forEach(({s,d})=>{const r=rowOf(d);if(r<0||r>=rows)return;let px=X(hod(d));const lx=lastX.get(r);if(lx!=null&&px-lx<gap)px=lx+gap;px=Math.min(px,R0-rad);lastX.set(r,px);
const cy=T0+r*RH+RH/2;x.fillStyle=cc.get(s.coffee_id||0);x.beginPath();x.arc(px,cy,rad,0,7);x.fill();
if(rad>=6){const t=tg.get(s.recipe||'');x.font='bold '+Math.round(rad*1.15)+'px sans-serif';x.fillStyle='#111';x.fillText(t,px-x.measureText(t).width/2,cy+rad*.42);x.font='12px sans-serif'}
cells.push({x:px-rad,y:cy-rad,w:2*rad,h:2*rad,click:s.id,txt:`<b>${hm(d)}</b> ${esc(s.coffee||'no coffee')}<br>${esc(s.recipe||'no recipe')}${s.final_weight_g?', '+f1(s.final_weight_g)+' g':''}<br><span class="dim">Click to open it</span>`})});
// later cells win: a shot over a power marker
chartCells(c,cells.reverse(),q=>q.txt,q=>openShot(q.click));
const sw=v=>`<span style="display:inline-block;width:12px;height:12px;border-radius:50%;background:${v};margin:0 4px 0 10px;vertical-align:middle"></span>`;
$('icleg').innerHTML='<span class="dim">Days:</span>'+[14,30,90].map(n=>` <button class="sm${n==DAYSPAN?' on':''}" onclick="DAYSPAN=${n};drawCalendar()">${n}</button>`).join('')+
(inR.length?cids.map(id=>{const q=inR.find(s=>(s.coffee_id||0)==id);return sw(cc.get(id))+esc(q.coffee||'no coffee')}).join('')+'<span class="dim" style="margin-left:12px">Recipes:</span>'+[...tg].map(([n,t])=>` <b>${esc(t)}</b> ${esc(n||'no recipe')}`).join(','):'<span class="dim" style="margin-left:10px">No shots in these days</span>')+
(PWR==null?'':'<span style="margin-left:12px;color:'+STC.ok+'">▲</span> machine on <span style="color:#9aa3ad">▼</span> off'+(pw.length?'':' <span class="dim">(none logged in these days)</span>'))}

// Bags in order: shots until the first one on time (dial-in), then the rest, each by how it ran: on
// time, fast or slow, so a bag that drifted after it was dialled in shows it.
function drawDialIn(){const c=$('idchart'),x=c.getContext('2d');x.clearRect(0,0,c.width,c.height);c._cells=[];$('idleg').innerHTML='';
const tol=S?S.grindToleranceS:2,sh=insShots().filter(s=>byWeight(s)&&s.target_time_s&&btime(s)>0&&s.timestamp).sort((a,b)=>new Date(a.timestamp)-new Date(b.timestamp));
const bags=new Map();sh.forEach(s=>{const k=(s.coffee_id||0)+'|'+(s.roast_date||0);let b=bags.get(k);if(!b){b={n:s.coffee||'no coffee',rd:s.roast_date,shots:[]};bags.set(k,b)}b.shots.push(s)});
const list=[...bags.values()].slice(-12);if(!list.length)return emptyNote(c,'idnote','Needs shots stopped by weight with a target time.');
const how=s=>{const e=btime(s)-s.target_time_s;return Math.abs(e)<=tol?'ok':e<0?'fast':'slow'};
list.forEach(b=>{const i=b.shots.findIndex(s=>how(s)=='ok');b.dial=i<0?null:i+1;b.total=b.shots.length;
const after=i<0?[]:b.shots.slice(i+1);b.after={ok:after.filter(s=>how(s)=='ok').length,fast:after.filter(s=>how(s)=='fast').length,slow:after.filter(s=>how(s)=='slow').length};
const last=b.shots[b.shots.length-1],e=btime(last)-last.target_time_s;b.last={how:how(last),e}});
const mx=Math.max(...list.map(b=>b.total)),L0=40,R0=c.width-12,T0=24,B0=c.height-40,bw=(R0-L0)/list.length,Y=v=>B0-v/mx*(B0-T0),cells=[];
x.font='12px sans-serif';x.lineWidth=1;
niceTicks(0,mx,4,1).forEach(v=>{const y=Math.round(Y(v))+.5;x.strokeStyle=TC.grid;x.beginPath();x.moveTo(L0,y);x.lineTo(R0,y);x.stroke();x.fillStyle=TC.dim;x.fillText(String(v),8,y+4)});
const sign=v=>(v>0?'+':v<0?'−':'')+f1(Math.abs(v));
list.forEach((b,i)=>{const bx=L0+i*bw+bw*.15,w=bw*.7,d=b.dial==null?b.total:b.dial;
// the dial-in: orange; dashed while no shot has been on time yet
x.fillStyle='#f08a24';x.fillRect(bx,Y(d),w,B0-Y(d));
if(b.dial==null){x.save();x.setLineDash([5,4]);x.strokeStyle=TC.fg;x.lineWidth=1.5;x.strokeRect(bx+.75,Y(d)+.75,w-1.5,B0-Y(d)-1.5);x.restore()}
// then the shots after it, stacked: on time, fast, slow
let at=d;[['ok',STC.ok],['fast',STC.fast],['slow',STC.slow]].forEach(([k,col])=>{const n=b.after[k];if(!n)return;x.fillStyle=col;x.globalAlpha=.75;x.fillRect(bx,Y(at+n),w,Y(at)-Y(at+n));x.globalAlpha=1;
if(Y(at)-Y(at+n)>14&&w>16){const t=String(n);x.fillStyle='#111';x.fillText(t,bx+w/2-x.measureText(t).width/2,Y(at+n)+(Y(at)-Y(at+n))/2+4)}at+=n});
const lab=b.n.length>14?b.n.slice(0,13)+'…':b.n,rd=ymd(b.rd);x.fillStyle=TC.mid;x.fillText(lab,bx+w/2-x.measureText(lab).width/2,B0+14);if(rd){const t=rd.toLocaleDateString([], {month:'short',day:'numeric'});x.fillStyle=TC.dim;x.fillText(t,bx+w/2-x.measureText(t).width/2,B0+28)}
{const t=String(b.total);x.fillStyle=TC.mid;x.fillText(t,bx+w/2-x.measureText(t).width/2,Y(b.total)-5)}if(B0-Y(d)>16&&w>16){const t=b.dial==null?'–':String(b.dial);x.fillStyle='#1d0f00';x.fillText(t,bx+w/2-x.measureText(t).width/2,B0-4)}
const a=b.after,parts=[a.ok&&a.ok+' on time',a.fast&&a.fast+' fast',a.slow&&a.slow+' slow'].filter(Boolean);
cells.push({x:bx,y:Y(b.total),w,h:B0-Y(b.total),txt:`<b>${esc(b.n)}</b>${rd?', roasted '+rd.toLocaleDateString():''}<br>${b.dial==null?'no shot on time yet, '+b.total+' shot'+(b.total==1?'':'s'):b.dial+' shot'+(b.dial==1?'':'s')+' to the first on time'+(parts.length?'; after it: '+parts.join(', '):'')}<br>latest shot: ${b.last.how=='ok'?'on time':b.last.how} (${sign(b.last.e)} s)`})});
x.strokeStyle=TC.edge;x.beginPath();x.moveTo(L0,T0);x.lineTo(L0,B0);x.lineTo(R0,B0);x.stroke();
chartCells(c,cells,q=>q.txt);
const sw=v=>`<span style="display:inline-block;width:12px;height:12px;border-radius:3px;background:${v};margin:0 4px 0 10px;vertical-align:middle"></span>`;
$('idleg').innerHTML=sw('#f08a24')+'shots to the first on time (dashed: none on time yet)<span class="dim" style="margin-left:12px">after it:</span>'+sw(STC.ok)+'on time'+sw(STC.fast)+'fast'+sw(STC.slow)+'slow';
const done=list.filter(b=>b.dial!=null).map(b=>b.dial),drift=list.filter(b=>b.dial!=null&&b.last.how!='ok').length;
$('idnote').textContent=list.length+' bag'+(list.length==1?'':'s')+(done.length?', '+(done.reduce((a,b)=>a+b,0)/done.length).toFixed(1)+' shots to dial in on average':'')+(drift?', '+drift+' drifted off since':'')}
// Grind advice against target (/api/grind/accuracy): where the next shot landed after each advised
// change, and the confidence as it stood (the controller's formula, core/GrindAdvisor.h adviceConfidence).
async function drawGrindAcc(){const c=$('igchart');if(!c)return;let d;try{d=await get('/api/grind/accuracy')}catch(e){return}
const x=c.getContext('2d');x.clearRect(0,0,c.width,c.height);c._cells=[];$('igleg').innerHTML='';$('igtiles').innerHTML='';
const ev=(d.events||[]).filter(e=>e.target_s>0),tol=d.tolerance_s||2;if(!ev.length)return emptyNote(c,'ignote','Shows once a shot has followed a change the advice asked for.');
const conf=i=>{const f=[];for(let k=i-1;k>=0&&f.length<30;k--)if(ev[k].followed)f.push(ev[k].time_s-ev[k].target_s);if(f.length<2)return .3;
const r=Math.sqrt(f.reduce((a,v)=>a+v*v,0)/f.length);return Math.max(0,Math.min(1,1-r/(3*tol)))*f.length/(f.length+3)};
const errs=ev.map(e=>e.time_s-e.target_s),mx=Math.max(tol*2,...errs.map(Math.abs))*1.1,L0=44,R0=c.width-44,T0=14,B0=c.height-30,cx=i=>L0+(ev.length==1?.5:i/(ev.length-1))*(R0-L0),
Y=v=>T0+(mx-v)/(2*mx)*(B0-T0),YC=v=>B0-v*(B0-T0),cells=[];x.font='12px sans-serif';
x.fillStyle=STC.ok;x.globalAlpha=.14;x.fillRect(L0,Y(tol),R0-L0,Y(-tol)-Y(tol));x.globalAlpha=1;
niceTicks(-mx,mx,5,1).forEach(v=>{const y=Math.round(Y(v))+.5;x.strokeStyle=TC.grid;x.beginPath();x.moveTo(L0,y);x.lineTo(R0,y);x.stroke();x.fillStyle=TC.dim;x.fillText((v>0?'+':'')+v+' s',4,y+4)});
[0,50,100].forEach(v=>{x.fillStyle=TC.dim;x.fillText(v+'%',R0+6,YC(v/100)+4)});
x.strokeStyle='#f08a24';x.lineWidth=2;x.beginPath();ev.forEach((e,i)=>{const y=YC(conf(i+1));i?x.lineTo(cx(i),y):x.moveTo(cx(i),y)});x.stroke();x.lineWidth=1;
ev.forEach((e,i)=>{const err=errs[i],px=cx(i),py=Y(err),col=Math.abs(err)<=tol?STC.ok:err<0?STC.fast:STC.slow;x.beginPath();x.arc(px,py,5,0,7);
if(e.followed){x.fillStyle=col;x.fill()}else{x.strokeStyle=TC.dim;x.stroke()}
const name=(coffee(e.coffee_id)||{}).name||'a coffee',when=e.at?new Date(e.at*1000).toLocaleDateString():'';
cells.push({x:px-6,y:py-6,w:12,h:12,txt:`<b>${esc(name)}</b> ${when}<br>advised ${esc(fs(e.from))} \u2192 ${esc(fs(e.advised))}${e.followed?'':', pulled at '+esc(fs(e.next))}<br>next shot ${f1(e.time_s)} s against ${f1(e.target_s)} s (${err>0?'+':''}${f1(err)} s)${e.confidence!=null?'<br>confidence then '+e.confidence+'%':''}`})});
x.strokeStyle=TC.edge;x.beginPath();x.moveTo(L0,T0);x.lineTo(L0,B0);x.lineTo(R0,B0);x.stroke();chartCells(c,cells,q=>q.txt);
const fo=ev.filter(e=>e.followed),on=fo.filter(e=>Math.abs(e.time_s-e.target_s)<=tol).length,pc=Math.round(100*d.score);
const tile=(t,v,u)=>`<div class="tile"><span>${t}</span><b>${v}</b><span>${u}</span></div>`;
$('igtiles').innerHTML=tile('On target',fo.length?Math.round(100*on/fo.length)+' %':'--',on+' of '+fo.length+' followed changes')+tile('Typical miss',fo.length?f1(d.rms_s)+' s':'--','from the target time')+tile('Confidence',pc+' %',pc>=60?'high: bigger steps allowed':pc>=35?'medium':'low: smaller steps');
const sw=v=>`<span style="display:inline-block;width:12px;height:12px;border-radius:50%;background:${v};margin:0 4px 0 10px;vertical-align:middle"></span>`;
$('igleg').innerHTML=sw(STC.ok)+'landed on time'+sw(STC.fast)+'still fast'+sw(STC.slow)+'still slow'+'<span style="display:inline-block;width:16px;height:2px;background:#f08a24;margin:0 4px 0 12px;vertical-align:middle"></span>confidence';
$('ignote').textContent=ev.length+' change'+(ev.length==1?'':'s')+' advised, '+fo.length+' followed'}
function drawFilter(){const cur=$('fcoffee').value;$('fcoffee').innerHTML='<option value="">All coffees</option>'+[...new Set(SH.map(s=>s.coffee).filter(x=>x))].map(n=>`<option ${n==cur?'selected':''}>${esc(n)}</option>`).join('')}
async function loadShots(){let a;try{a=await get('/api/shots?limit=500')}catch(e){return}if(!Array.isArray(a))return;SH=a;drawFilter();drawShots();
if(SH.length){showShot(SH[0].id,'h');if(!SEL||!SH.some(s=>s.id==SEL))SEL=SH[0].id;showShot(SEL,'s')}drawScatter();loadPrev();if(location.hash=='#insights')drawInsights()}
// The grind advice as it stood right after the newest shot: recorded with the shot (its context,
// /api/contexts), so it stays what was said even once the grinder has been moved.
let PREV=null,PREVTRY=0;
async function loadPrev(){const s=SH[0];if(!s){PREV=null;return drawPrev()}let a;try{a=await get('/api/contexts?limit=1')}catch(e){return}
PREV=Array.isArray(a)&&a[0]&&a[0].shot_id==s.id?a[0]:null;
if(!PREV&&PREVTRY<3&&s.timestamp&&Date.now()-new Date(s.timestamp)<120000){PREVTRY++;setTimeout(()=>{if(SH[0]&&SH[0].id==s.id)loadPrev()},3000)}else PREVTRY=0;drawPrev()}
function drawPrev(){const box=$('hprev'),s=SH[0],c=PREV;if(!L)return;
if(!s){box.innerHTML='<span class="dim">No shot yet. The advice given after your next shot appears here.</span>';return}
if(!c||c.shot_id!=s.id){box.innerHTML='<span class="dim">No grinder recommendation was recorded for the most recent shot.</span>';return}
const a=c.advice_action,fin=a=='finer',mv=fin||a=='coarser',p=L.presets[L.activePreset]||{};
const title=a=='hold'?'Keep '+fs(c.advice_um):mv?`Go ${fin?'finer':'coarser'} ${fdn(c.advice_delta_um)} ${fdu()}, to ${fs(c.advice_um)}${c.advice_refined?' (to your liked time)':''}`:a=='inconsistent'?'Brew times vary: no advice':a=='no_data'?'No advice: the shot did not count':a=='no_grind_set'?'No advice: no grind setting entered':'No advice';
const now=G&&G.current_um&&s.coffee_id==p.coffeeId&&s.recipe_id==p.recipeId&&G.current_um!=s.grind_um?' The grinder is at '+fs(G.current_um)+' now.':'';
box.innerHTML=`<div class="row"><span class="pill ${a=='hold'?'ok':mv||a=='inconsistent'?'warn':''}">${esc(title)}</span></div><div>After <b>${esc(s.coffee||'no coffee')}</b>, ${esc(s.recipe)}, ${esc(when(s))}, pulled at ${s.grind_um?esc(fs(s.grind_um)):'no setting recorded'}.${c.advice_shots?' From '+c.advice_shots+' shot'+(c.advice_shots==1?'':'s')+' at that setting.':''}${mv&&c.advice_step_limited?' Limited to the largest single change.':''}${esc(now)}</div>`}
const when=s=>s.timestamp?new Date(s.timestamp).toLocaleString([], {month:'short',day:'numeric',hour:'numeric',minute:'2-digit'}):'#'+s.id;
let SHOWN=20,SOPEN=true;
function pickShot(id){if(SEL==id&&SOPEN)SOPEN=false;else{SEL=id;SOPEN=true;showShot(id,'s')}drawShots()}
const dayOf=s=>{if(!s.timestamp)return 'Date not known';const d=new Date(s.timestamp),n=new Date(),k=x=>x.toDateString();if(k(d)==k(n))return 'Today';n.setDate(n.getDate()-1);if(k(d)==k(n))return 'Yesterday';return d.toLocaleDateString([], {weekday:'short',month:'short',day:'numeric',year:d.getFullYear()==new Date().getFullYear()?undefined:'numeric'})};
const clock=s=>s.timestamp?new Date(s.timestamp).toLocaleTimeString([], {hour:'numeric',minute:'2-digit'}):'#'+s.id;
function drawShots(){const fc=$('fcoffee').value,fw=$('fshow').value,tol=S?S.grindToleranceS:2;
const byW=s=>/^weight/.test(s.end_reason),bt=s=>s.time_basis=='pour'?s.pour_time_s:s.shot_time_s,late=s=>byW(s)&&s.target_time_s&&Math.abs(bt(s)-s.target_time_s)>tol;
const all=SH.filter(s=>(!fc||s.coffee==fc)&&(fw=='1'?byW(s):fw=='2'?late(s):fw=='3'?!s.rating&&!s.taste:true));
const w=all.filter(byW),timed=w.filter(s=>s.target_time_s),good=timed.filter(s=>!late(s)).length,miss=w.map(s=>Math.abs(s.final_weight_g-s.target_weight_g)).sort((a,b)=>a-b),rated=all.filter(s=>s.rating);
$('ssum').innerHTML=[['Shots',all.length,w.length+' stopped by weight'],['On time',timed.length?Math.round(100*good/timed.length)+' %':'--',timed.length?good+' of '+timed.length+' within ± '+tol+' s':'no target times yet'],['Typical miss',miss.length?f1(miss[miss.length>>1])+' g':'--','from the target weight'],['Rated',rated.length?(rated.reduce((a,s)=>a+s.rating,0)/rated.length).toFixed(1)+' ★':'--',rated.length+' of '+all.length+' shots']].map(x=>`<div class="tile"><span>${x[0]}</span><b>${x[1]}</b><span>${x[2]}</span></div>`).join('');
const det=$('sdetail');$('sstash').appendChild(det);
let h='<div class="srow shd"><span>Time</span><span>Coffee, recipe</span><span class="r">Out</span><span class="r">Brew</span><span class="r g">Grind '+U+'</span></div>',day='';
all.slice(0,SHOWN).forEach(s=>{const k=dayOf(s);if(k!=day){day=k;h+=`<div class="day">${k}</div>`}const on=s.id==SEL&&SOPEN;
h+=`<div class="srow pick ${on?'on':''}" role="button" tabindex="0" aria-expanded="${on}" onclick="pickShot(${s.id})" onkeydown="if(event.key=='Enter'||event.key==' '){event.preventDefault();pickShot(${s.id})}"><span class="dim">${esc(clock(s))}</span><span class="nm"><b>${esc(s.coffee||s.recipe)}</b><span>${s.coffee?esc(s.recipe):''}${s.rating?' <i class="st">'+'★'.repeat(s.rating)+'</i>':''}${s.taste?' '+s.taste:''}${byW(s)?'':' <i class="tag">'+esc(s.end_reason.replace(/_/g,' '))+'</i>'}${s.grind_vote=='ignore'?' <i class="tag q">ignored</i>':s.grind_vote=='count'?' <i class="tag q">counted</i>':''}</span></span><span class="r">${f1(s.final_weight_g)} g</span><span class="r ${late(s)?'off':''}">${f1(s.shot_time_s)} s</span><span class="r g">${fsn(s.grind_um)||'--'}</span></div>`;if(on)h+='<div id="sslot"></div>'});
if(!all.length)h+='<div class="dim" style="padding:16px 10px">'+(SH.length?'No shots match. Change what is shown above.':'No shots yet. Brew one and it appears here.')+'</div>';
$('hist').innerHTML=h;const slot=$('sslot');if(slot)slot.appendChild(det);
$('smore').innerHTML=all.length>SHOWN?`<button onclick="SHOWN+=20;drawShots()">Show 20 more</button><span class="sm dim">${SHOWN} of ${all.length} shown</span>`:all.length>20?`<span class="sm dim">All ${all.length} shown</span>`:''}
function editGrind(id,cur){const v=prompt('Grind setting for this shot, in '+U+' (0 = not recorded)',fsn(cur)||'');if(v===null)return;const um=(v.trim()===''||v.trim()=='0')?0:ps(v);if(um==null)return say('That is not a setting this grinder can have',false);post('/api/shot/grind?id='+id+'&um='+um).then(r=>{done(r,'');loadShots();loadGrind()})}
function vote(id,cur,next){if(next==null)next=cur==''?2:cur=='count'?1:0;post('/api/shot/vote?id='+id+'&vote='+next).then(r=>{done(r,'');loadShots();loadGrind()})}
function setPrep(id){const d=$('pdose'+id).value.trim().replace(',','.'),t=d===''?0:parseFloat(d);if(!(t===0||(t>=5&&t<=40)))return say('Dose weighed: 5 to 40 g, or leave it empty',false);const u=$('pune'+id).checked;post('/api/shot/prep?id='+id+'&dose='+t+'&purged='+($('ppur'+id).checked?1:0)+'&uneven='+(u?1:0)).then(r=>{done(r,u?'Saved. If this uneven shot should not steer the grind, choose Ignore.':'Saved');loadShots()})}
function prepRow(s){return `<span>Preparation</span><span style="display:inline-flex;align-items:center;gap:6px;white-space:nowrap;margin-left:4px"><span>dose weighed</span><input id="pdose${s.id}" style="width:64px" inputmode="decimal" aria-label="Dose weighed in grams" value="${s.dose_weighed_g?f1(s.dose_weighed_g):''}" placeholder="${s.dose_g?f1(s.dose_g):''}" onchange="setPrep(${s.id})"><span class="dim">g</span></span><label style="margin-left:8px;display:inline-flex;flex-direction:row;align-items:center;gap:4px;white-space:nowrap"><input type="checkbox" id="ppur${s.id}" ${s.purged?'checked':''} onchange="setPrep(${s.id})"> purged first</label><label style="margin-left:8px;display:inline-flex;flex-direction:row;align-items:center;gap:4px;white-space:nowrap"><input type="checkbox" id="pune${s.id}" ${s.uneven?'checked':''} onchange="setPrep(${s.id})"> flow looked uneven</label>`}
function setTds(id,v){const t=parseFloat(String(v).replace(',','.'));if(v!==''&&!(t>=0.5&&t<=25))return say('TDS is a percent from 0.5 to 25',false);post('/api/shot/tds?id='+id+'&tds='+(v===''?0:t)).then(r=>{done(r,'');loadShots()})}
function rate(id,rating,taste){post('/api/shot/rate?id='+id+'&rating='+rating+'&taste='+taste).then(r=>{done(r,'');loadShots()})}
const TN=['','sour','balanced','bitter'];
function rateRow(s){const t=TN.indexOf(s.taste||'');return '<span style="display:inline-flex;flex-wrap:wrap;align-items:center;gap:6px"><span>How did it taste?</span>'+[1,2,3].map(i=>`<button class="${t==i?'on':''}" onclick="rate(${s.id},${s.rating||0},${t==i?0:i})">${TN[i][0].toUpperCase()+TN[i].slice(1)}</button>`).join('')+'</span><span style="display:inline-flex;flex-wrap:wrap;align-items:center;gap:6px;margin-left:8px"><span>Rating</span>'+[1,2,3,4,5].map(i=>`<button class="${s.rating==i?'on':''}" aria-label="${i} of 5" onclick="rate(${s.id},${s.rating==i?0:i},${t<0?0:t})">${i}</button>`).join('')+'</span>'+`<span style="display:inline-flex;align-items:center;gap:6px;white-space:nowrap;margin-left:8px"><span>TDS %</span><input style="width:70px" inputmode="decimal" aria-label="TDS percent from a refractometer" value="${s.tds_pct||''}" onchange="setTds(${s.id},this.value.trim())">${s.extraction_pct?`<span class="dim">extraction ${f1(s.extraction_pct)} %</span>`:''}</span>`}
async function showShot(id,where){const s=SH.find(x=>x.id==id);if(!s)return;const big=where=='h';
if(big){$('hshot').className='';$('hshot').innerHTML=`<div class="row" style="gap:26px;align-items:baseline"><span><span class="num">${f1(s.final_weight_g)} g</span> <span class="dim">of ${s.target_weight_g}</span></span><span><span class="num">${f1(s.shot_time_s)} s</span>${s.target_time_s?` <span class="dim">of ${s.target_time_s}</span>`:''}</span><span class="dim">${esc((s.coffee?s.coffee+', ':'')+s.recipe)}. First drops ${f1(s.first_drop_s)} s. Pour ${f1(s.pour_time_s)} s at ${f2(s.pour_flow_g_s)} g/s${s.bloom_pause_s?'. Bloom '+s.bloom_pause_s+' s':''}. ${esc(when(s))}.</span></div>`;$('hrate').innerHTML=rateRow(s)+' <button style="margin-left:12px" onclick="toBeanconqueror('+s.id+')">Send to Beanconqueror</button>'+(panelOn('community')?' <button onclick="toReddit('+s.id+')">Share to Reddit</button>':'');
const d=s.final_weight_g-s.target_weight_g;$('res').textContent='Last shot '+(Math.abs(d)<0.05?'on target':f1(Math.abs(d))+' g '+(d>0?'over':'under'))+(s.final_valid?'':', did not settle')}
else{$('swhen').textContent=when(s);$('sname').textContent=(s.coffee?s.coffee+', ':'')+s.recipe;
$('stiles').innerHTML=[['In the cup',f1(s.final_weight_g)+' g','target '+s.target_weight_g+', drip '+f1(s.drip_weight_g)],['Brew time',f1(s.shot_time_s)+' s',s.target_time_s?'target '+s.target_time_s:''],['Pour',f1(s.pour_time_s)+' s',f2(s.pour_flow_g_s)+' g/s, first drops '+f1(s.first_drop_s)+' s'],['Grind',(s.grind_um?fs(s.grind_um):'--'),s.dose_g+' g dose · <a href="#" onclick="editGrind('+s.id+','+s.grind_um+');return false">change</a>'],['Flow at 25/50/75 %',[s.flow_25_g_s,s.flow_50_g_s,s.flow_75_g_s].map(f1).join(' / '),'peak '+f2(s.peak_flow_g_s)+' g/s'],['Ended by',esc(s.end_reason.replace(/_/g,' ')),'offset '+f2(s.stop_offset_g)+' → '+f2(s.next_stop_offset_g)+' g'+(s.bloom_pause_s?', bloom '+s.bloom_pause_s+' s':'')]].map(x=>`<div class="tile"><span>${x[0]}</span><b>${x[1]}</b><span>${x[2]}</span></div>`).join('');
cmpButton();$('svote').innerHTML=[['Auto',0,''],['Count',2,'count'],['Ignore',1,'ignore']].map(v=>`<button class="${s.grind_vote==v[2]?'on':''}" onclick="vote(${s.id},'',${v[1]})">${v[0]}</button>`).join('');$('srate').innerHTML=rateRow(s)+'<div class="row" style="margin-top:6px;gap:6px;align-items:center">'+prepRow(s)+'</div>'}
const cv=big?'hchart':'schart';if(!s.has_data){draw(cv,s,null);if(!big)$('sdata').textContent='The readings of this shot are no longer stored; the summary is.';return}
let r;try{r=await get('/api/shot/data?id='+id)}catch(e){return}if(!r.time_s)return;
// the shot you rated best of this coffee and recipe, drawn behind it for comparison
const refs=SH.filter(q=>q.id!=s.id&&q.coffee_id==s.coffee_id&&q.recipe_id==s.recipe_id&&q.has_data&&q.rating>=4).sort((a,b)=>b.rating-a.rating||b.id-a.id);let REF=null,refShot=refs[0];
if(refShot){try{const q=await get('/api/shot/data?id='+refShot.id);if(q.time_s)REF={T:q.time_s,W:q.weight_g}}catch(e){}}
draw(cv,s,r.time_s,r.weight_g,r.flow_g_s,REF);
if(!big){const d=r.data;$('sdata').innerHTML=`White: weight. Orange: flow.${REF?' Dashed green: your best-rated shot of this coffee ('+'\u2605'.repeat(refShot.rating)+', '+f1(refShot.shot_time_s)+' s).':''} ${d.samples} readings over ${d.duration_s} s, one every ${d.mean_period_ms} ms, longest gap ${d.max_gap_ms} ms. `+(d.timer_samples?`Scale timer in ${d.timer_samples} of them`+(d.timer_span_s>0?`: it counted ${d.timer_span_s} s while this controller counted ${d.clock_span_s} s. `:'. '):'The scale sent no timer. ')+`<a href="/shot.csv?id=${id}">Download this shot's readings</a>`}}
function draw(id,s,T,W,F,REF){const c=$(id),x=c.getContext('2d');x.clearRect(0,0,c.width,c.height);if(!T||T.length<2)return;
const fv=F?F.filter(v=>v!=null):[],tm=Math.max(T[T.length-1],REF?REF.T[REF.T.length-1]:0,1),wm=Math.max(s.target_weight_g*1.1,...W,...(REF?REF.W:[0])),fm=Math.max(1,...fv),R=52,X=t=>34+t/tm*(c.width-34-R),Y=w=>c.height-20-Math.max(w,0)/wm*(c.height-32),YF=v=>c.height-20-Math.max(v,0)/fm*(c.height-32);
x.lineWidth=1;x.strokeStyle='#6b7280';x.setLineDash([4,6]);x.beginPath();x.moveTo(34,Y(s.target_weight_g));x.lineTo(c.width-R,Y(s.target_weight_g));x.stroke();
x.fillStyle=TC.dim;x.font='12px sans-serif';x.strokeStyle=TC.edge;[[s.first_drop_s,'first drops'],[s.shot_time_s,'stop']].forEach(m=>{if(!(m[0]>0&&m[0]<=tm))return;x.beginPath();x.moveTo(X(m[0]),10);x.lineTo(X(m[0]),c.height-20);x.stroke();x.fillText(m[1],X(m[0])+4,c.height-26)});x.setLineDash([]);
if(F){x.strokeStyle='#f08a24';x.lineWidth=2;x.beginPath();let pen=false;T.forEach((t,i)=>{if(F[i]==null){pen=false;return}pen?x.lineTo(X(t),YF(F[i])):x.moveTo(X(t),YF(F[i]));pen=true});x.stroke()}
if(REF){x.strokeStyle='#4cc38a';x.lineWidth=1.5;x.setLineDash([6,5]);x.beginPath();REF.T.forEach((t,i)=>i?x.lineTo(X(t),Y(REF.W[i])):x.moveTo(X(t),Y(REF.W[i])));x.stroke();x.setLineDash([])}
x.strokeStyle=TC.fg;x.lineWidth=2.5;x.beginPath();T.forEach((t,i)=>i?x.lineTo(X(t),Y(W[i])):x.moveTo(X(t),Y(W[i])));x.stroke();
x.fillStyle=TC.fg;x.fillText(wm.toFixed(0)+' g',2,16);x.fillStyle=TC.dim;x.fillText(tm.toFixed(0)+' s',c.width-R-28,c.height-4);x.fillStyle='#f08a24';x.fillText(fm.toFixed(1)+' g/s',c.width-R+6,16)}

const UPK={backflush:'Backflush',descale:'Descale',grinder_clean:'Clean the grinder'};
async function loadUpkeep(){let m;try{m=await get('/api/maintenance')}catch(e){return}$('mt_total').textContent=m.shots_total+' shots counted';
const row=j=>`<div class="row" style="align-items:center"><b style="flex:1 1 150px">${UPK[j.id]}</b><span style="flex:1 1 200px" class="${j.due?'off':''}">${j.since_shots} shots since${j.last_epoch?', last done '+new Date(j.last_epoch*1000).toLocaleDateString():''}${j.due?' - due now':j.every_shots?' - next in '+Math.max(0,j.every_shots-j.since_shots)+' shots':''}</span><label style="flex:0 0 130px">Remind after<input type="number" min="0" max="5000" step="10" value="${j.every_shots||''}" placeholder="never" onchange="act2('/api/maintenance?job=${j.id}&action=every&every='+(this.value||0))"></label><button onclick="act2('/api/maintenance?job=${j.id}&action=done')">Done today</button></div>`;
$('mt_rows').innerHTML=m.jobs.filter(j=>j.id!='grinder_clean').map(row).join('');
MT={m,row};drawGrinderUpkeep()}
// The grinder's own upkeep: cleaning, and zeroing (which starts a new calibration, see saveGrinder).
// Drawn again with the charts, once the shots and the grinder are known.
let MT=null;function drawGrinderUpkeep(){if(!MT||!$('gu_rows'))return;const m=MT.m,row=MT.row;
const gc=m.jobs.find(j=>j.id=='grinder_clean'),g=L&&L.grinder,cal=g?g.calibration:0,since0=SH.filter(s=>(s.grinder_calibration||0)==cal).length;
$('gu_total').textContent=m.shots_total+' shots counted';
$('gu_rows').innerHTML=(gc?row(gc):'')+`<div class="row" style="align-items:center"><b style="flex:1 1 150px">Zero the grinder</b><span style="flex:1 1 200px">${since0} shots since${g&&g.zeroDate?', last zeroed '+fd(g.zeroDate):', never recorded'}</span><span style="flex:0 0 130px"></span><button onclick="saveGrinder(true).then(()=>setTimeout(loadUpkeep,1500))">Zeroed today</button></div>`}
function act2(u){post(u).then(r=>{done(r,'');loadUpkeep()})}
async function forget(role,what){if(await ask('Forget '+what+'? It will have to be paired again before it is used.'))pair(role,'',0)}
async function loadDevices(){let d;try{d=await get('/api/devices')}catch(e){return}
// A paired device's signal as last heard advertising (a number only: a bot sits on the machine's
// face to reach its button, so there is no placement advice to give).
const sigOf=(d,addr)=>{if(!addr)return'';const h=(d.seen||[]).find(x=>(x.address||'').toLowerCase()==addr.toLowerCase());if(!h)return'';
return ` <span class="dim">signal ${h.rssi} dBm</span>`};
$('dev').innerHTML=`<b>Scale</b>: ${d.scale_connected?esc(d.scale_name)+' ('+esc(d.scale_protocol)+'), connected':'not connected'} <span class="dim">[${esc(d.pairing.scale||'adopts the first supported scale found')}]</span>${sigOf(d,d.pairing.scale)}<br><b>Brew bot</b>${d.bot_label?' ('+esc(d.bot_label)+')':''}: ${d.bot_connected?'connected':(d.pairing.bot?'asleep until Start':'not paired')} <span class="dim">[${esc(d.pairing.bot||'pick one below')}]</span>${sigOf(d,d.pairing.bot)}`+(d.bot_firmware&&d.bot_firmware!='0.0'?`, firmware ${esc(d.bot_firmware)}`:'')+(d.bot_battery!=null?`, battery ${d.bot_battery}%`:'')+(d.bot_problem?` <b style="color:var(--warn)">${esc(d.bot_problem)}</b>`:'')+(d.bot_caps&&d.bot_caps.length&&!d.bot_caps.includes('down_up')?' <span class="dim">This pusher cannot hold the button: held pre-infusion and bloom restarts become plain presses.</span>':'')+`<br><b>Power bot</b>${d.power_label?' ('+esc(d.power_label)+')':''}: ${d.power_connected?'connected':(d.pairing.power?'asleep until needed':'not paired')} <span class="dim">[${esc(d.pairing.power||'optional: pick one below')}]</span>${sigOf(d,d.pairing.power)}`+(d.power_battery!=null?`, battery ${d.power_battery}%`:'')+(d.power_problem?` <b style="color:var(--warn)">${esc(d.power_problem)}</b>`:'')+(d.pairing.stop||d.stop_connected?`<br><b>Stop bot</b>${d.stop_label?' ('+esc(d.stop_label)+')':''}: ${d.stop_connected?'connected':'asleep until Start'} <span class="dim">[${esc(d.pairing.stop)}]</span>${sigOf(d,d.pairing.stop)}`+(d.stop_battery!=null?`, battery ${d.stop_battery}%`:'')+(d.stop_problem?` <b style="color:var(--warn)">${esc(d.stop_problem)}</b>`:''):'');
const q=d.scale_data,caps=d.scale_caps||[],no=['timer_control','timer','battery','buttons'].filter(c=>!caps.includes(c)).map(c=>({timer_control:'timer control',timer:'timer readings',battery:'battery level',buttons:'button presses'})[c]);$('scaledata').textContent=d.scale_connected&&q?`Scale link: ${q.weight_frames} readings since it connected`+(caps.includes('timer')?`, ${q.timer_frames} with the scale's timer`:'')+`, ${q.split_frames} frames arrived in two parts, ${q.bad_frames} failed their checksum`+(q.battery!=null?`, scale battery ${q.battery}%`:'')+'.'+(q.zeroes_itself?' <b style="color:var(--warn)">The scale tares itself (an auto-tare mode). A cup taken off can then go unseen, so a shot could start with no cup: set the scale to a mode without auto-tare.</b>':'')+(no.length?` This scale does not offer: ${no.join(', ')}.`:''):'';
const mm=d.memory;if(mm){const low=mm.internal_lowest<40000;$('scaledata').innerHTML=esc($('scaledata').textContent)+'<br>Memory inside the chip: '+Math.round(mm.internal_free/1024)+' kB free now, '+(low?'<b style="color:var(--warn)">':'')+Math.round(mm.internal_lowest/1024)+' kB at its lowest since start-up'+(low?'</b> (tight: below 40 kB)':'')+', largest single piece '+Math.round(mm.internal_largest/1024)+' kB. '+(mm.psram_total?'PSRAM: '+Math.round(mm.psram_free/1024)+' of '+Math.round(mm.psram_total/1024)+' kB free.':'<b style="color:var(--warn)">No PSRAM found: the large buffers are inside the chip.</b>')}
$('seen').innerHTML='<tr><th class="l">Type</th><th class="l">Name</th><th class="l">Address</th><th>Signal</th><th></th></tr>'+d.seen.map(s=>`<tr><td class="l">${s.kind}</td><td class="l">${esc(s.name)}</td><td class="l">${esc(s.address)}</td><td>${s.rssi}</td><td>${roleBtn(d,s,s.kind=='bot'?'bot':'scale',s.kind=='bot'?'Brew':'Use')}${s.kind=='bot'?roleBtn(d,s,'power','Power')+roleBtn(d,s,'stop','Stop'):''}</td></tr>`).join('');loadPower();loadPusher();loadMachine()}
let MC={builtins:[],lib:[],sel:null};
async function loadMachine(){let m;try{m=await get('/api/machine')}catch(e){return}MC.builtins=m.builtins;try{MC.lib=(await get('/api/community/machines')).machines||[]}catch(e){MC.lib=[]}
const c=m.current;$('mc').innerHTML='In use: <b>'+esc(c.maker+' '+c.model)+'</b>, '+esc(c.control)+' <span class="dim">('+(c.builtin?'comes with the firmware':({local:'yours, on this controller only',queued:'yours, waiting to be sent',sent:'yours, sent to the library',review:'yours, with a reviewer',published:'in the shared library as '+esc(c.library_id||''),declined:'not accepted by the library',refused:'refused by the library\'s checks'})[c.share_state]||'yours')+')</span>'+((c.share_state=='declined'||c.share_state=='refused')&&c.review_note?' <b style="color:var(--warn)">Reviewer: '+esc(c.review_note)+'</b>':'')+(!c.builtin&&c.changed_since_shared?' <span class="dim">Changed since it was sent: send it again to update the entry.</span>':'');
$('mc_send').style.display=c.can_share&&S&&S.shareEnabled?'':'none';$('mc_send').textContent=c.share_state=='refused'?'Send again':c.share_state&&c.share_state!='local'?'Send the new version':'Send to the shared library';
// Only machines with a profile of their own: the one in use, the one the firmware was built on, and the shared library's.
const b0=MC.builtins[0],nm=x=>esc(x.maker+' '+x.model+' - '+x.control);
$('mc_pick').innerHTML=`<option value="">${nm(c)} (in use${c.builtin?'':', yours'})</option>`+(c.builtin==1||!b0?'':`<option value="b0">${nm(b0)} (as it comes with the firmware)</option>`)+MC.lib.map((b,i)=>c.library_id&&c.library_id==b.id?'':`<option value="l${i}">${nm(b)} (shared library)</option>`).join('')+'<option value="new">Another machine: type it below</option>';fillMachine(c)}
function fillMachine(c){$('mc_maker').value=c.maker||'';$('mc_model').value=c.model||'';$('mc_control').value=c.control||'';$('mc_style').value=c.style||'toggle_button';$('mc_start').value=c.manual_start||'tap';$('mc_hold').value=+!!c.hold_preinfusion;$('mc_tap').value=+!!c.tap_preinfusion;$('mc_pow').value=+!!c.power_toggle;$('mc_self').value=c.self_stop_s||0;$('mc_aoff').value=c.auto_off_min!=null?c.auto_off_min:0;MC.sel=c}
function pickMachine(){const v=$('mc_pick').value;if(!v){loadMachine();return}if(v=='new'){fillMachine({style:$('mc_style').value,manual_start:$('mc_start').value});$('mc_maker').focus();return}const i=+v.slice(1);fillMachine(v[0]=='b'?Object.assign({},MC.builtins[i]):Object.assign({},MC.lib[i],{library_id:MC.lib[i].id,builtin:0}))}
function saveMachine(){const s=MC.sel||{},body={maker:$('mc_maker').value.trim(),model:$('mc_model').value.trim(),control:$('mc_control').value.trim(),style:$('mc_style').value,manual_start:$('mc_start').value,hold_preinfusion:$('mc_hold').value=='1',tap_preinfusion:$('mc_tap').value=='1',power_toggle:$('mc_pow').value=='1',self_stop_s:+$('mc_self').value||0,auto_off_min:Math.max(0,Math.min(1440,+$('mc_aoff').value||0))};
const same=['maker','model','control','style'].every(k=>(s[k]||'')==body[k])&&(s.manual_start||'tap')==body.manual_start&&!!s.hold_preinfusion==body.hold_preinfusion&&!!s.tap_preinfusion==body.tap_preinfusion&&!!s.power_toggle==body.power_toggle&&(s.self_stop_s||0)==body.self_stop_s;
const send=same&&s.builtin?{builtin:s.builtin,auto_off_min:body.auto_off_min}:same&&s.library_id?Object.assign(body,{library_id:s.library_id}):body;
post('/api/machine',send).then(r=>{done(r,'Machine saved');loadMachine()})}
function shareMachine(){post('/api/share/machine').then(r=>{done(r,'Queued for the shared library');loadMachine()})}
const DAYS=['Su','Mo','Tu','We','Th','Fr','Sa'];
// A role button: blue while this device holds that role.
function roleBtn(d,s,role,label){const on=(d.pairing[role]||'').toLowerCase()==String(s.address).toLowerCase();return `<button class="${on?'as':''}" title="${on?'Assigned':'Assign'}" onclick="pair('${role}','${esc(s.address)}',${s.type})">${on?'\u2713 ':''}${label}</button>`}
// Home: the machine's power button, shown when a power bot is paired. It is
// a plain button: one press of the machine's power button, no state shown.
async function loadHomePower(){let p;try{p=await get('/api/power')}catch(e){return}$('hpower').style.display=p&&p.paired?'':'none';showMachineState(p)}
// The machine's power as the controller believes it: set by each power bot
// press, corrected here by hand when it has drifted (nothing is pressed).
let MSTATE='unknown';
function showMachineState(p){const b=$('hmstate');if(!p||!p.paired){b.style.display='none';return}
b.style.display='';MSTATE=p.belief||'unknown';
b.textContent=MSTATE==='on'?'Espresso machine on':MSTATE==='off'?'Espresso machine off':'Espresso machine: on or off?';
b.className='pill mstate'+(MSTATE==='on'?' ok':MSTATE==='off'?'':' warn')}
function toggleMachineState(){const b=$('hmstate');if(b.disabled)return;const next=MSTATE==='on'?'off':'on';
b.classList.add('hit');b.disabled=true;
post('/api/power',{action:next==='on'?'believe_on':'believe_off'}).then(r=>{b.classList.remove('hit');b.disabled=false;
if(!r.ok)return say(r.error||'Not changed');showMachineState({paired:true,belief:next});say(next==='on'?'Marked as on (nothing pressed)':'Marked as off (nothing pressed)')})}
function homePower(){const b=$('hpower');if(b.disabled)return;b.classList.add('hit','busy');b.disabled=true;
const end=t=>{b.classList.remove('hit','busy');b.disabled=false;if(t)say(t)};
post('/api/power',{action:'press'}).then(async r=>{if(!r.ok)return end(r.error||'Not pressed');say('Waking the power bot...');
// The press happens once the bot has woken and connected (several seconds, longer if it is far away): follow it through.
for(let i=0;i<140;i++){await new Promise(z=>setTimeout(z,1500));let p;try{p=await get('/api/power')}catch(e){continue}if(!p.working){showMachineState(p);return end(p.last?'Power bot: '+p.last:'Power button pressed')}}
end('Still trying to reach the power bot')})}
setInterval(()=>{if($('p-home').classList.contains('on'))loadHomePower()},5000);
async function loadPower(){let p;try{p=await get('/api/power')}catch(e){return}{const o=$('powerOffWithScale');if(o){o.disabled=!p.paired;o.parentElement.title=p.paired?'':'Needs a power bot'}}
{const st=$('pwstate');st.textContent=p.belief=='on'?'On':p.belief=='off'?'Off':'Not known';st.className='pill'+(p.belief=='on'?' ok':p.belief=='off'?'':' warn')}
$('pw').innerHTML=[p.paired?'':'<b>No power bot paired.</b> Pick one marked "Power" in <a href="#setup">Setup › Devices</a>.',p.belief=='unknown'?'Say whether it is on or off below.':'',p.working?'Pressing the power button…':'',p.last?'Last: '+esc(p.last)+(p.last_by_schedule?' (schedule)':''):'',p.clock_set?'':'The clock is not set: the schedule waits for it.'].filter(Boolean).join(' · ');
const shown=Math.max(1,p.schedule.reduce((n,e,i)=>e.enabled?i+1:n,0));
$('pws').innerHTML='<tr><th>Use</th><th>Switch</th><th>At</th><th class="l">Days</th></tr>'+p.schedule.map((e,i)=>`<tr ${i>=shown?'style="display:none"':''}><td><input type="checkbox" id="pe${i}" aria-label="Schedule entry ${i+1} in use" ${e.enabled?'checked':''}></td><td><select id="po${i}" aria-label="Schedule entry ${i+1}: on or off"><option value="1" ${e.on?'selected':''}>On</option><option value="0" ${e.on?'':'selected'}>Off</option></select></td><td><input type="time" id="pt${i}" aria-label="Schedule entry ${i+1}: time" value="${String(Math.floor(e.minute/60)).padStart(2,'0')}:${String(e.minute%60).padStart(2,'0')}"></td><td class="l"><span class="days">`+DAYS.map((d,k)=>`<label title="${d}"><input type="checkbox" id="pd${i}_${k}" ${e.days>>k&1?'checked':''}><span>${d.slice(0,2)}</span></label>`).join('')+'</span></td></tr>').join('');
$('pwadd').style.display=shown<p.schedule.length?'':'none'}
function addPowerTime(){const rows=[...$('pws').querySelectorAll('tr')].slice(1),next=rows.find(r=>r.style.display=='none');if(next){next.style.display='';const c=next.querySelector('input[type=checkbox]');if(c)c.checked=true}if(!rows.some(r=>r.style.display=='none'))$('pwadd').style.display='none'}
function power(action){post('/api/power',{action}).then(r=>{done(r,action.startsWith('believe')?'Noted':'Power button: on its way');setTimeout(loadPower,600);setTimeout(loadPower,6000)})}
function savePowerSchedule(){const schedule=[];for(let i=0;$('pe'+i);i++){const t=$('pt'+i).value.split(':');let days=0;for(let k=0;k<7;k++)if($('pd'+i+'_'+k).checked)days|=1<<k;schedule.push({enabled:$('pe'+i).checked,on:$('po'+i).value=='1',minute:(+t[0]||0)*60+(+t[1]||0),days})}
post('/api/power',{action:'schedule',schedule,utc_offset_min:-new Date().getTimezoneOffset()}).then(r=>{done(r,'Schedule saved');loadPower()})}
async function loadPusher(){const role=$('pk_role').value;let k;try{k=await get('/api/pusher?role='+role)}catch(e){return}
$('pk_uuid').value=k.tuya_uuid||'';$('pk_dev').value=k.tuya_device_id||'';$('pk_key').value='';$('pk_tok').value='';$('pk_pw').value='';$('pk_var').value=k.tuya_variant||0;$('pk_pwl').style.display=role!='brew'?'':'none';
$('pk_state').textContent='Stored: Tuya local key '+(k.tuya_local_key_set?'yes':'no')+', MicroBot token '+(k.microbot_token_set?'yes':'no')+(role!='brew'?', SwitchBot password '+(k.password_set?'yes':'no'):'')+'. Empty key fields keep what is stored.'}
function savePusher(){post('/api/pusher',{role:$('pk_role').value,tuya_uuid:$('pk_uuid').value.trim(),tuya_device_id:$('pk_dev').value.trim(),tuya_local_key:$('pk_key').value,tuya_variant:+$('pk_var').value,microbot_token:$('pk_tok').value.trim(),password:$('pk_pw').value}).then(r=>{done(r,'Keys saved');loadPusher()})}
function pair(kind,address,type){post('/api/pair',{kind,address,type}).then(()=>setTimeout(loadDevices,800))}

post('/api/time?epoch='+Math.floor(Date.now()/1000));show();
/*BC-HANDOFF*/
const b64url=u8=>{let s='';for(let i=0;i<u8.length;i+=0x8000)s+=String.fromCharCode.apply(null,u8.subarray(i,i+0x8000));return btoa(s).replace(/\+/g,'-').replace(/\//g,'_').replace(/=+$/,'')};
const dateIso=n=>n?String(n).padStart(8,'0').replace(/(\d{4})(\d\d)(\d\d)/,'$1-$2-$3'):'';
// A finished shot as Beanconqueror's brew hand-off wants it (docs/brew-import-api.md in that project):
// shot: one row of /api/shots; data: /api/shot/data (time_s, weight_g); c: the coffee; grinder: the one in use.
function bcEnvelope(shot,data,c,grinder,nowIso){
const date=shot.timestamp||nowIso;const dose=shot.dose_g>0?shot.dose_g:undefined,out=shot.final_weight_g;
const note=[shot.taste?'Tasted '+shot.taste+'.':'',shot.tds_pct?'TDS '+shot.tds_pct+' %'+(shot.extraction_pct?', extraction '+shot.extraction_pct+' %':'')+'.':'',shot.end_reason?'Ended by '+String(shot.end_reason).replace(/_/g,' ')+'.':''].filter(x=>x).join(' ');
const brew={date,waterIn:{value:out,unit:'ml'},beverageOut:{value:out,unit:'g'},brewTime:shot.shot_time_s,preparationMethod:'Espresso'};
if(dose)brew.doseIn={value:dose,unit:'g'};
if(shot.first_drop_s>0)brew.firstDripTime=shot.first_drop_s;
if(shot.grind_text)brew.grindSize=shot.grind_text;
if(grinder&&grinder.model)brew.grinderName=grinder.model;
if(shot.rating>=1&&shot.rating<=5)brew.rating=shot.rating;
if(note)brew.note=note;
const env={v:1,app:{name:'Brew by Weight controller',version:'1'},brew,imported:{source:'bbw-controller',sourceName:'Brew by Weight controller',schema:1,params:{shotId:shot.id,endReason:shot.end_reason||'',controllerTargetG:shot.target_weight_g||0}}};
if(c&&c.name){const bean={name:c.name};if(c.roaster)bean.roaster=c.roaster;if(c.origin)bean.origin=c.origin;if(c.region)bean.region=c.region;if(c.producer)bean.farmer=c.producer;if(c.process)bean.process=c.process;if(c.varietal)bean.variety=c.varietal;if(c.altitudeM)bean.elevation=String(c.altitudeM);if(c.notes)bean.aromatics=c.notes;if(c.roastDate)bean.roastingDate=dateIso(c.roastDate);env.bean=bean}
if(data&&data.time_s&&data.weight_g&&data.time_s.length>1){let ts=data.time_s,ws=data.weight_g,fidelity='full';
if(ts.length>10000){const k=Math.ceil(ts.length/10000);ts=ts.filter((x,i)=>i%k==0);ws=ws.filter((x,i)=>i%k==0);fidelity='downsampled'}
const t=[],w=[];let pt=0,pw=0;for(let i=0;i<ts.length;i++){t.push(Math.max(0,Math.round((ts[i]-pt)*1000)));pt=ts[i];w.push(+((ws[i]-pw)*10).toFixed(2));pw=ws[i]}
env.flow={fidelity,t,waterDispensed:w.map(()=>0),weight:w}}
return env}
// Beanconqueror's link: JSON, gzip, unpadded base64url, in query chunks of at most 400 characters.
async function bcLink(env){if(typeof CompressionStream==='undefined')throw new Error('This browser cannot compress the shot for Beanconqueror. Use a recent Chrome or Safari.');
const bytes=new Uint8Array(await new Response(new Blob([JSON.stringify(env)]).stream().pipeThrough(new CompressionStream('gzip'))).arrayBuffer());
const p=b64url(bytes),chunks=p.match(/.{1,400}/g)||[];return 'beanconqueror://ADD_BREW?len='+p.length+'&'+chunks.map((x,i)=>'shareBrew'+i+'='+x).join('&')}
async function toBeanconqueror(id){const s=SH.find(x=>x.id==id);if(!s)return;let data=null;
if(s.has_data){try{data=await get('/api/shot/data?id='+id)}catch(e){}}
let now='';if(!s.timestamp){if(!await ask('This shot has no date (the controller’s clock was not set). Send it to Beanconqueror dated now?'))return;now=new Date().toISOString()}
const c=L.coffees.find(x=>x.id==s.coffee_id)||(s.coffee?{name:s.coffee,region:s.region,varietal:s.varietal,roastDate:s.roast_date}:null);
const g=Object.assign({},L.grinder),sh=Object.assign({},s,{grind_text:s.grind_um?fsn(s.grind_um):''});
let link;try{link=await bcLink(bcEnvelope(sh,data,c,g,now))}catch(e){return say(e.message)}
say('Opening Beanconqueror... if nothing happens, install it and open it once first.');location.href=link}
/*END-BC-HANDOFF*/
/*BC-IMPORT*/
const BCROAST={CINNAMON_ROAST:1,NEW_ENGLAND_ROAST:1,HALF_CITY_ROAST:1,MODERATE_LIGHT_ROAST:2,AMERICAN_ROAST:2,CITY_ROAST:3,CITY_PLUS_ROAST:3,FULL_CITY_ROAST:4,FULL_CITY_PLUS_ROAST:4,VIEANNA_ROAST:5,VIENNA_ROAST:5,ITALIAN_ROAST:5,FRENCH_ROAST:5};
const isoToYmd=s=>{const m=/^(\d{4})-(\d\d)-(\d\d)/.exec(s||'');return m?+(m[1]+m[2]+m[3]):0};
const clip=(s,n)=>String(s==null?'':s).replace(/["\r\n]+/g,' ').trim().slice(0,n);
// One bean of a Beanconqueror export (BEANS in its backup file) as a coffee and its bag. The roast level
// mapping (14 values to 5) is a judgement, not a standard.
function bcBeanToCoffee(b){const i=(b.bean_information&&b.bean_information[0])||{};const alt=(/\d[\d.,]*/.exec(String(i.elevation||''))||[''])[0].replace(/[.,]/g,'');const altM=+alt>=1&&+alt<=4000?+alt:0;
const frozen=isoToYmd(b.frozenDate),thaw=isoToYmd(b.unfrozenDate);
const coffee={name:clip(b.name,23),roaster:clip(b.roaster,23),origin:clip(i.country,23),region:clip(i.region,31),producer:clip(i.farmer||i.farm,31),varietal:clip(i.variety,31),process:clip(i.processing,15),notes:clip(b.aromatics,47),altitudeM:altM,roastLevel:BCROAST[b.roast]||0,roastDate:isoToYmd(b.roastingDate)};
const bag={roastDate:coffee.roastDate,openedDate:isoToYmd(b.openDate),frozenDate:frozen,thawDate:thaw,weightG:b.weight>0&&b.weight<65535?Math.round(b.weight):0};
return{coffee,bag,finished:!!b.finished,uuid:(b.config&&b.config.uuid)||''}}
function bcParseExport(json,withFinished){const list=Array.isArray(json.BEANS)?json.BEANS:(json.bean?[json.bean]:[]);const out=[],skipped=[];
for(const b of list){const r=bcBeanToCoffee(b);if(!r.coffee.name){skipped.push('a bean with no name');continue}if(r.finished&&!withFinished){skipped.push(r.coffee.name+' (finished)');continue}out.push(r)}return{beans:out,skipped,total:list.length}}
async function importBeanconqueror(input){const f=input.files&&input.files[0];if(!f)return;let json;try{json=JSON.parse(await f.text())}catch(e){return say('That is not a Beanconqueror backup file (it is not JSON)',false)}
const r=bcParseExport(json,$('bcfin').checked);if(!r.beans.length)return say('No beans found in that file'+(r.skipped.length?' ('+r.skipped.length+' skipped)':''));
const have=new Set(L.coffees.map(c=>(c.name+'|'+c.roaster).toLowerCase()));let added=0,dup=0,full=0;
for(const x of r.beans){const key=(x.coffee.name+'|'+x.coffee.roaster).toLowerCase();if(have.has(key)){dup++;continue}if(L.coffees.length+added>=L.maxCoffees){full++;continue}
const res=await post('/api/coffee',x.coffee);if(!res.ok&&!res.id){continue}added++;have.add(key);
if(x.bag.openedDate||x.bag.weightG||x.bag.frozenDate){const lib=await get('/api/library');const cc=lib.coffees.find(q=>q.id==res.id);if(cc&&cc.bagId)await post('/api/bag',Object.assign({id:cc.bagId},x.bag))}}
await loadLibrary();say('From Beanconqueror: '+added+' coffee'+(added==1?'':'s')+' added'+(dup?', '+dup+' already on the shelf':'')+(full?', '+full+' left out: the shelf is full':'')+(r.skipped.length?', '+r.skipped.length+' skipped':'')+'. Shots are not imported.');input.value=''}
/*END-BC-IMPORT*/
/*VZ-LOOKUP*/
// visualizer.coffee/api/canonical_coffee_bags, as its OpenAPI describes it: no sign-in, CORS open. It needs
// the phone to be online, so it works with the controller on a home network, not on its own access point.
const VZROAST=s=>{s=String(s||'').toLowerCase();return /medium.?dark|full city|vienna|dark|french|italian/.test(s)?(/medium.?dark|full city/.test(s)?4:5):/medium.?light|light.?medium/.test(s)?2:/light|cinnamon|nordic/.test(s)?1:/medium|city/.test(s)?3:0};
function vzToCoffee(b){const alt=(/\d[\d.,]*/.exec(String(b.elevation||''))||[''])[0].replace(/[.,]/g,'');const altM=+alt>=1&&+alt<=4000?+alt:0;
return{name:clip(b.name,23),roaster:clip(b.canonical_roaster_name,23),origin:clip(b.country,23),region:clip(b.region,31),producer:clip(b.farmer,31),varietal:clip(b.variety,31),process:clip(b.processing,15),notes:clip(b.tasting_notes,47),altitudeM:altM,roastLevel:VZROAST(b.roast_level)}}
async function vzSearch(q){const r=await fetch('https://visualizer.coffee/api/canonical_coffee_bags?items=8&q='+encodeURIComponent(q),{headers:{Accept:'application/json'}});if(!r.ok)throw new Error('Visualizer answered '+r.status);return(await r.json()).data||[]}
let VZ=[];
async function vzLookup(){const q=$('vzq').value.trim();if(q.length<3)return say('Type at least three letters of the coffee or roaster',false);$('vzres').textContent='Searching...';
try{VZ=await vzSearch(q)}catch(e){VZ=[];$('vzres').textContent='Could not reach visualizer.coffee ('+e.message+'). This needs the phone to be online: connect the controller to your home Wi-Fi, or use a phone with data.';return}
$('vzres').innerHTML=VZ.length?VZ.map((b,i)=>`<div class="row" style="align-items:center"><span style="flex:1 1 240px"><b>${esc(b.name)}</b> <span class="dim">${esc([b.canonical_roaster_name,b.country,b.processing].filter(x=>x).join(', '))}</span></span><button onclick="vzUse(${i})">Fill the form</button></div>`).join(''):'Nothing found.'}
function vzUse(i){const c=vzToCoffee(VZ[i]);C=0;drawCoffees();CF.forEach(k=>{if(k in c)$('c_'+k).value=c[k]||''});$('c_roastDate').value='';say('Filled in from visualizer.coffee. Check it, add the roast date from your bag, then Save.')}
/*END-VZ-LOOKUP*/
loadUpkeep();loadSettings().then(loadLibrary).then(loadShots).then(loadCommunityCoffees).then(loadCommunityGrinders);setInterval(tick,500);tick();

// ---- polish: finding things, and knowing a press was felt ----------------
(function(){
const LEAD={home:'What is being brewed, how the last shot went, and what to change next.',
shots:'Every shot pulled. Open one for its curve, its taste, or to leave it out of grind advice.',
coffees:'Your coffees and their bags, and which recipes you brew each one with.',
recipes:'Dose, yield, time, pre-infusion and bloom. One recipe can serve every coffee.',
grinder:'Your grinder, where it is set now, and which way to turn it.',
insights:'Your taste, and patterns across your shots.',
discover:'What to try next, picked by how the coffees you rated tasted to you.',
community:'What other owners share through the community library, and what you share with it.',
machine:'Power, schedule and upkeep for your machine.',
setup:'Scale, pushers, network, firmware and sharing: mostly set once.',
logs:'What the controller recorded, for looking into problems.'};
// Which cards start folded: the ones set once, or looked at rarely.
const FOLD={machine:i=>i==2,setup:i=>true,grinder:i=>i>=4,logs:i=>false,coffees:i=>i==2,recipes:i=>false};
const OPEN={setup:[0]};
const mem=(k,v)=>{try{if(v===undefined)return localStorage.getItem(k);localStorage.setItem(k,v)}catch(e){return null}};
const title=c=>{const h=c.firstElementChild&&(c.firstElementChild.matches('h2')?c.firstElementChild:c.firstElementChild.querySelector('h2'));return h&&!h.id?h.textContent.trim():''};
document.querySelectorAll('.page').forEach(page=>{const name=page.id.slice(2),head=page.querySelector('.head');
if(head&&LEAD[name]){const p=document.createElement('p');p.className='lead';p.textContent=LEAD[name];head.after(p)}
const cards=[...page.querySelectorAll('.card')].filter(c=>title(c));
if(FOLD[name])cards.forEach((c,i)=>{if(!FOLD[name](i))return;c.classList.add('fold');const key='fold:'+name+':'+title(c),was=mem(key);
const closed=was===null?!((OPEN[name]||[]).includes(i)):was==='1';c.classList.toggle('closed',closed);
const first=c.firstElementChild;first.setAttribute('role','button');first.setAttribute('tabindex','0');first.setAttribute('aria-expanded',String(!closed));
const flip=()=>{const now=c.classList.toggle('closed');mem(key,now?'1':'0');first.setAttribute('aria-expanded',String(!now));sync()};
first.addEventListener('click',e=>{if(!e.target.closest('a,button,input,select,label'))flip()});
first.addEventListener('keydown',e=>{if((e.key=='Enter'||e.key==' ')&&e.target==first){e.preventDefault();flip()}})});
// "On this page": a chip for each card, on pages with enough of them to get lost in.
let jump=null;if(cards.length>=5){const wrap=document.createElement('div'),tb=document.createElement('button');wrap.className='otp';tb.type='button';tb.className='otpb';tb.textContent='On this page';tb.setAttribute('aria-haspopup','true');tb.setAttribute('aria-expanded','false');
jump=document.createElement('div');jump.className='jump';
const shut=()=>{wrap.classList.remove('show');tb.setAttribute('aria-expanded','false')};
tb.onclick=()=>tb.setAttribute('aria-expanded',String(wrap.classList.toggle('show')));
document.addEventListener('click',e=>{if(!wrap.contains(e.target))shut()});document.addEventListener('keydown',e=>{if(e.key=='Escape')shut()});
cards.forEach(c=>{const b=document.createElement('button');b.type='button';b.textContent=title(c);b._card=c;
b.onclick=()=>{shut();if(c.classList.contains('closed')){c.classList.remove('closed');mem('fold:'+name+':'+title(c),'0');c.firstElementChild.setAttribute('aria-expanded','true')}
c.scrollIntoView({behavior:'smooth',block:'start'});c.classList.add('flash');setTimeout(()=>c.classList.remove('flash'),900);sync()};jump.appendChild(b)});
tb.title='On this page';wrap.append(tb,jump);page.prepend(wrap);page.classList.add('hasotp');
if(window.IntersectionObserver)new IntersectionObserver(es=>wrap.classList.toggle('mini',!es[0].isIntersecting)).observe(head)}
function sync(){if(jump)[...jump.children].forEach(b=>b.classList.toggle('open',!b._card.classList.contains('closed')))}sync()});
// The button that was pressed: post() shows it working, then how it went.
document.addEventListener('click',e=>{const b=e.target.closest('button');if(b){lastBtn=b;lastAt=Date.now()}
const link=e.target.closest('.item,nav a,#bnav a,#more a');if(link&&!leaving){const pg=document.querySelector('.page.on'),dirty=pg?[...pg.querySelectorAll('button.dirty')]:[];
const same=link.matches('a')&&pg&&link.getAttribute('href')=='#'+pg.id.slice(2);if(dirty.length&&!same){e.preventDefault();e.stopPropagation();unsavedBar(dirty,link)}}},true);
// Unsaved changes on this page and somewhere else asked for: save them, drop them, or stay.
let leaving=false;function unsavedBar(dirty,link){const bar=$('unsaved'),names=[...new Set(dirty.map(b=>{const c=b.closest('.card'),h=c&&c.querySelector('h2');return h?h.textContent:'this page'}))];
$('unsavedtxt').textContent='Unsaved changes in '+names.join(', ')+'.';bar.classList.add('on');
const go=()=>{bar.classList.remove('on');leaving=true;try{link.click()}finally{leaving=false}};
$('unsavedsave').onclick=()=>{dirty.forEach(b=>b.click());setTimeout(go,900)};
$('unsaveddrop').onclick=()=>{dirty.forEach(b=>b.classList.remove('dirty'));loadSettings().then(loadLibrary);go()};  // forms back to what is stored
$('unsavedstay').onclick=()=>{bar.classList.remove('on');const c=dirty[0].closest('.card');if(c)c.scrollIntoView({behavior:'smooth',block:'start'})}}
$('bmore').onclick=e=>{e.stopPropagation();const o=$('more').classList.toggle('open');$('bmore').setAttribute('aria-expanded',o?'true':'false')};
document.addEventListener('click',e=>{if(!e.target.closest('#more,#bmore'))$('more').classList.remove('open')});
addEventListener('beforeunload',e=>{if(document.querySelector('button.dirty')){e.preventDefault();e.returnValue=''}});
// Typed into a form that has a Save button: show that it is not saved yet.
const mark=e=>{const el=e.target;if(!el.matches||!el.matches('input,select,textarea')||el.type=='search')return;
const auto=(el.getAttribute('onchange')||'')+(el.getAttribute('oninput')||'');if(/act\(|post\(|draw|load|filter/i.test(auto))return;
const card=el.closest('.card'),save=card&&(card.querySelector('[data-save]')||card.querySelector('button.pri'));if(save)save.classList.add('dirty')};
document.addEventListener('change',e=>{if(e.target.matches&&e.target.matches('input,select,textarea')){lastIn=e.target;lastInAt=Date.now()}},true);
document.addEventListener('input',mark);document.addEventListener('change',mark);
// The controller's state, in sight on every page; and plainly when it cannot be reached.
const st=$('state'),ns=$('navstate'),off=$('offline');
const mirror=()=>{ns.textContent=st.textContent;ns.className=st.className;off.style.display=st.textContent=='offline'?'block':'none'};
new MutationObserver(mirror).observe(st,{childList:true,characterData:true,subtree:true,attributes:true});mirror();
})();
</script></body></html>)HTML";
