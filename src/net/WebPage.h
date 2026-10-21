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
<script>try{const t=localStorage.getItem('bbwTheme');if(t&&t!='dark')document.documentElement.dataset.theme=t}catch(e){}</script>
<style>
:root{--bg:#0e0f11;--card:#17191c;--up:#1f2226;--line:#2a2e33;--edge:#3a3f46;--fg:#eceef0;--mid:#c3c9d0;--dim:#9aa3ad;--ok:#4cc38a;--okbg:#14301f;--warn:#f5c451;--warnbg:#33290f;--bad:#ff6b5e;--acc:#f08a24;--accbg:#3a2410;--com:#6cb4ff;--combg:#111a24;--comline:#23374d;--grid:#22262b;--gridlo:#1c1f23;--empty:#15171a;--stripe:#121417;--acctext:#ffd0a3;--comtext:#cfe6ff;--comh:#8cc4ff;--oktext:#7be0aa;--hover:#1a1d21;--hover2:#1b1e22;--badbg:#3b1512;--pairbg:#2a1c0e;--pairline:#6b4118;--delline:#5c2a25;--deltext:#ff8a80;--inhover:#5b6672;--acchover:#ffb061;color-scheme:dark}
:root[data-theme="light"]{--bg:#f4f3f0;--card:#ffffff;--up:#eceae6;--line:#dcd9d3;--edge:#b9b5ad;--fg:#1a1a1a;--mid:#3d4249;--dim:#646b73;--ok:#1e8e4e;--okbg:#dff3e7;--warn:#9a6a00;--warnbg:#fbf0d2;--bad:#c0392b;--acc:#c2560c;--accbg:#fbe6d4;--com:#1d6fc4;--combg:#e6f0fb;--comline:#b9d3ef;--grid:#e3e0da;--gridlo:#ecebe7;--empty:#f0eeea;--stripe:#f7f6f3;--acctext:#8a3a05;--comtext:#0d4a8c;--comh:#1d6fc4;--oktext:#156b3a;--hover:#eceae6;--hover2:#f0eee9;--badbg:#fbe1dd;--pairbg:#fbe6d4;--pairline:#e2a46e;--delline:#e8b4ae;--deltext:#b0302a;--inhover:#9a958c;--acchover:#a64708;color-scheme:light}
:root[data-theme="nord"]{--bg:#2e3440;--card:#3b4252;--up:#434c5e;--line:#4c566a;--edge:#5e6a80;--fg:#eceff4;--mid:#d8dee9;--dim:#a3abb9;--ok:#a3be8c;--okbg:#2f3a2f;--warn:#ebcb8b;--warnbg:#3d3a2e;--bad:#bf616a;--acc:#d08770;--accbg:#4a3a33;--com:#88c0d0;--combg:#2f3b45;--comline:#4b6170;--grid:#434c5e;--gridlo:#3e4656;--empty:#353c4a;--stripe:#323946;--acctext:#f1d3c4;--comtext:#d8eef3;--comh:#88c0d0;--oktext:#c4dcb0;--hover:#434c5e;--hover2:#3f4757;--badbg:#4a2e33;--pairbg:#4a3a33;--pairline:#8a6455;--delline:#7a3f45;--deltext:#e8a0a7;--inhover:#7b8394;--acchover:#e3a58d;color-scheme:dark}
:root[data-theme="solarized"]{--bg:#002b36;--card:#073642;--up:#0d3f4c;--line:#1b4d5a;--edge:#2f6170;--fg:#eee8d5;--mid:#d5d0bd;--dim:#93a1a1;--ok:#859900;--okbg:#1d3a1a;--warn:#b58900;--warnbg:#2f3315;--bad:#dc322f;--acc:#cb4b16;--accbg:#3a2a14;--com:#268bd2;--combg:#08394d;--comline:#1c5a78;--grid:#0f4654;--gridlo:#0a3d4a;--empty:#04313c;--stripe:#03303b;--acctext:#f5c39e;--comtext:#cfe6f7;--comh:#6cb4e6;--oktext:#b5c95e;--hover:#0d3f4c;--hover2:#0a3a46;--badbg:#3a1715;--pairbg:#3a2a14;--pairline:#7a4a1e;--delline:#6b2522;--deltext:#f08a86;--inhover:#4f7380;--acchover:#e2733c;color-scheme:dark}
:root[data-theme="dracula"]{--bg:#21222c;--card:#282a36;--up:#343746;--line:#44475a;--edge:#5a5e78;--fg:#f8f8f2;--mid:#e2e2dc;--dim:#a9adc8;--ok:#50fa7b;--okbg:#1f3a2a;--warn:#f1fa8c;--warnbg:#3a3b25;--bad:#ff5555;--acc:#ffb86c;--accbg:#47392b;--com:#8be9fd;--combg:#253a44;--comline:#3b5d6a;--grid:#343746;--gridlo:#2e303e;--empty:#2a2c38;--stripe:#262833;--acctext:#ffe0bd;--comtext:#d5f6fd;--comh:#8be9fd;--oktext:#9bfbb4;--hover:#343746;--hover2:#30323f;--badbg:#4a2328;--pairbg:#47392b;--pairline:#8a6a45;--delline:#7a3038;--deltext:#ff9a9a;--inhover:#6a6e8a;--acchover:#ffd09c;color-scheme:dark}
:root[data-theme="contrast"]{--bg:#000000;--card:#0a0a0a;--up:#1a1a1a;--line:#5a5a5a;--edge:#9a9a9a;--fg:#ffffff;--mid:#f0f0f0;--dim:#d0d0d0;--ok:#00e676;--okbg:#003a1c;--warn:#ffd600;--warnbg:#3a3100;--bad:#ff3b30;--acc:#ffb000;--accbg:#3d2a00;--com:#4fc3f7;--combg:#00243a;--comline:#2a6a90;--grid:#333333;--gridlo:#262626;--empty:#141414;--stripe:#0d0d0d;--acctext:#ffe2a0;--comtext:#e0f5ff;--comh:#4fc3f7;--oktext:#7dffb5;--hover:#1f1f1f;--hover2:#1a1a1a;--badbg:#3a0d0a;--pairbg:#3d2a00;--pairline:#a07000;--delline:#a03028;--deltext:#ff8a80;--inhover:#bdbdbd;--acchover:#ffc940;color-scheme:dark}
*{box-sizing:border-box}
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
</style></head><body><div class="app">
<nav aria-label="Sections"><b>Brew by Weight</b><span class="pill" id="navstate" title="What the controller is doing">--</span>
<a href="#home">Home</a><a href="#shots">Shots</a><a href="#insights">Insights</a><a href="#coffees">Coffees</a><a href="#recipes">Recipes</a><a href="#grinder">Grinder</a><a href="#machine">Espresso machine</a><a href="#setup">Setup</a><a href="#logs">Logs</a>
<div id="link"></div></nav>
<div id="bnav" role="navigation" aria-label="Sections"><a href="#home">Home</a><a href="#shots">Shots</a><a href="#coffees">Coffees</a><a href="#grinder">Grinder</a><button type="button" id="bmore" aria-expanded="false" aria-controls="more">More</button></div>
<div id="more" role="menu"><a href="#insights">Insights</a><a href="#recipes">Recipes</a><a href="#machine">Espresso machine</a><a href="#setup">Setup</a><a href="#logs">Logs</a></div>
<main>
<div id="ctip" role="tooltip"></div>
<div id="askbox" role="dialog" aria-modal="true" aria-labelledby="asktxt"><div id="asktxt"></div><div class="row"><button id="askyes" class="pri">Yes</button><button id="askno">Cancel</button></div></div>
<div id="unsaved" role="alertdialog" aria-live="polite"><span id="unsavedtxt"></span><button class="pri" id="unsavedsave">Save</button><button id="unsaveddrop">Discard</button><button id="unsavedstay">Stay</button></div>
<div id="offline" role="status">Lost contact with the controller. Trying again...</div>
<div id="alert" role="alert"></div>

<div class="page" id="p-home">
<div class="head"><h1>Home</h1><div class="row"><button id="hpower" class="pwr" style="display:none" onclick="homePower()">Espresso machine power</button><button id="hmstate" class="pill mstate" style="display:none" onclick="toggleMachineState()" title="What the controller believes, from the power bot's last press. Tap to correct it: nothing is pressed.">Espresso machine off</button><span class="pill" id="state">--</span></div></div>
<section class="card"><div class="head">
<div class="live"><div><span class="big" id="w">--</span> <span class="dim">g</span></div><div><span class="big" id="t">--</span> <span class="dim">s</span></div><div><span class="big" id="f">--</span> <span class="dim">g/s</span></div></div>
<div class="row"><span class="mid" id="ihint"></span><button class="go" id="bstart" onclick="cmd('start')">Start</button><button class="stop" id="bstop" style="display:none" onclick="cmd('stop')">Stop</button><button id="btare" onclick="cmd('tare')">Tare</button><button id="back" style="display:none" onclick="cmd('ack')">Done with this shot</button></div></div>
<div class="row sm dim"><span><i class="dot" id="ds"></i>Scale <span id="sbat"></span></span><span><i class="dot" id="db"></i>Brew bot <span id="bat"></span></span><span id="cut"></span><span id="res"></span></div></section>
<div class="cols">
<section class="card"><div class="head"><h2>Brewing now</h2><a class="sm" href="#coffees">Coffees</a></div>
<label>What the controller will brew<select id="psel" onchange="act('preset_select',+this.value)"></select></label>
<div class="row" id="pchips"></div><div class="sm dim" id="pinfo"></div></section>
<div class="hstk"><section class="card tight"><div class="head"><div class="row"><h2>Grinder</h2><span><span class="num" id="hg">--</span> <span class="dim un">&micro;m</span></span><span class="pill" id="hgadv"></span></div><a class="sm" href="#grinder">Details</a></div>
<div class="sm mid"><span id="hgwhy"></span> <span class="dim" id="hgage"></span></div></section>
<section class="card tight"><div class="head"><h2>Previous grinder recommendation</h2><a class="sm" href="#shots">All shots</a></div>
<div id="hprev" class="sm mid"><span class="dim">No shot yet.</span></div></section></div>
</div>
<section class="card"><div class="head"><h2>Most recent shot</h2><a class="sm" href="#shots">All shots</a></div>
<div id="hshot" class="dim">No shot yet.</div><canvas id="hchart" width="900" height="200"></canvas>
<div class="row sm dim" id="hrate"></div></section>
<section class="card com"><div class="head"><h2>Community</h2><a class="sm" href="#coffees" style="color:var(--com)">Coffees</a></div><div id="hcom" class="sm mid"></div></section>
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
<div class="row"><button id="scmp" onclick="cmpToggle(SEL)">Add to comparison</button><span class="sm dim">For grind advice</span><span class="row" id="svote"></span></div></div>
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
<details class="note"><summary>A new bag keeps the coffee, its recipes and its grind setting, and starts a fresh run of shots for grind advice.</summary><ul><li>Earlier bags are kept below: put one back in use if you go back to it.</li><li>The dates above are those of the bag in use; correcting them corrects that bag.</li><li>Days in the freezer are not counted as age: enter the day the bag went in and the day it came out.</li></ul></details>
<div class="row" id="cshrow" style="border-top:1px solid var(--line);padding-top:12px"><span class="pill" id="cshpill" style="background:var(--combg);color:#8cc4ff;border:1px solid var(--comline)"></span><span class="sm mid" id="cshare" style="flex:1 1 260px"></span><button id="csend" onclick="sendCoffee()">Send to the shared library</button></div></section>
<section class="card" id="cpairs"><div class="head"><h2>Brew it with</h2><a class="sm" href="#recipes">Recipes</a></div>
<div class="list" id="cplist"></div><div class="sm dim" id="cpinfo"></div></section>
<section class="card"><h2>Add coffees from elsewhere</h2><div class="row"><label style="flex:1 1 240px">Look up a coffee on visualizer.coffee<input id="vzq" placeholder="coffee or roaster" onkeydown="if(event.key==='Enter')vzLookup()"></label><button style="align-self:flex-end" onclick="vzLookup()">Search</button></div><div id="vzres" class="sm mid"></div><details class="note"><summary>Fills the form above with what that shared database knows about the coffee.</summary><ul><li>You add the roast date and your own notes, then Save.</li><li>Needs a phone that is online.</li><li>The data is community-contributed and has its own terms.</li></ul></details><div class="row" style="margin-top:12px"><label style="flex:1 1 240px">Bring in coffees from a Beanconqueror backup<input type="file" accept=".json,application/json" onchange="importBeanconqueror(this)"></label><label class="chk" style="align-self:flex-end"><input type="checkbox" id="bcfin">include finished beans</label></div><details class="note"><summary>Adds the beans in a Beanconqueror export file that you do not have yet.</summary><ul><li>They come with their roast dates, opening dates, weights and any time in the freezer.</li><li>The file stays in your browser.</li><li>Shots are not imported: Beanconqueror does not know this machine's timing, so they would only confuse the grind advice.</li></ul></details></section>
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
<div class="row"><button class="pri" data-save onclick="saveRecipe()">Save</button><span class="sm dim" id="rinfo"></span></div></section>
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
<section class="card"><div class="head"><h2>Grind setting, shot by shot</h2><span class="sm dim" id="cnote"></span></div><canvas id="cchart" width="900" height="220" style="height:220px"></canvas><div class="row sm" id="cleg"></div><details class="note"><summary>The setting each shot was pulled at, oldest on the left: how each coffee was dialled in, and how it drifted as the beans aged.</summary><ul><li>A line per coffee, in its colour (named below the chart); the one being brewed now is orange.</li><li>Dots: green on time, yellow fast, red slow, each against that shot's own target and the tolerance.</li><li>Dashed lines: a new bag of that coffee. Dates along the bottom mark the first shot of each day.</li><li>White rings: shots you rated 4 or 5. Hover or tap a dot to see the shot; click it to open it.</li></ul></details></section>
<section class="card"><h2>How advice is worked out</h2><div class="grid">
<label>Brew time tolerance (&plusmn; s)<input id="grindToleranceS" type="number" step="0.5" min="0.5" max="10"></label>
<label>Shots averaged<input id="grindWindow" type="number" min="1" max="10"></label>
<label>Ratings<select id="grindRatedTarget"><option value="1">Refine the advice towards the shots I rated 4 or 5 stars, inside its tolerance</option><option value="0">Off: time advice only</option></select></label>
<label><span>Starting guess (<span class="un">&micro;m</span> per s)</span><input id="grindUmPerS" type="number" step="0.5" min="0.5" max="20"></label>
<label><span>Largest single change (<span class="un">&micro;m</span>)</span><input id="grindMaxStepUm" type="number" min="1" max="100"></label>
<label>Brew time measured<select id="grindTimeBasis"><option value="0">Total: start press to stop (with pre-infusion)</option><option value="1">Pour: first drops to stop</option></select></label></div>
<div class="row"><button class="pri" data-save onclick="saveSettings(this.closest('.card'))">Save</button><span class="sm dim">Changing how brew time is measured starts a fresh comparison: set each recipe's target time to match.</span></div></section>
<section class="card"><div class="head"><h2>Grinder upkeep</h2><span class="sm dim" id="gu_total"></span></div><div id="gu_rows"></div><details class="note"><summary>Cleaning is counted in shots, like the machine's upkeep. Zeroing starts a fresh comparison of settings.</summary><ul><li>After zeroing the grinder again (or changing burrs), settings from before are no longer compared with new ones: grind advice starts fresh for every coffee.</li><li>The date can be corrected under My grinder.</li></ul></details></section>
<section class="card"><h2>My grinder</h2><div class="grid">
<label>Model<select id="gm_model" onchange="grinderForm()"></select></label>
<label>Name<input id="gm_name" maxlength="31"></label>
<label>Burrs<input id="gm_burrs" maxlength="23" placeholder="e.g. 64 mm flat"></label>
<label>A setting is read in<input id="gm_unit" maxlength="7" placeholder="um, steps, dial"></label>
<label>Microns of burr gap per step (blank: not known)<input id="gm_umPerUnit" type="number" min="0" step="0.05"></label>
<label>A dial that reads in parts of a step<select id="gm_divisor"><option value="1">No: whole numbers (246, 12)</option><option value="2">Halves (3.5)</option><option value="4">Quarters (3.25)</option><option value="5">Fifths (3.2)</option><option value="10">Tenths (3.5)</option><option value="20">Twentieths (3.05)</option><option value="25">Twenty-fifths (3.04)</option><option value="50">Fiftieths (3.02)</option><option value="100">Hundredths (3.01)</option></select></label>
<label>Two rings (inner steps per outer step; 0: one scale)<input id="gm_subSteps" type="number" min="0" max="99" step="1" placeholder="0" oninput="$('gm_divisor').disabled=+this.value>=2"></label>
<label>Last zeroed, or burrs changed<input id="gm_zeroDate" type="date"></label></div>
<div class="row"><button class="pri" data-save onclick="saveGrinder(false)">Save</button><button id="gm_addother" onclick="saveGrinder(false,true)">Add what is typed above as my second grinder</button></div>
<div class="sm dim" id="gm_info"></div>
<div class="row" id="gm_shrow" style="border-top:1px solid var(--line);padding-top:12px"><span class="pill" id="gm_shpill" style="background:var(--combg);color:#8cc4ff;border:1px solid var(--comline)"></span><span class="sm mid" id="gm_share" style="flex:1 1 260px"></span><button id="gm_send" onclick="sendGrinder(L.grinder.id)">Send to the shared library</button></div>
<div class="row" id="gm_other" style="border-top:1px solid var(--line);padding-top:12px"></div>
<details class="note"><summary>Zeroing again, other burrs or another model start grind advice afresh.</summary><ul><li>They change what a setting means, so shots from before are no longer compared with new ones.</li><li>Naming the grinder for the first time changes nothing.</li><li>A second grinder keeps its own setting for every coffee, and its own shots; switching between the two loses neither.</li></ul></details></section>
<section class="card com"><h2>Shared grinder list</h2><details class="note"><summary>The model list holds only grinders with a known profile: the firmware's own, and those the shared library has published.</summary><ul><li>A listed model can be changed and saved: it is then kept as your own version.</li><li>A grinder on neither list can be typed in and kept here, or sent to the library for a reviewer to add.</li><li>The next owner of one then picks it from the list, and settings can be compared among everyone who has it.</li><li>Settings are translated between grinders through what each is read in and the coffees their owners have in common.</li></ul></details></section>
</div>

<div class="page" id="p-insights">
<div class="head"><h1>Insights</h1><label class="sm">Coffee<select id="icoffee" onchange="drawInsights()"></select></label></div>
<section class="card"><div class="head"><h2>Roast level against grind setting</h2><span class="sm dim" id="irnote"></span></div><canvas id="irchart" width="900" height="240" style="height:240px"></canvas><div class="row sm" id="irleg"></div><details class="note"><summary>One dot per shot stopped by weight on this grinder: the setting it was pulled at, its coffee's roast level, coloured by how old the coffee was.</summary><ul><li>Blue is fresh, red is old; grey: no roast date. A larger dot poured faster.</li><li>The green band on each roast level: the settings whose shots ran on time (within the tolerance of their target time); the line marks the middle one.</li><li>Hover or tap a dot to see the shot; click it to open it.</li></ul></details></section>
<section class="card"><div class="head"><h2>Coffee age against brew time</h2><span class="sm dim" id="ianote"></span></div><canvas id="iachart" width="900" height="260" style="height:260px"></canvas><div class="row sm" id="ialeg"></div><details class="note"><summary>How far each shot's brew time was from its target, by how many days the coffee was off roast. The darker the square, the more shots.</summary><ul><li>Rows above the outlined band ran slow, rows below ran fast.</li><li>Shots without a roast date or a target time are left out.</li></ul></details></section>
<section class="card"><div class="head"><h2>Brewing calendar</h2><div class="row" id="icmode" role="group" aria-label="What the calendar shows"><button data-m="shots" onclick="CALMODE='shots';drawCalendar()">Shots</button><button data-m="brew" onclick="CALMODE='brew';drawCalendar()">Coffee &amp; recipe</button><button data-m="drift" onclick="CALMODE='drift';drawCalendar()">Drift</button><button data-m="hours" onclick="CALMODE='hours';drawCalendar()">Time of day</button></div></div><div class="scroll"><canvas id="icchart" width="900" height="180" style="height:180px;min-width:620px"></canvas></div><div class="row sm" id="icleg"></div></section>
<section class="card"><div class="head"><h2>Dial-in shots per bag</h2><span class="sm dim" id="idnote"></span></div><canvas id="idchart" width="900" height="240" style="height:240px"></canvas><div class="row sm" id="idleg"></div><details class="note"><summary>For each bag (a coffee and its roast date), how many shots it took to pull the first one on time, and how many came after.</summary><ul><li>On time: within the tolerance of that shot's target. Only shots stopped by weight count.</li><li>A bag with no shot on time yet is shown in red.</li></ul></details></section>
</div>
<div class="page" id="p-machine">
<div class="head"><h1>Espresso machine</h1></div>
<section class="card"><div class="head"><h2>Upkeep</h2><span class="sm dim" id="mt_total"></span></div><div id="mt_rows"></div><details class="note"><summary>Counts shots (12 s or longer, 2 g or more) since each job was last done.</summary><ul><li>The controller cannot tell from the cup when a machine needs descaling or a grinder cleaning, so it counts.</li><li>You choose how often to be reminded; no interval is set until you set one.</li></ul></details></section>
<section class="card"><h2>Make and model</h2><div id="mc" class="mid"></div><div class="grid"><label>Pick one<select id="mc_pick" onchange="pickMachine()"></select></label><label>Maker<input id="mc_maker" maxlength="23"></label><label>Model<input id="mc_model" maxlength="31"></label><label>Button, lever or knob<input id="mc_control" maxlength="31"></label><label>How a shot is run<select id="mc_style"><option value="toggle_button">One button: a press starts, a press stops (the brew bot does both)</option><option value="start_button">A button starts, the machine stops itself (the brew bot starts)</option><option value="start_stop_buttons">Two buttons: one starts, another stops (brew bot on start, stop bot on stop)</option><option value="hold_button">One button, held down for the whole shot (the brew bot holds it; needs a SwitchBot)</option><option value="manual">By hand: lever, knob, wheel or paddle (nothing is pressed)</option></select></label><label>By hand: the clock starts<select id="mc_start"><option value="tap">when Start is tapped</option><option value="first_drip">with the first coffee on the scale</option></select></label><label>Holding the button holds pre-infusion<select id="mc_hold"><option value="0">No</option><option value="1">Yes</option></select></label><label>A tap uses the machine's own pre-infusion<select id="mc_tap"><option value="0">No</option><option value="1">Yes</option></select></label><label>The power button is a toggle<select id="mc_pow"><option value="0">No</option><option value="1">Yes</option></select></label><label>The machine ends a shot itself after (s, 0: no)<input id="mc_self" type="number" min="0" max="600"></label></div><div class="row"><button class="pri" data-save onclick="saveMachine()">Save</button><button id="mc_send" onclick="shareMachine()">Send to the shared library</button></div><details class="note"><summary>Change anything above and Save: a listed machine you have changed is kept as your own version.</summary><ul><li>Set a machine up once and send it: the next owner of the same machine picks it from the list.</li><li>The same machine worked by another button is another entry.</li><li>Where the bot does not stop the machine, the target weight is a cue: the screen says STOP NOW (and the scale sounds, if it can be told to) and the shot ends when the pour does.</li><li>Held pre-infusion and bloom in a recipe are only used on a machine whose button holds pre-infusion.</li></ul></details></section>
<section class="card"><h2>Machine power</h2><div id="pw" class="mid"></div><label class="chk"><input type="checkbox" id="keepPowerBotConnected" onchange="post('/api/settings',{keepPowerBotConnected:this.checked}).then(r=>done(r,'Saved'))">Keep the power bot connected while the scale is off, so the power button presses at once (it is let go when the scale comes on; uses more of its battery)</label><label style="max-width:520px">Let the scale switch itself off after this many minutes with nothing happening (0: keep it connected)<input id="scaleRestMin" type="number" min="0" max="60" step="1" onchange="post('/api/settings',{scaleRestMin:+this.value}).then(r=>{done(r,'Saved');loadSettings()})"></label><details class="note"><summary>An Acaia scale only counts down its own sleep timer while nothing is connected to it, so the controller lets go of it.</summary><ul><li>The brew bot is let go with it, and the power bot is connected once the scale is off.</li><li>Touching the screen, the touch key or this page keeps it connected. Pressing the touch key while it rests takes it back and starts the shot once it is there.</li></ul></details><div class="row"><button onclick="power('on')">Switch on</button><button onclick="power('off')">Switch off</button><button onclick="power('press')">Press the power button anyway</button></div><div class="row sm">It is really: <button onclick="power('believe_on')">on</button><button onclick="power('believe_off')">off</button></div><details class="note"><summary>If the machine was switched by hand, or switched itself off, put it right here: nothing is pressed.</summary><ul><li>The power button is a toggle and the machine does not say whether it is on, so the controller keeps count.</li></ul></details><h2 style="margin-top:10px">Schedule</h2><div class="scroll"><table id="pws"></table></div><div class="row"><button data-save onclick="savePowerSchedule()">Save schedule</button></div><details class="note"><summary>Times are this browser's local time.</summary><ul><li>Saving stores its time zone, so save again after a daylight-saving change.</li><li>A time is skipped if the machine is already believed to be that way, or while a shot is running.</li></ul></details></section>
</div>

<div class="page" id="p-setup">
<div class="head"><h1>Setup</h1></div>
<section class="card"><h2>Appearance</h2><p class="sm dim">The colour scheme of this page and of the controller's screen. Every browser and the controller follow it.</p><div class="row" id="themes"></div></section>
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
<div class="grid"><label>Library repository (owner/name)<input id="sh_repo" maxlength="63" placeholder="e.g. hallge/Community-Coffee" autocomplete="off"></label>
<label>Token for that repository only (blank keeps the one stored)<input id="sh_token" type="password" maxlength="127" autocomplete="new-password"></label></div>
<div class="sm mid" id="sh_link"></div>
<div class="sm mid">The token should be a fine-grained one limited to this repository, with Contents: read and write and nothing else. Save below stores it on the controller; it is never shown again.</div>
<label class="chk"><input type="checkbox" id="shareEnabled" style="accent-color:var(--com)">Share from this controller</label>
<fieldset><legend>What it shares</legend>
<label class="chk"><input type="checkbox" id="shareCoffees" style="accent-color:var(--com)">Coffees I add or change (name, roaster, origin, process, roast level, notes)</label>
<label class="chk"><input type="checkbox" id="shareDialIns" style="accent-color:var(--com)">Settled dial-ins: grind, dose, yield, brew time, grinder, roast date</label>
<label class="chk"><input type="checkbox" id="shareShots" style="accent-color:var(--com)">Every shot that counts for grind advice, with its flow</label>
<label class="chk"><input type="checkbox" id="shareRatings" style="accent-color:var(--com)">My taste ratings, with those shots</label></fieldset>
<div class="row"><button class="pri" style="background:var(--com);border-color:var(--com);color:#06203a" data-save onclick="saveShare()">Save</button><button onclick="post('/api/share/coffees').then(r=>{done(r,r.queued+' coffees queued');loadLibrary();loadShare()})">Queue coffees not sent yet</button><button onclick="ask('Forget what was sent before and send every coffee and grinder you have described again?').then(ok=>{if(ok){post('/api/share/again').then(r=>{done(r,r.queued+' queued');loadLibrary();loadShare()})}})">Send everything again</button></div>
<div class="sm mid" id="sh_queue"></div>
<details class="note"><summary>Every record is signed with this controller's own key.</summary><ul><li>It carries the key's short id, never a name, an address or anything about the network.</li><li>Coffees and grinders wait for a reviewer before they are published; measurements do not.</li></ul></details></section>
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
const F=["learningEnabled","learningFactor","adaptiveLearning","minValidWeightG","scaleTimeoutMs","dripObserveMs","minShotTimeMs","noFlowTimeoutS","cupMode","cupMinWeightG","autoStartAfterTare","scaleTimer","requireCup","actuationMode","pressDurationMs","releaseSettleMs","botHoldSeconds","grindToleranceS","grindWindow","grindUmPerS","grindMaxStepUm","grindTimeBasis","grindRatedTarget","scaleRestMin"];
const SB=['shareEnabled','shareCoffees','shareDialIns','shareShots','shareRatings','keepPowerBotConnected','displayFlipped'];
async function loadShare(){let s;try{s=await get('/api/share')}catch(e){return}SH_=s;if(L){drawCoffees();drawGrinder()}
$('sh_state').textContent=!S.shareEnabled?'off':s.can_send?'sending':'queueing';$('sh_state').className='pill '+(S.shareEnabled?'ok':'');
$('sh_key').textContent=s.key_ready?'This controller\'s key id: '+s.key_id+'. '+(s.real_signatures?'Signatures: ES256. Records travel by '+(s.via||'HTTPS')+'.':'This is the PC emulator: it has no real key, and marks what it writes as '+s.algorithm+'. The library will not accept these records; they are for trying the flow.'):'No device key: nothing can be shared.';
$('sh_queue').textContent=s.waiting+' waiting to send (room for '+s.capacity+'), '+s.sent+' sent.';
if(document.activeElement!=$('sh_repo'))$('sh_repo').value=s.repository||'';
$('sh_link').textContent=(s.real_signatures?(!s.configured?(s.repository?'No token stored yet.':'Not connected: records wait on the controller.'):s.problem?'Not reaching the library: '+s.problem+(s.last_answer?' (answer '+s.last_answer+')':'')+'.':s.can_send?'Connected'+(s.last_ok?', last reached '+new Date(s.last_ok*1000).toLocaleString():'')+'.':'Waiting to try again.')+(s.token_set?' A token is stored.':''):'This PC has no OpenSSL, so the emulator cannot sign: records go to a folder beside its data and no further.')+(s.real_signatures&&!s.to_library?' Not reaching the library: set the repository above, and log the GitHub CLI in on this PC (gh auth login).':'')}
// The Shared library card has one Save: the connection (repository, token) when either was changed,
// then the sharing switches. A token typed here is never left behind by a Save that says Saved.
function saveShare(){const repo=$('sh_repo').value.trim(),tok=$('sh_token').value.trim(),linkChanged=!!tok||!SH_||repo!==(SH_.repository||'');
(linkChanged?post('/api/share/link',{repository:repo,token:tok}):Promise.resolve({ok:true})).then(r=>{if(!r.ok){done(r,'');return}
if(linkChanged)$('sh_token').value='';saveSettings($('sh_repo').closest('.card'));loadShare()})}
const CF=['name','roaster','origin','region','producer','varietal','process','roastLevel','altitudeM','roastDate','frozenDate','thawDate','notes'];
const RF=['name','doseG','useRatio','targetWeightG','ratio','targetTimeS','preinfusion','preinfusionHoldS','bloomPauseS','timeLimit','maxShotTimeS','baselineOffsetG'];
const coffee=id=>L.coffees.find(c=>c.id==id),recipe=id=>L.recipes.find(r=>r.id==id);
const plabel=p=>{const c=coffee(p.coffeeId),r=recipe(p.recipeId);return (c?c.name+' — ':'')+(r?r.name:'?')};

function show(){const h=(location.hash||'#home').slice(1);document.querySelectorAll('.page').forEach(p=>p.classList.toggle('on',p.id=='p-'+h));
document.querySelectorAll('nav a,#bnav a,#more a').forEach(a=>a.classList.toggle('on',a.getAttribute('href')=='#'+h));
$('bmore').classList.toggle('on',['recipes','insights','machine','setup','logs'].includes(h));$('more').classList.remove('open');$('bmore').setAttribute('aria-expanded','false');if(h=='setup'){loadDevices();loadFirmware()}if(h=='machine'){loadUpkeep();loadMachine();loadPower()}if(h=='logs')loadLog();if(h=='coffees')loadCommunityCoffees();if(h=='grinder'){drawScatter();loadCommunityGrinders();loadUpkeep()}if(h=='insights')drawInsights();}
// Colour schemes, in the controller's order (bbw::UiTheme): name, and a strip of its colours.
const THEMES=[['dark','Dark',['#0e0f11','#17191c','#f08a24','#4cc38a']],['light','Light',['#f4f3f0','#ffffff','#c2560c','#1e8e4e']],
['nord','Nord',['#2e3440','#3b4252','#d08770','#a3be8c']],['solarized','Solarized Dark',['#002b36','#073642','#cb4b16','#859900']],
['dracula','Dracula',['#21222c','#282a36','#ffb86c','#50fa7b']],['contrast','High contrast',['#000000','#1a1a1a','#ffb000','#00e676']]];
function themeNow(){const t=document.documentElement.dataset.theme||'dark';return Math.max(0,THEMES.findIndex(x=>x[0]==t))}
function setTheme(n,save){const t=(THEMES[n]||THEMES[0])[0];if(t=='dark')delete document.documentElement.dataset.theme;else document.documentElement.dataset.theme=t;
try{localStorage.setItem('bbwTheme',t)}catch(e){}readTC();renderThemes();show();
if(save)post('/api/settings',{uiTheme:n}).then(r=>{if(r&&r.ok===false)return;if(S)S.uiTheme=n})}
function renderThemes(){const el=$('themes');if(!el)return;const cur=themeNow();el.innerHTML=THEMES.map(([k,name,cols],i)=>`<button class="theme${i==cur?' on':''}" onclick="setTheme(${i},true)" aria-pressed="${i==cur}"><span class="strip">${cols.map(c=>`<i style="background:${c}"></i>`).join('')}</span>${name}</button>`).join('')}
window.addEventListener('hashchange',show);loadHomePower();
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

async function loadSettings(){S=await get('/api/settings');if(S&&typeof S.uiTheme=='number'&&S.uiTheme!=themeNow())setTheme(S.uiTheme,false);renderThemes();F.forEach(k=>{const e=$(k),v=S[k];e.value=typeof v==='boolean'?(v?1:0):v});
SB.forEach(k=>$(k).checked=!!S[k]);loadShare();$('staSsid').value=S.net.staSsid;$('webStartEnabled').checked=S.net.webStartEnabled}
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
const m=L.coffees.filter(c=>!c.finished&&hit(c)),fin=L.coffees.filter(c=>c.finished&&hit(c));
$('clist').innerHTML=m.map(c=>{const n=L.presets.filter(p=>p.coffeeId==c.id).length,d=age(c.roastDate);return `<button class="item ${c.id==C?'on':''}" onclick="C=${c.id};drawCoffees()"><b>${esc(c.name)}</b><span>${esc([c.roaster,n?n+(n==1?' recipe':' recipes'):'not on the controller',d!=null?d+' days off roast':'',c.share&&c.share!='local'?(c.share=='sent'&&!lib()?'not sent':SHN[c.share])+(c.shareChanged?', changed since':''):''].filter(x=>x).join(' · '))}</span></button>`}).join('')||'<div class="dim sm">'+(q?'No coffee on the shelf matches.':'No coffee on the shelf.')+'</div>';
const fo=$('cfinished')&&$('cfinished').open;
$('clist').innerHTML+=fin.length?`<details id="cfinished" ${fo||fin.some(c=>c.id==C)||q?'open':''} style="margin-top:10px"><summary class="sm mid">Finished bags (${fin.length})</summary>`+fin.map(c=>`<button class="item ${c.id==C?'on':''}" style="opacity:.8" onclick="C=${c.id};drawCoffees()"><b>${esc(c.name)}</b><span>${esc([c.roaster,'finished '+fd(c.finished)].filter(x=>x).join(' · '))}</span></button>`).join('')+'</details>':'';
const onShelf=L.coffees.filter(c=>!c.finished).length,nf=L.coffees.length-onShelf;
$('ccount').textContent=onShelf+(onShelf==1?' coffee':' coffees')+' on the shelf'+(nf?', '+nf+' finished':'')+' (room for '+L.maxCoffees+' in all).';
const have=new Set(L.coffees.map(c=>c.libraryId).filter(x=>x)),sm=CC.filter(c=>!have.has(c.id)&&(!q||[c.name,c.roaster,c.origin,c.region,c.process,c.varietal].join(' ').toLowerCase().includes(q))).slice(0,8);
$('cshared').innerHTML=sm.length?'<h2 style="color:#8cc4ff;margin-top:6px">In the shared library</h2>'+sm.map(c=>`<div class="pair" style="background:var(--combg);border-color:var(--comline)"><span class="what"><b>${esc(c.name)}</b> <span class="dim">${esc([c.roaster,c.origin,c.process].filter(x=>x).join(', '))}</span></span><button onclick="adopt('${esc(c.id)}')">Add</button></div>`).join(''):''}
function drawCoffees(){drawCoffeeList();const c=coffee(C)||{roastLevel:0};$('ctitle').textContent=C?'Coffee':'New coffee';
CF.forEach(k=>{$('c_'+k).value=DK.includes(k)?i2d(c[k]):(c[k]==null?'':c[k])});drawBags(c);if(!c.altitudeM)$('c_altitudeM').value='';
$('cdel').style.display=$('cbag').style.display=C?'':'none';$('cbagrow').style.display='none';
$('cfin').style.display=C&&!c.finished?'':'none';$('cbag').textContent=c.finished?'New bag: back on the shelf...':'New bag...';drawCoffeeShare(c);if(C)drawCommunity('ccom',C);else $('ccom').textContent='';$('cpairs').style.display=C?'':'none';
$('cinfo').textContent=c.finished?'Bag finished '+fd(c.finished)+'. Off the controller until a new bag is started.':'';
if(!C)return;
$('cplist').innerHTML=L.recipes.map(r=>{const i=L.presets.findIndex(p=>p.coffeeId==C&&p.recipeId==r.id),on=i>=0,p=on?L.presets[i]:null;
return `<div class="pair ${on?'on':''}"><input type="checkbox" aria-label="Brew ${esc(c.name)} with ${esc(r.name)}" ${on?'checked':''} ${c.finished?'disabled title="Bag finished: start a new bag first"':''} onchange="${on?`act('preset_delete',${i})`:`act('preset_add',${r.id},${C})`}"><span class="what"><b>${esc(r.name)}</b> <span class="dim">${r.doseG} g in, ${r.targetWeightG} g out, ${r.targetTimeS} s${r.bloomPauseS?', bloom '+r.bloomPauseS+' s':''}</span></span>`+(on?`<input type="number" aria-label="Grind setting in microns" min="0" max="2000" value="${p.grindUm||''}" placeholder="${U}" onchange="act('preset_grind',${i},+this.value||0)"><span class="dim sm">${U}</span>`:'')+'</div>'}).join('');
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
return `<div class="tiles" style="margin-top:12px"><div class="tile" style="background:#2a2a1a"><span>A guess from your own coffees</span><b>${fsn(s.setting)} \u00b1 ${fdn(s.range)} ${esc(U)}</b><span>Starting from ${esc(s.from_coffee)} (${fs(s.from_setting)}), the nearest coffee you have a setting for${s.near_recipe?', on a close recipe, not the same one':''}. ${how}${hint.length&&s.basis!='fitted'?' This coffee is '+hint.join(' and ')+'.':''}${spread} The advice takes over from your first shot.</span></div></div><div class="row" style="margin-top:12px"><button onclick="useStart(${x.i},${s.setting})">Use ${fs(s.setting)} as my starting grind</button><span>${x.p.grindUm?'Your setting for it now: '+esc(fs(x.p.grindUm))+'.':'No setting entered for it yet.'}</span></div>`};
async function drawCommunity(el,coffeeId){const box=$(el),c=coffee(coffeeId),mine=++comSeq;if(!c){box.textContent=coffeeId?'':'No coffee is attached to what is being brewed, so there is nothing from other people to show.';return}
const ps=L.presets.map((p,i)=>({p,i})).filter(x=>x.p.coffeeId==coffeeId);if(!ps.length){box.textContent=c.libraryId?'Tick a recipe for this coffee to see what others settled on for it.':c.name+' is not in the shared library yet, so there is nothing from other people for it. Once it has been sent and a reviewer has published it, what others settle on appears here.';return}
const x=ps.find(x=>x.i==L.activePreset)||ps[0],r=recipe(x.p.recipeId);let m;try{m=await get('/api/community?coffee='+coffeeId+'&recipe='+r.id)}catch(e){return}if(mine!=comSeq&&el=='ccom')return;
const sim=simHtml(m,x,r);
if(!c.libraryId){box.innerHTML=esc(c.name+' is not in the shared library yet, so there is nothing from other people for it. Once it has been sent and a reviewer has published it, what others settle on appears here.')+sim;return}
if(!m.found){box.innerHTML=esc('Nobody has shared a settled dial-in for '+c.name+' with a recipe like '+r.name+' ('+r.doseG+' g in, '+r.targetWeightG+' g out) yet.'+(m.reachable?'':' The shared library cannot be reached from this controller yet.'))+sim;return}
const s=m.start,why={fitted:'translated to your grinder through '+m.coffees_in_common+' coffee'+(m.coffees_in_common==1?'':'s')+' you have in common with others',same_model:'what '+m.same_model+' other'+(m.same_model==1?'':'s')+' with your grinder model set',microns:'the shared scale read as microns; how your own grinder sits against it is not known yet',none:'cannot be translated to your grinder yet'}[s.basis]+(s.age_shift?'; includes '+fdn(s.age_shift)+' '+esc(fdu())+' '+(s.age_shift<0?'finer':'coarser')+' because your bag is '+m.my_age_days+' days off roast and theirs were about '+m.their_age_days:(m.their_age_days>=0&&m.my_age_days>=0&&Math.abs(m.my_age_days-m.their_age_days)>=7&&!m.age_rate_known?'; your bag is '+m.my_age_days+' days off roast, theirs were about '+m.their_age_days+', and the library has not yet measured what that difference is worth':''))+(s.flow_shift?'; includes '+fdn(s.flow_shift)+' '+esc(fdu())+' '+(s.flow_shift<0?'finer':'coarser')+' because your recipe aims for a '+(s.flow_shift<0?'slower':'faster')+' flow than theirs':'')+(m.recipe_match=='near'?'; nobody has settled this exact recipe, so this comes from '+m.their_dose_g+' g in, '+m.their_yield_g+' g out, with a wider range':'');
box.innerHTML=`<div class="tiles"><div class="tile" style="background:#16263a"><span>Start your grinder at</span><b>${s.setting?fsn(s.setting)+' \u00b1 '+fdn(s.range)+' '+esc(U):'not known'}</b><span>${why}</span></div><div class="tile" style="background:#16263a"><span>What a good shot looked like</span><b>${f1(m.time_s)} s</b><span>${r.doseG} g in, ${r.targetWeightG} g out (${esc(r.name)})</span></div><div class="tile" style="background:#16263a"><span>Based on</span><b>${m.contributors} ${m.contributors==1?'person':'people'}</b><span>${m.dialins} settled dial-in${m.dialins==1?'':'s'}, within \u00b1 ${f1(m.spread_um)} \u00b5m of each other</span></div></div>`+(s.setting?`<div class="row" style="margin-top:12px"><button style="background:var(--com);border-color:var(--com);color:#06203a;font-weight:600" onclick="useStart(${x.i},${s.setting})">Use ${fs(s.setting)} as my starting grind</button><span>${x.p.grindUm?'Your setting for it now: '+esc(fs(x.p.grindUm))+'.':'No setting entered for it yet.'} A start only: grind advice takes it from there.</span></div>`:'');box.insertAdjacentHTML('beforeend',sim)}
async function useStart(i,um){const p=L.presets[i];if(p.grindUm&&p.grindUm!=um&&!await ask('Replace your setting of '+fs(p.grindUm)+' with '+fs(um)+'?'))return;act('preset_grind',i,um)}
async function saveCoffee(extra){const b={id:C||0};CF.forEach(k=>{const v=$('c_'+k).value;b[k]=DK.includes(k)?d2i(v):(k=='roastLevel'||k=='altitudeM')?+v||0:v.trim()});Object.assign(b,extra||{});
if(!b.name)return say('A coffee needs a name',false);
if(!C&&L.coffees.some(c=>c.name.toLowerCase()==b.name.toLowerCase()&&c.roaster.toLowerCase()==b.roaster.toLowerCase())&&!await ask('"'+b.name+'" is already on the shelf. Add it again?'))return;
post('/api/coffee',b).then(r=>{if(done(r,'Coffee saved')){C=r.id;loadLibrary()}})}
function drawBags(c){const el=$('cbags');if(!el)return;const bs=c?L.bags.filter(b=>b.coffeeId==c.id).sort((a,b)=>b.id-a.id):[];if(!bs.length){el.innerHTML='';return}
el.innerHTML='<h3 style="margin:14px 0 6px">Bags of '+esc(c.name)+'</h3>'+bs.map(b=>{const f=frozenFor({frozenDate:b.frozenDate,thawDate:b.thawDate});const bits=[b.roastDate?'roasted '+i2d(b.roastDate):'roast date not entered',b.openedDate?'opened '+i2d(b.openedDate):'',f?f+' days frozen'+(b.frozenDate&&!b.thawDate?' (still there)':''):'',b.weightG?b.weightG+' g':''].filter(x=>x).join(', ');
return `<div class="row" style="align-items:center"><span style="flex:1 1 220px">${esc(bits)}${b.current?' <span class="pill">in use</span>':''}</span>${b.current?'':`<button onclick="act('bag_use',${c.id},${b.id})">Use this bag</button><button class="del" onclick="ask('Forget this bag? Its shots stay in the history.').then(ok=>{if(ok){act('bag_delete',${b.id})}})">Forget</button>`}</div>`}).join('')}
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
$('rdup').style.display=$('rdel').style.display=R?'':'none';$('rused').style.display=R?'':'none';$('rinfo').textContent=R?'':'New recipe: not saved yet.';
if(!R)return;const use=L.presets.map((p,i)=>({p,i})).filter(x=>x.p.recipeId==R);
$('rtable').innerHTML='<tr><th class="l">Coffee</th><th>Grind '+U+'</th><th>Learned stop offset g</th><th class="l"></th></tr>'+(use.map(x=>{const c=coffee(x.p.coffeeId);
return `<tr><td class="l"><b>${c?esc(c.name):'No coffee attached'}</b>${x.i==L.activePreset?' <span class="dim">in use now</span>':''}${x.p.hidden?' <span class="dim">bag finished</span>':''}</td><td>${fsn(x.p.grindUm)||'not set'}</td><td>${f2(x.p.stopOffsetG)}</td><td class="l"><a href="#" onclick="post('/api/offset/reset?recipe=${x.i}').then(loadLibrary);return false">reset offset</a> &nbsp; <a href="#" onclick="act('preset_delete',${x.i});return false">take off the controller</a></td></tr>`}).join('')||'<tr><td class="l dim" colspan="4">Not on the controller yet.</td></tr>');
$('ralone').style.display=use.some(x=>!x.p.coffeeId)?'none':''}
function readRecipe(){const b={id:R||0};RF.forEach(k=>{const v=$('r_'+k).value;b[k]=k=='name'?v.trim():k=='useRatio'?v==='1':+v});if(b.useRatio)b.targetWeightG=Math.round(b.doseG*b.ratio*10)/10;return b}
function saveRecipe(){const b=readRecipe();if(!b.name)return say('A recipe needs a name',false);post('/api/recipe',b).then(r=>{if(done(r,'Recipe saved')){R=r.id;loadLibrary()}})}
function dupRecipe(){const b=readRecipe();b.id=0;b.name=(b.name+' 2').slice(0,23);post('/api/recipe',b).then(r=>{if(done(r,'Copy made: not on the controller yet')){R=r.id;loadLibrary()}})}
async function delRecipe(){const r=recipe(R);if(!r||!await ask('Delete the recipe "'+r.name+'"?'))return;act('recipe_delete',R).then(()=>{R=-1;loadLibrary()})}

async function loadGrind(){try{G=await get('/api/grind')}catch(e){return}
const cur=fsn(G.current_um)||'--';$('gcur').textContent=cur;$('hg').textContent=cur;$('gfor').textContent='Advice for '+G.recipe+(G.coffee?', '+G.coffee:'');
const b=$('gapply');b.style.display='none';let title,cls='',why,short;
const basis=`Average ${G.time_basis=='pour'?'pour':'total'} time ${f1(G.average_time_s)} s over ${G.shots_used} shot${G.shots_used==1?'':'s'} at ${fs(G.current_um)}, target ${G.target_time_s} s ± ${G.tolerance_s} s.`;
const out=G.outliers_ignored?` ${G.outliers_ignored} one-off shot${G.outliers_ignored==1?'':'s'} left out (far from the others at this setting).`:'';
const sens=G.sensitivity_source==='measured'?` Sensitivity ${fdf(G.um_per_s)} ${fdu()} per second, measured from ${G.learn_shots} of your shots${G.data_weight<0.99?' ('+Math.round(100*G.data_weight)+'% from this coffee, the rest from what this grinder has shown)':''}.`:G.sensitivity_source==='grinder'?` Sensitivity ${fdf(G.um_per_s)} ${fdu()} per second, from what this grinder has shown on ${G.grinder_coffees} other coffee(s) (${G.grinder_shots} shots); this coffee's own shots take over as they come.`:` Sensitivity ${fdf(G.um_per_s)} ${fdu()} per second is the starting guess; it is measured once shots exist at two or more grind settings.`;
if(G.action=='no_grind_set'){title='Enter the grinder’s setting';why=short='Shots are stored with the grind setting entered here. Advice starts with the first valid shot.'}
else if(G.action=='no_data'){title='No shot at this setting yet';why=short=`Pull a shot at ${fs(G.current_um)}. A shot counts if it ends on the weight target, settles, and lasts more than 15 s and less than target + 15 s.`}
else if(G.action=='inconsistent'){title='Brew times vary: no advice';cls='warn';short=`The last ${G.shots_used} shots at ${fs(G.current_um)} ran from ${f1(G.min_time_s)} s to ${f1(G.max_time_s)} s against ${G.target_time_s} s ± ${G.tolerance_s} s. Check dose, distribution and tamp before moving the grind.`;
why=`The last ${G.shots_used} shots at ${fs(G.current_um)} ran from ${f1(G.min_time_s)} s to ${f1(G.max_time_s)} s against a target of ${G.target_time_s} s ± ${G.tolerance_s} s, some fast and some slow. Their average (${f1(G.average_time_s)} s) describes none of them, so it is not used. If the grinder was changed between them, enter its setting: shots are only compared at the same setting. Otherwise look at dose, distribution and tamp. A shot can be left out on the Shots page.`+out}
else if(G.out_of_range){const fin=G.action=='finer';title=`Go ${fin?'finer':'coarser'} ${fdn(G.delta_um)} ${fdu()}, to ${fs(G.suggested_um)}`;cls='warn';why=short=`The last shot ran ${fin?'too short (under 15 s)':'more than 15 s over the target'} to be counted, so its time cannot size the change. The direction is clear: this is the largest single change. The next shot gets the usual advice.`}
else if(G.action=='hold'&&G.between_steps){title='Keep '+fs(G.current_um)+' (closest step)';cls='ok';why=basis+' The target lies between this setting and the next one, which ran on the other side of it; there is no setting between them, so the closer one is kept.'+out;short=basis}
else if(G.action=='hold'){title='Keep '+fs(G.current_um);cls='ok';why=basis+' Inside tolerance.'+out;short=basis}
else{const fin=G.action=='finer';title=`Go ${fin?'finer':'coarser'} ${fdn(G.delta_um)} ${fdu()}, to ${fs(G.suggested_um)}${G.refined?' (to your liked time)':''}`;cls='warn';short=basis+` Running ${f1(Math.abs(G.error_s))} s ${fin?'fast':'slow'}.`;
why=short+sens+(G.step_limited?' Limited to the largest single change; expect another step.':'')+out;b.style.display='';b.textContent='I set the grinder to '+fs(G.suggested_um)}
$('gadv').textContent=$('hgadv').textContent=title;$('gadv').className=$('hgadv').className='pill '+cls;$('gwhy').textContent=why;$('hgwhy').textContent=short;
const ag=G.bean_age_days,ds=G.days_since_brewed,sh=G.age_shift;let at='',as='';
if(!G.coffee){at=''}else if(ag==null||ag<0){at='Bean age: no roast date is entered for this bag, so its age cannot be taken into account.'}
else{as='Bag '+ag+' day'+(ag==1?'':'s')+' off roast';at='Bean age: this bag is '+ag+' day'+(ag==1?'':'s')+' off roast.';
if(ag<5)at+=' Very fresh coffee is still giving off gas: expect shots to vary for a few days, and settings found now to need revisiting.';
if(ds>=3){at+=' It is '+ds+' days since a shot of it last counted here. Coffee runs faster as it ages, so the grind usually has to go finer.';
if(sh){at+=' From what contributors to the shared library have measured, expect about '+fdn(sh)+' '+fdu()+' '+(sh<0?'finer':'coarser')+' than last time'+(G.current_um?' (around '+fs(G.current_um+sh)+')':'')+'. A guide only: the advice above follows from your next shot.';as+=', '+ds+' days since last brewed: about '+fdn(sh)+' '+fdu()+' '+(sh<0?'finer':'coarser')+' expected'}
else{at+=G.age_rate_known?' Not enough is known about this grinder to put a number on it.':' The shared library has not measured how fast yet, so no number is offered: the advice above follows from your next shot.';as+=', '+ds+' days since last brewed'}}}
$('gage').textContent=at;$('hgage').textContent=as?as+'.':'';
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
function grinderForm(){const p=gmPick(),m=p.own||(p.lib&&{model:p.lib.model,unit:p.lib.unit,umPerUnit:p.lib.um_per_unit,burrs:p.lib.burrs||''});
if(m){$('gm_name').value=m.model;$('gm_unit').value=m.unit;$('gm_umPerUnit').value=m.umPerUnit||'';$('gm_divisor').value=m.divisor||1;$('gm_subSteps').value=m.subSteps||m.sub_steps||'';const known=L.grinderModels.map(x=>x.burrs).concat(GM.map(x=>x.burrs||''));if(!$('gm_burrs').value||known.includes($('gm_burrs').value))$('gm_burrs').value=m.burrs}
$('gm_divisor').disabled=+$('gm_subSteps').value>=2}
function drawGrinder(){const g=L.grinder,o=L.otherGrinder,libSel=g.libraryId&&GM.some(x=>x.id==g.libraryId);
$('gm_model').innerHTML=L.grinderModels.filter(m=>m.umPerUnit>0||m.id==g.modelId).map(m=>`<option value="${m.id}" ${m.id==g.modelId?'selected':''}>${esc(m.model)}</option>`).join('')+GM.filter(m=>!L.grinderModels.some(x=>x.model==m.model)).map(m=>`<option value="L:${esc(m.id)}" ${!g.modelId&&g.libraryId==m.id?'selected':''}>${esc(m.model)} (shared library)</option>`).join('')+`<option value="0" ${g.modelId||libSel?'':'selected'}>${g.model&&!g.modelId&&!libSel?esc(g.model)+' (yours)':g.model||g.modelId?'Another grinder: type it below':'Not set yet: pick one, or type it below'}</option>`;
$('gm_name').value=g.model;$('gm_burrs').value=g.burrs;$('gm_unit').value=g.unit;$('gm_umPerUnit').value=g.umPerUnit||'';$('gm_divisor').value=g.divisor||1;$('gm_subSteps').value=g.subSteps||'';$('gm_zeroDate').value=i2d(g.zeroDate);grinderForm();
if(!gmPick().own&&!gmPick().lib){$('gm_name').value=g.model;$('gm_unit').value=g.unit;$('gm_umPerUnit').value=g.umPerUnit||''}
$('gm_info').textContent=(g.model?'In use: '+g.model+(g.burrs?', '+g.burrs:'')+'. ':'No grinder named yet. ')+'Settings are whole numbers in the unit shown; a dial read in tenths is not supported yet.';
const st=g.share||'local',on=S&&S.shareEnabled,known=g.modelId>0;$('gm_shrow').style.display=g.model?'':'none';$('gm_shpill').textContent=known?'a known model':SHN[st];
$('gm_share').textContent=known?'This model is in the firmware\'s own list: there is nothing to send.':({local:on?'Kept on this controller only. Sending it lets a reviewer add the model to the shared list.':'Kept on this controller only. Sharing is switched off (Setup).',queued:'Signed and waiting in the outbox.',sent:'Sent. The library has not answered yet.',review:'The library has it. A reviewer has not decided yet.',published:'In the shared list'+(g.libraryId?' as '+g.libraryId:'')+'.',declined:'A reviewer did not accept it'+(g.reviewNote?': "'+g.reviewNote+'"':'')+'. What you typed is unchanged.',refused:'The library\'s checks turned this record away'+(g.reviewNote?': "'+g.reviewNote+'"':'')+'. Correct it if needed, then send it again.'})[st]+(g.shareChanged?' You have changed it since that version was sent.':'');
$('gm_send').style.display=on&&g.canShare?'':'none';$('gm_send').textContent=st=='local'?'Send to the shared library':st=='refused'?'Send again':'Send the new version';
$('gm_addother').style.display=o?'none':'';
$('gm_other').innerHTML=o?`<span class="sm mid" style="flex:1 1 260px">Your other grinder: <b>${esc(o.model)}</b>${o.burrs?', '+esc(o.burrs):''} (read in ${esc(o.unit=='um'?'µm':o.unit)}).</span><button onclick="switchGrinder()">Use it instead</button>`+(S&&S.shareEnabled&&o.canShare?`<button onclick="sendGrinder(${o.id})">Send it to the shared library</button>`:'')+`<button class="del" onclick="removeOther()">Remove it</button>`:'<span class="sm dim">One grinder. A second can be kept beside it: choose or type it above, then "Add ... as my second grinder".</span>'}
function grinderBody(rezeroed){const p=gmPick(),src=p.own||(p.lib&&{model:p.lib.model,unit:p.lib.unit,umPerUnit:p.lib.um_per_unit}),
// changed from the listed model: it is saved as the owner's own version
same=src&&$('gm_name').value.trim()==src.model&&($('gm_unit').value.trim()||'um')==src.unit&&Math.abs((+$('gm_umPerUnit').value||0)-(src.umPerUnit||0))<1e-6;
return {modelId:p.own&&same?p.own.id:0,libraryId:p.lib&&same?p.lib.id:'',model:$('gm_name').value.trim(),burrs:$('gm_burrs').value.trim(),unit:$('gm_unit').value.trim()||'um',umPerUnit:+$('gm_umPerUnit').value||0,divisor:(+$('gm_subSteps').value>=2?1:+$('gm_divisor').value)||1,subSteps:+$('gm_subSteps').value>=2?+$('gm_subSteps').value:0,zeroDate:rezeroed?today():d2i($('gm_zeroDate').value),rezeroed:!!rezeroed}}
async function saveGrinder(rezeroed,asOther){const g=L.grinder,b=grinderBody(rezeroed);if(!b.modelId&&!b.model)return say('Pick a model, or type its name',false);
if(asOther){b.asOther=true;b.rezeroed=false;if(b.model==g.model&&b.burrs==g.burrs)return say('That is the grinder already in use: choose or type the other one first',false);
post('/api/grinder',b).then(r=>{done(r,'Second grinder added');loadLibrary()});return}
const named=g.model||g.modelId,changes=named&&(b.model!=g.model||b.burrs!=g.burrs||b.unit!=g.unit);
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
function drawInsights(){if(!L||!Array.isArray(SH))return;loadPowerEvents();const sel=$('icoffee'),cur=sel.value,ids=[...new Map(SH.filter(s=>s.coffee).map(s=>[String(s.coffee_id||0),s.coffee])).entries()];
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
cells.push({x:px-z-2,y:cy-z-2,w:2*z+4,h:2*z+4,txt:`<b>Machine ${e.on?'on':'off'}${e.ok?'':': not done'}</b><br>${hm(d)}, ${e.by=='schedule'?'by the schedule':'by you'}${e.ok?'':'<br>'+esc(e.outcome)}${e.sure===false?'<br>pressed while on/off was not known':''}`})});
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

function drawDialIn(){const c=$('idchart'),x=c.getContext('2d');x.clearRect(0,0,c.width,c.height);c._cells=[];$('idleg').innerHTML='';
const tol=S?S.grindToleranceS:2,sh=insShots().filter(s=>byWeight(s)&&s.target_time_s&&btime(s)>0&&s.timestamp).sort((a,b)=>new Date(a.timestamp)-new Date(b.timestamp));
const bags=new Map();sh.forEach(s=>{const k=(s.coffee_id||0)+'|'+(s.roast_date||0);let b=bags.get(k);if(!b){b={n:s.coffee||'no coffee',rd:s.roast_date,shots:[]};bags.set(k,b)}b.shots.push(s)});
const list=[...bags.values()].slice(-12);if(!list.length)return emptyNote(c,'idnote','Needs shots stopped by weight with a target time.');
list.forEach(b=>{const i=b.shots.findIndex(s=>Math.abs(btime(s)-s.target_time_s)<=tol);b.dial=i<0?null:i+1;b.total=b.shots.length});
const mx=Math.max(...list.map(b=>b.total)),L0=40,R0=c.width-12,T0=24,B0=c.height-40,bw=(R0-L0)/list.length,Y=v=>B0-v/mx*(B0-T0),cells=[];
x.font='12px sans-serif';x.lineWidth=1;
niceTicks(0,mx,4,1).forEach(v=>{const y=Math.round(Y(v))+.5;x.strokeStyle=TC.grid;x.beginPath();x.moveTo(L0,y);x.lineTo(R0,y);x.stroke();x.fillStyle=TC.dim;x.fillText(String(v),8,y+4)});
list.forEach((b,i)=>{const bx=L0+i*bw+bw*.15,w=bw*.7,d=b.dial==null?b.total:b.dial;
x.fillStyle=b.dial==null?STC.slow:'#f08a24';x.fillRect(bx,Y(d),w,B0-Y(d));if(b.dial!=null&&b.total>d){x.fillStyle=STC.ok;x.globalAlpha=.55;x.fillRect(bx,Y(b.total),w,Y(d)-Y(b.total));x.globalAlpha=1}
const lab=b.n.length>14?b.n.slice(0,13)+'…':b.n,rd=ymd(b.rd);x.fillStyle=TC.mid;x.fillText(lab,bx+w/2-x.measureText(lab).width/2,B0+14);if(rd){const t=rd.toLocaleDateString([], {month:'short',day:'numeric'});x.fillStyle=TC.dim;x.fillText(t,bx+w/2-x.measureText(t).width/2,B0+28)}
{const t=String(b.total);x.fillStyle=TC.mid;x.fillText(t,bx+w/2-x.measureText(t).width/2,Y(b.total)-5)}if(b.dial!=null&&B0-Y(d)>16&&w>16){const t=String(b.dial);x.fillStyle='#1d0f00';x.fillText(t,bx+w/2-x.measureText(t).width/2,B0-4)}
cells.push({x:bx,y:Y(b.total),w,h:B0-Y(b.total),txt:`<b>${esc(b.n)}</b>${rd?', roasted '+rd.toLocaleDateString():''}<br>${b.dial==null?'no shot on time yet, '+b.total+' shot'+(b.total==1?'':'s'):b.dial+' shot'+(b.dial==1?'':'s')+' to the first on time, '+b.total+' in all'}`})});
x.strokeStyle=TC.edge;x.beginPath();x.moveTo(L0,T0);x.lineTo(L0,B0);x.lineTo(R0,B0);x.stroke();
chartCells(c,cells,q=>q.txt);
const sw=v=>`<span style="display:inline-block;width:12px;height:12px;border-radius:3px;background:${v};margin:0 4px 0 10px;vertical-align:middle"></span>`;
$('idleg').innerHTML=sw('#f08a24')+'shots to the first on time'+sw(STC.ok)+'shots after'+sw(STC.slow)+'not on time yet';
const done=list.filter(b=>b.dial!=null).map(b=>b.dial);$('idnote').textContent=list.length+' bag'+(list.length==1?'':'s')+(done.length?', '+(done.reduce((a,b)=>a+b,0)/done.length).toFixed(1)+' shots to dial in on average':'')}

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
if(big){$('hshot').className='';$('hshot').innerHTML=`<div class="row" style="gap:26px;align-items:baseline"><span><span class="num">${f1(s.final_weight_g)} g</span> <span class="dim">of ${s.target_weight_g}</span></span><span><span class="num">${f1(s.shot_time_s)} s</span>${s.target_time_s?` <span class="dim">of ${s.target_time_s}</span>`:''}</span><span class="dim">${esc((s.coffee?s.coffee+', ':'')+s.recipe)}. First drops ${f1(s.first_drop_s)} s. Pour ${f1(s.pour_time_s)} s at ${f2(s.pour_flow_g_s)} g/s${s.bloom_pause_s?'. Bloom '+s.bloom_pause_s+' s':''}. ${esc(when(s))}.</span></div>`;$('hrate').innerHTML=rateRow(s)+' <button style="margin-left:12px" onclick="toBeanconqueror('+s.id+')">Send to Beanconqueror</button>';
const d=s.final_weight_g-s.target_weight_g;$('res').textContent='Last shot '+(Math.abs(d)<0.05?'on target':(d>0?'over +':'under ')+f1(d)+' g')+(s.final_valid?'':', did not settle')}
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
$('dev').innerHTML=`<b>Scale</b>: ${d.scale_connected?esc(d.scale_name)+' ('+esc(d.scale_protocol)+'), connected':'not connected'} <span class="dim">[${esc(d.pairing.scale||'adopts the first supported scale found')}]</span><br><b>Brew bot</b>${d.bot_label?' ('+esc(d.bot_label)+')':''}: ${d.bot_connected?'connected':(d.pairing.bot?'asleep until Start':'not paired')} <span class="dim">[${esc(d.pairing.bot||'pick one below')}]</span>`+(d.bot_firmware&&d.bot_firmware!='0.0'?`, firmware ${esc(d.bot_firmware)}`:'')+(d.bot_battery!=null?`, battery ${d.bot_battery}%`:'')+(d.bot_problem?` <b style="color:var(--warn)">${esc(d.bot_problem)}</b>`:'')+(d.bot_caps&&d.bot_caps.length&&!d.bot_caps.includes('down_up')?' <span class="dim">This pusher cannot hold the button: held pre-infusion and bloom restarts become plain presses.</span>':'')+`<br><b>Power bot</b>${d.power_label?' ('+esc(d.power_label)+')':''}: ${d.power_connected?'connected':(d.pairing.power?'asleep until needed':'not paired')} <span class="dim">[${esc(d.pairing.power||'optional: pick one below')}]</span>`+(d.power_battery!=null?`, battery ${d.power_battery}%`:'')+(d.power_problem?` <b style="color:var(--warn)">${esc(d.power_problem)}</b>`:'')+(d.pairing.stop||d.stop_connected?`<br><b>Stop bot</b>${d.stop_label?' ('+esc(d.stop_label)+')':''}: ${d.stop_connected?'connected':'asleep until Start'} <span class="dim">[${esc(d.pairing.stop)}]</span>`+(d.stop_battery!=null?`, battery ${d.stop_battery}%`:'')+(d.stop_problem?` <b style="color:var(--warn)">${esc(d.stop_problem)}</b>`:''):'');
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
function fillMachine(c){$('mc_maker').value=c.maker||'';$('mc_model').value=c.model||'';$('mc_control').value=c.control||'';$('mc_style').value=c.style||'toggle_button';$('mc_start').value=c.manual_start||'tap';$('mc_hold').value=+!!c.hold_preinfusion;$('mc_tap').value=+!!c.tap_preinfusion;$('mc_pow').value=+!!c.power_toggle;$('mc_self').value=c.self_stop_s||0;MC.sel=c}
function pickMachine(){const v=$('mc_pick').value;if(!v){loadMachine();return}if(v=='new'){fillMachine({style:$('mc_style').value,manual_start:$('mc_start').value});$('mc_maker').focus();return}const i=+v.slice(1);fillMachine(v[0]=='b'?Object.assign({},MC.builtins[i]):Object.assign({},MC.lib[i],{library_id:MC.lib[i].id,builtin:0}))}
function saveMachine(){const s=MC.sel||{},body={maker:$('mc_maker').value.trim(),model:$('mc_model').value.trim(),control:$('mc_control').value.trim(),style:$('mc_style').value,manual_start:$('mc_start').value,hold_preinfusion:$('mc_hold').value=='1',tap_preinfusion:$('mc_tap').value=='1',power_toggle:$('mc_pow').value=='1',self_stop_s:+$('mc_self').value||0};
const same=['maker','model','control','style'].every(k=>(s[k]||'')==body[k])&&(s.manual_start||'tap')==body.manual_start&&!!s.hold_preinfusion==body.hold_preinfusion&&!!s.tap_preinfusion==body.tap_preinfusion&&!!s.power_toggle==body.power_toggle&&(s.self_stop_s||0)==body.self_stop_s;
const send=same&&s.builtin?{builtin:s.builtin}:same&&s.library_id?Object.assign(body,{library_id:s.library_id}):body;
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
async function loadPower(){let p;try{p=await get('/api/power')}catch(e){return}
$('pw').innerHTML=(p.paired?'':'<b>No power bot paired.</b> Pick a pusher with "Power" in the Devices list on the <a href="#setup">Setup</a> page. ')+'The machine is believed to be <b>'+esc(p.belief=='unknown'?'unknown: say which below':p.belief)+'</b>.'+(p.working?' Pressing the power button...':'')+(p.last?' Last: '+esc(p.last)+(p.last_by_schedule?' (schedule)':'')+'.':'')+(p.clock_set?'':' The clock is not set: the schedule waits for it.');
$('pws').innerHTML='<tr><th>Use</th><th>Do</th><th>At</th>'+DAYS.map(d=>'<th>'+d+'</th>').join('')+'</tr>'+p.schedule.map((e,i)=>`<tr><td><input type="checkbox" id="pe${i}" ${e.enabled?'checked':''}></td><td><select id="po${i}"><option value="1" ${e.on?'selected':''}>On</option><option value="0" ${e.on?'':'selected'}>Off</option></select></td><td><input type="time" id="pt${i}" value="${String(Math.floor(e.minute/60)).padStart(2,'0')}:${String(e.minute%60).padStart(2,'0')}"></td>`+DAYS.map((d,k)=>`<td><input type="checkbox" id="pd${i}_${k}" ${e.days>>k&1?'checked':''}></td>`).join('')+'</tr>').join('')}
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
shots:'Every shot the controller has pulled. Pick one to see its curve, rate how it tasted, or leave it out of grind advice.',
coffees:'Your coffees and their bags, and which recipes you brew each one with.',
recipes:'How a shot is pulled: dose, yield, time, pre-infusion and bloom. One recipe can serve every coffee.',
grinder:'Your grinder, where it is set now, and which way to turn it.',
insights:'Patterns across your shots: roast and grind, ageing coffee, when you brew, and how long each bag took to dial in.',
machine:'Which machine this is, switching it on and off, and when it is due a clean.',
setup:'Scale, button pushers, network, firmware and sharing. Most of this is set once and left alone.',
logs:'What the controller has recorded, for when something needs looking into.'};
// Which cards start folded: the ones set once, or looked at rarely.
const FOLD={machine:i=>false,setup:i=>true,grinder:i=>i>=4,logs:i=>false,coffees:i=>i==1,recipes:i=>false};
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
