// Grinder: setting now, advice in full, the new-bag recommendation, brew time against setting,
// setting shot by shot, how advice is worked out (settings), grinder upkeep, my grinder (model,
// how it adjusts, range, zero date), a second grinder, sharing (WebPage.h:363-406, 1378-1579).
import { BrewSection } from "../lib/section.js";
import { html, raw } from "../lib/styles.js";
import { fs, fsn, ps, grindOf, d2i, i2d, today, esc } from "../lib/format.js";
import { adviceText, recommendationText } from "../lib/advice.js";
import { grindScatter, grindByShot, theme, attachTips, emptyNote, CCOL, STC, swatch, when, TIP_CSS } from "../lib/charts.js";
import { confirmDialog, numberField, selectField } from "../lib/forms.js";
import { upkeepHtml, wireUpkeep } from "../lib/upkeep.js";

export const meta = { id: "grinder", title: "Grinder", icon: "mdi:grain" };

const UI = { all: false };
const SHN = { local: "only here", queued: "queued", sent: "sent", review: "in review", published: "published", declined: "declined", refused: "refused" };
const ADVICE_KEYS = ["grindToleranceS", "grindWindow", "grindRatedTarget", "grindConfidence", "grindUmPerS", "grindMaxStepUm", "grindTimeBasis"];
const DIVISORS = [[1, "Whole number (12)"], [2, "0.5 (12.5)"], [4, "0.25 (12.25)"], [5, "0.2 (12.2)"], [10, "0.1 (12.1)"], [20, "0.05 (12.05)"], [25, "0.04 (12.04)"], [50, "0.02 (12.02)"], [100, "0.01 (12.01)"]];

/** The maker a model belongs to, for the picker's groups (WebPage.h:1465). */
function maker(n) {
  const m = [["Barista Express", "Breville"], ["Breville", "Breville"], ["Baratza", "Baratza"], ["Eureka", "Eureka"], ["Niche", "Niche"], ["DF", "Turin (DF)"], ["1Zpresso", "1Zpresso"], ["Comandante", "Comandante"], ["Timemore", "Timemore"], ["Fellow", "Fellow"], ["Mahlk", "Mahlkönig"]].find((x) => n.startsWith(x[0]));
  return m ? m[1] : (n.split(" ")[0] || "Other");
}
const kindOf = (g) => (g.unit == "um" ? "um" : ((g.divisor || 1) > 1 || g.unit == "dial") ? "dial" : "steps");

class BrewGrinder extends BrewSection {
  static get styles() {
    return `${TIP_CSS}.leg{display:flex;flex-wrap:wrap;align-items:center;gap:4px;font-size:13px;margin-top:8px}
    .banner{border-left:4px solid var(--warning-color,#ffa600);padding:8px 12px;background:rgba(var(--rgb-warning-color,255,166,0),.08);border-radius:8px;margin-top:10px}`;
  }

  async load() {
    const [L, G, SH, S, maint, share] = await Promise.all([
      this.store.get("library"), this.store.get("grind"), this.store.get("shots"), this.store.get("settings"),
      this.store.get("maintenance").catch(() => null), this.store.get("share").catch(() => null),
    ]);
    const GM = await this.api.getOr("/api/community/grinders", null, null);
    Object.assign(this, { L, G, SH: Array.isArray(SH) ? SH : [], S, maint, share, GM: (GM && GM.grinders) || [], g: grindOf(L) });
  }

  /** Shots on this grinder calibration stopped by weight and not ignored (WebPage.h:1502). */
  grindShots() {
    const L = this.L, S = this.S;
    return this.SH.filter((s) => (s.grinder_calibration || 0) == (L.grinder || {}).calibration && /^weight/.test(s.end_reason || "") && s.grind_vote != "ignore")
      .map((s) => ({ id: s.id, t: S && S.grindTimeBasis == 1 ? s.pour_time_s : s.shot_time_s, u: s.grind_um, r: s.rating, c: s.coffee_id || 0, n: s.coffee || "no coffee", rc: s.recipe || "", w: when(s), tt: s.target_time_s || 0, ts: s.timestamp }))
      .filter((q) => q.t > 0 && q.u);
  }

  render() {
    const { L, G, S, g } = this, gr = L.grinder || {}, other = L.otherGrinder;
    const adv = adviceText(G, g), rec = recommendationText(G, g);
    const sv = (k) => (S && S[k] != null ? S[k] : "");
    const shareOn = S && S.shareEnabled, st = gr.share || "local", known = gr.modelId > 0;
    return html`
    <div class="grid">
      <div class="card"><h3>Setting now</h3>
        <div class="row"><span class="big">${fsn(g, G.current_um) || "--"}</span><span class="muted">${g.unit}</span></div>
        <div class="hint">${G.recipe ? "Advice for " + G.recipe + (G.coffee ? ", " + G.coffee : "") : ""}</div>
        <div class="row" style="margin-top:8px"><input id="gset" placeholder="as the dial shows" style="width:140px" autocomplete="off"><button id="gsetb">I moved the grinder to this</button></div>
        <div class="hint">Shots are only compared with others made at the same setting.</div>
        ${rec ? html`<div class="banner"><div>${rec.text}</div><div class="row" style="margin-top:8px"><button class="primary" id="recuse">${rec.use}</button><button id="reckeep">${rec.keep}</button></div></div>` : ""}
      </div>
      <div class="card"><h3>Advice</h3>
        ${adv ? html`<span class="pill ${adv.cls}" style="font-size:15px">${adv.title}</span>
          <p class="hint">${adv.why}</p>${adv.age ? html`<p class="hint">${adv.age}</p>` : ""}${adv.rated ? html`<p class="hint">${adv.rated}</p>` : ""}
          ${adv.apply ? html`<button class="primary" id="apply" data-um="${adv.apply}">I set the grinder to ${fs(g, adv.apply)}</button>` : ""}` : html`<div class="hint">No advice yet.</div>`}
      </div>
      <div class="card" style="grid-column:1/-1"><div class="spread"><h3>Brew time against grind setting</h3><div class="row"><span class="hint">${adv ? adv.sensitivity : ""}</span><label class="c"><input type="checkbox" id="gall" ${UI.all ? "checked" : ""}> All coffees</label></div></div>
        <canvas id="gchart"></canvas><div class="leg" id="gleg"></div><div class="hint" id="gnote"></div></div>
      <div class="card" style="grid-column:1/-1"><h3>Grind setting, shot by shot</h3><canvas id="cchart"></canvas><div class="leg" id="cleg"></div><div class="hint" id="cnote"></div></div>
      <div class="card"><h3>How advice is worked out</h3>
        <form id="advform" class="form">
          ${raw(numberField("grindToleranceS", "Brew time tolerance (± s)", sv("grindToleranceS"), { step: 0.5, min: 0.5, max: 10 }))}
          ${raw(numberField("grindWindow", "Shots averaged", sv("grindWindow"), { min: 1, max: 10, step: 1 }))}
          ${raw(selectField("grindRatedTarget", "Ratings", [[1, "Refine towards my 4 and 5 star shots"], [0, "Off: time advice only"]], +sv("grindRatedTarget")))}
          ${raw(selectField("grindConfidence", "Confidence", [[1, "Size each change by how past advice landed"], [0, "Off: the same size every time"]], +sv("grindConfidence")))}
          ${raw(numberField("grindUmPerS", `Starting guess (${g.unit} per s)`, sv("grindUmPerS"), { step: 0.5, min: 0.5, max: 20 }))}
          ${raw(numberField("grindMaxStepUm", `Largest single change (${g.unit})`, sv("grindMaxStepUm"), { min: 1, max: 100, step: 1 }))}
          ${raw(selectField("grindTimeBasis", "Brew time measured", [[0, "Total: start press to stop"], [1, "Pour: first drops to stop"]], +sv("grindTimeBasis")))}
        </form>
        <div class="row" style="margin-top:10px"><button class="primary" id="advsave">Save</button><span class="hint">Changing how brew time is measured starts a fresh comparison.</span></div></div>
      <div class="card"><div class="spread"><h3>Grinder upkeep</h3><button id="zeroed">Zeroed today</button></div>
        <div id="gurows">${raw(upkeepHtml(this.maint, ["grinder_clean"]))}</div>
        <div class="hint" style="margin-top:8px">After zeroing the grinder again (or changing burrs), settings from before are no longer compared with new ones.</div></div>
      <div class="card" style="grid-column:1/-1"><h3>My grinder</h3>
        <form id="gm" class="form">
          <label class="f">Model<select name="model">${raw(this.modelOptions())}</select></label>
          <label class="f" id="gm_namel">Name<input name="name" maxlength="31" placeholder="e.g. My hand grinder"></label>
          <label class="f">How it adjusts<select name="kind"><option value="steps">Steps or clicks</option><option value="dial">Stepless dial</option><option value="um">Microns (display)</option></select></label>
          <label class="f" id="gm_divl">Read it to the nearest<select name="divisor">${raw(DIVISORS.map(([v, l]) => `<option value="${v}">${l}</option>`).join(""))}</select></label>
          <label class="f" id="gm_unitl">A setting is called (optional)<input name="unit" maxlength="7" placeholder="step"></label>
          <label class="f">Finest setting<input name="min" inputmode="decimal" placeholder="1"></label>
          <label class="f">Coarsest setting<input name="max" inputmode="decimal" placeholder="e.g. 60"></label>
          <label class="f">The range covers<select name="covers"><option value="0">Not said</option><option value="2">Espresso to filter</option><option value="1">Espresso only</option></select></label>
          <label class="f">Micro adjustment: steps per macro step (empty: none)<input name="subSteps" type="number" min="2" max="99" placeholder="e.g. 9"></label>
          <label class="f">Last zeroed, recalibrated or burrs changed<input name="zeroDate" type="date"></label>
        </form>
        <div class="hint" id="gm_sum" style="margin:8px 0"></div>
        <div class="row"><button class="primary" id="gmsave">Save</button>${other ? "" : html`<button id="gmother">Add what is typed above as my second grinder</button>`}</div>
        <div class="hint" style="margin-top:6px">${gr.model ? "In use: " + gr.model + ". " : "No grinder named yet. "}Changing how a listed model adjusts makes it your own version.</div>
        ${gr.model ? html`<div class="row" style="margin-top:10px;border-top:1px solid var(--divider-color);padding-top:10px">
          <span class="pill">${known ? "a known model" : SHN[st] || st}</span>
          <span class="hint" style="flex:1">${known ? "This model is in the firmware's own list: there is nothing to send." : shareOn ? "Sending it lets a reviewer add the model to the shared list." : "Sharing is switched off (Settings)."}${gr.shareChanged ? " You have changed it since that version was sent." : ""}</span>
          ${shareOn && gr.canShare ? html`<button id="gmsend" data-id="${gr.id}">${st == "local" ? "Send to the shared library" : st == "refused" ? "Send again" : "Send the new version"}</button>` : ""}</div>` : ""}
        <div class="row" style="margin-top:10px;border-top:1px solid var(--divider-color);padding-top:10px">
          ${other ? html`<span class="hint" style="flex:1">Your other grinder: <b>${other.model}</b> (read in ${other.unit == "um" ? "µm" : other.unit}).</span><button id="gmswitch">Use it instead</button>${shareOn && other.canShare ? html`<button id="gmsendother" data-id="${other.id}">Send it to the shared library</button>` : ""}<button class="danger" id="gmremove">Remove it</button>`
            : html`<span class="hint">One grinder. A second can be kept beside it: choose or type it above, then "Add … as my second grinder".</span>`}
        </div>
      </div>
      <div class="card"><h3>Shared grinder list</h3>
        ${this.GM.length ? html`<table><tr><th>Model</th><th>Read in</th><th>Range</th></tr>${raw(this.GM.map((m) => `<tr><td>${esc(m.model)}</td><td>${esc(m.unit == "um" ? "µm" : m.unit)}${m.um_per_unit ? " (" + m.um_per_unit + " µm)" : ""}</td><td>${m.range ? esc(m.range) : m.min && m.max ? m.min + "–" + m.max : ""}</td></tr>`).join(""))}</table>`
          : html`<div class="hint">The shared library's grinders are not available from the controller right now.</div>`}
        <div class="hint" style="margin-top:8px">The model list holds the firmware's own grinders and those the shared library has published. Pick one in My grinder above.</div></div>
    </div>
    <div class="tip" id="tip"></div>`;
  }

  modelOptions() {
    const L = this.L, gr = L.grinder || {}, GM = this.GM;
    const libSel = gr.libraryId && GM.some((x) => x.id == gr.libraryId);
    const pick = [...(L.grinderModels || []).map((m) => ({ v: m.id, name: m.model, sel: m.id == gr.modelId, note: m.umPerUnit > 0 ? "" : " (steps learned from your shots)" })),
      ...GM.filter((m) => !(L.grinderModels || []).some((x) => x.model == m.model || x.libraryId == m.id)).map((m) => ({ v: "L:" + m.id, name: m.model, sel: !gr.modelId && gr.libraryId == m.id, note: " (shared library)" }))];
    const groups = {};
    pick.forEach((m) => { (groups[maker(m.name)] = groups[maker(m.name)] || []).push(m); });
    return Object.keys(groups).sort((a, b) => a.localeCompare(b)).map((k) => `<optgroup label="${esc(k)}">` + groups[k].sort((a, b) => a.name.localeCompare(b.name)).map((m) => `<option value="${esc(String(m.v))}" ${m.sel ? "selected" : ""}>${esc(m.name)}${m.note}</option>`).join("") + "</optgroup>").join("") +
      `<option value="0" ${gr.modelId || libSel ? "" : "selected"}>${gr.model && !gr.modelId && !libSel ? esc(gr.model) + " (yours)" : gr.model || gr.modelId ? "Another grinder: type it" : "Not set yet: pick one, or type it"}</option>`;
  }

  // --- grinder form (WebPage.h:1434-1493) -------------------------------------------------------
  gmPick() {
    const v = this.$("#gm").model.value;
    return v[0] == "L" ? { lib: this.GM.find((x) => x.id == v.slice(2)) } : { own: (this.L.grinderModels || []).find((x) => x.id == +v) };
  }
  gmFmt() {
    const f = this.$("#gm"), k = f.kind.value, r = Math.max(0, +f.subSteps.value || 0);
    return { divisor: k == "dial" && r < 2 ? (+f.divisor.value || 1) : 1, subSteps: r >= 2 ? r : 0 };
  }
  gmFill(m, keepRange) {
    const f = this.$("#gm"), k = kindOf(m);
    f.name.value = m.model || "";
    f.kind.value = k;
    f.unit.value = k == "um" || m.unit == ({ steps: "step", dial: "dial" })[k] ? "" : m.unit || "";
    f.divisor.value = m.divisor || 1;
    const r = m.subSteps || 0;
    f.subSteps.value = r >= 2 ? r : "";
    if (!keepRange) {
      const fmt = { divisor: r >= 2 ? 1 : (m.divisor || 1), subSteps: r >= 2 ? r : 0, unit: "" };
      f.min.value = m.max ? (r >= 2 ? Math.floor(((m.min || 1) - 1) / r) + 1 : fsn(fmt, m.min || 1)) : "";
      f.max.value = m.max ? (r >= 2 ? Math.ceil(m.max / r) : fsn(fmt, m.max)) : "";
    }
    this.gmSummary();
  }
  gmRange() {
    const f = this.$("#gm"), fmt = this.gmFmt(), r = fmt.subSteps, a = f.min.value.trim(), b = f.max.value.trim();
    if (!b) return [0, 0];
    let lo, hi;
    if (r >= 2) { lo = a ? ((parseInt(a) || 1) - 1) * r + 1 : 1; hi = (parseInt(b) || 0) * r; }
    else { lo = a ? ps(fmt, a) : 1; hi = ps(fmt, b); }
    if (!lo && a && /^0+([.,]0*)?$/.test(a)) lo = 1;
    return lo && hi && hi > lo && hi <= 2000 ? [lo, hi] : [0, 0];
  }
  gmSummary() {
    const f = this.$("#gm"), k = f.kind.value, fmt = this.gmFmt(), p = this.gmPick();
    const src = p.own || (p.lib && { umPerUnit: p.lib.um_per_unit, burrs: p.lib.burrs });
    this.$("#gm_namel").style.display = p.own || p.lib ? "none" : "";
    this.$("#gm_divl").style.display = k == "dial" && fmt.subSteps < 2 ? "" : "none";
    this.$("#gm_unitl").style.display = k == "um" ? "none" : "";
    const word = k == "um" ? "µm" : (f.unit.value.trim() || (k == "dial" ? "dial" : "step")), [lo, hi] = this.gmRange(), bits = [];
    const ex = k == "um" ? 246 : fmt.subSteps >= 2 ? (12 - 1) * fmt.subSteps + 3 : fmt.divisor > 1 ? Math.round(12.5 * fmt.divisor) : 12;
    bits.push("Settings look like " + fsn(fmt, ex) + (k == "um" ? " µm of burr gap" : " (" + word + ")"));
    if (hi) bits.push("range " + (fmt.subSteps >= 2 ? Math.floor((lo - 1) / fmt.subSteps + 1) + "–" + Math.ceil(hi / fmt.subSteps) + " on the macro" : fsn(fmt, lo) + "–" + fsn(fmt, hi)));
    else if (f.max.value.trim()) bits.push("the range is not readable: finest must be below coarsest");
    bits.push(k == "um" ? "one step is one micron" : src && src.umPerUnit > 0 ? "the maker says one step is " + src.umPerUnit + " µm" : "how far one step moves the shot is learned from your shots");
    const cv = +f.covers.value;
    if (hi && cv) bits.push("first guess for espresso: " + fsn(fmt, Math.round(lo + (cv == 2 ? 0.2 : 0.5) * (hi - lo))));
    this.$("#gm_sum").textContent = bits.join(" · ") + "." + (src && src.burrs ? " Burrs: " + src.burrs + "." : "");
  }
  grinderBody(rezeroed) {
    const f = this.$("#gm"), p = this.gmPick(), gr = this.L.grinder || {}, k = f.kind.value, fmt = this.gmFmt(), [lo, hi] = this.gmRange();
    const src = p.own ? { model: p.own.model, unit: p.own.unit, umPerUnit: p.own.umPerUnit, divisor: p.own.divisor || 1, subSteps: p.own.subSteps || 0 }
      : p.lib ? { model: p.lib.model, unit: p.lib.unit, umPerUnit: p.lib.um_per_unit, divisor: p.lib.divisor || 1, subSteps: p.lib.sub_steps || 0 } : null;
    const model = src ? src.model : f.name.value.trim(), unit = k == "um" ? "um" : (f.unit.value.trim() || (k == "dial" ? "dial" : "step"));
    const same = src && unit == src.unit && fmt.subSteps == src.subSteps && fmt.divisor == src.divisor;
    const umPerUnit = k == "um" ? 1 : same ? (src.umPerUnit || 0) : (!src && model == gr.model && unit == gr.unit ? (gr.umPerUnit || 0) : 0);
    return { modelId: p.own && same ? p.own.id : 0, libraryId: p.lib && same ? p.lib.id : "", model, unit, umPerUnit, divisor: fmt.divisor, subSteps: fmt.subSteps, min: lo, max: hi, covers: +f.covers.value || 0, zeroDate: rezeroed ? today() : d2i(f.zeroDate.value), rezeroed: !!rezeroed };
  }
  async saveGrinder(rezeroed, asOther) {
    const gr = this.L.grinder || {}, b = this.grinderBody(rezeroed), f = this.$("#gm");
    if (!b.modelId && !b.model) { this.panel.toast("Pick a model, or type its name", false); return; }
    if (asOther) {
      b.asOther = true; b.rezeroed = false;
      if (b.model == gr.model) { this.panel.toast("That is the grinder already in use: choose or type the other one first", false); return; }
      const r = await this.post("/api/grinder", b, "Second grinder added");
      if (r.ok) this.store.invalidate("library");
      return;
    }
    if (f.subSteps.value.trim() && b.subSteps < 2) { this.panel.toast("A micro adjustment needs 2 to 99 steps per macro step, or leave it empty", false); return; }
    if (f.max.value.trim() && !b.max) { this.panel.toast("The range is not readable: the finest setting must be below the coarsest", false); return; }
    const named = gr.model || gr.modelId, changes = named && (b.model != gr.model || b.unit != gr.unit || b.divisor != (gr.divisor || 1) || b.subSteps != (gr.subSteps || 0));
    if ((rezeroed || changes) && !(await confirmDialog(this.shadowRoot, "Grind settings from before will no longer be compared with new ones: grind advice starts fresh for every coffee, and if the unit changes the settings entered so far are cleared. To keep both grinders, use \"Add … as my second grinder\" instead. Continue?", { yes: "Continue" }))) return;
    const r = await this.post("/api/grinder", b, rezeroed ? "Zero date saved" : "Grinder saved");
    if (r.ok) this.store.invalidate("library", "grind");
  }

  wire() {
    const g = this.g, G = this.G;
    const setGrind = async (um) => {
      if (um == null || !(um >= 1 && um <= 2000)) { this.panel.toast("That is not a setting this grinder can have" + ((g.subSteps || 0) >= 2 ? " (outer ring : inner ring, for example 12:3)" : (g.divisor || 1) > 1 ? " (a number such as 3.5)" : " (a whole number)"), false); return; }
      const r = await this.post("/api/grind/set", null, "Grind setting saved", { um: Math.round(um) });
      if (r.ok) this.store.invalidate("library", "grind");
    };
    this.$("#gsetb").onclick = () => setGrind(ps(g, this.$("#gset").value));
    this.$("#gset").onkeydown = (e) => { if (e.key == "Enter") setGrind(ps(g, this.$("#gset").value)); };
    const ap = this.$("#apply"); if (ap) ap.onclick = () => setGrind(+ap.dataset.um);
    const rec = async (use) => { const r = await this.post("/api/grind/recommendation", null, use ? "Grind setting saved" : null, { use }); if (r.ok) this.store.invalidate("grind", "library"); };
    const ru = this.$("#recuse"); if (ru) ru.onclick = () => rec(1);
    const rk = this.$("#reckeep"); if (rk) rk.onclick = () => rec(0);
    this.$("#gall").onchange = (e) => { UI.all = e.target.checked; this.drawCharts(); };
    this.$("#advsave").onclick = async () => {
      const f = this.$("#advform"), body = {};
      ADVICE_KEYS.forEach((k) => { const v = f[k].value; if (v === "") return; const n = +v; if (!this.S || this.S[k] !== n) body[k] = n; });
      if (!Object.keys(body).length) { this.panel.toast("Nothing changed"); return; }
      const r = await this.post("/api/settings", body, "Saved");
      if (r.ok) this.store.invalidate("settings", "grind");
    };
    wireUpkeep(this, this.$("#gurows"), () => this.store.invalidate("maintenance"));
    this.$("#zeroed").onclick = () => this.saveGrinder(true);
    // grinder form
    const f = this.$("#gm"), gr = this.L.grinder || {};
    f.zeroDate.value = i2d(gr.zeroDate);
    f.covers.value = gr.covers || 0;
    this.gmFill(gr);
    f.model.onchange = () => {
      const p = this.gmPick();
      const m = p.own ? { model: p.own.model, unit: p.own.unit, divisor: p.own.divisor, subSteps: p.own.subSteps, min: p.own.min, max: p.own.max }
        : p.lib ? { model: p.lib.model, unit: p.lib.unit, divisor: p.lib.divisor || 1, subSteps: p.lib.sub_steps || 0, min: p.lib.min, max: p.lib.max } : null;
      if (m) this.gmFill(m); else { if (gr.modelId || gr.libraryId) f.name.value = ""; this.gmSummary(); }
    };
    ["kind", "divisor", "unit", "min", "max", "covers", "subSteps", "name"].forEach((n) => { f[n].oninput = () => this.gmSummary(); f[n].onchange = () => this.gmSummary(); });
    this.$("#gmsave").onclick = () => this.saveGrinder(false);
    const go = this.$("#gmother"); if (go) go.onclick = () => this.saveGrinder(false, true);
    const send = async (id) => { const r = await this.post("/api/share/grinder", null, "Queued for the shared library", { id }); if (r.ok) this.store.invalidate("library", "share"); };
    const gs = this.$("#gmsend"); if (gs) gs.onclick = () => send(+gs.dataset.id);
    const gso = this.$("#gmsendother"); if (gso) gso.onclick = () => send(+gso.dataset.id);
    const sw = this.$("#gmswitch");
    if (sw) sw.onclick = async () => { const r = await this.post("/api/library/do", null, "Now using " + this.L.otherGrinder.model, { what: "grinder_switch", a: 0, b: 0 }); if (r.ok) this.store.invalidate("library", "grind"); };
    const rm = this.$("#gmremove");
    if (rm) rm.onclick = async () => {
      if (!(await confirmDialog(this.shadowRoot, "Remove " + this.L.otherGrinder.model + "? Its settings for each coffee go with it; its shots stay in the history."))) return;
      const r = await this.post("/api/library/do", null, "Removed", { what: "grinder_remove_other", a: 0, b: 0 });
      if (r.ok) this.store.invalidate("library");
    };
    this.drawCharts();
  }

  drawCharts() {
    const L = this.L, G = this.G, g = this.g, T = theme(this), tip = this.$("#tip"), open = (q) => q.click && this.panel.navigate("shots", q.click);
    const cur = ((L.presets || [])[L.activePreset] || {}).coffeeId || 0;
    const allP = this.grindShots(), mine = allP.filter((q) => q.c == cur), pts = UI.all || !mine.length ? allP : mine;
    // colours: the coffee being brewed now first (WebPage.h:1505-1506)
    const ids = [...new Set(allP.map((q) => q.c))].sort((a, b) => (b == cur) - (a == cur) || a - b), off = ids.includes(cur) ? 0 : 1;
    const col = new Map(ids.map((id, i) => [id, CCOL[(i + off) % CCOL.length]]));
    const legend = (list) => [...new Set(list.map((q) => q.c))].map((id) => { const q = list.find((p) => p.c == id); return swatch(col.get(id), true) + esc(q.n) + (id == cur ? ' <span class="muted">(brewing now)</span>' : "") + ' <span class="muted">' + list.filter((p) => p.c == id).length + "</span>"; }).join("");
    const c1 = this.$("#gchart");
    if (!pts.length || !G) { emptyNote(c1, "No shots stopped by weight on this grinder yet.", T); this.$("#gleg").innerHTML = ""; this.$("#gnote").textContent = ""; }
    else {
      const tg = G.target_time_s, tol = G.tolerance_s;
      const adv = G.has_number && (G.action == "finer" || G.action == "coarser") && G.suggested_um ? G.suggested_um : 0;
      const cells = grindScatter(c1, pts.map((q) => ({ ...q, col: col.get(q.c), cur: q.c == cur, txt: `<b>${esc(q.n)}</b> ${esc(q.rc)}<br>${esc(fs(g, q.u))}, ${q.t.toFixed(1)} s${q.r ? " " + "★".repeat(q.r) : ""}<br><span class="dim">${esc(q.w)}</span>` })),
        { tg, tol, ups: G.um_per_s, tc: G.average_time_s, uc: G.current_um, adv, fsn: (v) => fsn(g, v), fs: (v) => fs(g, v) }, T);
      attachTips(c1, cells, tip, open);
      this.$("#gleg").innerHTML = legend(pts);
      const set = new Set(pts.map((q) => q.u));
      this.$("#gnote").textContent = pts.length + " shot" + (pts.length == 1 ? "" : "s") + " stopped by weight, at " + set.size + " setting" + (set.size == 1 ? "" : "s") + ". Green band: " + tg + " s ± " + tol + " s." + (G.um_per_s > 0 && G.average_time_s > 0 ? " Dashed: the advisor's line." : "") + (adv ? " Orange diamond: the advice." : "") + (pts === allP || UI.all ? "" : " This coffee only; tick All coffees for the rest.");
    }
    const c2 = this.$("#cchart"), tol = this.S ? this.S.grindToleranceS : 2;
    const seq = allP.slice().sort((a, b) => new Date(a.ts || 0) - new Date(b.ts || 0)).slice(-60);
    if (!seq.length) { emptyNote(c2, "No shots on this grinder yet.", T); return; }
    const k = (q) => { if (!q.tt) return "ok"; const e = q.t - q.tt; return Math.abs(e) <= tol ? "ok" : e < 0 ? "fast" : "slow"; };
    const cells = grindByShot(c2, seq.map((q) => ({ ...q, k: k(q), cur: q.c == cur, txt: `<b>${esc(q.n)}</b> ${esc(q.rc)}<br>${esc(fs(g, q.u))}, ${q.t.toFixed(1)} s against ${q.tt || "?"} s<br><span class="dim">${esc(q.w)}</span>` })), { fsn: (v) => fsn(g, v) }, T);
    attachTips(c2, cells, tip, open);
    const n = { ok: 0, fast: 0, slow: 0 }; seq.forEach((q) => n[k(q)]++);
    this.$("#cleg").innerHTML = swatch(STC.ok, true) + "on time" + swatch(STC.fast, true) + "fast" + swatch(STC.slow, true) + "slow" + '<span class="muted" style="margin-left:12px">ring: rated 4 or 5</span>';
    this.$("#cnote").textContent = seq.length + " shots, oldest first: " + n.ok + " on time, " + n.fast + " fast, " + n.slow + " slow";
  }
}

customElements.define("brew-grinder", BrewGrinder);
