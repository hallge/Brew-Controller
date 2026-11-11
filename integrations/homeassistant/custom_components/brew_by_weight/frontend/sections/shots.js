// Shots: summary tiles, history with filters, shot detail (curve, stats, grind vote, preparation,
// rating, TDS), comparison of up to 8 shots, exports (WebPage.h:268-283, 1801-1853, 1580-1602).
// Correcting a shot's grind and clearing the history are done in the controller's own page.
import { BrewSection } from "../lib/section.js";
import { html, raw } from "../lib/styles.js";
import { f1, f2, fs, fsn, grindOf, esc } from "../lib/format.js";
import { summaryLine, rateRowHtml, wireRateRow, drawShot, shotData } from "../lib/shotview.js";
import { compareChart, theme, CCOL, when, TIP_CSS } from "../lib/charts.js";

export const meta = { id: "shots", title: "Shots", icon: "mdi:coffee" };

const byW = (s) => /^weight/.test(s.end_reason || "");
const bt = (s) => (s.time_basis == "pour" ? s.pour_time_s : s.shot_time_s);
const dayOf = (s) => {
  if (!s.timestamp) return "Date not known";
  const d = new Date(s.timestamp), n = new Date(), k = (x) => x.toDateString();
  if (k(d) == k(n)) return "Today";
  n.setDate(n.getDate() - 1);
  if (k(d) == k(n)) return "Yesterday";
  return d.toLocaleDateString([], { weekday: "short", month: "short", day: "numeric", year: d.getFullYear() == new Date().getFullYear() ? undefined : "numeric" });
};
const clock = (s) => (s.timestamp ? new Date(s.timestamp).toLocaleTimeString([], { hour: "numeric", minute: "2-digit" }) : "#" + s.id);

// Kept while the panel is open: what is selected and compared.
const UI = { coffee: "", show: "", shown: 20, sel: 0, open: true, cmp: [], align: false, flow: true };

class BrewShots extends BrewSection {
  static get styles() {
    return `${TIP_CSS}
    .hist .day{font-size:12px;color:var(--secondary-text-color);padding:12px 8px 4px;font-weight:500}
    .srow{display:grid;grid-template-columns:70px 1fr 64px 64px 70px;gap:8px;padding:8px;border-bottom:1px solid var(--divider-color);align-items:center;cursor:pointer}
    .srow:hover{background:var(--secondary-background-color)} .srow.on{background:rgba(var(--rgb-primary-color,3,169,244),.08)}
    .srow.hd{cursor:default;font-size:12px;color:var(--secondary-text-color)} .srow.hd:hover{background:none}
    .r{text-align:right} .off{color:var(--warning-color,#ffa600)}
    .nm b{display:block} .nm span{font-size:13px;color:var(--secondary-text-color)}
    .st{color:var(--warning-color,#ffa600)} .tag{font-style:normal;font-size:11px;padding:1px 6px;border-radius:8px;background:var(--secondary-background-color);margin-left:4px}
    .detail{padding:12px 8px 16px;border-bottom:1px solid var(--divider-color)}
    .leg{display:flex;flex-wrap:wrap;gap:6px 14px;margin-top:8px;font-size:13px}
    @media (max-width:600px){.srow{grid-template-columns:54px 1fr 56px 56px}.srow .g{display:none}}`;
  }

  async load() {
    const [SH, L, S] = await Promise.all([this.store.get("shots"), this.store.get("library"), this.store.get("settings")]);
    Object.assign(this, { SH: Array.isArray(SH) ? SH : [], L, S, g: grindOf(L) });
    if (this.arg) { UI.sel = +this.arg; UI.open = true; this.arg = null; }
    if (!UI.sel && this.SH.length) UI.sel = this.SH[0].id;
  }

  filtered() {
    const tol = this.S ? this.S.grindToleranceS : 2;
    const late = (s) => byW(s) && s.target_time_s && Math.abs(bt(s) - s.target_time_s) > tol;
    return this.SH.filter((s) => (!UI.coffee || s.coffee == UI.coffee) && (UI.show == "1" ? byW(s) : UI.show == "2" ? late(s) : UI.show == "3" ? !s.rating && !s.taste : true));
  }

  render() {
    const tol = this.S ? this.S.grindToleranceS : 2, g = this.g;
    const late = (s) => byW(s) && s.target_time_s && Math.abs(bt(s) - s.target_time_s) > tol;
    const all = this.filtered();
    const w = all.filter(byW), timed = w.filter((s) => s.target_time_s), good = timed.filter((s) => !late(s)).length;
    const miss = w.map((s) => Math.abs(s.final_weight_g - (s.stop_target_g ?? s.target_weight_g))).sort((a, b) => a - b), rated = all.filter((s) => s.rating);
    const coffees = [...new Set(this.SH.map((s) => s.coffee).filter((x) => x))];
    let rows = "", day = "";
    all.slice(0, UI.shown).forEach((s) => {
      const k = dayOf(s);
      if (k != day) { day = k; rows += `<div class="day">${esc(k)}</div>`; }
      const on = s.id == UI.sel && UI.open;
      rows += `<div class="srow ${on ? "on" : ""}" data-id="${s.id}" role="button" tabindex="0"><span class="muted">${esc(clock(s))}</span>
        <span class="nm"><b>${esc(s.coffee || s.recipe)}</b><span>${s.coffee ? esc(s.recipe) : ""}${s.rating ? ' <i class="st">' + "★".repeat(s.rating) + "</i>" : ""}${s.taste ? " " + esc(s.taste) : ""}${byW(s) ? "" : ' <i class="tag">' + esc((s.end_reason || "").replace(/_/g, " ")) + "</i>"}${s.grind_vote == "ignore" ? ' <i class="tag">ignored</i>' : s.grind_vote == "count" ? ' <i class="tag">counted</i>' : ""}</span></span>
        <span class="r">${f1(s.final_weight_g)} g</span><span class="r ${late(s) ? "off" : ""}">${f1(s.shot_time_s)} s</span><span class="r g">${esc(fsn(g, s.grind_um) || "--")}</span></div>`;
      if (on) rows += `<div class="detail" id="detail"></div>`;
    });
    if (!all.length) rows += `<div class="empty">${this.SH.length ? "No shots match. Change what is shown above." : "No shots yet. Brew one and it appears here."}</div>`;
    return html`
    <div class="card">
      <div class="spread"><h3>Shots</h3><div class="row">
        <button id="csv">Download CSV</button><button id="json">Download JSON</button></div></div>
      <div class="tiles" style="margin:8px 0 12px">
        <div class="tile"><div class="k">Shots</div><div class="v">${all.length}</div><div class="k">${w.length} stopped by weight</div></div>
        <div class="tile"><div class="k">On time</div><div class="v">${timed.length ? Math.round(100 * good / timed.length) + " %" : "--"}</div><div class="k">${timed.length ? good + " of " + timed.length + " within ± " + tol + " s" : "no target times yet"}</div></div>
        <div class="tile"><div class="k">Typical miss</div><div class="v">${miss.length ? f1(miss[miss.length >> 1]) + " g" : "--"}</div><div class="k">from the target weight</div></div>
        <div class="tile"><div class="k">Rated</div><div class="v">${rated.length ? (rated.reduce((a, s) => a + s.rating, 0) / rated.length).toFixed(1) + " ★" : "--"}</div><div class="k">${rated.length} of ${all.length} shots</div></div>
      </div>
      <div class="row">
        <select id="fcoffee"><option value="">All coffees</option>${raw(coffees.map((n) => `<option ${n == UI.coffee ? "selected" : ""}>${esc(n)}</option>`).join(""))}</select>
        <select id="fshow">${raw([["", "All shots"], ["1", "Stopped by weight"], ["2", "Brew time off target"], ["3", "Not rated"]].map(([v, l]) => `<option value="${v}" ${v == UI.show ? "selected" : ""}>${l}</option>`).join(""))}</select>
      </div>
      <div class="hist" style="margin-top:8px"><div class="srow hd"><span>Time</span><span>Coffee, recipe</span><span class="r">Out</span><span class="r">Brew</span><span class="r g">Grind ${g.unit}</span></div>${raw(rows)}</div>
      <div class="row" style="margin-top:8px">${all.length > UI.shown ? html`<button id="more">Show 20 more</button><span class="hint">${UI.shown} of ${all.length} shown</span>` : all.length > 20 ? html`<span class="hint">All ${all.length} shown</span>` : ""}</div>
    </div>
    <div class="card" id="cmpcard" style="margin-top:16px;${UI.cmp.length ? "" : "display:none"}">
      <div class="spread"><h3>Compare shots</h3><div class="row">
        <label class="c"><input type="checkbox" id="cmpflow" ${UI.flow ? "checked" : ""}> Flow</label>
        <label class="c"><input type="checkbox" id="cmpalign" ${UI.align ? "checked" : ""}> Line up at first drops</label>
        <button id="cmpclear">Clear</button></div></div>
      <canvas id="cmpchart"></canvas><div class="leg" id="cmpleg"></div>
    </div>
    <div class="tip" id="tip"></div>`;
  }

  detailHtml(s) {
    const g = this.g;
    const tile = (k, v, u) => `<div class="tile"><div class="k">${k}</div><div class="v">${v}</div><div class="k">${u}</div></div>`;
    const inCmp = UI.cmp.includes(s.id);
    return `<div class="spread"><div><b>${esc((s.coffee ? s.coffee + ", " : "") + s.recipe)}</b> <span class="muted">${esc(when(s))}</span></div>
      <div class="row"><button id="cmp">${inCmp ? "Remove from comparison" : "Add to comparison"}</button><button id="dlshot">Readings CSV</button></div></div>
      <div class="tiles" style="margin:8px 0">
        ${tile("In the cup", f1(s.final_weight_g) + " g", "target " + f1(s.stop_target_g ?? s.target_weight_g) + ", drip " + f1(s.drip_weight_g))}
        ${tile("Brew time", f1(s.shot_time_s) + " s", s.target_time_s ? "target " + s.target_time_s : "")}
        ${tile("Pour", f1(s.pour_time_s) + " s", f2(s.pour_flow_g_s) + " g/s, first drops " + f1(s.first_drop_s) + " s")}
        ${tile("Grind", s.grind_um ? esc(fs(g, s.grind_um)) : "--", s.dose_g + " g dose")}
        ${tile("Flow at 25/50/75 %", [s.flow_25_g_s, s.flow_50_g_s, s.flow_75_g_s].map(f1).join(" / "), "peak " + f2(s.peak_flow_g_s) + " g/s")}
        ${tile("Ended by", esc((s.end_reason || "").replace(/_/g, " ")), "offset " + f2(s.stop_offset_g) + " → " + f2(s.next_stop_offset_g) + " g" + (s.bloom_pause_s ? ", bloom " + s.bloom_pause_s + " s" : ""))}
      </div>
      <canvas id="schart"></canvas><div class="hint" id="sdata"></div>
      <div class="row" style="margin-top:10px"><span class="muted">Grind advice</span>
        ${[["Auto", 0, ""], ["Count", 2, "count"], ["Ignore", 1, "ignore"]].map((v) => `<button class="${(s.grind_vote || "") == v[2] ? "primary" : ""}" data-vote="${v[1]}">${v[0]}</button>`).join("")}</div>
      <div style="margin-top:8px">${rateRowHtml(s)}</div>
      <div class="row" style="margin-top:8px"><span class="muted">Preparation</span>
        <label class="row" style="gap:6px">dose weighed <input id="pdose" style="width:64px" inputmode="decimal" value="${s.dose_weighed_g ? f1(s.dose_weighed_g) : ""}" placeholder="${s.dose_g ? f1(s.dose_g) : ""}"> g</label>
        <label class="c"><input type="checkbox" id="ppur" ${s.purged ? "checked" : ""}> purged first</label>
        <label class="c"><input type="checkbox" id="pune" ${s.uneven ? "checked" : ""}> flow looked uneven</label></div>
      <div class="hint" style="margin-top:6px">${summaryLine(s)}</div>`;
  }

  wire() {
    this.$("#fcoffee").onchange = (e) => { UI.coffee = e.target.value; UI.shown = 20; this._paint(); };
    this.$("#fshow").onchange = (e) => { UI.show = e.target.value; UI.shown = 20; this._paint(); };
    const more = this.$("#more"); if (more) more.onclick = () => { UI.shown += 20; this._paint(); };
    this.$$(".srow[data-id]").forEach((r) => {
      const pick = () => { const id = +r.dataset.id; if (UI.sel == id && UI.open) UI.open = false; else { UI.sel = id; UI.open = true; } this._paint(); };
      r.onclick = pick;
      r.onkeydown = (e) => { if (e.key == "Enter" || e.key == " ") { e.preventDefault(); pick(); } };
    });
    this.$("#csv").onclick = () => this.api.download("shots_csv", {}, "brew-shots.csv").catch((e) => this.panel.toast(e.message, false));
    this.$("#json").onclick = () => {
      const blob = new Blob([JSON.stringify(this.SH, null, 1)], { type: "application/json" });
      const a = document.createElement("a"); a.href = URL.createObjectURL(blob); a.download = "brew-shots.json"; a.click();
      setTimeout(() => URL.revokeObjectURL(a.href), 5000);
    };
    const det = this.$("#detail"), s = this.SH.find((x) => x.id == UI.sel);
    if (det && s) this.wireDetail(det, s);
    this.wireCompare();
  }

  wireDetail(det, s) {
    det.innerHTML = this.detailHtml(s);
    const after = () => this.store.invalidate("shots", "grind");
    wireRateRow(this, det, s, after);
    det.querySelectorAll("[data-vote]").forEach((b) => b.onclick = async () => { const r = await this.post("/api/shot/vote", null, "Saved", { id: s.id, vote: +b.dataset.vote }); if (r.ok) after(); });
    det.querySelector("#cmp").onclick = () => this.toggleCompare(s.id);
    det.querySelector("#dlshot").onclick = () => this.api.download("shot_csv", { id: s.id }, `brew-shot-${s.id}.csv`).catch((e) => this.panel.toast(e.message, false));
    const prep = async () => {
      const d = det.querySelector("#pdose").value.trim().replace(",", "."), t = d === "" ? 0 : parseFloat(d);
      if (!(t === 0 || (t >= 5 && t <= 40))) { this.panel.toast("Dose weighed: 5 to 40 g, or leave it empty", false); return; }
      const u = det.querySelector("#pune").checked;
      const r = await this.post("/api/shot/prep", null, u ? "Saved. If this uneven shot should not steer the grind, choose Ignore." : "Saved",
        { id: s.id, dose: t, purged: det.querySelector("#ppur").checked ? 1 : 0, uneven: u ? 1 : 0 });
      if (r.ok) after();
    };
    ["#pdose", "#ppur", "#pune"].forEach((sel) => det.querySelector(sel).onchange = prep);
    drawShot(this.api, det.querySelector("#schart"), s, this.SH, 240).then(({ ref, data }) => {
      const el = det.querySelector("#sdata");
      if (!el) return;
      if (!s.has_data) { el.textContent = "The readings of this shot are no longer stored; the summary is."; return; }
      if (!data) return;
      const d = data.data || {};
      el.textContent = `Dark: weight. Orange: flow.${ref ? " Dashed green: your best-rated shot of this coffee (" + "★".repeat(ref.rating) + ", " + f1(ref.shot_time_s) + " s)." : ""} ${d.samples || 0} readings over ${d.duration_s || 0} s, one every ${d.mean_period_ms || 0} ms, longest gap ${d.max_gap_ms || 0} ms. ` +
        (d.timer_samples ? `Scale timer in ${d.timer_samples} of them` + (d.timer_span_s > 0 ? `: it counted ${d.timer_span_s} s while this controller counted ${d.clock_span_s} s.` : ".") : "The scale sent no timer.");
    });
  }

  toggleCompare(id) {
    const i = UI.cmp.indexOf(id);
    if (i >= 0) UI.cmp.splice(i, 1);
    else { if (UI.cmp.length >= 8) { this.panel.toast("Up to 8 shots can be compared", false); return; } UI.cmp.push(id); }
    this._paint();
    if (i < 0) setTimeout(() => this.$("#cmpcard") && this.$("#cmpcard").scrollIntoView({ behavior: "smooth", block: "nearest" }), 100);
  }

  async wireCompare() {
    if (!UI.cmp.length) return;
    this.$("#cmpflow").onchange = (e) => { UI.flow = e.target.checked; this.wireCompare(); };
    this.$("#cmpalign").onchange = (e) => { UI.align = e.target.checked; this.wireCompare(); };
    this.$("#cmpclear").onclick = () => { UI.cmp = []; this._paint(); };
    const rows = [];
    for (const [k, id] of UI.cmp.entries()) {
      const s = this.SH.find((q) => q.id == id);
      if (!s) continue;
      let d = null;
      if (s.has_data) { try { const r = await shotData(this.api, id); if (r.time_s) d = { T: r.time_s, W: r.weight_g, F: r.flow_g_s }; } catch (e) { /* none */ } }
      rows.push({ id, s, d, col: CCOL[k % CCOL.length] });
    }
    const c = this.$("#cmpchart");
    if (!c) return;
    compareChart(c, rows, { align: UI.align, flow: UI.flow }, theme(c));
    const g = this.g;
    this.$("#cmpleg").innerHTML = rows.map((r) => `<span class="row" style="gap:6px"><span style="display:inline-block;width:18px;height:3px;border-radius:2px;background:${r.col}"></span>${esc(r.s.coffee || r.s.recipe)} <span class="muted">${esc(when(r.s))}, ${f1(r.s.final_weight_g)} g in ${f1(r.s.shot_time_s)} s${r.s.grind_um ? ", " + esc(fs(g, r.s.grind_um)) : ""}${r.d ? "" : ", no readings stored"}</span> <button class="link" data-rm="${r.id}" aria-label="Remove from the comparison">✕</button></span>`).join("");
    this.$$("[data-rm]").forEach((b) => b.onclick = () => this.toggleCompare(+b.dataset.rm));
  }
}

customElements.define("brew-shots", BrewShots);
