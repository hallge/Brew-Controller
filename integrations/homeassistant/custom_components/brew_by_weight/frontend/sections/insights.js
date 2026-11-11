// Insights: taste profile, roast level against grind, coffee age against brew time, brewing
// calendar, dial-in shots per bag, grind advice against target (WebPage.h:408-419, 935-944,
// 1603-1799). Everything is drawn from the shots and a few read-only endpoints.
import { BrewSection } from "../lib/section.js";
import { html, raw } from "../lib/styles.js";
import { f1, fs, fsn, grindOf, coffeeById, esc } from "../lib/format.js";
import {
  theme, attachTips, emptyNote, radar, roastGrind, ageHeat, calendar, dayTimes, dialIn, grindAccuracy,
  RL, STC, FLOW, ramp, heat, swatch, byWeight, btime, ageDays, TIP_CSS,
} from "../lib/charts.js";

export const meta = { id: "insights", title: "Insights", icon: "mdi:chart-scatter-plot", optional: true };

const UI = { coffee: "", drinker: 0, cal: "shots", days: 14 };

class BrewInsights extends BrewSection {
  static get styles() {
    return `${TIP_CSS}
    .leg{display:flex;flex-wrap:wrap;align-items:center;gap:4px;font-size:13px;margin-top:8px}
    .two{display:grid;grid-template-columns:1fr 1fr;gap:8px} @media (max-width:700px){.two{grid-template-columns:1fr}}
    .likes{display:flex;flex-wrap:wrap;gap:6px;margin-top:6px}
    .seg button.on{background:var(--primary-color);color:var(--text-primary-color,#fff);border-color:var(--primary-color)}`;
  }

  async load() {
    const [SH, L, S] = await Promise.all([this.store.get("shots"), this.store.get("library"), this.store.get("settings")]);
    const [RT, TP, ACC, PWR] = await Promise.all([
      this.store.get("ratings").catch(() => null),
      this.api.getOr("/api/taste/profile", { drinker: UI.drinker }, null),
      this.api.getOr("/api/grind/accuracy", null, null),
      this.api.getOr("/api/power/events", { days: 90 }, null),
    ]);
    Object.assign(this, { SH: Array.isArray(SH) ? SH : [], L, S, RT, TP, ACC, PWR: Array.isArray(PWR) ? PWR : null, g: grindOf(L) });
  }

  shots() { return this.SH.filter((s) => !UI.coffee || String(s.coffee_id || 0) == UI.coffee); }
  get tol() { return this.S ? this.S.grindToleranceS : 2; }

  render() {
    const ids = [...new Map(this.SH.filter((s) => s.coffee).map((s) => [String(s.coffee_id || 0), s.coffee])).entries()];
    const RT = this.RT, named = RT && RT.named, people = named ? RT.drinkers.map((n, i) => [i + 1, n]).filter((x) => x[1]) : [];
    if (!named) UI.drinker = 0;
    return html`
    <div class="row" style="margin-bottom:12px"><label class="f">Coffee<select id="icoffee"><option value="">All coffees</option>${raw(ids.map(([id, n]) => `<option value="${id}" ${id == UI.coffee ? "selected" : ""}>${esc(n)}</option>`).join(""))}</select></label></div>
    <div class="grid">
      <div class="card" style="grid-column:1/-1">
        <div class="spread"><h3>Your coffee taste</h3>${people.length ? html`<div class="row seg">${raw([[0, "Household"], ...people].map(([id, n]) => `<button class="${id == UI.drinker ? "on" : ""}" data-drinker="${id}">${esc(n)}</button>`).join(""))}</div>` : ""}</div>
        ${this.TP ? html`<div class="two"><canvas id="tcup"></canvas><canvas id="tfl"></canvas></div><div class="hint" id="tnote">${raw(this.tasteNote())}</div>${raw(this.likedHtml())}` : html`<div class="hint">No taste profile from the controller.</div>`}
      </div>
      <div class="card"><h3>Roast level against grind</h3><canvas id="irchart"></canvas><div class="leg" id="irleg"></div><div class="hint" id="irnote"></div></div>
      <div class="card"><h3>Coffee age against brew time</h3><canvas id="iachart"></canvas><div class="leg" id="ialeg"></div><div class="hint" id="ianote"></div></div>
      <div class="card" style="grid-column:1/-1">
        <div class="spread"><h3>Brewing calendar</h3><div class="row seg">${raw([["shots", "Shots"], ["brew", "Coffee & recipe"], ["drift", "Drift"], ["hours", "Time of day"]].map(([m, l]) => `<button class="${UI.cal == m ? "on" : ""}" data-cal="${m}">${l}</button>`).join(""))}</div></div>
        <div style="overflow-x:auto"><canvas id="icchart" style="min-width:640px"></canvas></div><div class="leg" id="icleg"></div>
      </div>
      <div class="card"><h3>Dial-in shots per bag</h3><canvas id="idchart"></canvas><div class="leg" id="idleg"></div><div class="hint" id="idnote"></div></div>
      <div class="card"><h3>Grind advice against target</h3><div class="tiles" id="igtiles" style="margin-bottom:8px"></div><canvas id="igchart"></canvas><div class="leg" id="igleg"></div><div class="hint" id="ignote"></div></div>
    </div>
    <div class="tip" id="tip"></div>`;
  }

  tasteNote() {
    const t = this.TP, p = t.profile;
    const lean = t.dims.map((d, i) => ({ d, i, s: (p.v[i] - 0.5) * Math.min(1, p.known[i] * 1.5) })).filter((o) => Math.abs(o.s) > 0.06);
    const name = (o) => (o.i < 5 ? (o.s > 0 ? "more " : "less ") + o.d : o.d);
    const to = lean.filter((o) => o.s > 0).sort((a, b) => b.s - a.s).slice(0, 4).map(name);
    const from = lean.filter((o) => o.s < 0 && o.i >= 5).sort((a, b) => a.s - b.s).slice(0, 3).map(name);
    if (!p.coffees) return "No coffees rated yet. Rate the coffees you drink on the controller (How you liked it) and your taste shows here.";
    return (!p.shown ? `Early days: ${p.coffees} coffee${p.coffees == 1 ? "" : "s"} rated; the profile shows from 3. ` : "") +
      (to.length ? `You lean towards <b>${esc(to.join(", "))}</b>` : "") + (from.length ? `${to.length ? "; " : "You lean "}away from <b>${esc(from.join(", "))}</b>` : "") + (to.length || from.length ? "." : "") +
      (p.shown && !p.settled ? ` (${p.coffees} coffees rated: settles from 8)` : "");
  }

  likedHtml() {
    const best = (this.TP && this.TP.best) || [];
    if (!best.length) return "";
    return `<div class="muted" style="margin-top:10px">Liked best</div><div class="likes">${best.map((b) => { const c = coffeeById(this.L, b.coffee); return c ? `<span class="pill">${esc(c.name)} <b style="color:var(--warning-color)">${"★".repeat(b.stars)}</b></span>` : ""; }).join("")}</div>`;
  }

  wire() {
    this.$("#icoffee").onchange = (e) => { UI.coffee = e.target.value; this._paint(); };
    this.$$("[data-drinker]").forEach((b) => b.onclick = () => { UI.drinker = +b.dataset.drinker; this.refresh(); });
    this.$$("[data-cal]").forEach((b) => b.onclick = () => { UI.cal = b.dataset.cal; this._paint(); });
    const tip = this.$("#tip"), open = (q) => q.click && this.panel.navigate("shots", q.click);
    const T = theme(this);
    // taste
    if (this.TP) {
      const p = this.TP.profile, lab = this.TP.dims.map((d) => d[0].toUpperCase() + d.slice(1));
      radar(this.$("#tcup"), lab.slice(0, 5), p.v.slice(0, 5), p.known.slice(0, 5), T);
      radar(this.$("#tfl"), lab.slice(5), p.v.slice(5), p.known.slice(5), T);
    }
    const tol = this.tol, L = this.L, sh = this.shots(), g = this.g;
    // roast vs grind
    {
      const c = this.$("#irchart");
      const pts = sh.filter((s) => byWeight(s) && s.grind_um && RL.indexOf(s.roast_level) > 0 && (s.grinder_calibration || 0) == (L.grinder || {}).calibration);
      if (!pts.length) { emptyNote(c, "Needs shots with a roast level and a grind setting on this grinder.", T); }
      else {
        const r = roastGrind(c, pts, { tol, fsn: (v) => fsn(g, v), fs: (v) => fs(g, v) }, T);
        attachTips(c, r.cells, tip, open);
        this.$("#irleg").innerHTML = '<span class="muted">Days off roast:</span>' + [0, 0.25, 0.5, 0.75, 1].map((t) => swatch(ramp(t)) + Math.round(t * r.amax)).join("") + '<span class="muted" style="margin-left:14px">larger dot: faster pour</span>' +
          (r.bands.length ? "<br>" + swatch("rgba(76,195,138,.4)") + '<span class="muted">ran on time at </span>' + r.bands.map((b) => `${RL[b.l]} ${b.lo == b.hi ? esc(fsn(g, b.lo)) : esc(fsn(g, b.lo) + "–" + fsn(g, b.hi))} <span class="muted">(middle ${esc(fsn(g, b.md))}, ${b.n} shot${b.n == 1 ? "" : "s"})</span>`).join(", ") : '<br><span class="muted">No shot on this grinder ran on time yet: no band.</span>');
        this.$("#irnote").textContent = pts.length + " shots, " + r.levels + " roast level" + (r.levels == 1 ? "" : "s");
      }
    }
    // age heat
    {
      const c = this.$("#iachart"), all = sh.filter(byWeight), pts = all.filter((s) => s.target_time_s && ageDays(s) != null && btime(s) > 0);
      if (!pts.length) emptyNote(c, "Needs shots with a roast date and a target time.", T);
      else {
        const r = ageHeat(c, pts, tol, T);
        attachTips(c, r.cells, tip);
        this.$("#ialeg").innerHTML = '<span class="muted">Shots:</span>' + [1, Math.ceil(r.mx / 2), r.mx].filter((v, i, a) => a.indexOf(v) == i).map((v) => swatch(heat(0.2 + 0.8 * v / r.mx, T)) + v).join("");
        this.$("#ianote").textContent = pts.length + " shots" + (all.length > pts.length ? ", " + (all.length - pts.length) + " without a roast date or target left out" : "");
      }
    }
    // calendar
    {
      const c = this.$("#icchart"), dated = sh.filter((s) => s.timestamp), cur = ((L.presets || [])[L.activePreset] || {}).coffeeId || 0;
      if (!dated.length) emptyNote(c, "No dated shots yet.", T);
      else if (UI.cal == "hours") {
        const r = dayTimes(c, dated, this.PWR, UI.days, { curCoffee: cur }, T);
        attachTips(c, r.cells, tip, open);
        this.$("#icleg").innerHTML = '<span class="muted">Days:</span>' + [14, 30, 90].map((n) => ` <button class="${n == UI.days ? "primary" : ""}" data-days="${n}">${n}</button>`).join("") +
          (r.inR.length ? r.cids.map((id) => { const q = r.inR.find((s) => (s.coffee_id || 0) == id); return swatch(r.cc.get(id), true) + esc(q.coffee || "no coffee"); }).join("") + '<span class="muted" style="margin-left:12px">Recipes:</span>' + [...r.tg].map(([n, t]) => ` <b>${esc(t)}</b> ${esc(n || "no recipe")}`).join(",") : '<span class="muted" style="margin-left:10px">No shots in these days</span>') +
          (this.PWR == null ? "" : `<span style="margin-left:12px;color:${STC.ok}">▲</span> machine on <span class="muted">▼</span> off`);
        this.$$("[data-days]").forEach((b) => b.onclick = () => { UI.days = +b.dataset.days; this._paint(); });
      } else {
        const r = calendar(c, dated, UI.cal, { tol, curCoffee: cur }, T);
        attachTips(c, r.cells, tip, open);
        if (UI.cal == "brew") this.$("#icleg").innerHTML = r.cids.map((id) => { const q = r.inR.find((s) => (s.coffee_id || 0) == id); return swatch(r.cc.get(id)) + esc(q ? q.coffee || "no coffee" : "") + (id == cur && id ? ' <span class="muted">(brewing now)</span>' : ""); }).join("") + '<span class="muted" style="margin-left:14px">Recipes:</span>' + [...r.tg].map(([n, t]) => ` <b>${esc(t)}</b> ${esc(n || "no recipe")}`).join(",");
        else this.$("#icleg").innerHTML = UI.cal == "shots" ? '<span class="muted">Shots a day:</span>' + swatch(heat(0.2 + 0.8 / r.mx, T)) + "1" + swatch(heat(1, T)) + r.mx : '<span class="muted">Average brew time against target:</span>' + swatch(STC.ok) + "on time" + swatch(STC.fast) + "fast" + swatch(STC.slow) + "slow" + swatch(T.edge) + "not timed";
        c.parentElement.scrollLeft = c.parentElement.scrollWidth;
      }
    }
    // dial-in
    {
      const c = this.$("#idchart"), pts = sh.filter((s) => byWeight(s) && s.target_time_s && btime(s) > 0 && s.timestamp).sort((a, b) => new Date(a.timestamp) - new Date(b.timestamp));
      if (!pts.length) emptyNote(c, "Needs shots stopped by weight with a target time.", T);
      else {
        const r = dialIn(c, pts, tol, T);
        attachTips(c, r.cells, tip);
        this.$("#idleg").innerHTML = swatch(FLOW) + "shots to the first on time (dashed: none on time yet)" + '<span class="muted" style="margin-left:12px">after it:</span>' + swatch(STC.ok) + "on time" + swatch(STC.fast) + "fast" + swatch(STC.slow) + "slow";
        this.$("#idnote").textContent = r.n + " bag" + (r.n == 1 ? "" : "s") + (r.avg != null ? ", " + r.avg.toFixed(1) + " shots to dial in on average" : "") + (r.drift ? ", " + r.drift + " drifted off since" : "");
      }
    }
    // grind accuracy
    {
      const c = this.$("#igchart"), d = this.ACC;
      const ev = d ? (d.events || []).filter((e) => e.target_s > 0) : [];
      if (!ev.length) emptyNote(c, "Shows once a shot has followed a change the advice asked for.", T);
      else {
        const r = grindAccuracy(c, d, { nameOf: (id) => (coffeeById(L, id) || {}).name, fs: (v) => fs(g, v) }, T);
        attachTips(c, r.cells, tip);
        const pc = Math.round(100 * d.score), tile = (t, v, u) => `<div class="tile"><div class="k">${t}</div><div class="v">${v}</div><div class="k">${u}</div></div>`;
        this.$("#igtiles").innerHTML = tile("On target", r.followed ? Math.round(100 * r.on / r.followed) + " %" : "--", r.on + " of " + r.followed + " followed changes") + tile("Typical miss", r.followed ? f1(d.rms_s) + " s" : "--", "from the target time") + tile("Confidence", pc + " %", pc >= 60 ? "high: bigger steps allowed" : pc >= 35 ? "medium" : "low: smaller steps");
        this.$("#igleg").innerHTML = swatch(STC.ok, true) + "landed on time" + swatch(STC.fast, true) + "still fast" + swatch(STC.slow, true) + "still slow" + `<span style="display:inline-block;width:16px;height:2px;background:${FLOW};margin:0 4px 0 12px;vertical-align:middle"></span>confidence`;
        this.$("#ignote").textContent = r.events + " change" + (r.events == 1 ? "" : "s") + " advised, " + r.followed + " followed";
      }
    }
  }
}

customElements.define("brew-insights", BrewInsights);
