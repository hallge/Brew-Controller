// Recipes: list, form (dose and yield by weight or ratio, target time, pre-infusion and bloom,
// who ends the shot, starting offset), duplicate, delete, share to the recipe book, used with
// (grind, learned offset, reset, take off), list by itself (WebPage.h:334-361, 1361-1376,
// 1077-1082). Another tab can pre-fill a new recipe through `store.prefill`.
import { BrewSection } from "../lib/section.js";
import { html, raw } from "../lib/styles.js";
import { f2, fsn, grindOf, coffeeById, esc } from "../lib/format.js";
import { confirmDialog } from "../lib/forms.js";

export const meta = { id: "recipes", title: "Recipes", icon: "mdi:book-open-variant" };

const RF = ["name", "doseG", "useRatio", "targetWeightG", "ratio", "targetTimeS", "preinfusion", "preinfusionHoldS", "bloomPauseS", "timeLimit", "maxShotTimeS", "baselineOffsetG"];
const BLANK = { name: "", doseG: 18, useRatio: false, targetWeightG: 36, ratio: 2, targetTimeS: 28, preinfusion: 0, preinfusionHoldS: 3, bloomPauseS: 0, timeLimit: 0, maxShotTimeS: 50, baselineOffsetG: 1.5 };
const SHARE_WORDS = { queued: "waiting to be sent", sent: "sent", review: "in review", published: "published in the recipe book", declined: "declined", refused: "refused" };
const UI = { R: -1 };

class BrewRecipes extends BrewSection {
  static get styles() {
    return `.split{display:grid;grid-template-columns:minmax(240px,320px) 1fr;gap:16px;align-items:start} @media (max-width:860px){.split{grid-template-columns:1fr}}
    .item{display:block;width:100%;text-align:left;border:1px solid var(--divider-color);border-radius:10px;padding:8px 10px;margin:6px 0;background:transparent;color:var(--primary-text-color);font-weight:400}
    .item.on{border-color:var(--primary-color);background:rgba(var(--rgb-primary-color,3,169,244),.08)} .item b{display:block} .item span{font-size:13px;color:var(--secondary-text-color)}
    fieldset{border:1px solid var(--divider-color);border-radius:10px;margin:0 0 12px;padding:10px 12px} legend{color:var(--secondary-text-color);font-size:13px;padding:0 6px}
    .stack > .card{margin-bottom:16px}`;
  }

  async load() {
    const L = await this.store.get("library");
    this.L = L;
    this.g = grindOf(L);
    this.LP = await this.api.getOr("/api/library/pending", null, null);
    if (this.store.prefill) { UI.R = 0; }
    else if (UI.R < 0 || (UI.R && !(L.recipes || []).some((r) => r.id == UI.R))) UI.R = ((L.presets || [])[L.activePreset] || {}).recipeId || ((L.recipes || [])[0] || {}).id || 0;
  }

  render() {
    const L = this.L, R = UI.R;
    const r = { ...BLANK, ...((L.recipes || []).find((x) => x.id == R) || {}), ...(this.store.prefill || {}) };
    const sel = (name, opts, v) => `<select name="${name}">${opts.map(([k, l]) => `<option value="${k}" ${String(k) == String(v) ? "selected" : ""}>${l}</option>`).join("")}</select>`;
    const st = R ? ((this.LP && this.LP.recipes) || []).find((x) => x.id == R) || {} : {};
    const use = R ? L.presets.map((p, i) => ({ p, i })).filter((x) => x.p.recipeId == R) : [];
    return html`<div class="split">
      <div class="card"><div class="spread"><h3>Recipes</h3><button class="primary" id="new">New recipe</button></div>
        ${raw((L.recipes || []).map((x) => { const n = L.presets.filter((p) => p.recipeId == x.id).length; return `<button class="item ${x.id == R ? "on" : ""}" data-r="${x.id}"><b>${esc(x.name)}</b><span>${x.doseG} g in, ${x.targetWeightG} g out · ${n ? n + (n == 1 ? " entry" : " entries") + " on the controller" : "not on the controller"}</span></button>`; }).join(""))}
        <div class="hint">A recipe says how to pull the shot. It holds nothing about the coffee or the grind, so one recipe serves every coffee.</div></div>
      <div class="stack">
        <div class="card">
          <form id="rf">
            <div class="spread"><label class="f" style="flex:1">Name<input name="name" maxlength="23" value="${r.name}" style="font-size:17px"></label>
              ${R ? html`<div class="row"><button type="button" id="dup">Duplicate</button><button type="button" class="danger" id="del">Delete</button></div>` : ""}</div>
            <fieldset><legend>Dose and yield</legend><div class="form">
              <label class="f">Dose in (g)<input name="doseG" type="number" step="0.1" min="5" max="30" value="${r.doseG}"></label>
              <label class="f">Yield set by${raw(sel("useRatio", [[0, "Target weight"], [1, "Ratio"]], r.useRatio ? 1 : 0))}</label>
              <label class="f">Target out (g)<input name="targetWeightG" type="number" step="0.1" min="5" max="200" value="${r.targetWeightG}"></label>
              <label class="f">Ratio 1:<input name="ratio" type="number" step="0.1" min="1" max="5" value="${r.ratio}"></label>
              <label class="f">Target brew time (s)<input name="targetTimeS" type="number" step="0.5" min="5" max="60" value="${r.targetTimeS}"></label></div>
              <div class="hint">The target brew time is what grind advice steers toward.</div></fieldset>
            <fieldset><legend>Pre-infusion and bloom</legend><div class="form">
              <label class="f">How the button is pressed${raw(sel("preinfusion", [[0, "Tap: the machine's own pre-infusion"], [1, "Hold the button for a set time"]], r.preinfusion))}</label>
              <label class="f">Hold time (s)<input name="preinfusionHoldS" type="number" step="0.1" min="0.3" max="30" value="${r.preinfusionHoldS}"></label>
              <label class="f">Bloom pause (s, 0 = none)<input name="bloomPauseS" type="number" step="1" min="0" max="45" value="${r.bloomPauseS}"></label></div>
              <div class="hint" id="rbloom"></div></fieldset>
            <fieldset><legend>If the target is never reached</legend><div class="form">
              <label class="f">Who ends the shot${raw(sel("timeLimit", [[0, "The machine's own time-out"], [1, "The Bot, after a set time"]], r.timeLimit))}</label>
              <label class="f">Stop after (s)<input name="maxShotTimeS" type="number" min="5" max="90" value="${r.maxShotTimeS}"></label>
              <label class="f">Starting stop offset, until learned (g)<input name="baselineOffsetG" type="number" step="0.1" min="0" max="10" value="${r.baselineOffsetG}"></label></div></fieldset>
          </form>
          <div class="row"><button class="primary" id="save">Save</button>
            ${R ? html`<button id="share" ${this.LP && this.LP.linked ? "" : "disabled"} title="${this.LP && this.LP.linked ? "" : "Link the shared library in Settings first"}">${st.state && st.state != "local" ? "Share again" : "Share to the recipe book"}</button>` : ""}
            <span class="hint">${!R ? (this.store.prefill ? "Filled in: check it and save." : "New recipe: not saved yet.") : st.state && st.state != "local" ? "Recipe book: " + (SHARE_WORDS[st.state] || st.state) + (st.note ? ": " + st.note : "") : ""}</span></div>
        </div>
        ${R ? html`<div class="card"><div class="spread"><h3>Used with</h3><a class="link" data-go="coffees">Add to a coffee</a></div>
          <table><tr><th>Coffee</th><th>Grind ${this.g.unit}</th><th>Learned stop offset g</th><th></th></tr>
          ${raw(use.length ? use.map((x) => { const c = coffeeById(L, x.p.coffeeId); return `<tr><td><b>${c ? esc(c.name) : "No coffee attached"}</b>${x.i == L.activePreset ? ' <span class="muted">in use now</span>' : ""}${x.p.hidden ? ' <span class="muted">bag finished</span>' : ""}</td><td>${esc(fsn(this.g, x.p.grindUm) || "not set")}</td><td>${f2(x.p.stopOffsetG)}</td><td><button class="link" data-reset="${x.i}">reset offset</button> <button class="link" data-off="${x.i}">take off the controller</button></td></tr>`; }).join("") : '<tr><td colspan="4" class="muted">Not on the controller yet.</td></tr>')}</table>
          ${use.some((x) => !x.p.coffeeId) ? "" : html`<div class="row" style="margin-top:8px"><button id="alone">List it on the controller by itself</button></div>`}
          <div class="hint">The controller lists coffee and recipe as one line to tap. A recipe listed by itself is for guests and one-off bags.</div></div>` : ""}
      </div></div>`;
  }

  /** Bloom forces a held pre-infusion; ratio or weight; time limit (WebPage.h:1361-1364). */
  formState() {
    const f = this.$("#rf"), bloom = +f.bloomPauseS.value > 0;
    if (bloom) f.preinfusion.value = 1;
    f.preinfusion.disabled = bloom;
    f.targetWeightG.disabled = f.useRatio.value == "1";
    f.ratio.disabled = f.useRatio.value != "1";
    f.preinfusionHoldS.disabled = f.preinfusion.value != "1";
    f.maxShotTimeS.disabled = f.timeLimit.value != "1";
    this.$("#rbloom").textContent = bloom
      ? "Bloom: the button is held for the hold time, then pressed again to switch the pump off. After the pause it is held for one second, which skips the machine's own pre-infusion, and the brew runs to the target. The pause counts toward the brew time, and \"Stop after\" must leave room for it."
      : "Holding MANUAL on the Breville keeps it in low-pressure pre-infusion for as long as it is held. A bloom pause switches the pump off after the hold and starts it again after the pause; it needs a held pre-infusion.";
  }

  read() {
    const f = this.$("#rf"), b = { id: UI.R || 0 };
    RF.forEach((k) => { const v = f[k].value; b[k] = k == "name" ? v.trim() : k == "useRatio" ? v === "1" : +v; });
    if (b.useRatio) b.targetWeightG = Math.round(b.doseG * b.ratio * 10) / 10; // WebPage.h:1373
    return b;
  }

  async act(what, a, b, ok) {
    const r = await this.post("/api/library/do", null, ok || "Saved", { what, a: a || 0, b: b || 0 });
    if (r.ok) this.store.invalidate("library");
    return r;
  }

  wire() {
    this.formState();
    const f = this.$("#rf");
    ["useRatio", "preinfusion", "timeLimit"].forEach((n) => f[n].onchange = () => this.formState());
    f.bloomPauseS.oninput = () => this.formState();
    this.$("#new").onclick = () => { UI.R = 0; this.store.prefill = null; this._paint(); };
    this.$$("[data-r]").forEach((b) => b.onclick = () => { UI.R = +b.dataset.r; this.store.prefill = null; this._paint(); });
    this.$$("[data-go]").forEach((a) => a.onclick = () => this.panel.navigate(a.dataset.go));
    this.$("#save").onclick = async () => {
      const b = this.read();
      if (!b.name) { this.panel.toast("A recipe needs a name", false); return; }
      const r = await this.post("/api/recipe", b, "Recipe saved");
      if (r.ok) { UI.R = r.id; this.store.prefill = null; this.store.invalidate("library"); }
    };
    const dup = this.$("#dup");
    if (dup) dup.onclick = async () => { const b = this.read(); b.id = 0; b.name = (b.name + " 2").slice(0, 23); const r = await this.post("/api/recipe", b, "Copy made: not on the controller yet"); if (r.ok) { UI.R = r.id; this.store.invalidate("library"); } };
    const del = this.$("#del");
    if (del) del.onclick = async () => { const r0 = this.L.recipes.find((x) => x.id == UI.R); if (!r0 || !(await confirmDialog(this.shadowRoot, 'Delete the recipe "' + r0.name + '"?'))) return; const r = await this.act("recipe_delete", UI.R, 0, "Deleted"); if (r.ok) UI.R = -1; };
    const sh = this.$("#share");
    if (sh) sh.onclick = async () => { const r = await this.post("/api/recipes/share", null, "Queued for the recipe book", { id: UI.R }); if (r.ok) setTimeout(() => this.refresh(), 1500); };
    this.$$("[data-reset]").forEach((b) => b.onclick = async () => { const r = await this.post("/api/offset/reset", null, "Offset reset", { recipe: +b.dataset.reset }); if (r.ok) this.store.invalidate("library"); });
    this.$$("[data-off]").forEach((b) => b.onclick = () => this.act("preset_delete", +b.dataset.off, 0, "Taken off the controller"));
    const alone = this.$("#alone"); if (alone) alone.onclick = () => this.act("preset_add", UI.R, 0, "Listed on the controller");
  }
}

customElements.define("brew-recipes", BrewRecipes);
