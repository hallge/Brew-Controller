// Home: live readout, what is being brewed, grinder advice, the last shot, getting started,
// community dial-in and suggestions (WebPage.h:243-266, 759-776, 829-853, 879-883, 946-954,
// 1125-1133, 1380-1430, 1806-1815, 1841-1853). Start, stop, tare and machine power are hardware:
// the controller page does those.
import { BrewSection } from "../lib/section.js";
import { html, raw } from "../lib/styles.js";
import { f1, f2, fs, fsn, fdn, fdu, grindOf, ageNote, age, presetLabel, coffeeById, recipeById, stateWords, esc } from "../lib/format.js";
import { adviceText, recommendationText, noGrindText } from "../lib/advice.js";
import { summaryLine, resultLine, rateRowHtml, wireRateRow, drawShot } from "../lib/shotview.js";
import { when, TIP_CSS } from "../lib/charts.js";
import { confirmDialog } from "../lib/forms.js";

export const meta = { id: "home", title: "Home", icon: "mdi:home" };

const START_STEPS = [
  ["Join your Wi-Fi", (x) => x.wifi.connected, (x) => (x.wifi.connected ? "Connected to your home network." : x.wifi.joined ? "Saved, not connected yet." : "Running on its own access point."), "controller"],
  ["Pair your scale", (x) => x.scale.paired, (x) => (x.scale.connected ? "Connected." : x.scale.paired ? "Paired. Switch it on to connect." : "Switch the scale on near the controller."), "controller"],
  ["Pair the brew bot", (x) => x.brew_bot.paired, (x) => (x.brew_bot.paired ? "Paired. It wakes when you press Start." : "Put the bot on the brew button, then pick it in the controller page."), "controller"],
  ["Check your espresso machine", (x) => !!x.machine.maker, (x) => (x.machine.maker ? esc(x.machine.maker + " " + x.machine.model) + "." : "Choose your make and model in the controller page."), "controller"],
  ["Describe your grinder", (x) => x.grinder.set && x.grinder.range, (x) => (x.grinder.set ? esc(x.grinder.model || "Your grinder") + (x.grinder.range ? "." : ": add its range of settings for a first guess.") : "Pick your grinder and how it is set."), "grinder"],
  ["Choose a recipe", (x) => x.recipes > 0, (x) => (x.recipes ? x.recipes + " recipe" + (x.recipes == 1 ? "" : "s") + " ready." : "Add a recipe: dose, yield and brew time."), "recipes"],
  ["Add your coffee", (x) => x.coffees_on_recipe > 0, (x) => (x.coffees_on_recipe ? "On the controller, ready to pick." : "Add the coffee you are brewing and tick a recipe for it."), "coffees"],
  ["Pull your first shot", (x) => x.shots, (x) => (x.shots ? "Done. Grind advice learns from every shot." : "Cup on the scale, then Start on the controller."), "home"],
];

class BrewHome extends BrewSection {
  static get styles() {
    return `${TIP_CSS}
    .live{display:flex;flex-wrap:wrap;gap:24px;align-items:baseline}
    .live .u{font-size:16px;color:var(--secondary-text-color);margin-left:2px}
    .dots{display:flex;flex-wrap:wrap;gap:14px;margin-top:10px;font-size:13px}
    .dot{display:inline-block;width:9px;height:9px;border-radius:50%;background:var(--disabled-text-color,#999);margin-right:6px}
    .dot.on{background:var(--success-color,#43a047)} .dot.am{background:var(--warning-color,#ffa600)} .dot.zz{background:var(--info-color,#039be5)}
    .chips{display:flex;flex-wrap:wrap;gap:6px;margin:8px 0}
    .banner{border-left:4px solid var(--warning-color,#ffa600);padding:8px 12px;background:rgba(var(--rgb-warning-color,255,166,0),.08);border-radius:8px;margin-top:10px}
    .alert{border-left:4px solid var(--error-color);background:rgba(var(--rgb-error-color,219,68,55),.1);padding:10px 12px;border-radius:8px;font-weight:500}
    .steps .st{display:flex;gap:10px;align-items:center;padding:6px 0;border-bottom:1px solid var(--divider-color)}
    .steps .n{width:24px;height:24px;border-radius:50%;display:inline-flex;align-items:center;justify-content:center;background:var(--secondary-background-color);font-size:13px;flex:none}
    .steps .done .n{background:var(--success-color,#43a047);color:#fff}
    .steps .tx{flex:1}
    .bar{height:6px;border-radius:3px;background:var(--secondary-background-color);overflow:hidden;margin:8px 0}
    .bar > i{display:block;height:100%;background:var(--primary-color)}
    .chip-link{cursor:pointer}`;
  }

  async load() {
    const [L, G, SH, S, start, power] = await Promise.all([
      this.store.get("library"), this.store.get("grind"), this.store.get("shots"), this.store.get("settings"),
      this.store.get("start").catch(() => null), this.store.get("power").catch(() => null),
    ]);
    Object.assign(this, { L, G, SH, S, start, power });
    this.g = grindOf(L);
    this.prev = null;
    if (SH && SH.length) {
      const ctx = await this.api.getOr("/api/contexts", { limit: 1 }, []);
      this.prev = Array.isArray(ctx) && ctx[0] && ctx[0].shot_id == SH[0].id ? ctx[0] : null;
    }
    this.community = null;
    const p = (L.presets || [])[L.activePreset];
    if (p && p.coffeeId && p.recipeId) this.community = await this.api.getOr("/api/community", { coffee: p.coffeeId, recipe: p.recipeId }, null);
    const hidden = new Set(this.panel.hiddenTabs || []);
    this.forYou = hidden.has("discover") ? null : await Promise.all([
      this.api.getOr("/api/taste/recommend", { drinker: 0 }, null),
      hidden.has("subscriptions") ? null : this.api.getOr("/api/subscriptions/recommend", { drinker: 0 }, null),
    ]);
  }

  render() {
    const { L, G, SH, g } = this;
    const s = this.panel.status || {};
    const p = (L.presets || [])[L.activePreset] || {};
    const r = recipeById(L, p.recipeId), c = coffeeById(L, p.coffeeId);
    const adv = adviceText(G, g), rec = recommendationText(G, g), nog = noGrindText(G, g);
    const last = SH && SH[0];
    return html`
    <div class="grid">
      <div class="card" id="now">${raw(this.liveHtml(s))}</div>

      <div class="card">
        <h3>Brewing now</h3>
        <label class="f">What the controller will brew
          <select id="psel">${raw((L.presets || []).map((q, i) => (q.hidden ? "" : `<option value="${i}" ${i == L.activePreset ? "selected" : ""}>${esc(presetLabel(L, q))}</option>`)).join(""))}</select>
        </label>
        ${r ? raw(`<div class="chips">${[esc(r.name), r.doseG + " g in, " + r.targetWeightG + " g out", r.targetTimeS + " s",
          r.preinfusion == 1 ? "hold " + r.preinfusionHoldS + " s" + (r.bloomPauseS ? ", bloom " + r.bloomPauseS + " s" : "") : "machine pre-infusion",
          p.grindUm ? esc(fs(g, p.grindUm)) : "grind not set"].map((x) => `<span class="pill">${x}</span>`).join("")}</div>`) : ""}
        <div class="hint">${c ? [c.roaster, c.origin, c.region, c.process].filter((x) => x).join(", ") + (age(c.roastDate) != null ? ((c.roaster || c.origin || c.region || c.process) ? ". " : "") + ageNote(c) : "") + ". " : "No coffee attached. "}${(L.presets || []).filter((q) => !q.hidden).length} entries on the controller${(L.presets || []).some((q) => q.hidden) ? " (" + (L.presets || []).filter((q) => q.hidden).length + " more wait for a new bag)" : ""}, room for ${L.maxPresets}.</div>
      </div>

      <div class="card">
        <div class="spread"><h3>Grinder</h3><a class="link" data-go="grinder">Details</a></div>
        <div class="row"><span class="big">${adv ? adv.current : "--"}</span><span class="muted">${g.unit}</span></div>
        ${adv ? html`<div class="row" style="margin-top:6px"><span class="pill ${adv.cls}">${adv.title}</span></div><div class="hint" style="margin-top:6px">${adv.short}</div>${adv.ageShort ? html`<div class="hint">${adv.ageShort}.</div>` : ""}` : ""}
        ${adv && adv.apply ? html`<div class="row" style="margin-top:8px"><button id="apply" data-um="${adv.apply}">I set the grinder to ${fs(g, adv.apply)}</button></div>` : ""}
        ${rec ? html`<div class="banner"><div>${rec.text}</div><div class="row" style="margin-top:8px"><button class="primary" id="recuse">${rec.use}</button><button id="reckeep">${rec.keep}</button></div></div>` : ""}
        ${nog ? html`<div class="banner"><div>${nog.text}</div><div class="row" style="margin-top:8px">${nog.last ? html`<button class="primary" id="setlast" data-um="${nog.last}">Set ${fs(g, nog.last)}</button>` : ""}<input id="gsetv" placeholder="setting" style="width:110px"><button id="gset">Set</button></div></div>` : ""}
      </div>

      <div class="card"><h3>Previous grinder recommendation</h3>${raw(this.prevHtml())}</div>

      <div class="card" style="grid-column:1/-1">
        <div class="spread"><h3>Most recent shot</h3><a class="link" data-go="shots">All shots</a></div>
        ${last ? html`
          <div class="row" style="gap:26px;align-items:baseline"><span><span class="big">${f1(last.final_weight_g)} g</span> <span class="muted">of ${last.target_weight_g}</span></span>
          <span><span class="big">${f1(last.shot_time_s)} s</span>${last.target_time_s ? html` <span class="muted">of ${last.target_time_s}</span>` : ""}</span></div>
          <div class="hint">${raw(summaryLine(last))} ${resultLine(last)}.</div>
          <canvas id="hchart" style="margin-top:8px"></canvas><div id="href" class="hint"></div>
          ${raw(rateRowHtml(last))}` : html`<div class="empty">No shot yet. Brew one and it appears here.</div>`}
      </div>

      ${raw(this.startHtml())}
      ${raw(this.communityHtml(c, r, p))}
      ${raw(this.forYouHtml())}
    </div>
    <div class="tip" id="tip"></div>`;
  }

  liveHtml(s) {
    if (!s.state) return `<h3>Now</h3><div class="muted">Waiting for the controller…</div>`;
    const fault = /FAILED|DISCONNECTED|STALE|TIMEOUT|INVALID|ERROR/.test(s.state);
    const nocup = s.state == "READY" && s.cup_required && !s.cup_present;
    const label = s.state == "BLOOMING" ? "Bloom " + Math.ceil(s.bloom_left_s || 0) + " s" : stateWords(s.state);
    const belief = this.power && this.power.paired ? this.power.belief : null;
    return `<div class="spread"><h3>Now</h3><span class="pill ${fault ? "bad" : s.state == "READY" ? "ok" : "warn"}">${esc(label)}</span></div>
      ${s.manual_stop_needed ? `<div class="alert">STOP THE MACHINE BY HAND. The stop press was not delivered or confirmed.</div>` : ""}
      ${s.bot_switch_mode ? `<div class="alert">The SwitchBot is in Switch mode. Set it to Press mode in the SwitchBot app.</div>` : ""}
      <div class="live"><span><span class="big" id="lw">${f1(s.weight_g || 0)}</span><span class="u">g</span></span>
        <span><span class="big" id="lt">${f1(s.shot_time_s || 0)}</span><span class="u">s</span></span>
        <span><span class="big" id="lf">${f2(s.flow_g_s || 0)}</span><span class="u">g/s</span></span></div>
      <div class="dots">
        <span><span class="dot ${s.scale_connected ? (nocup ? "am" : "on") : ""}"></span>Scale ${!s.scale_connected ? "not connected" : (nocup ? "no cup" : "connected") + (s.scale_battery != null ? ", " + s.scale_battery + "%" : "")}</span>
        <span><span class="dot ${s.bot_connected ? "on" : s.bot_paired ? "zz" : ""}"></span>Brew bot ${s.bot_connected ? "connected" : s.bot_paired ? "asleep until Start" : "not paired"}${s.bot_battery != null ? ", " + s.bot_battery + "%" : ""}</span>
        ${belief ? `<span><span class="dot ${belief == "on" ? "on" : ""}"></span>Machine ${belief == "on" ? "on" : belief == "off" ? "off" : "not known"} <button class="link" id="belief" data-to="${belief == "on" ? "believe_off" : "believe_on"}" title="Correct what the controller believes; nothing is pressed">it is really ${belief == "on" ? "off" : "on"}</button></span>` : ""}
      </div>
      <div class="hint" style="margin-top:8px">Target ${f1(s.target_weight_g)} g, stops at ${f1(s.target_weight_g - s.stop_offset_g)} g.${s.scale_resting ? " Scale resting: the touch key wakes it and brews." : ""}</div>
      <div class="hw" style="margin-top:10px">Start, stop, tare and machine power are in the <a id="openctl">controller page</a>.</div>`;
  }

  prevHtml() {
    const s = this.SH && this.SH[0], c = this.prev, g = this.g, L = this.L;
    if (!s) return `<span class="muted">No shot yet. The advice given after your next shot appears here.</span>`;
    if (!c || c.shot_id != s.id) return `<span class="muted">No grinder recommendation was recorded for the most recent shot.</span>`;
    const a = c.advice_action, fin = a == "finer", mv = fin || a == "coarser", p = (L.presets || [])[L.activePreset] || {}, G = this.G;
    const title = a == "hold" ? "Keep " + fs(g, c.advice_um) : mv ? `Go ${fin ? "finer" : "coarser"} ${fdn(g, c.advice_delta_um)} ${fdu(g)}, to ${fs(g, c.advice_um)}${c.advice_refined ? " (to your liked time)" : ""}` : a == "inconsistent" ? "Brew times vary: no advice" : a == "no_data" ? "No advice: the shot did not count" : a == "no_grind_set" ? "No advice: no grind setting entered" : "No advice";
    const now = G && G.current_um && s.coffee_id == p.coffeeId && s.recipe_id == p.recipeId && G.current_um != s.grind_um ? " The grinder is at " + fs(g, G.current_um) + " now." : "";
    return `<div class="row"><span class="pill ${a == "hold" ? "ok" : mv || a == "inconsistent" ? "warn" : ""}">${esc(title)}</span></div>
      <div class="hint" style="margin-top:6px">After <b>${esc(s.coffee || "no coffee")}</b>, ${esc(s.recipe)}, ${esc(when(s))}, pulled at ${s.grind_um ? esc(fs(g, s.grind_um)) : "no setting recorded"}.${c.advice_shots ? " From " + c.advice_shots + " shot" + (c.advice_shots == 1 ? "" : "s") + " at that setting." : ""}${mv && c.advice_step_limited ? " Limited to the largest single change." : ""}${esc(now)}</div>`;
  }

  startHtml() {
    const x = this.start;
    if (!x || !x.wifi) return "";
    const done = START_STEPS.map((s) => !!s[1](x)), n = done.filter(Boolean).length;
    if (n == START_STEPS.length) return "";
    const next = done.indexOf(false);
    return `<div class="card"><div class="spread"><h3>Getting started</h3><span class="pill">${n} of ${START_STEPS.length} done</span></div>
      <div class="bar"><i style="width:${Math.round(100 * n / START_STEPS.length)}%"></i></div>
      <div class="steps">${START_STEPS.map((s, i) => `<div class="st ${done[i] ? "done" : ""}"><span class="n">${done[i] ? "✓" : i + 1}</span><div class="tx"><b>${s[0]}</b><div class="hint">${s[2](x)}</div></div>
        <button class="${i == next ? "primary" : ""}" data-step="${s[3]}">${done[i] ? "Change" : "Set it up"}</button></div>`).join("")}</div></div>`;
  }

  communityHtml(c, r, p) {
    const m = this.community, g = this.g;
    if (!c || !r || !m) return "";
    if (!m.found || !m.start) return `<div class="card"><h3>From other owners</h3><div class="hint">${esc("Nobody has shared a settled dial-in for " + c.name + " with a recipe like " + r.name + " yet." + (m.reachable === false ? " The shared library cannot be reached from this controller yet." : ""))}</div></div>`;
    const s = m.start;
    return `<div class="card"><h3>From other owners</h3><div class="tiles">
      <div class="tile"><div class="k">Start your grinder at</div><div class="v">${s.setting ? esc(fsn(g, s.setting) + " ± " + fdn(g, s.range) + " " + g.unit) : "not known"}</div></div>
      <div class="tile"><div class="k">What a good shot looked like</div><div class="v">${f1(m.time_s)} s</div><div class="k">${r.doseG} g in, ${r.targetWeightG} g out</div></div>
      <div class="tile"><div class="k">Based on</div><div class="v">${m.contributors} ${m.contributors == 1 ? "person" : "people"}</div><div class="k">${m.dialins} dial-in${m.dialins == 1 ? "" : "s"}, within ± ${f1(m.spread_um)} µm</div></div></div>
      ${s.setting ? `<div class="row" style="margin-top:10px"><button id="usestart" data-um="${s.setting}">Use ${esc(fs(g, s.setting))} as my starting grind</button><span class="hint">${p.grindUm ? "Your setting for it now: " + esc(fs(g, p.grindUm)) + "." : "No setting entered for it yet."}</span></div>` : ""}</div>`;
  }

  forYouHtml() {
    if (!this.forYou) return "";
    const [t, sub] = this.forYou, chips = [];
    if (t && t.items && t.items.length) chips.push(`<span class="pill chip-link" data-go="discover">Coffees you might like: ${esc(t.items[0].name)}${t.items.length > 1 ? " and " + (t.items.length - 1) + " more" : ""}</span>`);
    if (sub && sub.items && sub.items.length) chips.push(`<span class="pill chip-link" data-go="discover">A subscription for you: ${esc(sub.items[0].service.name)}</span>`);
    const G = this.G;
    if (G && (G.action == "finer" || G.action == "coarser")) chips.push(`<span class="pill chip-link warn" data-go="grinder">Next shot: go ${G.action} ${esc(fdn(this.g, G.delta_um))} ${esc(fdu(this.g))}</span>`);
    return chips.length ? `<div class="card"><h3>For you</h3><div class="row">${chips.join("")}</div></div>` : "";
  }

  wire() {
    this.$$("[data-go]").forEach((a) => a.onclick = () => this.panel.navigate(a.dataset.go));
    this.$$("[data-step]").forEach((b) => b.onclick = () => (b.dataset.step === "controller" ? this.panel.openController() : this.panel.navigate(b.dataset.step)));
    this.wireLive();
    const psel = this.$("#psel");
    if (psel) psel.onchange = async () => {
      const r = await this.post("/api/library/do", null, "Brewing " + psel.selectedOptions[0].textContent, { what: "preset_select", a: +psel.value });
      if (r.ok) this.store.invalidate("library", "grind");
    };
    const setGrind = async (um) => {
      if (um == null || !(um >= 1 && um <= 2000)) { this.panel.toast("That is not a setting this grinder can have", false); return; }
      const r = await this.post("/api/grind/set", null, "Grind setting saved", { um: Math.round(um) });
      if (r.ok) this.store.invalidate("library", "grind");
    };
    const apply = this.$("#apply"); if (apply) apply.onclick = () => setGrind(+apply.dataset.um);
    const setlast = this.$("#setlast"); if (setlast) setlast.onclick = () => setGrind(+setlast.dataset.um);
    const gset = this.$("#gset"); if (gset) gset.onclick = () => {
      import("../lib/format.js").then(({ ps }) => setGrind(ps(this.g, this.$("#gsetv").value)));
    };
    const rec = async (use) => { const r = await this.post("/api/grind/recommendation", null, use ? "Grind setting saved" : null, { use }); if (r.ok) this.store.invalidate("grind", "library"); };
    const ru = this.$("#recuse"); if (ru) ru.onclick = () => rec(1);
    const rk = this.$("#reckeep"); if (rk) rk.onclick = () => rec(0);
    const us = this.$("#usestart");
    if (us) us.onclick = async () => {
      const L = this.L, i = L.activePreset, p = L.presets[i], um = +us.dataset.um;
      if (p.grindUm && p.grindUm != um && !(await confirmDialog(this.shadowRoot, "Replace your setting of " + fs(this.g, p.grindUm) + " with " + fs(this.g, um) + "?"))) return;
      const r = await this.post("/api/library/do", null, "Starting grind set", { what: "preset_grind", a: i, b: um });
      if (r.ok) this.store.invalidate("library", "grind");
    };
    const last = this.SH && this.SH[0];
    if (last) {
      wireRateRow(this, this.shadowRoot, last, () => this.store.invalidate("shots"));
      drawShot(this.api, this.$("#hchart"), last, this.SH, 220).then(({ ref }) => {
        const el = this.$("#href");
        if (el) el.textContent = "White: weight. Orange: flow." + (ref ? " Dashed green: your best-rated shot of this coffee (" + "★".repeat(ref.rating) + ", " + f1(ref.shot_time_s) + " s)." : "");
      });
    }
  }

  wireLive() {
    const oc = this.$("#openctl"); if (oc) oc.onclick = () => this.panel.openController();
    const b = this.$("#belief");
    if (b) b.onclick = async () => { const r = await this.post("/api/power", { action: b.dataset.to }, "Noted: nothing was pressed"); if (r.ok) { this.store.invalidate("power"); } };
  }

  onStatus(s) {
    const now = this.$("#now");
    if (!now) return;
    // Light update: the numbers every second; the whole card only when the state changes.
    if (this._lastState === s.state && this.$("#lw")) {
      this.$("#lw").textContent = f1(s.weight_g || 0);
      this.$("#lt").textContent = f1(s.shot_time_s || 0);
      this.$("#lf").textContent = f2(s.flow_g_s || 0);
      return;
    }
    this._lastState = s.state;
    now.innerHTML = this.liveHtml(s);
    this.wireLive();
  }
}

customElements.define("brew-home", BrewHome);
