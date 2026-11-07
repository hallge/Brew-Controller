// Formatters ported from the controller page (src/net/WebPage.h:682-703). Keep them identical in
// behaviour: the panel must show the same numbers as the controller.

/** Escape text for innerHTML (WebPage.h:658). */
export const esc = (s) => String(s == null ? "" : s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

// --- dates (YYYYMMDD integers on the wire) ---------------------------------------------------
/** "2026-11-07" -> 20261107 (WebPage.h:683). */
export const d2i = (v) => (v ? +String(v).replace(/-/g, "") : 0);
/** 20261107 -> "2026-11-07". */
export const i2d = (n) => (n ? String(n).padStart(8, "0").replace(/(\d{4})(\d\d)(\d\d)/, "$1-$2-$3") : "");
/** Today as YYYYMMDD by the browser's clock. */
export const today = () => { const d = new Date(); return d.getFullYear() * 10000 + (d.getMonth() + 1) * 100 + d.getDate(); };
/** Whole days since a YYYYMMDD date, or null (WebPage.h:684). */
export const age = (n) => (n ? Math.floor((Date.now() - new Date(i2d(n)).getTime()) / 864e5) : null);
/** Days a coffee spent in the freezer (WebPage.h:685). */
export const frozenFor = (c) => {
  if (!c || !c.frozenDate) return 0;
  const a = new Date(i2d(c.frozenDate)).getTime();
  const z = c.thawDate ? new Date(i2d(c.thawDate)).getTime() : Date.now();
  return Math.max(0, Math.floor((Math.min(z, Date.now()) - a) / 864e5));
};
/** "Roasted 12 days ago (3 in the freezer, so about 9 days of ageing)" (WebPage.h:686). */
export const ageNote = (c) => {
  const d = c ? age(c.roastDate) : null;
  if (d == null) return "";
  const f = frozenFor(c);
  return "Roasted " + d + " days ago" + (f ? " (" + f + " in the freezer" + (c.frozenDate && !c.thawDate ? ", still there" : "") + ", so about " + Math.max(0, d - Math.round(f * 0.9)) + " days of ageing)" : "");
};
/** Seconds-ago in words (WebPage.h:703). `s` is epoch seconds. */
export const ago = (s) => {
  if (!s) return "never";
  const d = Math.round(Date.now() / 1000 - s);
  return d < 90 ? "just now" : d < 5400 ? Math.round(d / 60) + " min ago" : d < 129600 ? Math.round(d / 3600) + " h ago" : Math.round(d / 86400) + " days ago";
};
export const f1 = (v) => (+v).toFixed(1);
export const f2 = (v) => (+v).toFixed(2);
/** Cents to money text in the browser's locale. */
export const money = (cents, currency) => {
  if (cents == null) return "";
  try { return (cents / 100).toLocaleString(undefined, { style: "currency", currency: currency || "USD" }); } catch (e) { return (cents / 100).toFixed(2); }
};

// --- grind settings (stored steps on the wire; display depends on the grinder) -----------------
// WebPage.h:688-698. `g` is the grinder object from /api/library (`divisor`, `subSteps`, `unit`).
const dpl = (d) => (d == 2 || d == 5 || d == 10 ? 1 : 2);
export const grindOf = (library) => {
  const g = (library && library.grinder) || {};
  // The controller stores "um" for microns and shows "µm" (WebPage.h:876).
  return { divisor: g.divisor || 1, subSteps: g.subSteps || 0, unit: !g.unit || g.unit == "um" ? "µm" : g.unit };
};
/** Display a stored setting, e.g. 35 -> "3.5" (divisor 10) or "4:8" (9 sub-steps). */
export const fsn = (g, v) => {
  if (!v) return "";
  const d = g.divisor || 1, r = g.subSteps || 0;
  if (r >= 2) return (Math.floor((v - 1) / r) + 1) + ":" + ((v - 1) % r + 1);
  if (d > 1) { const p = dpl(d), sc = p == 1 ? 10 : 100; return Math.floor(v / d) + "." + String((v % d) * (sc / d)).padStart(p, "0"); }
  return String(v);
};
/** Display a difference between settings (always positive). */
export const fdn = (g, v) => {
  v = Math.abs(Math.round(v));
  const d = g.divisor || 1;
  if ((g.subSteps || 0) < 2 && d > 1) { const p = dpl(d), sc = p == 1 ? 10 : 100; return Math.floor(v / d) + "." + String((v % d) * (sc / d)).padStart(p, "0"); }
  return String(v);
};
/** A fractional amount of settings, trimmed. */
export const fdf = (g, x) => { const d = (g.subSteps || 0) >= 2 ? 1 : (g.divisor || 1); return (+x / d).toFixed(2).replace(/\.?0+$/, ""); };
/** Unit for differences. */
export const fdu = (g) => ((g.subSteps || 0) >= 2 ? "steps" : g.unit || "µm");
/** Setting with its unit. */
export const fs = (g, v) => fsn(g, v) + " " + (g.unit || "µm");
/** Parse what the user typed into stored steps (1-2000), or null (WebPage.h:694-697). */
export const ps = (g, t) => {
  t = String(t == null ? "" : t).trim();
  if (!t) return null;
  const d = g.divisor || 1, r = g.subSteps || 0;
  let v;
  if (r >= 2) {
    const m = t.match(/^(\d+)(?:\s*[:\/-]\s*(\d+))?$/);
    if (!m) return null;
    const a = +m[1], b = m[2] === undefined ? 1 : +m[2];
    if (a < 1 || b < 1 || b > r) return null;
    v = (a - 1) * r + b;
  } else {
    const x = parseFloat(t.replace(",", "."));
    if (!isFinite(x) || !/^[\d.,]+$/.test(t)) return null;
    v = Math.floor(x * d + 0.5);
  }
  return v >= 1 && v <= 2000 ? v : null;
};

// --- library helpers (WebPage.h:731-732) -----------------------------------------------------
export const coffeeById = (L, id) => (L && L.coffees || []).find((c) => c.id == id);
export const recipeById = (L, id) => (L && L.recipes || []).find((r) => r.id == id);
export const presetLabel = (L, p) => { const c = coffeeById(L, p.coffeeId), r = recipeById(L, p.recipeId); return (c ? c.name + " — " : "") + (r ? r.name : "?"); };

// --- machine states in words (src/core/Types.h:84-105) -------------------------------------------
export const STATE_WORDS = {
  BOOT: "Starting", INITIALIZING: "Starting", CONNECTING_SCALE: "Connecting scale", READY: "Ready",
  TARING: "Taring", WAITING_FOR_CUP: "Waiting for the cup", READY_TO_BREW: "Ready to brew",
  STARTING_SHOT: "Starting the shot", BREWING: "Brewing", BLOOMING: "Blooming", STOPPING_SHOT: "Stopping",
  POST_SHOT_DRIP: "Drip", SHOT_COMPLETE: "Shot complete", SCALE_DISCONNECTED: "Scale disconnected",
  SWITCHBOT_DISCONNECTED: "Brew bot disconnected", SCALE_DATA_STALE: "Scale not reading",
  START_COMMAND_FAILED: "Start failed", STOP_COMMAND_FAILED: "Stop failed", TARE_FAILED: "Tare failed",
  SHOT_TIMEOUT: "Shot timed out", INVALID_WEIGHT: "Weight not valid", BLE_ERROR: "Bluetooth error",
};
export const stateWords = (s) => STATE_WORDS[s] || (s ? String(s).replace(/_/g, " ").toLowerCase() : "");
