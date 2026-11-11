// Summary: the Brew Controller at a glance, Home Assistant's home screen for it.
// Read-only apart from the everyday actions the proxy allows (rate the last shot, set the advised
// grind, bag finished / opened / arrived, a delivery arrived or skipped, upkeep done). Everything
// that sets the controller up links to the controller's own page.
import { BrewSection } from "../lib/section.js";
import { html, raw } from "../lib/styles.js";
import { f1, f2, fs, grindOf, age, ageNote, ago, money, coffeeById, stateWords, esc } from "../lib/format.js";
import { adviceText, recommendationText, noGrindText } from "../lib/advice.js";
import { resultLine, rateRowHtml, wireRateRow, drawShot } from "../lib/shotview.js";
import { TIP_CSS } from "../lib/charts.js";
import { confirmDialog } from "../lib/forms.js";
import { JOB_NAMES } from "../lib/upkeep.js";
import { nextPowerEvents, daysText, hhmm, whenText } from "../lib/schedule.js";

export const meta = { id: "summary", title: "Summary", icon: "mdi:view-dashboard" };

const FAULT = /FAILED|DISCONNECTED|STALE|TIMEOUT|INVALID|ERROR/;
const START_STEPS = [
  ["Join your Wi-Fi", (x) => x.wifi && x.wifi.connected],
  ["Pair your scale", (x) => x.scale && x.scale.paired],
  ["Pair the brew bot", (x) => x.brew_bot && x.brew_bot.paired],
  ["Choose your espresso machine", (x) => x.machine && !!x.machine.maker],
  ["Describe your grinder", (x) => x.grinder && x.grinder.set],
  ["Choose a recipe", (x) => x.recipes > 0],
  ["Add your coffee", (x) => x.coffees_on_recipe > 0],
  ["Pull your first shot", (x) => !!x.shots],
];
const stars = (n) => (n ? "★".repeat(n) + "☆".repeat(Math.max(0, 5 - n)) : "");
const plural = (n, one, many) => n + " " + (n == 1 ? one : many || one + "s");
const localYmd = (t) => { const d = new Date(t); return d.getFullYear() * 10000 + (d.getMonth() + 1) * 100 + d.getDate(); };
const hm = (t) => new Date(t).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
const frozenNow = (b) => !!(b && b.frozenDate && !b.thawDate);
/** A YYYYMMDD date as a local Date (new Date("2026-11-16") would be UTC midnight: a day early in the Americas). */
const ymdDate = (n) => new Date(Math.floor(n / 10000), Math.floor(n / 100) % 100 - 1, n % 100);

class BrewSummary extends BrewSection {
  static get styles() {
    return `${TIP_CSS}
    .grid{grid-template-columns:repeat(auto-fill,minmax(330px,1fr))}
    .card.wide{grid-column:1/-1}
    .hd{display:flex;align-items:center;gap:8px;margin-bottom:10px}
    .hd ha-icon{--mdc-icon-size:20px;color:var(--secondary-text-color)}
    .hd h3{margin:0;flex:1}
    .hd .more{font-size:13px;cursor:pointer;color:var(--primary-color);background:none;border:none;padding:0}
    .fig{display:flex;align-items:baseline;gap:18px;flex-wrap:wrap}
    .fig .u{font-size:15px;color:var(--secondary-text-color);margin-left:2px}
    .fig .of{font-size:14px;color:var(--secondary-text-color)}
    .line{display:flex;justify-content:space-between;align-items:center;gap:8px;padding:7px 0;border-bottom:1px solid var(--divider-color)}
    .line:last-child{border-bottom:none}
    .line .l{min-width:0;flex:1}
    .line .l b{font-weight:500}
    .dot{display:inline-block;width:9px;height:9px;border-radius:50%;background:var(--disabled-text-color,#999);margin-right:6px;flex:none}
    .dot.on{background:var(--success-color,#43a047)} .dot.am{background:var(--warning-color,#ffa600)} .dot.zz{background:var(--info-color,#039be5)} .dot.bad{background:var(--error-color,#db4437)}
    .banner{border-left:4px solid var(--warning-color,#ffa600);padding:8px 12px;background:rgba(var(--rgb-warning-color,255,166,0),.08);border-radius:8px;margin-top:10px}
    .banner.info{border-left-color:var(--info-color,#039be5);background:rgba(var(--rgb-info-color,3,155,229),.08)}
    .alert{border-left:4px solid var(--error-color);background:rgba(var(--rgb-error-color,219,68,55),.1);padding:10px 12px;border-radius:8px;font-weight:500;margin-bottom:8px}
    .bar{height:6px;border-radius:3px;background:var(--secondary-background-color);overflow:hidden;margin-top:4px}
    .bar > i{display:block;height:100%;background:var(--primary-color)}
    .bar.due > i{background:var(--warning-color,#ffa600)}
    .chips{display:flex;flex-wrap:wrap;gap:6px;margin-top:8px}
    .stars{color:var(--warning-color,#ffa600);letter-spacing:1px}
    .acts{display:flex;gap:6px;flex-wrap:wrap;margin-top:10px}
    .line .acts{margin-top:0;flex:none}
    .na{color:var(--secondary-text-color);font-size:13px}
    .rate{margin-top:10px}
    canvas{margin-top:8px}`;
  }

  async load() {
    const keys = ["library", "grind", "shots", "maintenance", "subs", "power", "start", "ratings"];
    const got = await Promise.allSettled(keys.map((k) => this.store.get(k)));
    this.d = {};
    keys.forEach((k, i) => { this.d[k] = got[i].status === "fulfilled" ? got[i].value : null; });
    // Without the library nothing reads sensibly: let the section show its retry card.
    if (!this.d.library) throw got[0].reason || new Error("The controller's library could not be read");
    this.g = grindOf(this.d.library);
    this.extras = null;
  }

  render() {
    const s = this.panel.status || {};
    return html`
    <div class="grid">
      <div class="card" id="now">${raw(this.nowHtml(s))}</div>
      ${raw(this.lastShotHtml())}
      ${raw(this.todayHtml())}
      ${raw(this.grindHtml())}
      ${raw(this.powerHtml())}
      <div class="card" id="links">${raw(this.linksHtml(s))}</div>
      ${raw(this.shelfHtml())}
      ${raw(this.subsHtml())}
      ${raw(this.upkeepHtml())}
      <div class="card" id="ratings">${raw(this.ratingsHtml())}</div>
      ${raw(this.startHtml())}
      <div class="card" id="health">${raw(this.healthHtml())}</div>
    </div>
    <div class="tip" id="tip"></div>`;
  }

  hd(icon, title, more, moreLabel) {
    const m = more ? `<button class="more" data-${more === "controller" ? "ctl" : "go"}="${more}">${esc(moreLabel || "More")}</button>` : "";
    return `<div class="hd"><ha-icon icon="${icon}"></ha-icon><h3>${esc(title)}</h3>${m}</div>`;
  }

  // --- 1. Now ----------------------------------------------------------------------------------
  nowHtml(s) {
    if (!s.state) return this.hd("mdi:coffee-maker", "Now") + `<div class="na">Waiting for the controller…</div>`;
    const fault = FAULT.test(s.state), busy = /BREWING|BLOOMING|STARTING_SHOT|STOPPING_SHOT|POST_SHOT_DRIP|TARING/.test(s.state);
    const nocup = s.state == "READY" && s.cup_required && !s.cup_present;
    const label = s.state == "BLOOMING" ? "Bloom " + Math.ceil(s.bloom_left_s || 0) + " s" : nocup ? "Ready, no cup" : stateWords(s.state);
    const rc = (this.d.library.recipes || []).find((r) => r.name == s.recipe);
    return this.hd("mdi:coffee-maker", "Now", "controller", "Brew in the controller") + `
      ${s.manual_stop_needed ? `<div class="alert">Stop the machine by hand: the stop press was not delivered or confirmed.</div>` : ""}
      ${s.bot_switch_mode ? `<div class="alert">The SwitchBot is in Switch mode. Set it to Press mode in the SwitchBot app.</div>` : ""}
      <div class="spread"><span class="pill ${fault ? "bad" : s.state == "READY" && !nocup ? "ok" : "warn"}">${esc(label)}</span>
        <span class="hint">${esc(s.recipe || "")}${s.target_weight_g ? " · " + f1(s.target_weight_g) + " g" : ""}</span></div>
      <div class="fig" style="margin-top:10px">
        <span><span class="big" id="lw">${f1(s.weight_g || 0)}</span><span class="u">g</span></span>
        <span><span class="big" id="lt">${f1(s.shot_time_s || 0)}</span><span class="u">s</span></span>
        ${busy ? `<span><span class="big" id="lf">${f2(s.flow_g_s || 0)}</span><span class="u">g/s</span></span>` : ""}
      </div>
      <div class="hint" style="margin-top:6px">${busy ? "Shot running: stops at " + f1(s.target_weight_g - (s.stop_offset_g || 0)) + " g." : rc ? esc(rc.doseG + " g in, " + rc.targetWeightG + " g out in " + rc.targetTimeS + " s.") : ""}</div>`;
  }

  // --- 2. Last shot ----------------------------------------------------------------------------
  lastShotHtml() {
    const SH = this.d.shots, last = SH && SH[0];
    const head = this.hd("mdi:coffee", "Last shot", "shots", "All shots");
    if (!SH) return `<div class="card">${head}<div class="na">Shots are not available right now (the controller answers fewer requests during a shot).</div></div>`;
    if (!last) return `<div class="card">${head}<div class="na">No shot yet. Brew one and it appears here.</div></div>`;
    const tgt = last.stop_target_g ?? last.target_weight_g;
    const tdiff = last.target_time_s ? last.shot_time_s - last.target_time_s : null;
    return `<div class="card">${head}
      <div class="fig"><span><span class="big">${f1(last.final_weight_g)}</span><span class="u">g</span> <span class="of">of ${f1(tgt)}${last.stop_target_g != null && last.stop_target_g != last.target_weight_g ? " (from the grinder's dose)" : ""}</span></span>
        <span><span class="big">${f1(last.shot_time_s)}</span><span class="u">s</span>${last.target_time_s ? ` <span class="of">of ${last.target_time_s}</span>` : ""}</span></div>
      <div class="hint" style="margin-top:4px">${esc((last.coffee ? last.coffee + ", " : "") + last.recipe)} · ${esc(hm(last.timestamp))}${last.grind_um ? " · " + esc(fs(this.g, last.grind_um)) : ""}. ${esc(resultLine(last))}${tdiff != null && Math.abs(tdiff) >= 0.5 ? ", " + f1(Math.abs(tdiff)) + " s " + (tdiff > 0 ? "slow" : "fast") : ""}.</div>
      <canvas id="hchart"></canvas>
      ${rateRowHtml(last)}</div>`;
  }

  // --- 3. Today --------------------------------------------------------------------------------
  todayHtml() {
    const SH = this.d.shots, G = this.d.grind, tol = (G && G.tolerance_s) || 3;
    const head = this.hd("mdi:calendar-today", "Today", "insights", "Insights");
    if (!SH) return `<div class="card">${head}<div class="na">Not available right now.</div></div>`;
    const t0 = localYmd(Date.now()), todays = SH.filter((s) => s.timestamp && localYmd(s.timestamp) == t0);
    if (!todays.length) {
      const last = SH[0];
      return `<div class="card">${head}<div class="fig"><span class="big">0</span><span class="of">shots today</span></div>
        <div class="hint">${last ? "The last one was " + esc(ago(Date.parse(last.timestamp) / 1000)) + "." : ""}</div></div>`;
    }
    const on = todays.filter((s) => s.target_time_s && Math.abs(s.shot_time_s - s.target_time_s) <= tol).length;
    const avg = todays.reduce((a, s) => a + (+s.shot_time_s || 0), 0) / todays.length;
    const grams = todays.reduce((a, s) => a + (+s.dose_g || 0), 0);
    return `<div class="card">${head}
      <div class="fig"><span><span class="big">${todays.length}</span> <span class="of">${todays.length == 1 ? "shot" : "shots"}</span></span>
        <span><span class="big">${on}</span> <span class="of">on time (± ${f1(tol)} s)</span></span></div>
      <div class="hint">Average ${f1(avg)} s${grams ? ", " + f1(grams) + " g of coffee" : ""}.</div>
      <div class="chips">${todays.slice(0, 12).map((s) => `<span class="pill ${s.target_time_s && Math.abs(s.shot_time_s - s.target_time_s) <= tol ? "ok" : ""}" title="${esc((s.coffee || "") + " " + s.recipe)}">${esc(hm(s.timestamp))} · ${f1(s.final_weight_g)} g · ${f1(s.shot_time_s)} s${s.rating ? " · " + s.rating + "★" : ""}</span>`).join("")}</div></div>`;
  }

  // --- 4. Grind advice -------------------------------------------------------------------------
  grindHtml() {
    const G = this.d.grind, g = this.g;
    const head = this.hd("mdi:grain", "Grind advice", "controller", "Grinder setup");
    if (!G) return `<div class="card">${head}<div class="na">Grind advice is not available right now.</div></div>`;
    const adv = adviceText(G, g), rec = recommendationText(G, g), nog = noGrindText(G, g);
    const room = this.roomText(G.room);
    const remind = G.reminder_due && adv && adv.apply;
    return `<div class="card">${head}
      <div class="spread"><span><span class="big">${esc(adv ? adv.current : "--")}</span> <span class="of">${esc(g.unit || "")}</span></span>
        ${adv && /^(finer|coarser|hold)$/.test(G.action) ? `<span class="pill ${adv.cls}">${G.action == "hold" ? "Keep" : "Go " + G.action}</span>` : ""}</div>
      ${adv ? `<div style="margin-top:6px"><b>${esc(adv.title)}</b></div><div class="hint" style="margin-top:4px">${esc(adv.short)}${adv.ageShort ? " " + esc(adv.ageShort) + "." : ""}</div>` : ""}
      ${room ? `<div class="hint" style="margin-top:6px"><ha-icon icon="mdi:home-thermometer" style="--mdc-icon-size:16px"></ha-icon> ${esc(room)}</div>` : ""}
      ${G.grinder_dose_g ? `<div class="hint" style="margin-top:4px">Grinder add-on: last grind ${f1(G.grinder_dose_g)} g${G.grinder_gap_um ? " at " + f1(G.grinder_gap_um) + " µm" : ""}${G.grinder_age_s != null ? ", " + esc(ago(Date.now() / 1000 - G.grinder_age_s)) : ""}.</div>` : ""}
      ${remind ? `<div class="banner"><b>Grind reminder.</b> The advice is ${esc(fs(g, adv.apply))} and the grinder is still at ${esc(fs(g, G.current_um))}, the setting of the last shot.</div>` : ""}
      ${adv && adv.apply ? `<div class="acts"><button class="${remind ? "primary" : ""}" id="apply" data-um="${adv.apply}">I set the grinder to ${esc(fs(g, adv.apply))}</button></div>` : ""}
      ${rec ? `<div class="banner info"><div>${esc(rec.text)}</div><div class="acts"><button class="primary" id="recuse">${esc(rec.use)}</button><button id="reckeep">${esc(rec.keep)}</button></div></div>` : ""}
      ${nog ? `<div class="banner"><div>${esc(nog.text)}</div><div class="acts">${nog.last ? `<button class="primary" id="setlast" data-um="${nog.last}">Set ${esc(fs(g, nog.last))}</button>` : ""}<button data-ctl="controller">Enter it in the controller</button></div></div>` : ""}
    </div>`;
  }

  /** /api/grind `room` (v0.1.30, core/GrindAdvisor.h grindRoomEffect) in words, or "". */
  roomText(r) {
    if (!r || !r.on) return "";
    const now = r.now_c != null ? f1(r.now_c) + " °C (12 h average)" : "no reading yet";
    if (!r.learned) return `Room: ${now}. Still learning how the room changes brew time` + (r.shots ? ` (${plural(r.shots, "shot")} over ${f1(r.span_c || 0)} °C so far).` : ".");
    const per = r.s_per_c != null ? ` ${f1(Math.abs(r.s_per_c))} s per °C, colder runs ${r.s_per_c < 0 ? "slower" : "faster"}` : "";
    if (!r.applied || !r.shift_s) return `Room: ${now}.${per ? per[0].toUpperCase() + per.slice(1) + "." : ""} No correction needed for the newest shot.`;
    return `Room: ${now}. The newest shot's time was corrected by ${r.shift_s > 0 ? "+" : "−"}${f1(Math.abs(r.shift_s))} s for it (${per.trim()}).`;
  }

  // --- 5. Machine power and schedule -----------------------------------------------------------
  powerHtml() {
    const P = this.d.power;
    const head = this.hd("mdi:power", "Machine power", "controller", "Schedule in the controller");
    if (!P) return `<div class="card">${head}<div class="na">Not available.</div></div>`;
    if (!P.paired) return `<div class="card">${head}<div class="na">No power bot is paired. The controller can switch the machine on and off on a schedule with one; set it up in the controller.</div></div>`;
    const nxt = nextPowerEvents(P), on = (P.schedule || []).filter((e) => e.enabled);
    const belief = P.belief == "on" ? ["on", "On"] : P.belief == "off" ? ["", "Off"] : ["am", "Not known"];
    return `<div class="card">${head}
      <div class="spread"><span><span class="dot ${belief[0]}"></span><span class="big" style="font-size:28px">${belief[1]}</span></span>
        ${P.working ? `<span class="pill warn">pressing…</span>` : ""}</div>
      ${P.last ? `<div class="hint" style="margin-top:4px">Last: ${esc(P.last)}${P.last_by_schedule ? " (schedule)" : ""}.</div>` : ""}
      <div style="margin-top:8px">
        ${nxt.next_on ? `<div class="line"><span class="l">Next switch-on</span><b>${esc(whenText(nxt.next_on))}</b></div>` : ""}
        ${nxt.next_off ? `<div class="line"><span class="l">Next switch-off</span><b>${esc(whenText(nxt.next_off))}</b></div>` : ""}
        ${!on.length ? `<div class="hint">No schedule is set.</div>` : P.clock_set === false ? `<div class="hint">The controller's clock is not set yet, so the schedule waits.</div>` : ""}
      </div>
      ${on.length ? `<div class="chips">${on.map((e) => `<span class="pill">${e.on ? "On" : "Off"} ${hhmm(e.minute)} ${esc(daysText(e.days))}</span>`).join("")}</div>` : ""}
      <div id="pevents"></div></div>`;
  }

  // --- 6. Links: scale, bots, add-ons ------------------------------------------------------------
  linksHtml(s) {
    const P = this.d.power, x = this.extras || {};
    const scale = !s.state ? ["", "not known"] : s.scale_connected ? ["on", "connected" + (s.scale_battery != null ? ", " + s.scale_battery + "%" : "")]
      : s.scale_resting ? ["zz", "resting (the touch key wakes it)"] : ["", "not connected"];
    const bot = !s.state ? ["", "not known"] : s.bot_connected ? ["on", "connected"] : s.bot_paired ? ["zz", "asleep until Start"] : ["bad", "not paired"];
    const add = x.addons && x.addons.compiled > 0 && Array.isArray(x.addons.addons) ? x.addons.addons : [];
    return this.hd("mdi:bluetooth-connect", "Links", "controller", "Pairing in the controller") + `
      <div class="line"><span class="l"><span class="dot ${scale[0]}"></span>Scale</span><span class="hint">${esc(scale[1])}</span></div>
      <div class="line"><span class="l"><span class="dot ${bot[0]}"></span>Brew bot</span><span class="hint">${esc(bot[1])}${s.bot_battery != null ? ", " + s.bot_battery + "%" : ""}</span></div>
      <div class="line"><span class="l"><span class="dot ${P && P.paired ? "on" : ""}"></span>Power bot</span><span class="hint">${P ? (P.paired ? "paired" : "not paired") : "not known"}</span></div>
      ${add.map((a) => `<div class="line"><span class="l"><span class="dot ${a.enabled && a.api_ok !== false ? "on" : ""}"></span>${esc(a.name || a.id)} <span class="hint">add-on ${esc(a.version || "")}</span></span><span class="hint">${esc(a.enabled ? a.status || "on" : "off")}</span></div>`).join("")}
      ${s.scale_start_pending ? `<div class="banner info">A Start is waiting for the scale to come back.</div>` : ""}`;
  }

  // --- 7. Bean shelf ------------------------------------------------------------------------------
  shelfHtml() {
    const L = this.d.library, bags = L.bags || [];
    const head = this.hd("mdi:sack", "Bean shelf", "coffees", "Shelf");
    const p = (L.presets || [])[L.activePreset] || {}, c = coffeeById(L, p.coffeeId);
    const bag = c && bags.find((b) => b.id == c.bagId);
    const unopened = bags.filter((b) => b.unopened), coming = bags.filter((b) => b.onTheWay);
    const inUse = bags.filter((b) => b.current && !b.unopened && !b.onTheWay && (!c || b.id != c.bagId));
    const name = (b) => { const x = coffeeById(L, b.coffeeId); return x ? x.name + (x.roaster ? " · " + x.roaster : "") : "a coffee"; };
    const bagAge = (b) => (b.roastDate ? plural(age(b.roastDate), "day") + " off roast" : "no roast date");
    const lines = [];
    if (c) {
      lines.push(`<div class="line"><span class="l"><b>${esc(c.name)}</b>${frozenNow(bag) ? ' <span class="pill">❄ frozen</span>' : ""}<div class="hint">${esc((c.roaster ? c.roaster + ". " : "") + (ageNote(bag ? { ...c, ...bag } : c) || "No roast date entered") + ".")}${bag && bag.weightG ? " " + bag.weightG + " g bag." : ""}</div></span>
        <span class="acts"><button id="finish" data-coffee="${c.id}" title="Take it off the controller's list; it can come back with a new bag">Bag finished</button></span></div>`);
    } else lines.push(`<div class="hint">No coffee is attached to what the controller brews.</div>`);
    inUse.forEach((b) => lines.push(`<div class="line"><span class="l">${esc(name(b))}${frozenNow(b) ? ' <span class="pill">❄ frozen</span>' : ""}<div class="hint">Open, ${esc(bagAge(b))}.</div></span></div>`));
    unopened.forEach((b) => lines.push(`<div class="line"><span class="l">${esc(name(b))}${frozenNow(b) ? ' <span class="pill">❄ frozen</span>' : ""}<div class="hint">Unopened, ${esc(bagAge(b))}.</div></span><span class="acts"><button data-open="${b.id}">Open</button></span></div>`));
    coming.forEach((b) => lines.push(`<div class="line"><span class="l">${esc(name(b))}<div class="hint">On its way${b.expectedDate ? ", expected " + esc(ymdDate(b.expectedDate).toLocaleDateString([], { month: "short", day: "numeric" })) : ""}.</div></span><span class="acts"><button data-arrived="${b.id}">It arrived</button></span></div>`));
    return `<div class="card">${head}${lines.join("")}</div>`;
  }

  // --- 8. Subscriptions -----------------------------------------------------------------------
  subsHtml() {
    const S = this.d.subs;
    const head = this.hd("mdi:truck-delivery", "Subscriptions", "subscriptions", "All");
    if (!S || !Array.isArray(S.subscriptions)) return `<div class="card">${head}<div class="na">Not available.</div></div>`;
    const active = S.subscriptions.filter((x) => (x.status || "active") == "active");
    if (!active.length) return `<div class="card">${head}<div class="na">No active subscription. Coffee and supplies that come by post are set up in the controller.</div></div>`;
    const by = [...active].sort((a, b) => (a.days_until ?? 1e9) - (b.days_until ?? 1e9));
    const days = (n) => (n == null ? "" : n < 0 ? plural(-n, "day") + " overdue" : n == 0 ? "today" : n == 1 ? "tomorrow" : "in " + n + " days");
    return `<div class="card">${head}
      ${by.slice(0, 5).map((x) => `<div class="line"><span class="l"><b>${esc(x.name || x.site || "Subscription")}</b>
          ${x.days_until != null && x.days_until < 0 ? '<span class="pill bad">overdue</span>' : x.due_soon ? '<span class="pill warn">soon</span>' : ""}
          ${x.stock_short ? '<span class="pill warn">runs short</span>' : ""}
          <div class="hint">${x.next_date ? "Next " + esc(days(x.days_until)) : "No date"}${x.kind == "supplies" && x.stock != null ? ` · ${x.stock} ${esc(x.item || "left")}${x.stock_days_left != null ? ", about " + plural(x.stock_days_left, "day") : ""}` : ""}${x.price_cents ? " · " + esc(money(x.price_cents)) : ""}</div></span>
        ${x.next_date ? `<span class="acts"><button data-sarr="${x.id}" title="A delivery came: the next date moves on${x.kind == "supplies" ? " and the stock goes up" : ""}">Arrived</button><button data-sskip="${x.id}" title="Moves the date on here; skip it at the shop too">Skip</button></span>` : ""}</div>`).join("")}
      <div class="hint" style="margin-top:6px">${S.monthly_cents ? "About " + esc(money(S.monthly_cents)) + " a month. " : ""}${S.shots_per_day ? f1(S.shots_per_day) + " shots a day lately." : ""}</div></div>`;
  }

  // --- 9. Maintenance -------------------------------------------------------------------------
  upkeepHtml() {
    const M = this.d.maintenance;
    const head = this.hd("mdi:wrench", "Maintenance", "controller", "Intervals in the controller");
    if (!M || !Array.isArray(M.jobs)) return `<div class="card">${head}<div class="na">Not available.</div></div>`;
    return `<div class="card">${head}
      ${M.jobs.map((j) => {
        const pc = j.every_shots ? Math.min(100, Math.round(100 * j.since_shots / j.every_shots)) : 0;
        return `<div class="line"><span class="l"><b>${esc(JOB_NAMES[j.id] || j.id)}</b> ${j.due ? '<span class="pill warn">due</span>' : ""}
          <div class="hint">${j.every_shots ? `${j.since_shots} of ${j.every_shots} shots` : "No reminder"}${j.last_epoch ? ", last done " + esc(ago(j.last_epoch)) : ""}</div>
          ${j.every_shots ? `<div class="bar ${j.due ? "due" : ""}"><i style="width:${pc}%"></i></div>` : ""}</span>
          <span class="acts"><button data-done="${j.id}" class="${j.due ? "primary" : ""}">Done</button></span></div>`;
      }).join("")}
      <div class="hint" style="margin-top:6px">${M.shots_total != null ? plural(M.shots_total, "shot") + " in all." : ""}</div></div>`;
  }

  // --- 10. Coffee ratings ------------------------------------------------------------------------
  ratingsHtml() {
    const R = this.d.ratings, L = this.d.library, x = this.extras || {};
    const head = this.hd("mdi:star-face", "Coffee ratings", "coffees", "Shelf");
    if (!R || !Array.isArray(R.ratings)) return head + `<div class="na">Not available.</div>`;
    const who = (d) => (d == 0 ? "household" : R.named && R.drinkers && R.drinkers[d - 1] ? R.drinkers[d - 1] : "drinker " + d);
    const rows = [...R.ratings].sort((a, b) => (b.t || 0) - (a.t || 0)).slice(0, 3);
    const best = x.profile && Array.isArray(x.profile.best) ? x.profile.best.slice(0, 2) : [];
    const again = x.recommend && Array.isArray(x.recommend.buy_again) ? x.recommend.buy_again.slice(0, 3) : [];
    const next = x.recommend && Array.isArray(x.recommend.items) ? x.recommend.items[0] : null;
    return head + `
      ${rows.length ? rows.map((r) => { const c = coffeeById(L, r.coffee); return `<div class="line"><span class="l">${esc(c ? c.name : "A coffee")}<div class="hint">${esc(who(r.drinker))}${r.t ? ", " + esc(ago(r.t)) : ""}</div></span><span class="stars">${stars(r.stars)}</span></div>`; }).join("") : `<div class="na">No coffee rated yet. Bags are rated on the controller.</div>`}
      ${best.length ? `<div class="hint" style="margin-top:6px">Liked best: ${esc(best.map((b) => b.name || b.coffee || "").filter(Boolean).join(", "))}.</div>` : ""}
      ${again.length || next ? `<div class="chips">${again.map((b) => `<span class="pill ok">Buy again: ${esc(b.name || "")}</span>`).join("")}${next ? `<span class="pill" title="${esc(next.why || "")}">You might like: ${esc(next.name)}${next.roaster ? " (" + esc(next.roaster) + ")" : ""}</span>` : ""}</div>` : ""}`;
  }

  // --- 11. Getting started -----------------------------------------------------------------------
  startHtml() {
    const x = this.d.start;
    if (!x || !x.wifi) return "";
    const done = START_STEPS.map((s) => !!s[1](x)), n = done.filter(Boolean).length;
    if (n == START_STEPS.length) return "";
    return `<div class="card">${this.hd("mdi:rocket-launch", "Getting started", "controller", "Set up in the controller")}
      <div class="hint">${n} of ${START_STEPS.length} done. Setup happens in the controller's own page.</div>
      <div class="bar"><i style="width:${Math.round(100 * n / START_STEPS.length)}%"></i></div>
      <div style="margin-top:6px">${START_STEPS.map((s, i) => `<div class="line"><span class="l">${done[i] ? "✓" : "○"} ${esc(s[0])}</span></div>`).join("")}</div></div>`;
  }

  // --- 12. Health ---------------------------------------------------------------------------------
  healthHtml() {
    const x = this.extras;
    const head = this.hd("mdi:heart-pulse", "Controller", "controller", "Open the controller");
    if (!x) return head + `<div class="na">Loading…</div>`;
    const D = x.diag;
    if (!D) return head + `<div class="na">Not available.</div>`;
    const fw = D.firmware || {}, w = D.wifi || {}, up = fw.uptime_s || 0;
    const upText = up >= 86400 ? plural(Math.floor(up / 86400), "day") : up >= 3600 ? plural(Math.floor(up / 3600), "hour") : plural(Math.floor(up / 60), "minute");
    const rssi = w.rssi_dbm, sig = rssi == null ? "" : rssi >= -60 ? "good" : rssi >= -72 ? "fair" : "weak";
    const crash = x.crash && Object.keys(x.crash).length ? x.crash : null;
    return head + `
      <div class="line"><span class="l">Firmware</span><span class="hint">${esc(fw.version || "")}${fw.latest_release && fw.latest_release != fw.version ? ` · ${esc(fw.latest_release)} available` : ""}</span></div>
      <div class="line"><span class="l">Running for</span><span class="hint">${esc(upText)}${fw.reset_reason ? " · " + esc(fw.reset_reason) : ""}</span></div>
      <div class="line"><span class="l"><span class="dot ${w.station_connected ? (sig == "weak" ? "am" : "on") : "bad"}"></span>Wi-Fi</span><span class="hint">${w.station_connected ? esc((sig ? sig + ", " : "") + (rssi != null ? rssi + " dBm" : "")) : "not connected"}</span></div>
      ${x.ambient && (x.ambient.temp_c != null || x.ambient.rh_pct != null) ? `<div class="line"><span class="l"><span class="dot ${x.ambient.fresh ? "on" : "am"}"></span>Room reading</span><span class="hint">${x.ambient.temp_c != null ? f1(x.ambient.temp_c) + " °C" : ""}${x.ambient.rh_pct != null ? ", " + Math.round(x.ambient.rh_pct) + "%" : ""}${x.ambient.source ? " from " + (x.ambient.source == "ha" ? "Home Assistant" : "a push") : ""}${x.ambient.fresh === false ? " (stale)" : ""}</span></div>` : ""}
      ${crash ? `<div class="banner">The controller restarted after a crash${crash.version ? " in " + esc(crash.version) : ""}. The report is in the controller's diagnostics.</div>` : ""}`;
  }

  // --- the second wave: what only one card needs, after the page is drawn -----------------------
  async loadExtras() {
    const [diag, crash, addons, profile, recommend, events, ambient] = await Promise.all([
      this.api.getOr("/api/diagnostics", null, null),
      this.api.getOr("/api/crash", null, null),
      this.api.getOr("/api/addons", null, null),
      this.api.getOr("/api/taste/profile", { drinker: 0 }, null),
      this.api.getOr("/api/taste/recommend", { drinker: 0 }, null),
      this.d.power && this.d.power.paired ? this.api.getOr("/api/power/events", { days: 3 }, null) : null,
      this.api.getOr("/api/ambient", null, null),
    ]);
    if (!this.isConnected) return;
    this.extras = { diag, crash, addons, profile, recommend, events, ambient };
    const set = (id, h) => { const el = this.$(id); if (el) el.innerHTML = h; };
    set("#health", this.healthHtml());
    set("#ratings", this.ratingsHtml());
    set("#links", this.linksHtml(this.panel.status || {}));
    if (Array.isArray(events) && events.length) {
      set("#pevents", `<div class="hint" style="margin-top:8px">Recent presses: ${events.slice(-3).reverse().map((e) =>
        esc((e.on ? "on " : "off ") + whenText(e.t) + (e.by == "schedule" ? " (schedule)" : e.by == "scale" ? " (scale off)" : "") + (e.ok ? "" : ": " + e.outcome))).join("; ")}.</div>`);
    }
    this.wireLinks();
  }

  // --- events ---------------------------------------------------------------------------------
  wireLinks() {
    this.$$("[data-go]").forEach((a) => a.onclick = () => this.panel.navigate(a.dataset.go));
    this.$$("[data-ctl]").forEach((a) => a.onclick = () => this.panel.openController());
  }

  wire() {
    this.wireLinks();
    const g = this.g, after = (...k) => this.store.invalidate(...k);
    const setGrind = async (um) => { const r = await this.post("/api/grind/set", null, "Grind setting saved", { um: Math.round(um) }); if (r.ok) after("library", "grind"); };
    const apply = this.$("#apply"); if (apply) apply.onclick = () => setGrind(+apply.dataset.um);
    const setlast = this.$("#setlast"); if (setlast) setlast.onclick = () => setGrind(+setlast.dataset.um);
    const rec = async (use) => { const r = await this.post("/api/grind/recommendation", null, use ? "Grind setting saved" : null, { use }); if (r.ok) after("grind", "library"); };
    const ru = this.$("#recuse"); if (ru) ru.onclick = () => rec(1);
    const rk = this.$("#reckeep"); if (rk) rk.onclick = () => rec(0);

    const fin = this.$("#finish");
    if (fin) fin.onclick = async () => {
      const c = coffeeById(this.d.library, +fin.dataset.coffee);
      if (!(await confirmDialog(this.shadowRoot, `Is the bag of ${c ? c.name : "this coffee"} finished? The controller takes it off its list until a new bag comes, and moves on to the next bag on the shelf.`, { yes: "Bag finished" }))) return;
      const r = await this.post("/api/library/do", null, "Bag finished", { what: "coffee_finish", a: +fin.dataset.coffee });
      if (r.ok) after("library", "grind");
    };
    this.$$("[data-open]").forEach((b) => b.onclick = async () => {
      const r = await this.post("/api/library/do", null, "Bag opened: in use from today", { what: "bag_open", a: +b.dataset.open });
      if (r.ok) after("library", "grind");
    });
    this.$$("[data-arrived]").forEach((b) => b.onclick = async () => {
      const r = await this.post("/api/library/do", null, "On the shelf, unopened. Add its roast date in the controller.", { what: "bag_arrived", a: +b.dataset.arrived });
      if (r.ok) after("library");
    });
    this.$$("[data-sarr]").forEach((b) => b.onclick = async () => {
      const r = await this.post("/api/subscriptions/arrived", { id: +b.dataset.sarr }, "Arrived: the next date moved on");
      if (r.ok) after("subs", "library");
    });
    this.$$("[data-sskip]").forEach((b) => b.onclick = async () => {
      if (!(await confirmDialog(this.shadowRoot, "Skip the next delivery here? This only moves the date on in the controller: skip it at the shop too.", { yes: "Skip one" }))) return;
      const r = await this.post("/api/subscriptions/skip", { id: +b.dataset.sskip }, "Skipped one: the next date moved on");
      if (r.ok) after("subs");
    });
    this.$$("[data-done]").forEach((b) => b.onclick = async () => {
      const name = JOB_NAMES[b.dataset.done] || "Job";
      if (!(await confirmDialog(this.shadowRoot, `${name} done today? Its count starts again from 0.`, { yes: "Done" }))) return;
      const r = await this.post("/api/maintenance", null, name + " marked done", { job: b.dataset.done, action: "done" });
      if (r.ok) after("maintenance");
    });

    const SH = this.d.shots, last = SH && SH[0];
    if (last) {
      wireRateRow(this, this.shadowRoot, last, () => after("shots"));
      drawShot(this.api, this.$("#hchart"), last, SH, 160).catch(() => {});
    }
    if (!this._extrasFor || this._extrasFor !== this.d) { this._extrasFor = this.d; this.loadExtras(); }
  }

  onStatus(s) {
    const now = this.$("#now");
    if (!now) return;
    let redrawn = false;
    // The Links card when a device comes or goes (battery figures change too often to matter).
    const links = [s.scale_connected, s.scale_resting, s.scale_start_pending, s.bot_connected, s.bot_paired].join();
    if (this._lastLinks !== links) {
      this._lastLinks = links;
      const l = this.$("#links"); if (l) { l.innerHTML = this.linksHtml(s); redrawn = true; }
    }
    // The numbers every second; the Now card only when the state changes.
    if (this._lastState === s.state && this.$("#lw")) {
      this.$("#lw").textContent = f1(s.weight_g || 0);
      this.$("#lt").textContent = f1(s.shot_time_s || 0);
      const lf = this.$("#lf"); if (lf) lf.textContent = f2(s.flow_g_s || 0);
    } else {
      this._lastState = s.state;
      now.innerHTML = this.nowHtml(s);
      redrawn = true;
    }
    if (redrawn) this.wireLinks();
  }
}

customElements.define("brew-summary", BrewSummary);
