// Shared CSS for the Brew panel. Only Home Assistant's own CSS variables, so light, dark and
// custom themes apply by themselves. Sections add their own small rules after these.

export const sharedStyles = `
:host { display: block; color: var(--primary-text-color); font-family: var(--paper-font-body1_-_font-family, Roboto, system-ui, sans-serif); }
* { box-sizing: border-box; }
a { color: var(--primary-color); }
h2 { font-size: 20px; font-weight: 400; margin: 8px 0 12px; }
h3 { font-size: 16px; font-weight: 500; margin: 0 0 8px; }
.grid { display: grid; gap: 16px; grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); align-items: start; }
.card { background: var(--ha-card-background, var(--card-background-color, #fff)); border-radius: var(--ha-card-border-radius, 12px);
  border: var(--ha-card-border-width, 1px) solid var(--ha-card-border-color, var(--divider-color, #e0e0e0));
  box-shadow: var(--ha-card-box-shadow, none); padding: 16px; }
.card > h3:first-child { display: flex; align-items: center; gap: 8px; }
.muted, .hint { color: var(--secondary-text-color); }
.hint { font-size: 13px; line-height: 1.4; }
.row { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
.spread { display: flex; justify-content: space-between; align-items: center; gap: 8px; }
.tiles { display: grid; grid-template-columns: repeat(auto-fit, minmax(110px, 1fr)); gap: 8px; }
.tile { border-radius: 10px; padding: 10px 12px; background: var(--secondary-background-color, rgba(127,127,127,.08)); }
.tile .v { font-size: 22px; font-weight: 500; }
.tile .k { font-size: 12px; color: var(--secondary-text-color); }
.big { font-size: 40px; font-weight: 300; line-height: 1.1; }
.pill { display: inline-flex; align-items: center; gap: 4px; padding: 2px 10px; border-radius: 999px; font-size: 13px;
  background: var(--secondary-background-color, rgba(127,127,127,.12)); color: var(--primary-text-color); }
.pill.ok { background: rgba(var(--rgb-success-color, 67,160,71), .18); color: var(--success-color, #43a047); }
.pill.warn { background: rgba(var(--rgb-warning-color, 255,166,0), .2); color: var(--warning-color, #ffa600); }
.pill.bad { background: rgba(var(--rgb-error-color, 219,68,55), .18); color: var(--error-color, #db4437); }
button, .btn { font: inherit; font-size: 14px; font-weight: 500; border-radius: 18px; padding: 6px 14px; cursor: pointer;
  border: 1px solid var(--divider-color, #ccc); background: transparent; color: var(--primary-color); }
button.primary { background: var(--primary-color); color: var(--text-primary-color, #fff); border-color: var(--primary-color); }
button.danger { color: var(--error-color, #db4437); border-color: var(--error-color, #db4437); }
button:disabled { opacity: .5; cursor: default; }
button.link { border: none; padding: 0 4px; border-radius: 4px; }
input, select, textarea { font: inherit; color: var(--primary-text-color); background: var(--input-fill-color, var(--secondary-background-color, transparent));
  border: none; border-bottom: 1px solid var(--input-idle-line-color, var(--divider-color, #999)); border-radius: 4px 4px 0 0; padding: 8px; min-width: 0; }
input:focus, select:focus, textarea:focus { outline: none; border-bottom: 2px solid var(--primary-color); }
label.f { display: flex; flex-direction: column; gap: 4px; font-size: 12px; color: var(--secondary-text-color); }
label.f > input, label.f > select, label.f > textarea { font-size: 15px; }
label.c { display: inline-flex; align-items: center; gap: 6px; }
.form { display: grid; gap: 12px; grid-template-columns: repeat(auto-fit, minmax(160px, 1fr)); }
table { width: 100%; border-collapse: collapse; }
th, td { text-align: left; padding: 6px 8px; border-bottom: 1px solid var(--divider-color, #e0e0e0); font-size: 14px; }
th { font-weight: 500; color: var(--secondary-text-color); font-size: 12px; }
tr.click { cursor: pointer; }
tr.click:hover { background: var(--secondary-background-color, rgba(127,127,127,.06)); }
.empty { color: var(--secondary-text-color); padding: 16px 0; text-align: center; }
.err { color: var(--error-color, #db4437); }
canvas, svg.chart { width: 100%; display: block; }
details > summary { cursor: pointer; color: var(--primary-color); margin: 8px 0; }
.hw { font-size: 13px; color: var(--secondary-text-color); }
.hw a { cursor: pointer; }
@media (max-width: 600px) { .grid { grid-template-columns: 1fr; gap: 12px; } .card { padding: 12px; } .big { font-size: 32px; } }
`;

/** Tagged template that escapes interpolations unless wrapped in raw(). */
export function html(strings, ...values) {
  let out = strings[0];
  values.forEach((v, i) => { out += render(v) + strings[i + 1]; });
  return out;
}
const RAW = Symbol("raw");
export const raw = (s) => ({ [RAW]: String(s == null ? "" : s) });
function render(v) {
  if (v == null || v === false) return "";
  if (Array.isArray(v)) return v.map(render).join("");
  if (typeof v === "object" && RAW in v) return v[RAW];
  return String(v).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
}
