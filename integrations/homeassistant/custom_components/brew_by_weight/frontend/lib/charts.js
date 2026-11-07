// Canvas charts for the Brew panel, ported from the controller page (src/net/WebPage.h:929-934,
// 1501-1602, 1621-1799, 1854-1861). Colours of the chart lines are the controller's; the frame
// (text, grid, empty cells) comes from Home Assistant's theme so light and dark both read well.
import { esc, f1, f2 } from "./format.js";

export const STC = { ok: "#4cc38a", fast: "#f5c451", slow: "#ff6b5e" };            // WebPage.h:1530
export const CCOL = ["#f59e0b", "#38bdf8", "#a78bfa", "#f472b6", "#34d399", "#facc15", "#fb7185", "#60a5fa", "#2dd4bf", "#c084fc"]; // :1501
export const RL = ["", "light", "medium-light", "medium", "medium-dark", "dark"];  // :1113
export const FLOW = "#f08a24";

/** Theme colours for chart frames, read from Home Assistant's CSS variables on `el`. */
export function theme(el) {
  const st = getComputedStyle(el);
  const v = (n, d) => (st.getPropertyValue(n).trim() || d);
  const fg = v("--primary-text-color", "#212121");
  const dim = v("--secondary-text-color", "#727272");
  const card = v("--card-background-color", "#ffffff");
  return {
    fg, dim, mid: fg, edge: v("--divider-color", "rgba(127,127,127,.4)"),
    grid: "rgba(127,127,127,.18)", gridLo: "rgba(127,127,127,.10)", empty: "rgba(127,127,127,.10)",
    stripe: "rgba(127,127,127,.05)", cardRgb: rgbOf(card),
  };
}
function rgbOf(c) {
  const m = /^#([0-9a-f]{6})$/i.exec(c);
  if (m) return [0, 2, 4].map((k) => parseInt(m[1].slice(k, k + 2), 16));
  const r = /rgba?\((\d+),\s*(\d+),\s*(\d+)/.exec(c);
  return r ? [+r[1], +r[2], +r[3]] : [38, 42, 47];
}

/** Size a canvas to its box at the screen's pixel ratio; draw in CSS pixels. */
export function prepare(canvas, height) {
  const dpr = window.devicePixelRatio || 1;
  const W = Math.max(200, canvas.clientWidth || canvas.parentElement.clientWidth || 600);
  const H = height || canvas.clientHeight || 220;
  canvas.style.height = H + "px";
  canvas.width = Math.round(W * dpr);
  canvas.height = Math.round(H * dpr);
  const x = canvas.getContext("2d");
  x.setTransform(dpr, 0, 0, dpr, 0, 0);
  x.clearRect(0, 0, W, H);
  x.font = "12px sans-serif";
  return { x, W, H };
}

/** Round axis values (WebPage.h:1509-1510). */
export function niceTicks(lo, hi, n, minStep) {
  const span = hi - lo;
  if (!(span > 0)) return [lo];
  const raw = span / n, p = Math.pow(10, Math.floor(Math.log10(raw))), f = raw / p;
  const step = Math.max(minStep || 0, (f < 1.5 ? 1 : f < 3 ? 2 : f < 7 ? 5 : 10) * p), out = [];
  for (let v = Math.ceil(lo / step) * step; v <= hi + 1e-9; v += step) out.push(+v.toFixed(6));
  return out;
}

/** Fresh (blue) to old (red) (WebPage.h:1610). */
export function ramp(t) {
  t = Math.max(0, Math.min(1, t));
  const a = [[56, 189, 248], [52, 211, 153], [250, 204, 21], [249, 115, 22], [239, 68, 68]];
  const v = t * (a.length - 1), i = Math.min(a.length - 2, Math.floor(v)), f = v - i;
  return "rgb(" + a[i].map((c, k) => Math.round(c + (a[i + 1][k] - c) * f)) + ")";
}
/** A count, from the card colour to the accent (WebPage.h:1611). */
export function heat(t, T) {
  const a = T.cardRgb, b = [240, 138, 36];
  t = Math.max(0, Math.min(1, t));
  return "rgb(" + a.map((c, k) => Math.round(c + (b[k] - c) * t)) + ")";
}

export function emptyNote(canvas, text, T, height = 120) {
  const { x } = prepare(canvas, height);
  x.fillStyle = T.dim;
  x.font = "14px sans-serif";
  x.fillText(text, 16, 40);
  canvas._cells = [];
}

/**
 * Hover tips and clicks for rectangles a chart drew (WebPage.h:1613-1618).
 * `cells`: [{x,y,w,h,txt,click}] in CSS pixels. `tip` is a positioned element in the same root.
 */
export function attachTips(canvas, cells, tip, onClick) {
  canvas._cells = cells;
  canvas._onClick = onClick;
  if (canvas._tipsWired) return;
  canvas._tipsWired = true;
  const at = (e) => {
    const r = canvas.getBoundingClientRect(), px = e.clientX - r.left, py = e.clientY - r.top;
    return (canvas._cells || []).find((q) => px >= q.x && px < q.x + q.w && py >= q.y && py < q.y + q.h);
  };
  const show = (q, e) => {
    if (!q || !q.txt) { tip.style.display = "none"; canvas.style.cursor = ""; return; }
    tip.innerHTML = q.txt;
    tip.style.display = "block";
    tip.style.left = Math.max(8, Math.min(window.innerWidth - tip.offsetWidth - 8, e.clientX + 12)) + "px";
    tip.style.top = Math.min(window.innerHeight - tip.offsetHeight - 8, e.clientY + 14) + "px";
    canvas.style.cursor = canvas._onClick && q.click ? "pointer" : "";
  };
  canvas.addEventListener("pointermove", (e) => { if (e.pointerType === "mouse") show(at(e), e); });
  canvas.addEventListener("pointerleave", () => { tip.style.display = "none"; });
  canvas.addEventListener("click", (e) => {
    const q = at(e);
    if (!q) return show(null, e);
    if (e.pointerType !== "mouse" && tip._for !== q) { tip._for = q; show(q, e); return; }
    tip._for = null;
    tip.style.display = "none";
    if (canvas._onClick && q.click) canvas._onClick(q);
  });
}
/** Dots become 12 px cells for attachTips. */
export const dotCells = (dots) => dots.map((d) => ({ x: d.x - 6, y: d.y - 6, w: 12, h: 12, txt: d.txt, click: d.click }));
export const TIP_CSS = `.tip{position:fixed;display:none;z-index:20;max-width:280px;padding:8px 10px;border-radius:8px;font-size:13px;line-height:1.35;
  background:var(--card-background-color,#fff);color:var(--primary-text-color);border:1px solid var(--divider-color);box-shadow:0 4px 14px rgba(0,0,0,.25);pointer-events:none}
.tip .dim{color:var(--secondary-text-color)}`;

// ---------------------------------------------------------------------------------------------
/** One shot: weight (solid), flow (orange, own scale), target, first drops and stop, a reference
 *  shot dashed green (WebPage.h:1854-1861). */
export function shotChart(canvas, s, T, W, F, REF, Th, height = 220) {
  const { x, W: cw, H: ch } = prepare(canvas, height);
  if (!T || T.length < 2) { x.fillStyle = Th.dim; x.fillText("No readings stored for this shot.", 16, 30); return; }
  const fv = F ? F.filter((v) => v != null) : [];
  const tm = Math.max(T[T.length - 1], REF ? REF.T[REF.T.length - 1] : 0, 1);
  const wm = Math.max((s.target_weight_g || 0) * 1.1, ...W, ...(REF ? REF.W : [0]));
  const fm = Math.max(1, ...fv), R = 52;
  const X = (t) => 34 + t / tm * (cw - 34 - R), Y = (w) => ch - 20 - Math.max(w, 0) / wm * (ch - 32), YF = (v) => ch - 20 - Math.max(v, 0) / fm * (ch - 32);
  x.lineWidth = 1; x.strokeStyle = "#6b7280"; x.setLineDash([4, 6]);
  x.beginPath(); x.moveTo(34, Y(s.target_weight_g)); x.lineTo(cw - R, Y(s.target_weight_g)); x.stroke();
  x.fillStyle = Th.dim; x.strokeStyle = Th.edge;
  [[s.first_drop_s, "first drops"], [s.shot_time_s, "stop"]].forEach((m) => {
    if (!(m[0] > 0 && m[0] <= tm)) return;
    x.beginPath(); x.moveTo(X(m[0]), 10); x.lineTo(X(m[0]), ch - 20); x.stroke();
    x.fillText(m[1], X(m[0]) + 4, ch - 26);
  });
  x.setLineDash([]);
  if (F) {
    x.strokeStyle = FLOW; x.lineWidth = 2; x.beginPath(); let pen = false;
    T.forEach((t, i) => { if (F[i] == null) { pen = false; return; } pen ? x.lineTo(X(t), YF(F[i])) : x.moveTo(X(t), YF(F[i])); pen = true; });
    x.stroke();
  }
  if (REF) {
    x.strokeStyle = STC.ok; x.lineWidth = 1.5; x.setLineDash([6, 5]); x.beginPath();
    REF.T.forEach((t, i) => (i ? x.lineTo(X(t), Y(REF.W[i])) : x.moveTo(X(t), Y(REF.W[i]))));
    x.stroke(); x.setLineDash([]);
  }
  x.strokeStyle = Th.fg; x.lineWidth = 2.5; x.beginPath();
  T.forEach((t, i) => (i ? x.lineTo(X(t), Y(W[i])) : x.moveTo(X(t), Y(W[i]))));
  x.stroke();
  x.fillStyle = Th.fg; x.fillText(wm.toFixed(0) + " g", 2, 16);
  x.fillStyle = Th.dim; x.fillText(tm.toFixed(0) + " s", cw - R - 28, ch - 4);
  x.fillStyle = FLOW; x.fillText(fm.toFixed(1) + " g/s", cw - R + 6, 16);
}

/** Several shots: weight solid, flow dashed on its own scale, optionally lined up at first drops
 *  (WebPage.h:1586-1601). rows: [{s, d:{T,W,F}, col}]. */
export function compareChart(canvas, rows, { align, flow }, Th, height = 260) {
  const { x, W: cw, H: ch } = prepare(canvas, height);
  const off = (r) => (align && r.s.first_drop_s > 0 ? r.s.first_drop_s : 0);
  let t0 = 0, t1 = 1, wm = 1, fm = 1;
  rows.forEach((r) => {
    if (!r.d) return;
    const o = off(r);
    t0 = Math.min(t0, -o); t1 = Math.max(t1, r.d.T[r.d.T.length - 1] - o);
    wm = Math.max(wm, ...r.d.W);
    if (r.d.F) fm = Math.max(fm, ...r.d.F.filter((v) => v != null));
  });
  wm *= 1.05; fm *= 1.1;
  const L0 = 50, R0 = cw - (flow ? 50 : 20), B0 = ch - 24;
  const X = (t) => L0 + (t - t0) / (t1 - t0) * (R0 - L0), Y = (w) => B0 - Math.max(w, 0) / wm * (B0 - 10), YF = (v) => B0 - Math.max(v, 0) / fm * (B0 - 10);
  x.lineWidth = 1;
  niceTicks(0, wm, 4, 1).forEach((v) => { const y = Math.round(Y(v)) + 0.5; x.strokeStyle = Th.grid; x.beginPath(); x.moveTo(L0, y); x.lineTo(R0, y); x.stroke(); x.fillStyle = Th.dim; x.fillText(v + " g", 6, y + 4); });
  niceTicks(t0, t1, 6, 1).forEach((v) => {
    const gx = Math.round(X(v)) + 0.5; x.strokeStyle = Th.grid; x.beginPath(); x.moveTo(gx, 10); x.lineTo(gx, B0); x.stroke();
    const lab = v + " s"; x.fillStyle = Th.dim; x.fillText(lab, Math.min(R0 - x.measureText(lab).width, Math.max(L0, gx - x.measureText(lab).width / 2)), ch - 6);
  });
  if (flow) niceTicks(0, fm, 4, 0.5).forEach((v) => { x.fillStyle = Th.dim; x.fillText(v + " g/s", R0 + 6, Math.round(YF(v)) + 4); });
  if (align) { x.strokeStyle = Th.edge; x.setLineDash([3, 5]); x.beginPath(); x.moveTo(X(0), 10); x.lineTo(X(0), B0); x.stroke(); x.setLineDash([]); x.fillStyle = Th.dim; x.fillText("first drops", X(0) + 4, 22); }
  x.strokeStyle = Th.edge; x.beginPath(); x.moveTo(L0, 10); x.lineTo(L0, B0); x.lineTo(R0, B0); x.stroke();
  const line = (T, V, o, f) => { x.beginPath(); let on = false; T.forEach((t, i) => { const v = V[i]; if (v == null) { on = false; return; } const px = X(t - o), py = f(v); if (on) x.lineTo(px, py); else { x.moveTo(px, py); on = true; } }); x.stroke(); };
  rows.forEach((r) => {
    if (!r.d) return;
    const o = off(r);
    x.strokeStyle = r.col; x.lineWidth = 2; x.setLineDash([]); line(r.d.T, r.d.W, o, Y);
    if (flow && r.d.F) { x.lineWidth = 1.5; x.setLineDash([5, 4]); line(r.d.T, r.d.F, o, YF); x.setLineDash([]); }
  });
  x.lineWidth = 1;
}

/** Brew time against grind setting with the on-time band, the advisor's line and the advice
 *  (WebPage.h:1556-1579). pts: [{u,t,c,col,cur,txt}]. Returns cells for tips. */
export function grindScatter(canvas, pts, o, Th, height = 260) {
  const { x, W: cw, H: ch } = prepare(canvas, height);
  const { tg, tol, ups, tc, uc, adv, fsn, fs } = o;
  const us = pts.map((q) => q.u);
  const lo = Math.min(...us, uc || us[0], adv || us[0]) - 10, hi = Math.max(...us, uc || us[0], adv || us[0]) + 10;
  const ts = pts.map((q) => q.t), t0 = Math.min(...ts, tg - tol) - 3, t1 = Math.max(...ts, tg + tol) + 3;
  const L0 = 50, R0 = cw - 20, B0 = ch - 26, X = (u) => L0 + (u - lo) / (hi - lo) * (R0 - L0), Y = (t) => B0 - (t - t0) / (t1 - t0) * (ch - 40);
  x.lineWidth = 1;
  niceTicks(t0, t1, 4, 1).forEach((v) => { const y = Math.round(Y(v)) + 0.5; x.strokeStyle = Th.grid; x.beginPath(); x.moveTo(L0, y); x.lineTo(R0, y); x.stroke(); x.fillStyle = Th.dim; x.fillText(v + " s", 6, y + 4); });
  niceTicks(lo, hi, 6, 1).forEach((v) => {
    const gx = Math.round(X(v)) + 0.5; x.strokeStyle = Th.grid; x.beginPath(); x.moveTo(gx, 10); x.lineTo(gx, B0); x.stroke();
    const lab = fsn(Math.round(v)); x.fillStyle = Th.dim; x.fillText(lab, Math.min(R0 - x.measureText(lab).width, gx - x.measureText(lab).width / 2), ch - 8);
  });
  x.fillStyle = "rgba(76,195,138,.16)"; x.fillRect(L0, Y(tg + tol), R0 - L0, Y(tg - tol) - Y(tg + tol));
  x.strokeStyle = STC.ok; x.setLineDash([4, 6]); x.beginPath(); x.moveTo(L0, Y(tg)); x.lineTo(R0, Y(tg)); x.stroke(); x.setLineDash([]);
  x.strokeStyle = Th.edge; x.beginPath(); x.moveTo(L0, 10); x.lineTo(L0, B0); x.lineTo(R0, B0); x.stroke();
  const inT = (t) => t >= t0 && t <= t1;
  if (ups > 0 && tc > 0 && uc) {
    const tAt = (u) => tc - (u - uc) / ups;
    let ua = lo, ub = hi; const ta = tAt(ua), tb = tAt(ub);
    if (!inT(ta)) ua = uc + (tc - (ta > t1 ? t1 : t0)) * ups;
    if (!inT(tb)) ub = uc + (tc - (tb < t0 ? t0 : t1)) * ups;
    x.save(); x.strokeStyle = Th.fg; x.globalAlpha = 0.55; x.setLineDash([6, 5]); x.lineWidth = 1.5;
    x.beginPath(); x.moveTo(X(ua), Y(tAt(ua))); x.lineTo(X(ub), Y(tAt(ub))); x.stroke(); x.restore();
    x.strokeStyle = Th.edge; x.beginPath(); x.moveTo(X(uc), 10); x.lineTo(X(uc), B0); x.stroke();
    x.fillStyle = Th.dim; x.fillText("now " + fsn(uc), X(uc) + 4, 22);
  }
  if (adv) {
    const ax = X(adv), ay = Y(tg);
    x.fillStyle = FLOW; x.beginPath(); x.moveTo(ax, ay - 8); x.lineTo(ax + 8, ay); x.lineTo(ax, ay + 8); x.lineTo(ax - 8, ay); x.closePath(); x.fill();
    const lab = "advice " + fs(adv); x.font = "bold 12px sans-serif"; x.fillText(lab, Math.min(R0 - x.measureText(lab).width, ax + 10), ay - 10); x.font = "12px sans-serif";
  }
  const dots = [];
  pts.map((q, i) => i).sort((a, b) => (pts[a].cur ? 1 : 0) - (pts[b].cur ? 1 : 0)).forEach((i) => {
    const q = pts[i], px = X(q.u) + ((i * 37) % 13 - 6), py = Y(q.t);
    x.fillStyle = q.col; x.globalAlpha = q.cur ? 1 : 0.85; x.beginPath(); x.arc(px, py, q.cur ? 5.5 : 4.5, 0, 7); x.fill(); x.globalAlpha = 1;
    dots.push({ x: px, y: py, txt: q.txt, click: q.id });
  });
  return dotCells(dots);
}

/** Grind setting by shot, coloured by on time / fast / slow (simplified from WebPage.h:1531-1555). */
export function grindByShot(canvas, pts, o, Th, height = 200) {
  const { x, W: cw, H: ch } = prepare(canvas, height);
  const us = pts.map((q) => q.u), lo = Math.min(...us) - 4, hi = Math.max(...us) + 4;
  const L0 = 50, R0 = cw - 12, B0 = ch - 22, X = (i) => L0 + (pts.length == 1 ? 0.5 : i / (pts.length - 1)) * (R0 - L0), Y = (u) => B0 - (u - lo) / (hi - lo) * (B0 - 10);
  niceTicks(lo, hi, 4, 1).forEach((v) => { const y = Math.round(Y(v)) + 0.5; x.strokeStyle = Th.grid; x.beginPath(); x.moveTo(L0, y); x.lineTo(R0, y); x.stroke(); x.fillStyle = Th.dim; x.fillText(o.fsn(Math.round(v)), 6, y + 4); });
  x.strokeStyle = Th.edge; x.beginPath(); x.moveTo(L0, 10); x.lineTo(L0, B0); x.lineTo(R0, B0); x.stroke();
  x.strokeStyle = Th.dim; x.globalAlpha = 0.5; x.beginPath();
  pts.forEach((q, i) => { const px = X(i), py = Y(q.u); if (!i) x.moveTo(px, py); else { x.lineTo(px, Y(pts[i - 1].u)); x.lineTo(px, py); } });
  x.stroke(); x.globalAlpha = 1;
  const dots = [];
  pts.forEach((q, i) => {
    const px = X(i), py = Y(q.u);
    x.fillStyle = STC[q.k]; x.beginPath(); x.arc(px, py, q.cur ? 4.5 : 3.5, 0, 7); x.fill();
    if (q.r >= 4) { x.strokeStyle = Th.fg; x.lineWidth = 2; x.beginPath(); x.arc(px, py, 7, 0, 7); x.stroke(); x.lineWidth = 1; }
    dots.push({ x: px, y: py, txt: q.txt, click: q.id });
  });
  return dotCells(dots);
}

/** Taste radar (WebPage.h:929-934). vals and known are 0..1. */
export function radar(canvas, labels, vals, known, Th, height = 260) {
  const { x, W, H } = prepare(canvas, height);
  const cx = W / 2, cy = H / 2 + 4, R = Math.min(W, H) / 2 - 46, n = labels.length;
  x.font = "13px sans-serif";
  const P = (i, r) => [cx + r * Math.sin(2 * Math.PI * i / n), cy - r * Math.cos(2 * Math.PI * i / n)];
  [0.25, 0.5, 0.75, 1].forEach((f) => {
    x.beginPath();
    for (let i = 0; i <= n; i++) { const [a, b] = P(i % n, R * f); i ? x.lineTo(a, b) : x.moveTo(a, b); }
    x.strokeStyle = f == 0.5 ? Th.edge : Th.grid; x.lineWidth = f == 0.5 ? 1.5 : 1; x.stroke();
  });
  x.lineWidth = 1;
  labels.forEach((l, i) => {
    const [a, b] = P(i, R); x.strokeStyle = Th.grid; x.beginPath(); x.moveTo(cx, cy); x.lineTo(a, b); x.stroke();
    const [lx, ly] = P(i, R + 22); x.fillStyle = known[i] > 0.05 ? Th.fg : Th.dim;
    const w = x.measureText(l).width; x.fillText(l, Math.max(2, Math.min(W - w - 2, lx - w / 2)), ly + 5);
  });
  x.beginPath(); vals.forEach((v, i) => { const [a, b] = P(i, R * v); i ? x.lineTo(a, b) : x.moveTo(a, b); }); x.closePath();
  x.fillStyle = "rgba(240,138,36,.22)"; x.fill(); x.strokeStyle = FLOW; x.lineWidth = 2; x.stroke(); x.lineWidth = 1;
  vals.forEach((v, i) => { const [a, b] = P(i, R * v); x.globalAlpha = 0.35 + 0.65 * Math.min(1, known[i]); x.fillStyle = FLOW; x.beginPath(); x.arc(a, b, 4, 0, 7); x.fill(); x.globalAlpha = 1; });
}

// --- Insights helpers ---------------------------------------------------------------------------
export const ymd = (n) => (n && n > 19000101 ? new Date(Math.floor(n / 10000), Math.floor(n / 100) % 100 - 1, n % 100) : null);
/** Days off roast at the shot (WebPage.h:1606). */
export function ageDays(s) { const r = ymd(s.roast_date); if (!r || !s.timestamp) return null; const d = Math.floor((new Date(s.timestamp) - r) / 864e5); return d >= 0 && d < 1000 ? d : null; }
export const btime = (s) => (s.time_basis == "pour" ? s.pour_time_s : s.shot_time_s);
export const byWeight = (s) => /^weight/.test(s.end_reason || "");
export const when = (s) => (s.timestamp ? new Date(s.timestamp).toLocaleString([], { month: "short", day: "numeric", hour: "numeric", minute: "2-digit" }) : "#" + s.id);

/** Roast level rows against grind setting; colour by days off roast, size by pour flow; the
 *  on-time band per level (WebPage.h:1627-1649). Returns {cells, bands, amax}. */
export function roastGrind(canvas, pts, o, Th, height = 260) {
  const { x, W: cw, H: ch } = prepare(canvas, height);
  const lv = [...new Set(pts.map((s) => RL.indexOf(s.roast_level)))].sort((a, b) => a - b);
  const us = pts.map((s) => s.grind_um), lo = Math.min(...us), hi = Math.max(...us), pad = Math.max(2, (hi - lo) * 0.1), u0 = lo - pad, u1 = hi + pad;
  const ages = pts.map(ageDays).filter((a) => a != null), amax = Math.max(30, ...ages), fmax = Math.max(0.1, ...pts.map((s) => s.pour_flow_g_s || 0));
  const L0 = 110, R0 = cw - 20, T0 = 12, B0 = ch - 26, X = (u) => L0 + (u - u0) / (u1 - u0) * (R0 - L0), rowH = (B0 - T0) / lv.length, Y = (l) => T0 + rowH * (lv.indexOf(l) + 0.5);
  lv.forEach((l) => { const y = Math.round(Y(l)) + 0.5; x.strokeStyle = Th.grid; x.beginPath(); x.moveTo(L0, y); x.lineTo(R0, y); x.stroke(); x.fillStyle = Th.dim; x.fillText(RL[l], 6, y + 4); });
  niceTicks(u0, u1, 6, 1).forEach((v) => { const gx = Math.round(X(v)) + 0.5; x.strokeStyle = Th.gridLo; x.beginPath(); x.moveTo(gx, T0); x.lineTo(gx, B0); x.stroke(); const lab = o.fsn(Math.round(v)); x.fillStyle = Th.dim; x.fillText(lab, Math.min(R0 - x.measureText(lab).width, gx - x.measureText(lab).width / 2), ch - 8); });
  x.strokeStyle = Th.edge; x.beginPath(); x.moveTo(L0, T0); x.lineTo(L0, B0); x.lineTo(R0, B0); x.stroke();
  const tol = o.tol, bands = [];
  lv.forEach((l) => {
    const g = pts.filter((s) => RL.indexOf(s.roast_level) == l && s.target_time_s > 0 && btime(s) > 0 && Math.abs(btime(s) - s.target_time_s) <= tol).map((s) => s.grind_um).sort((a, b) => a - b);
    if (!g.length) return;
    const q = (f) => g[Math.min(g.length - 1, Math.max(0, Math.round(f * (g.length - 1))))];
    const blo = g.length >= 5 ? q(0.1) : g[0], bhi = g.length >= 5 ? q(0.9) : g[g.length - 1], md = q(0.5), y = Y(l), bx = X(blo) - 4, bw = Math.max(8, X(bhi) - X(blo) + 8);
    x.fillStyle = "rgba(76,195,138,.16)"; x.fillRect(bx, y - rowH * 0.38, bw, rowH * 0.76);
    x.strokeStyle = "rgba(76,195,138,.55)"; x.strokeRect(bx + 0.5, y - rowH * 0.38 + 0.5, bw - 1, rowH * 0.76 - 1);
    x.strokeStyle = STC.ok; x.lineWidth = 2; x.beginPath(); x.moveTo(X(md), y - rowH * 0.38); x.lineTo(X(md), y + rowH * 0.38); x.stroke(); x.lineWidth = 1;
    bands.push({ l, lo: blo, hi: bhi, md, n: g.length });
  });
  const dots = [];
  pts.forEach((s, i) => {
    const a = ageDays(s), px = X(s.grind_um), py = Y(RL.indexOf(s.roast_level)) + (((i * 37) % 17) - 8) * rowH / 40, rad = 3 + 4 * Math.min(1, (s.pour_flow_g_s || 0) / fmax);
    x.fillStyle = a == null ? "#6b7280" : ramp(a / amax); x.globalAlpha = 0.85; x.beginPath(); x.arc(px, py, rad, 0, 7); x.fill(); x.globalAlpha = 1;
    dots.push({ x: px, y: py, click: s.id, txt: `<b>${esc(s.coffee || "no coffee")}</b><br>${a != null ? a + " days off roast, " : ""}${esc(s.roast_level)}<br>${esc(o.fs(s.grind_um))}, ${f1(btime(s) || 0)} s<br><span class="dim">${esc(when(s))}</span>` });
  });
  return { cells: dotCells(dots), bands, amax, levels: lv.length };
}

/** Days off roast (weeks) against brew time minus target (2 s rows) (WebPage.h:1652-1670). */
export function ageHeat(canvas, pts, tol, Th, height = 240) {
  const { x, W: cw, H: ch } = prepare(canvas, height);
  const amax = Math.max(...pts.map(ageDays)), nx = Math.min(26, Math.floor(amax / 7) + 1), edges = [-Infinity, -6, -4, -2, 0, 2, 4, 6, Infinity], ny = edges.length - 1;
  const lab = (k) => (k == 0 ? "≤ −6 s" : k == ny - 1 ? "≥ +6 s" : (edges[k] > 0 ? "+" : "") + edges[k] + " to " + (edges[k + 1] > 0 ? "+" : "") + edges[k + 1] + " s");
  const cnt = [...Array(nx)].map(() => Array(ny).fill(0));
  pts.forEach((s) => { const a = Math.min(nx - 1, Math.floor(ageDays(s) / 7)), e = btime(s) - s.target_time_s; let k = 0; while (k < ny - 1 && e >= edges[k + 1]) k++; cnt[a][k]++; });
  const mx = Math.max(1, ...cnt.flat()), L0 = 96, R0 = cw - 12, T0 = 8, B0 = ch - 26, cwid = (R0 - L0) / nx, chh = (B0 - T0) / ny, cells = [];
  for (let k = 0; k < ny; k++) { const y = B0 - (k + 1) * chh; x.fillStyle = Th.dim; x.fillText(lab(k), 4, y + chh / 2 + 4); }
  for (let a = 0; a < nx; a++) {
    if (nx <= 10 || a % 2 == 0) { const l = (a * 7) + "-" + (a * 7 + 6) + " d"; x.fillStyle = Th.dim; x.fillText(l, L0 + a * cwid + cwid / 2 - x.measureText(l).width / 2, ch - 8); }
    for (let k = 0; k < ny; k++) {
      const v = cnt[a][k], cx = L0 + a * cwid, cy = B0 - (k + 1) * chh;
      x.fillStyle = v ? heat(0.2 + 0.8 * v / mx, Th) : Th.empty; x.fillRect(cx + 1, cy + 1, cwid - 2, chh - 2);
      if (v && cwid > 18) { const t = String(v); x.fillStyle = v / mx > 0.55 ? "#1d0f00" : Th.fg; x.fillText(t, cx + cwid / 2 - x.measureText(t).width / 2, cy + chh / 2 + 4); }
      cells.push({ x: cx, y: cy, w: cwid, h: chh, txt: `<b>${v} shot${v == 1 ? "" : "s"}</b><br>${a * 7} to ${a * 7 + 6} days off roast<br>brew time ${lab(k).replace(" s", "")} s from target` });
    }
  }
  const rows = []; for (let k = 0; k < ny; k++) if (edges[k] >= -tol - 1e-9 && edges[k + 1] <= tol + 1e-9) rows.push(k);
  if (rows.length) { const top = B0 - (Math.max(...rows) + 1) * chh, bot = B0 - Math.min(...rows) * chh; x.strokeStyle = STC.ok; x.lineWidth = 2; x.strokeRect(L0 + 0.5, top + 0.5, R0 - L0 - 1, bot - top - 1); x.lineWidth = 1; x.fillStyle = STC.ok; x.fillText("on time", R0 - 52, top - 3); }
  return { cells, mx };
}

/** Recipe tags: first letters, unique (WebPage.h:1675-1676). */
export function recipeTags(sh) {
  const k = new Map(); sh.forEach((s) => { const n = s.recipe || ""; k.set(n, (k.get(n) || 0) + 1); });
  const used = new Set(), m = new Map();
  [...k.keys()].sort((a, b) => k.get(b) - k.get(a)).forEach((n) => {
    const w = n.replace(/[^A-Za-z0-9]/g, "").toUpperCase() || "?";
    let t = [w[0], w.slice(0, 2), w.slice(0, 3)].find((c) => c && !used.has(c));
    for (let i = 2; !t; i++) if (!used.has(w[0] + i)) t = w[0] + i;
    used.add(t); m.set(n, t);
  });
  return m;
}
const WD = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"];
const dkey = (d) => d.getFullYear() + "-" + (d.getMonth() + 1) + "-" + d.getDate();

/** The last 26 weeks, a column a week: mode "shots" (count), "brew" (coffee stripes and recipe
 *  letters) or "drift" (average brew time against target) (WebPage.h:1678-1702). */
export function calendar(canvas, sh, mode, o, Th, height = 180) {
  const { x, W: cw, H: ch } = prepare(canvas, height);
  const tol = o.tol, L0 = 40, T0 = 22, cells = [];
  const today = new Date(); today.setHours(0, 0, 0, 0);
  const start = new Date(today); start.setDate(start.getDate() - ((today.getDay() + 6) % 7) - 25 * 7);
  const days = new Map();
  sh.forEach((s) => {
    const d = new Date(s.timestamp); if (d < start) return;
    const k = dkey(d); let v = days.get(k); if (!v) { v = { n: 0, es: 0, en: 0, ids: [], sh: [] }; days.set(k, v); }
    v.n++; v.ids.push(s.id); v.sh.push(s);
    if (byWeight(s) && s.target_time_s && btime(s) > 0) { v.es += btime(s) - s.target_time_s; v.en++; }
  });
  const mx = Math.max(1, ...[...days.values()].map((v) => v.n)), cwid = (cw - L0 - 8) / 26, chh = (ch - T0 - 6) / 7;
  let lastM = -1;
  const inR = [...days.values()].flatMap((v) => v.sh), cur = o.curCoffee || 0;
  const cids = [...new Set(inR.map((s) => s.coffee_id || 0))].sort((a, b) => (b == cur) - (a == cur) || a - b);
  const cc = new Map(cids.map((id, i) => [id, CCOL[i % CCOL.length]])), tg = recipeTags(inR);
  const hm = (s) => new Date(s.timestamp).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
  for (let w = 0; w < 26; w++) {
    const first = new Date(start); first.setDate(first.getDate() + w * 7);
    if (first.getMonth() != lastM) { lastM = first.getMonth(); x.fillStyle = Th.dim; x.fillText(first.toLocaleDateString([], { month: "short" }), L0 + w * cwid, 14); }
    for (let r = 0; r < 7; r++) {
      const d = new Date(first); d.setDate(d.getDate() + r); if (d > today) continue;
      const v = days.get(dkey(d)), cx = L0 + w * cwid, cy = T0 + r * chh; let col = Th.empty, what = "no shots";
      if (v) {
        if (mode == "brew") what = v.sh.map((s) => hm(s) + " " + esc(s.coffee || "no coffee") + " · " + esc(s.recipe || "no recipe")).join("<br>");
        else if (mode == "shots") { col = heat(0.2 + 0.8 * v.n / mx, Th); what = v.n + " shot" + (v.n == 1 ? "" : "s"); }
        else if (v.en) { const e = v.es / v.en; col = Math.abs(e) <= tol ? STC.ok : e < 0 ? STC.fast : STC.slow; x.globalAlpha = 0.35 + 0.65 * Math.min(1, Math.abs(e) / (3 * tol) + (Math.abs(e) <= tol ? 0.6 : 0)); what = v.n + " shot" + (v.n == 1 ? "" : "s") + ", brew time " + (e > 0 ? "+" : "") + f1(e) + " s from target on average"; }
        else { col = Th.edge; what = v.n + " shot" + (v.n == 1 ? "" : "s") + ", none timed against a target"; }
      }
      if (mode == "brew" && v) {
        const l = v.sh.slice(0, 4).reverse(), sw = (cwid - 2) / l.length;
        l.forEach((s, i) => { x.fillStyle = cc.get(s.coffee_id || 0); x.fillRect(cx + 1 + i * sw, cy + 1, sw, chh - 2); });
        const t = [...new Set(v.sh.map((s) => tg.get(s.recipe || "")))].join(" ");
        x.font = "bold 11px sans-serif"; x.fillStyle = "#111"; x.fillText(t, cx + cwid / 2 - x.measureText(t).width / 2, cy + chh / 2 + 4); x.font = "12px sans-serif";
      } else { x.fillStyle = col; x.fillRect(cx + 1, cy + 1, cwid - 2, chh - 2); }
      x.globalAlpha = 1;
      cells.push({ x: cx, y: cy, w: cwid, h: chh, click: v ? v.ids[0] : 0, txt: `<b>${d.toLocaleDateString([], { weekday: "short", month: "short", day: "numeric" })}</b><br>${what}${v ? '<br><span class="dim">' + (v.n > 1 ? "Click to open the last of them" : "Click to open it") + "</span>" : ""}` });
    }
  }
  for (let r = 0; r < 7; r += 2) { x.fillStyle = Th.dim; x.fillText(WD[r], 4, T0 + r * chh + chh / 2 + 4); }
  return { cells, mx, cc, cids, tg, inR };
}

/** Time of day: a row a day, dots per shot, the machine's power presses (WebPage.h:1709-1743). */
export function dayTimes(canvas, sh, pwr, rows, o, Th) {
  const RH = rows <= 14 ? 24 : rows <= 30 ? 19 : 12, T0 = 24, L0 = 92, H = T0 + rows * RH + 6;
  const { x, W: cw } = prepare(canvas, H);
  const R0 = cw - 12;
  const today = new Date(); today.setHours(0, 0, 0, 0);
  const first = new Date(today); first.setDate(first.getDate() - rows + 1);
  const inR = sh.filter((s) => new Date(s.timestamp) >= first), pw = (pwr || []).filter((e) => e.t * 1000 >= +first);
  const hrs = [...inR.map((s) => new Date(s.timestamp)), ...pw.map((e) => new Date(e.t * 1000))].map((d) => d.getHours() + d.getMinutes() / 60);
  let h0 = hrs.length ? Math.floor(Math.min(...hrs)) : 6, h1 = hrs.length ? Math.ceil(Math.max(...hrs) + 0.01) : 22;
  if (h1 - h0 < 6) { const m = (h0 + h1) / 2; h0 = Math.max(0, Math.floor(m - 3)); h1 = Math.min(24, h0 + 6); }
  const X = (h) => L0 + (h - h0) / (h1 - h0) * (R0 - L0), hod = (d) => d.getHours() + d.getMinutes() / 60 + d.getSeconds() / 3600;
  const rowOf = (d) => { const k = new Date(d); k.setHours(0, 0, 0, 0); return Math.round((today - k) / 864e5); };
  const cur = o.curCoffee || 0, cids = [...new Set(inR.map((s) => s.coffee_id || 0))].sort((a, b) => (b == cur) - (a == cur) || a - b);
  const cc = new Map(cids.map((id, i) => [id, CCOL[i % CCOL.length]])), tg = recipeTags(inR);
  const hm = (d) => d.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" }), cells = [];
  const step = h1 - h0 > 12 ? 2 : 1;
  for (let h = h0; h <= h1; h += step) {
    const gx = Math.round(X(h)) + 0.5; x.strokeStyle = Th.gridLo; x.beginPath(); x.moveTo(gx, T0 - 4); x.lineTo(gx, T0 + rows * RH); x.stroke();
    const t = String(h % 24).padStart(2, "0") + ":00"; x.fillStyle = Th.dim; x.fillText(t, Math.max(L0, Math.min(R0 - x.measureText(t).width, gx - x.measureText(t).width / 2)), 14);
  }
  for (let r = 0; r < rows; r++) {
    const d = new Date(today); d.setDate(d.getDate() - r); const y = T0 + r * RH;
    if (r % 2 == 0) { x.fillStyle = Th.stripe; x.fillRect(L0, y, R0 - L0, RH); }
    if (RH >= 19 || r % 2 == 0) { x.fillStyle = d.getDay() == 0 || d.getDay() == 6 ? Th.fg : Th.dim; x.fillText(WD[(d.getDay() + 6) % 7] + " " + d.toLocaleDateString([], { day: "numeric", month: "short" }), 4, y + RH / 2 + 4); }
  }
  const byRow = new Map();
  pw.forEach((e) => { const r = rowOf(new Date(e.t * 1000)); if (r < 0 || r >= rows) return; if (!byRow.has(r)) byRow.set(r, []); byRow.get(r).push(e); });
  byRow.forEach((es, r) => {
    es.sort((a, b) => a.t - b.t); let from = null;
    es.forEach((e) => { if (!e.ok) return; const h = hod(new Date(e.t * 1000)); if (e.on) from = from ?? h; else if (from != null) { x.fillStyle = "rgba(76,195,138,.13)"; x.fillRect(X(from), T0 + r * RH + 2, X(h) - X(from), RH - 4); from = null; } });
    if (from != null) { const endH = r == 0 ? Math.min(h1, hod(new Date())) : h1; if (endH > from) { x.fillStyle = "rgba(76,195,138,.13)"; x.fillRect(X(from), T0 + r * RH + 2, X(endH) - X(from), RH - 4); } }
  });
  pw.forEach((e) => {
    const d = new Date(e.t * 1000), r = rowOf(d); if (r < 0 || r >= rows) return;
    const px = X(hod(d)), cy = T0 + r * RH + RH / 2, z = Math.min(6, RH / 2 - 2);
    x.beginPath(); if (e.on) { x.moveTo(px, cy - z); x.lineTo(px - z, cy + z); x.lineTo(px + z, cy + z); } else { x.moveTo(px, cy + z); x.lineTo(px - z, cy - z); x.lineTo(px + z, cy - z); } x.closePath();
    const col = e.on ? STC.ok : Th.dim; if (e.ok) { x.fillStyle = col; x.fill(); } else { x.strokeStyle = col; x.lineWidth = 1.5; x.stroke(); x.lineWidth = 1; }
    cells.push({ x: px - z - 2, y: cy - z - 2, w: 2 * z + 4, h: 2 * z + 4, txt: `<b>Machine ${e.on ? "on" : "off"}${e.ok ? "" : ": not done"}</b><br>${hm(d)}, ${e.by == "schedule" ? "by the schedule" : e.by == "scale" ? "when the scale switched off" : "by you"}${e.ok ? "" : "<br>" + esc(e.outcome)}` });
  });
  const rad = Math.max(3, Math.min(9, RH / 2 - 2)), gap = 2 * rad + 1, lastX = new Map();
  inR.map((s) => ({ s, d: new Date(s.timestamp) })).sort((a, b) => a.d - b.d).forEach(({ s, d }) => {
    const r = rowOf(d); if (r < 0 || r >= rows) return;
    let px = X(hod(d)); const lx = lastX.get(r); if (lx != null && px - lx < gap) px = lx + gap; px = Math.min(px, R0 - rad); lastX.set(r, px);
    const cy = T0 + r * RH + RH / 2; x.fillStyle = cc.get(s.coffee_id || 0); x.beginPath(); x.arc(px, cy, rad, 0, 7); x.fill();
    if (rad >= 6) { const t = tg.get(s.recipe || ""); x.font = "bold " + Math.round(rad * 1.15) + "px sans-serif"; x.fillStyle = "#111"; x.fillText(t, px - x.measureText(t).width / 2, cy + rad * 0.42); x.font = "12px sans-serif"; }
    cells.push({ x: px - rad, y: cy - rad, w: 2 * rad, h: 2 * rad, click: s.id, txt: `<b>${hm(d)}</b> ${esc(s.coffee || "no coffee")}<br>${esc(s.recipe || "no recipe")}${s.final_weight_g ? ", " + f1(s.final_weight_g) + " g" : ""}<br><span class="dim">Click to open it</span>` });
  });
  return { cells: cells.reverse(), cc, cids, tg, inR, pw };
}

/** Bags in order: shots to the first on time, then on time / fast / slow (WebPage.h:1747-1775). */
export function dialIn(canvas, sh, tol, Th, height = 240) {
  const { x, W: cw, H: ch } = prepare(canvas, height);
  const bags = new Map();
  sh.forEach((s) => { const k = (s.coffee_id || 0) + "|" + (s.roast_date || 0); let b = bags.get(k); if (!b) { b = { n: s.coffee || "no coffee", rd: s.roast_date, shots: [] }; bags.set(k, b); } b.shots.push(s); });
  const list = [...bags.values()].slice(-12);
  const how = (s) => { const e = btime(s) - s.target_time_s; return Math.abs(e) <= tol ? "ok" : e < 0 ? "fast" : "slow"; };
  list.forEach((b) => {
    const i = b.shots.findIndex((s) => how(s) == "ok"); b.dial = i < 0 ? null : i + 1; b.total = b.shots.length;
    const after = i < 0 ? [] : b.shots.slice(i + 1);
    b.after = { ok: after.filter((s) => how(s) == "ok").length, fast: after.filter((s) => how(s) == "fast").length, slow: after.filter((s) => how(s) == "slow").length };
    const last = b.shots[b.shots.length - 1]; b.last = { how: how(last), e: btime(last) - last.target_time_s };
  });
  const mx = Math.max(...list.map((b) => b.total)), L0 = 40, R0 = cw - 12, T0 = 24, B0 = ch - 40, bw = (R0 - L0) / list.length, Y = (v) => B0 - v / mx * (B0 - T0), cells = [];
  niceTicks(0, mx, 4, 1).forEach((v) => { const y = Math.round(Y(v)) + 0.5; x.strokeStyle = Th.grid; x.beginPath(); x.moveTo(L0, y); x.lineTo(R0, y); x.stroke(); x.fillStyle = Th.dim; x.fillText(String(v), 8, y + 4); });
  const sign = (v) => (v > 0 ? "+" : v < 0 ? "−" : "") + f1(Math.abs(v));
  list.forEach((b, i) => {
    const bx = L0 + i * bw + bw * 0.15, w = bw * 0.7, d = b.dial == null ? b.total : b.dial;
    x.fillStyle = FLOW; x.fillRect(bx, Y(d), w, B0 - Y(d));
    if (b.dial == null) { x.save(); x.setLineDash([5, 4]); x.strokeStyle = Th.fg; x.lineWidth = 1.5; x.strokeRect(bx + 0.75, Y(d) + 0.75, w - 1.5, B0 - Y(d) - 1.5); x.restore(); }
    let at = d;
    [["ok", STC.ok], ["fast", STC.fast], ["slow", STC.slow]].forEach(([k, col]) => {
      const n = b.after[k]; if (!n) return;
      x.fillStyle = col; x.globalAlpha = 0.75; x.fillRect(bx, Y(at + n), w, Y(at) - Y(at + n)); x.globalAlpha = 1;
      if (Y(at) - Y(at + n) > 14 && w > 16) { const t = String(n); x.fillStyle = "#111"; x.fillText(t, bx + w / 2 - x.measureText(t).width / 2, Y(at + n) + (Y(at) - Y(at + n)) / 2 + 4); }
      at += n;
    });
    const lab = b.n.length > 14 ? b.n.slice(0, 13) + "…" : b.n, rd = ymd(b.rd);
    x.fillStyle = Th.fg; x.fillText(lab, bx + w / 2 - x.measureText(lab).width / 2, B0 + 14);
    if (rd) { const t = rd.toLocaleDateString([], { month: "short", day: "numeric" }); x.fillStyle = Th.dim; x.fillText(t, bx + w / 2 - x.measureText(t).width / 2, B0 + 28); }
    { const t = String(b.total); x.fillStyle = Th.fg; x.fillText(t, bx + w / 2 - x.measureText(t).width / 2, Y(b.total) - 5); }
    if (B0 - Y(d) > 16 && w > 16) { const t = b.dial == null ? "–" : String(b.dial); x.fillStyle = "#1d0f00"; x.fillText(t, bx + w / 2 - x.measureText(t).width / 2, B0 - 4); }
    const a = b.after, parts = [a.ok && a.ok + " on time", a.fast && a.fast + " fast", a.slow && a.slow + " slow"].filter(Boolean);
    cells.push({ x: bx, y: Y(b.total), w, h: B0 - Y(b.total), txt: `<b>${esc(b.n)}</b>${rd ? ", roasted " + rd.toLocaleDateString() : ""}<br>${b.dial == null ? "no shot on time yet, " + b.total + " shot" + (b.total == 1 ? "" : "s") : b.dial + " shot" + (b.dial == 1 ? "" : "s") + " to the first on time" + (parts.length ? "; after it: " + parts.join(", ") : "")}<br>latest shot: ${b.last.how == "ok" ? "on time" : b.last.how} (${sign(b.last.e)} s)` });
  });
  x.strokeStyle = Th.edge; x.beginPath(); x.moveTo(L0, T0); x.lineTo(L0, B0); x.lineTo(R0, B0); x.stroke();
  const done = list.filter((b) => b.dial != null).map((b) => b.dial), drift = list.filter((b) => b.dial != null && b.last.how != "ok").length;
  return { cells, n: list.length, avg: done.length ? done.reduce((a, b) => a + b, 0) / done.length : null, drift };
}

/** Grind advice against target: where the next shot landed after each advised change, and the
 *  confidence line (WebPage.h:1778-1799). `nameOf(coffee_id)`, `fs(setting)`. */
export function grindAccuracy(canvas, d, o, Th, height = 220) {
  const { x, W: cw, H: ch } = prepare(canvas, height);
  const ev = (d.events || []).filter((e) => e.target_s > 0), tol = d.tolerance_s || 2;
  const conf = (i) => { const f = []; for (let k = i - 1; k >= 0 && f.length < 30; k--) if (ev[k].followed) f.push(ev[k].time_s - ev[k].target_s); if (f.length < 2) return 0.3; const r = Math.sqrt(f.reduce((a, v) => a + v * v, 0) / f.length); return Math.max(0, Math.min(1, 1 - r / (3 * tol))) * f.length / (f.length + 3); };
  const errs = ev.map((e) => e.time_s - e.target_s), mx = Math.max(tol * 2, ...errs.map(Math.abs)) * 1.1;
  const L0 = 44, R0 = cw - 44, T0 = 14, B0 = ch - 30, cx = (i) => L0 + (ev.length == 1 ? 0.5 : i / (ev.length - 1)) * (R0 - L0);
  const Y = (v) => T0 + (mx - v) / (2 * mx) * (B0 - T0), YC = (v) => B0 - v * (B0 - T0), cells = [];
  x.fillStyle = STC.ok; x.globalAlpha = 0.14; x.fillRect(L0, Y(tol), R0 - L0, Y(-tol) - Y(tol)); x.globalAlpha = 1;
  niceTicks(-mx, mx, 5, 1).forEach((v) => { const y = Math.round(Y(v)) + 0.5; x.strokeStyle = Th.grid; x.beginPath(); x.moveTo(L0, y); x.lineTo(R0, y); x.stroke(); x.fillStyle = Th.dim; x.fillText((v > 0 ? "+" : "") + v + " s", 4, y + 4); });
  [0, 50, 100].forEach((v) => { x.fillStyle = Th.dim; x.fillText(v + "%", R0 + 6, YC(v / 100) + 4); });
  x.strokeStyle = FLOW; x.lineWidth = 2; x.beginPath(); ev.forEach((e, i) => { const y = YC(conf(i + 1)); i ? x.lineTo(cx(i), y) : x.moveTo(cx(i), y); }); x.stroke(); x.lineWidth = 1;
  ev.forEach((e, i) => {
    const err = errs[i], px = cx(i), py = Y(err), col = Math.abs(err) <= tol ? STC.ok : err < 0 ? STC.fast : STC.slow;
    x.beginPath(); x.arc(px, py, 5, 0, 7);
    if (e.followed) { x.fillStyle = col; x.fill(); } else { x.strokeStyle = Th.dim; x.stroke(); }
    const name = o.nameOf(e.coffee_id) || "a coffee", day = e.at ? new Date(e.at * 1000).toLocaleDateString() : "";
    cells.push({ x: px - 6, y: py - 6, w: 12, h: 12, txt: `<b>${esc(name)}</b> ${day}<br>advised ${esc(o.fs(e.from))} → ${esc(o.fs(e.advised))}${e.followed ? "" : ", pulled at " + esc(o.fs(e.next))}<br>next shot ${f1(e.time_s)} s against ${f1(e.target_s)} s (${err > 0 ? "+" : ""}${f1(err)} s)${e.confidence != null ? "<br>confidence then " + e.confidence + "%" : ""}` });
  });
  x.strokeStyle = Th.edge; x.beginPath(); x.moveTo(L0, T0); x.lineTo(L0, B0); x.lineTo(R0, B0); x.stroke();
  const fo = ev.filter((e) => e.followed), on = fo.filter((e) => Math.abs(e.time_s - e.target_s) <= tol).length;
  return { cells, events: ev.length, followed: fo.length, on };
}

/** Legend swatch HTML. */
export const swatch = (col, round) => `<span style="display:inline-block;width:12px;height:12px;border-radius:${round ? "50%" : "3px"};background:${col};margin:0 4px 0 10px;vertical-align:middle"></span>`;
export { f1, f2 };
