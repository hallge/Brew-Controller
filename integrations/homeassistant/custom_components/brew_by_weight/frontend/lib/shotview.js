// A shot's summary, rating row and curve, shared by Home and Shots (WebPage.h:1833-1853).
import { f1, f2, esc } from "./format.js";
import { shotChart, theme, when } from "./charts.js";

export const TN = ["", "sour", "balanced", "bitter"];

/** "50.4 g of 48 in 17.6 s of 29 · Honduras, Triple · First drops …" */
export function summaryLine(s) {
  return `${esc((s.coffee ? s.coffee + ", " : "") + s.recipe)}. First drops ${f1(s.first_drop_s)} s. Pour ${f1(s.pour_time_s)} s at ${f2(s.pour_flow_g_s)} g/s${s.bloom_pause_s ? ". Bloom " + s.bloom_pause_s + " s" : ""}. ${esc(when(s))}.`;
}

/** "Last shot 2.4 g over" (WebPage.h:1843). */
export function resultLine(s) {
  const d = s.final_weight_g - s.target_weight_g;
  return "Last shot " + (Math.abs(d) < 0.05 ? "on target" : f1(Math.abs(d)) + " g " + (d > 0 ? "over" : "under")) + (s.final_valid === false ? ", did not settle" : "");
}

/** Taste, rating and TDS controls for a shot (WebPage.h:1840). */
export function rateRowHtml(s) {
  const t = TN.indexOf(s.taste || "");
  return `<div class="row rate" data-shot="${s.id}">
    <span class="muted">Taste</span>${[1, 2, 3].map((i) => `<button class="${t == i ? "primary" : ""}" data-taste="${t == i ? 0 : i}">${TN[i][0].toUpperCase() + TN[i].slice(1)}</button>`).join("")}
    <span class="muted" style="margin-left:8px">Rating</span>${[1, 2, 3, 4, 5].map((i) => `<button class="${s.rating == i ? "primary" : ""}" data-rating="${s.rating == i ? 0 : i}" aria-label="${i} of 5">${i}</button>`).join("")}
    <label class="row" style="margin-left:8px;gap:6px"><span class="muted">TDS %</span><input class="tds" style="width:72px" inputmode="decimal" value="${s.tds_pct || ""}" aria-label="TDS percent"></label>
    ${s.extraction_pct ? `<span class="muted">extraction ${f1(s.extraction_pct)} %</span>` : ""}
  </div>`;
}

/** Wire the rate row inside `root` for shot `s`; calls `after()` once saved. */
export function wireRateRow(section, root, s, after) {
  const row = root.querySelector(`.rate[data-shot="${s.id}"]`);
  if (!row) return;
  const t = TN.indexOf(s.taste || "");
  row.querySelectorAll("[data-taste]").forEach((b) => b.onclick = async () => {
    const r = await section.post("/api/shot/rate", null, null, { id: s.id, rating: s.rating || 0, taste: +b.dataset.taste });
    if (r.ok) after();
  });
  row.querySelectorAll("[data-rating]").forEach((b) => b.onclick = async () => {
    const r = await section.post("/api/shot/rate", null, null, { id: s.id, rating: +b.dataset.rating, taste: t < 0 ? 0 : t });
    if (r.ok) after();
  });
  const tds = row.querySelector(".tds");
  tds.onchange = async () => {
    const v = tds.value.trim(), x = parseFloat(v.replace(",", "."));
    if (v !== "" && !(x >= 0.5 && x <= 25)) { section.panel.toast("TDS is a percent from 0.5 to 25", false); return; }
    const r = await section.post("/api/shot/tds", null, "Saved", { id: s.id, tds: v === "" ? 0 : x });
    if (r.ok) after();
  };
}

const DATA = new Map();
/** Shot readings, cached (they never change). */
export async function shotData(api, id) {
  if (!DATA.has(id)) {
    const p = api.get("/api/shot/data", { id }).catch((e) => { DATA.delete(id); throw e; });
    DATA.set(id, p);
  }
  return DATA.get(id);
}

/** Draw a shot's curve with the best-rated shot of the same coffee and recipe dashed behind it. */
export async function drawShot(api, canvas, s, shots, height) {
  const Th = theme(canvas);
  if (!s.has_data) { shotChart(canvas, s, null, null, null, null, Th, height); return { ref: null, data: null }; }
  let r;
  try { r = await shotData(api, s.id); } catch (e) { return { ref: null, data: null }; }
  if (!r.time_s) return { ref: null, data: null };
  const refShot = (shots || []).filter((q) => q.id != s.id && q.coffee_id == s.coffee_id && q.recipe_id == s.recipe_id && q.has_data && q.rating >= 4)
    .sort((a, b) => b.rating - a.rating || b.id - a.id)[0];
  let REF = null;
  if (refShot) { try { const q = await shotData(api, refShot.id); if (q.time_s) REF = { T: q.time_s, W: q.weight_g }; } catch (e) { /* none */ } }
  if (!canvas.isConnected) return { ref: refShot, data: r };
  shotChart(canvas, s, r.time_s, r.weight_g, r.flow_g_s, REF, Th, height);
  return { ref: REF ? refShot : null, data: r };
}
