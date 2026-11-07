// Subscriptions: summary, reminders, cards (manage, arrived, skip, edit), add and edit with the
// shop page read by the controller, sharing to the library, reviews per drinker, the library's
// services, suggestions with country and what matters (WebPage.h:438-479, 1146-1349). The
// Chrome-helper import is left out: it needs that browser extension on the page itself.
import { BrewSection } from "../lib/section.js";
import { html, raw } from "../lib/styles.js";
import { d2i, i2d, today, coffeeById, esc } from "../lib/format.js";
import { confirmDialog, formDialog } from "../lib/forms.js";

export const meta = { id: "subscriptions", title: "Subscriptions", icon: "mdi:truck-delivery", optional: true };

const money = (c) => "$" + ((c || 0) / 100).toFixed(2);
const perDay = (v) => { const n = Math.round(v * 10) / 10; return n + (n == 1 ? " shot" : " shots") + " a day"; };
const cap = (t) => (t ? t.charAt(0).toUpperCase() + t.slice(1) : t);
const fd = (n) => (n && n > 20000101 ? new Date(Math.floor(n / 10000), Math.floor(n / 100) % 100 - 1, n % 100).toLocaleDateString([], { month: "short", day: "numeric", year: "numeric" }) : "(date not known)");
const subEvery = (d) => (!d ? "not regular" : d % 7 == 0 ? (d == 7 ? "every week" : "every " + d / 7 + " weeks") : d >= 28 && d <= 31 ? "every month" : d >= 58 && d <= 62 ? "every 2 months" : "every " + d + " days");
const subWhen = (s) => { if (s.days_until == null) return ""; const d = s.days_until, t = fd(s.next_date); return d < 0 ? t + " · " + (-d) + (d == -1 ? " day" : " days") + " overdue" : d == 0 ? "today" : d == 1 ? "tomorrow" : t + " · in " + d + " days"; };
const KIND = { coffee: "Coffee", supplies: "Supplies", other: "Subscription" };
const PLAT = { recharge: "Recharge", ordergroove: "Ordergroove", skio: "Skio", loop: "Loop", appstle: "Appstle", bold: "Bold", shopify: "Shopify" };
const STYLE = { single_roaster: "Single roaster", multi_roaster: "Many roasters", curated: "Curated", matched: "Matched to your taste", single_origin: "Single origin", blends: "Blends", decaf: "Decaf", espresso: "For espresso", filters: "Filters", cleaning: "Cleaning", equipment: "Equipment" };
const SRC = { fair_trade: "Fair trade", direct_trade: "Direct trade", organic: "Organic", farm_named: "Farms named", prices_published: "Prices published", sustainable_packaging: "Sustainable packaging", carbon_neutral: "Carbon neutral", b_corp: "B Corp" };
const ASP = [["value", "Value"], ["quality", "Quality"], ["consistency", "Consistency"], ["reliability", "Reliability"], ["flexibility", "Flexibility"], ["transparency", "Transparency"]];
const SHARE = { local: "On this controller only", queued: "Waiting to be sent", sent: "Sent: waiting for the library", review: "In review", published: "In the shared library", declined: "Declined", refused: "Refused by the library's checks" };
const hostOf = (u) => { try { return new URL(u).hostname.replace(/^www\./, ""); } catch (e) { return ""; } };
const img = (u) => (u ? `<img src="${esc(u)}" alt="" loading="lazy" referrerpolicy="no-referrer" onerror="this.remove()">` : "");
const UI = { libq: "" };

class BrewSubscriptions extends BrewSection {
  static get styles() {
    return `.subs{display:grid;grid-template-columns:repeat(auto-fill,minmax(260px,1fr));gap:12px}
    .sub{border:1px solid var(--divider-color);border-radius:12px;overflow:hidden;display:flex;flex-direction:column;background:var(--card-background-color)}
    .sub.off{opacity:.65} .ban{height:84px;position:relative;display:flex;align-items:center;justify-content:center;overflow:hidden}
    .ban img{width:100%;height:100%;object-fit:cover} .ini{font-size:40px;font-weight:600;color:#fff;opacity:.85}
    .bd{padding:10px 12px;display:flex;flex-direction:column;gap:3px;flex:1} .bd b{font-size:15px}
    .acts{display:flex;flex-wrap:wrap;gap:6px;padding:0 12px 12px} .acts a{text-decoration:none}
    .tags{position:absolute;top:8px;right:8px;display:flex;gap:4px}
    .meter{height:6px;border-radius:3px;background:var(--secondary-background-color);overflow:hidden} .meter i{display:block;height:100%;background:var(--success-color,#43a047)} .meter.warn i{background:var(--warning-color)}
    .why{display:flex;flex-wrap:wrap;gap:4px;margin-top:4px} .why span{font-size:12px;padding:1px 8px;border-radius:8px;background:var(--secondary-background-color)}
    .chips button.on{background:var(--primary-color);color:var(--text-primary-color,#fff);border-color:var(--primary-color)}
    .remind{display:flex;flex-wrap:wrap;gap:8px;align-items:center;padding:8px 0;border-bottom:1px solid var(--divider-color)}
    .stars button.on{color:var(--warning-color);border-color:var(--warning-color)}`;
  }

  async load() {
    const [SUBS, L, RT] = await Promise.all([this.store.get("subs"), this.store.get("library"), this.store.get("ratings").catch(() => null)]);
    const [lib, rev, reco] = await Promise.all([
      this.api.getOr("/api/subscriptions/library", null, null), this.api.getOr("/api/subscriptions/reviews", null, null),
      this.api.getOr("/api/subscriptions/recommend", { drinker: 0 }, null),
    ]);
    Object.assign(this, { SUBS, L, RT, LIBS: (lib && lib.services) || [], SREV: (rev && rev.reviews) || [], RECO: reco });
  }
  sub(id) { return (this.SUBS.subscriptions || []).find((s) => s.id == id); }

  card(s, preview) {
    const col = /^#[0-9a-f]{6}$/i.test(s.color || "") ? s.color : "#5b6470", name = s.name || s.title || s.site || "Subscription";
    const off = s.status && s.status != "active", bits = [KIND[s.kind] || "", subEvery(s.every_days), s.price_cents ? money(s.price_cents) : ""].filter((x) => x).join(" · ");
    const tags = (off ? `<span class="pill">${s.status == "paused" ? "Paused" : "Cancelled"}</span>` : "") + (!off && s.due_soon ? `<span class="pill warn">${s.days_until < 0 ? "Overdue" : "Soon"}</span>` : "");
    let sup = "";
    if (s.kind == "supplies" && (s.stock || s.per_delivery)) {
      const full = Math.max(s.per_delivery || 0, s.stock || 0, 1), pct = Math.min(100, Math.round(100 * (s.stock || 0) / full));
      sup = `<div class="hint">${s.stock || 0} ${esc(s.item || "left")}${s.stock_days_left != null ? " · about " + s.stock_days_left + " days" + (this.SUBS.shots_per_day ? " at " + perDay(this.SUBS.shots_per_day) : "") : ""}</div><div class="meter${s.stock_short ? " warn" : ""}"><i style="width:${pct}%"></i></div>${s.stock_short ? '<div class="hint" style="color:var(--warning-color)">Runs out before the next delivery</div>' : ""}`;
    }
    const otw = !preview ? (this.L.bags || []).filter((b) => b.onTheWay && b.subscriptionId == s.id).map((b) => { const c = coffeeById(this.L, b.coffeeId); return `<span class="hint">On its way: ${esc(c ? c.name : "a bag")}${b.expectedDate ? ", expected " + fd(b.expectedDate) : ""}</span>`; }).join("") : "";
    const acts = preview ? "" : `<div class="acts">${s.manage_url ? `<a class="btn primary" href="${esc(s.manage_url)}" target="_blank" rel="noopener noreferrer">Manage ↗</a>` : s.url ? `<a class="btn" href="${esc(s.url)}" target="_blank" rel="noopener noreferrer">Open ↗</a>` : ""}${off ? "" : `<button data-arrived="${s.id}">Arrived</button><button data-skip="${s.id}">Skip one</button>`}<button data-edit="${s.id}">Edit</button></div>`;
    return `<article class="sub${off ? " off" : ""}"><div class="ban" style="background:linear-gradient(135deg,${col},${col}99 55%,var(--secondary-background-color))">${s.image ? img(s.image) : `<div class="ini">${esc(name.trim().charAt(0).toUpperCase())}</div>`}<div class="tags">${tags}</div></div>
      <div class="bd"><b>${esc(name)}</b><span class="hint">${esc([s.site && s.site != name ? s.site : "", PLAT[s.platform] ? "via " + PLAT[s.platform] : ""].filter((x) => x).join(" · "))}</span><span class="hint">${esc(bits)}</span>
      ${s.next_date ? `<span>Next: ${esc(subWhen(s) || fd(s.next_date))}</span>` : ""}${otw}${sup}${s.note ? `<span class="hint">${esc(s.note)}</span>` : ""}${s.last_arrived ? `<span class="hint">Last arrived ${fd(s.last_arrived)}</span>` : ""}</div>${acts}</article>`;
  }

  libCard(e, why) {
    const col = /^#[0-9a-f]{6}$/i.test(e.color || "") ? e.color : "#3b6e9e", rv = e.reviews;
    const have = (this.SUBS.subscriptions || []).some((x) => x.library_id == e.id || (hostOf(x.url) && hostOf(x.url) == hostOf(e.url)));
    const bits = [KIND[e.kind] || "", (e.cadences || []).length ? "every " + e.cadences.map((d) => (d % 7 ? d + " d" : d / 7 + " wk")).join(" / ") : "", e.price_from_cents ? "from " + money(e.price_from_cents) + (e.currency && e.currency != "USD" ? " " + e.currency : "") : ""].filter((x) => x).join(" · ");
    const tags = [...(e.style || []).map((k) => STYLE[k]), ...(e.sourcing || []).map((k) => SRC[k])].filter((x) => x);
    return `<article class="sub"><div class="ban" style="background:linear-gradient(135deg,${col},${col}99 55%,var(--secondary-background-color))">${e.image ? img(e.image) : `<div class="ini">${esc((e.name || "?").charAt(0).toUpperCase())}</div>`}<div class="tags"><span class="pill">Library</span></div></div>
      <div class="bd"><b>${esc(e.name)}</b><span class="hint">${esc(bits)}</span>${e.ships_to ? `<span class="hint">Ships to ${esc((e.ships_to || []).join(", "))}</span>` : ""}
      ${rv ? `<span>${"★".repeat(Math.round(rv.stars))} ${rv.stars.toFixed(1)} from ${rv.n} reviews</span>` : '<span class="hint">Not enough reviews yet</span>'}
      ${e.about ? `<span class="hint">${esc(e.about)}</span>` : ""}${tags.length ? `<div class="why">${tags.map((t) => `<span>${esc(t)}</span>`).join("")}</div>` : ""}${why || ""}</div>
      <div class="acts">${have ? '<span class="hint">You have this one</span>' : `<button class="primary" data-libadd="${esc(e.id)}">Add to my subscriptions</button>`}<a class="btn" href="${esc(e.url)}" target="_blank" rel="noopener noreferrer">Visit ↗</a></div></article>`;
  }

  render() {
    const SUBS = this.SUBS, list = SUBS.subscriptions || [], act = list.filter((s) => s.status == "active");
    const order = (a, b) => (a.status != "active") - (b.status != "active") || (a.days_until == null) - (b.days_until == null) || (a.days_until || 0) - (b.days_until || 0);
    const next = act.filter((s) => s.days_until != null).sort((a, b) => a.days_until - b.days_until)[0], sup = list.filter((s) => s.kind == "supplies" && s.per_shot > 0);
    const q = UI.libq.toLowerCase(), libs = this.LIBS.filter((e) => !q || [e.name, e.about, e.kind, ...(e.style || []).map((k) => STYLE[k]), ...(e.sourcing || []).map((k) => SRC[k])].join(" ").toLowerCase().includes(q));
    const R = this.RECO, matters = SUBS.matters || [];
    return html`
    <div class="card" style="margin-bottom:16px"><div class="spread"><h3>Subscriptions</h3><button class="primary" id="add">Add a subscription</button></div>
      ${list.length ? html`<div class="tiles" style="margin:8px 0">
        <div class="tile"><div class="k">Each month</div><div class="v">${money(SUBS.monthly_cents || 0)}</div><div class="k">${act.length} active</div></div>
        ${next ? html`<div class="tile"><div class="k">Next up</div><div class="v">${next.name || next.site}</div><div class="k">${subWhen(next)}</div></div>` : ""}
        ${raw(sup.map((s) => `<div class="tile"><div class="k">${esc(cap(s.item || s.name))}</div><div class="v">${s.stock} left</div><div class="k">${s.stock_days_left != null ? "about " + s.stock_days_left + " days at " + perDay(SUBS.shots_per_day) : "not used up yet"}</div></div>`).join(""))}</div>` : ""}
      ${raw(act.filter((s) => s.due_soon || s.stock_short).map((s) => `<div class="remind"><span style="flex:1 1 240px">${s.due_soon ? `<b>${esc(s.name)}</b> ${s.days_until < 0 ? "was due " + fd(s.next_date) + ": did it come?" : s.days_until == 0 ? "charges or ships today" : "charges or ships " + (s.days_until == 1 ? "tomorrow" : "in " + s.days_until + " days") + ". Skip it at the shop if you do not need it."}` : `<b>${esc(cap(s.item || s.name))}</b> will run out before the next delivery.`}</span>${s.manage_url ? `<a class="btn" href="${esc(s.manage_url)}" target="_blank" rel="noopener noreferrer">Manage ↗</a>` : ""}${s.due_soon && s.days_until <= 0 ? `<button data-arrived="${s.id}">Arrived</button>` : ""}</div>`).join(""))}
      <div class="subs" style="margin-top:10px">${raw(list.length ? list.slice().sort(order).map((s) => this.card(s)).join("") : `<div class="hint" style="grid-column:1/-1"><b>Keep your subscriptions in one place.</b> Coffee, paper filters, anything that comes by post: add the link and the controller builds a card, tracks when the next one comes, what it costs, and how many filters you have left.</div>`)}</div>
    </div>
    <div class="card" style="margin-bottom:16px"><div class="spread"><h3>Suggested for you</h3><span class="hint">${R ? (R.basis == "taste" ? "By how their coffees taste to you" : R.basis == "reviews" ? "By reviews and what matters to you" : "") : ""}</span></div>
      <div class="row"><label class="f">Your country (2 letters)<input id="country" maxlength="2" style="width:70px" value="${SUBS.country || ""}"></label></div>
      <div class="muted" style="margin-top:6px">What matters to you</div>
      <div class="row chips">${raw(Object.keys(SRC).map((k) => `<button class="${matters.includes(k) ? "on" : ""}" data-matter="${k}">${SRC[k]}</button>`).join(""))}</div>
      <div class="subs" style="margin-top:10px">${raw(R && (R.items || []).length ? R.items.map((it) => this.libCard(it.service, `${it.why && it.why.length ? `<div class="why">${it.why.map((w) => `<span>${esc(w)}</span>`).join("")}</div>` : ""}${it.people ? `<span class="hint">${esc(it.people)}</span>` : ""}`)).join("") : `<div class="hint" style="grid-column:1/-1">${this.LIBS.length ? "Nothing to suggest yet: services you already have are left out, and so are those that do not ship to your country." : "Suggestions come from the shared library's services. The library has none listed yet."}</div>`)}</div></div>
    <div class="card"><div class="spread"><h3>The shared library's services</h3><span class="hint">${this.LIBS.length ? this.LIBS.length + (this.LIBS.length == 1 ? " service" : " services") : ""}</span></div>
      <input id="libq" type="search" placeholder="Search services" value="${UI.libq}" style="width:100%;margin-bottom:10px">
      <div class="subs">${raw(libs.length ? libs.map((e) => this.libCard(e)).join("") : `<div class="hint" style="grid-column:1/-1">${this.LIBS.length ? "No service matches." : "The shared library has no services yet."}</div>`)}</div></div>`;
  }

  wire() {
    this.$("#add").onclick = () => this.edit(0);
    this.$$("[data-edit]").forEach((b) => b.onclick = () => this.edit(+b.dataset.edit));
    this.$$("[data-skip]").forEach((b) => b.onclick = () => this.skip(+b.dataset.skip));
    this.$$("[data-arrived]").forEach((b) => b.onclick = () => this.arrived(+b.dataset.arrived));
    this.$$("[data-libadd]").forEach((b) => b.onclick = () => this.libAdd(b.dataset.libadd));
    const lq = this.$("#libq");
    lq.oninput = () => { UI.libq = lq.value; clearTimeout(this._lt); this._lt = setTimeout(() => this._paint(), 250); };
    const prefs = async () => {
      const r = await this.post("/api/subscriptions/prefs", { country: this.$("#country").value.trim().toUpperCase(), matters: this.SUBS.matters || [] }, null);
      if (r.ok) this.store.invalidate("subs");
    };
    this.$("#country").onchange = prefs;
    this.$$("[data-matter]").forEach((b) => b.onclick = () => { const m = new Set(this.SUBS.matters || []); m.has(b.dataset.matter) ? m.delete(b.dataset.matter) : m.add(b.dataset.matter); this.SUBS.matters = [...m]; prefs(); });
  }

  async skip(id) {
    const s = this.sub(id);
    if (!s || !(await confirmDialog(this.shadowRoot, 'Skip the next one of "' + s.name + '"? The date moves on here only: skip it on the shop\'s page too.', { yes: "Skip" }))) return;
    const r = await this.post("/api/subscriptions/skip", { id }, "Moved on to the next date");
    if (r.ok) { this.store.invalidate("subs"); if (s.manage_url) window.open(s.manage_url, "_blank", "noopener"); }
  }

  async libAdd(libId) {
    const e = this.LIBS.find((x) => x.id == libId);
    if (!e) return;
    const f = { id: 0, library_id: e.id, name: e.name, url: e.url, manage_url: e.manage_url || "", kind: e.kind || "coffee", status: "active", platform: e.platform || "", image: e.image || "", icon: e.icon || "", color: e.color || "",
      every_days: (e.cadences || [])[0] || 0, ships_to: (e.ships_to || []).join(","), cadences: e.cadences || [], price_from_cents: e.price_from_cents || 0, currency: e.currency || "", style: e.style || [], sourcing: e.sourcing || [], description: e.about || "" };
    const r = await this.post("/api/subscriptions", f, e.name + " added: set its next date and price");
    if (r.ok) { this.store.invalidate("subs"); setTimeout(() => this.edit(r.id), 600); }
  }

  /** Coffee: which coffee and its roast date, then the bag goes on the shelf unopened (WebPage.h:1330-1349). */
  async arrived(id) {
    const s = this.sub(id), L = this.L;
    if (!s) return;
    if (s.kind != "coffee") {
      const msg = s.kind == "supplies" && s.per_delivery ? "Add " + s.per_delivery + " " + (s.item || "") + " to the stock and move the next date on?" : 'Mark "' + s.name + '" arrived and move the next date on?';
      if (!(await confirmDialog(this.shadowRoot, msg, { yes: "Arrived" }))) return;
      const r = await this.post("/api/subscriptions/arrived", { id }, "Arrived");
      if (r.ok) this.store.invalidate("subs");
      return;
    }
    const waiting = (c) => (L.bags || []).some((b) => b.coffeeId == c.id && b.unopened);
    const shelf = L.coffees.filter((c) => !c.finished || waiting(c));
    const otw = (L.bags || []).filter((b) => b.onTheWay && (b.subscriptionId == s.id || !b.subscriptionId));
    const same = shelf.find((c) => c.roaster && s.name && c.roaster.toLowerCase() == s.name.toLowerCase());
    const pre = otw.length ? "otw:" + otw[0].id : same ? String(same.id) : "0";
    const opts = otw.map((b) => { const c = coffeeById(L, b.coffeeId); return [`otw:${b.id}`, "On its way: " + (c ? c.name : "a bag")]; }).concat([["0", "A new coffee"]], shelf.map((c) => [String(c.id), c.name + (c.roaster ? " (" + c.roaster + ")" : "")]));
    const body = `<div class="form">
      <label class="f" style="grid-column:1/-1">Which coffee<select name="pick">${opts.map(([v, l]) => `<option value="${esc(v)}" ${v == pre ? "selected" : ""}>${esc(l)}</option>`).join("")}</select></label>
      <label class="f">Roast date printed on the bag<input name="roast" type="date"></label>
      <label class="f">Weight (g)<input name="weight" type="number" min="0" max="5000"></label>
      <div class="newc" style="display:contents"><label class="f">New coffee's name<input name="name" maxlength="23"></label><label class="f">Roaster<input name="roaster" maxlength="23" value="${esc((s.site || s.name || "").slice(0, 23))}"></label><label class="f" style="grid-column:1/-1">Tasting notes<input name="notes" maxlength="47"></label></div></div>`;
    const p = formDialog(this.shadowRoot, (s.name || "Coffee") + " arrived", body, async (f) => {
      const pick = f.pick.value, roast = d2i(f.roast.value);
      if (!roast) { this.panel.toast("Enter the roast date printed on the bag", false); return false; }
      if (roast > today()) { this.panel.toast("That date has not come yet", false); return false; }
      if (pick.startsWith("otw:")) {
        const bid = +pick.slice(4), r = await this.api.post("/api/library/do", null, { what: "bag_arrived", a: bid, b: 0 });
        if (!r.ok) { this.panel.toast(r.error || "Not saved", false); return false; }
        const bag = (L.bags || []).find((b) => b.id == bid) || {};
        await this.api.post("/api/bag", { id: bid, roastDate: roast, openedDate: 0, frozenDate: 0, thawDate: 0, weightG: Math.round(+f.weight.value || 0) || bag.weightG || 0 });
      } else {
        let cid = +pick;
        if (!cid) {
          const name = f.name.value.trim();
          if (!name) { this.panel.toast("Give the new coffee a name", false); return false; }
          const r = await this.api.post("/api/coffee", { name, roaster: f.roaster.value.trim(), notes: f.notes.value.trim() });
          if (!r.ok) { this.panel.toast(r.error || "Not saved", false); return false; }
          cid = r.id;
        }
        const b = await this.api.post("/api/bag", { coffeeId: cid, roastDate: roast, weightG: Math.round(+f.weight.value || 0), unopened: true, subscriptionId: s.id });
        if (!b.ok) { this.panel.toast(b.error || "Not saved", false); return false; }
      }
      const a = await this.api.post("/api/subscriptions/arrived", { id: s.id });
      this.panel.toast(a.ok ? "On the shelf, unopened" : a.error || "Not saved", !!a.ok);
      this.store.invalidate("subs", "library");
      return true;
    }, { submit: "Put it on the shelf" });
    const dlg = [...this.shadowRoot.querySelectorAll("form.dlg")].pop();
    const sync = () => { dlg.querySelector(".newc").style.display = dlg.pick.value == "0" ? "contents" : "none"; };
    dlg.pick.onchange = sync; sync();
    await p;
  }

  /** Add or edit, with the shop page read, sharing and reviews (WebPage.h:1293-1327, 1261-1281). */
  async edit(id) {
    const s = id ? this.sub(id) : null, SF = s ? { ...s } : { id: 0, kind: "coffee", status: "active", color: "" };
    SF.style = SF.style || []; SF.sourcing = SF.sourcing || [];
    const d = SF.every_days || 0, u = d && d % 7 == 0 ? 7 : d >= 28 && d <= 31 && d != 28 ? 30.44 : 1;
    const v = (x) => esc(x == null ? "" : x);
    const chips = (key, txt) => Object.keys(txt).map((k) => `<button type="button" class="${SF[key].includes(k) ? "on" : ""}" data-chip="${key}:${k}">${txt[k]}</button>`).join("");
    const RT = this.RT, named = RT && RT.named, people = named ? RT.drinkers.map((n, i) => [i + 1, n]).filter((x) => x[1]) : [];
    const body = `
      <div class="row"><input name="url" placeholder="Link to the subscription or shop (https://…)" value="${v(SF.url)}" style="flex:1 1 260px"><button type="button" id="sfread">Read the page</button></div>
      <div class="hint" id="sfreadmsg" style="margin:4px 0 10px"></div>
      <div class="form">
        <label class="f">Name<input name="name" maxlength="31" value="${v(SF.name)}"></label>
        <label class="f">Kind<select name="kind">${Object.entries(KIND).map(([k, l]) => `<option value="${k}" ${SF.kind == k ? "selected" : ""}>${l}</option>`).join("")}</select></label>
        <label class="f">Status<select name="status">${[["active", "Active"], ["paused", "Paused"], ["cancelled", "Cancelled"]].map(([k, l]) => `<option value="${k}" ${SF.status == k ? "selected" : ""}>${l}</option>`).join("")}</select></label>
        <label class="f">Every<span class="row" style="gap:6px"><input name="every" type="number" min="0" max="366" style="width:80px" value="${d ? (u == 30.44 ? Math.round(d / 30.44) : d / u) : ""}"><select name="unit">${[[1, "days"], [7, "weeks"], [30.44, "months"]].map(([k, l]) => `<option value="${k}" ${String(d ? u : 7) == String(k) ? "selected" : ""}>${l}</option>`).join("")}</select></span></label>
        <label class="f">Next delivery or charge<input name="next" type="date" value="${i2d(SF.next_date || 0)}"></label>
        <label class="f">Price each time ($)<input name="price" type="number" step="0.01" min="0" value="${SF.price_cents ? (SF.price_cents / 100).toFixed(2) : ""}"></label>
        <label class="f" style="grid-column:1/-1">Manage link (the shop's account page)<input name="manage" value="${v(SF.manage_url)}"></label>
        <label class="f" style="grid-column:1/-1">Note<input name="note" maxlength="47" value="${v(SF.note)}"></label>
      </div>
      <div class="form sup" style="margin-top:10px">
        <label class="f">Item (e.g. filters)<input name="item" maxlength="15" value="${v(SF.item)}"></label>
        <label class="f">In stock<input name="stock" type="number" min="0" value="${SF.stock || ""}"></label>
        <label class="f">Used per shot<input name="pershot" type="number" min="0" value="${SF.per_shot || ""}"></label>
        <label class="f">Per delivery<input name="perdel" type="number" min="0" value="${SF.per_delivery || ""}"></label>
      </div>
      <details style="margin-top:10px"><summary>For the shared library (optional)</summary><div class="form">
        <label class="f">Ships to (countries, e.g. US, CA)<input name="ships" value="${v((SF.ships_to || "").split(",").filter((x) => x).join(", "))}"></label>
        <label class="f">Deliveries offered (weeks, e.g. 1, 2, 4)<input name="cad" value="${v((SF.cadences || []).map((x) => Math.round(x / 7 * 10) / 10).join(", "))}"></label>
        <label class="f">Price from<input name="pfrom" type="number" step="0.01" min="0" value="${SF.price_from_cents ? (SF.price_from_cents / 100).toFixed(2) : ""}"></label>
        <label class="f">Currency<input name="cur" maxlength="3" value="${v(SF.currency)}"></label></div>
        <div class="muted" style="margin-top:6px">Style</div><div class="row chips">${chips("style", STYLE)}</div>
        <div class="muted" style="margin-top:6px">Sourcing</div><div class="row chips">${chips("sourcing", SRC)}</div></details>
      <details style="margin-top:6px"><summary>Card look</summary><div class="form">
        <label class="f">Picture link<input name="image" value="${v(SF.image)}"></label><label class="f">Icon link<input name="icon" value="${v(SF.icon)}"></label>
        <label class="f">Colour<input name="color" type="color" value="${/^#[0-9a-f]{6}$/i.test(SF.color || "") ? SF.color : "#5b6470"}"></label></div></details>
      ${SF.id ? `<div class="row" style="margin-top:12px;border-top:1px solid var(--divider-color);padding-top:10px"><span class="pill">${esc(SHARE[SF.share || "local"] || SF.share)}</span><span class="hint" style="flex:1">${!this.SUBS.sharing ? "Sharing is off (Settings, Shared library)." : !SF.can_share ? "It needs a name and an https link to be shared." : SF.share == "published" && SF.library_id ? "Listed as " + esc(SF.library_id) + "." : ""}</span>${this.SUBS.sharing && SF.can_share && (!SF.share || ["local", "declined", "refused"].includes(SF.share) || SF.share_changed) ? '<button type="button" id="sfshare">Send to the shared library</button>' : ""}</div>
        <div style="margin-top:12px;border-top:1px solid var(--divider-color);padding-top:10px"><b>Your review</b>
          ${people.length ? `<div class="row" style="margin-top:6px"><span class="hint">Who is reviewing:</span><select id="svwho">${people.map(([pid, n]) => `<option value="${pid}">${esc(n)}</option>`).join("")}</select></div>` : ""}
          <div class="row stars" id="svstars" style="margin:6px 0"></div><div class="form" id="svasp"></div>
          <label class="f" style="margin-top:6px">Note<input id="svnote" maxlength="119"></label>
          <div class="row" style="margin-top:6px"><button type="button" id="svsave">Save review</button><button type="button" class="danger" id="svdel">Delete review</button><span class="hint">${this.SUBS.share_ratings ? "Shared anonymously once the service is in the library" : "Kept on this controller"}</span></div></div>` : ""}`;
    const extra = SF.id ? '<button type="button" class="danger" id="sfdel" style="margin-right:auto">Delete</button>' : "";
    const p = formDialog(this.shadowRoot, s ? "Edit " + (s.name || "subscription") : "Add a subscription", body, async (f) => {
      const every = +f.every.value || 0, unit = +f.unit.value;
      const out = { ...SF, url: f.url.value.trim(), name: f.name.value.trim(), kind: f.kind.value, status: f.status.value, every_days: Math.min(366, Math.round(every * unit)), next_date: d2i(f.next.value), price_cents: Math.round((+f.price.value || 0) * 100), manage_url: f.manage.value.trim(), note: f.note.value.trim(),
        item: f.item.value.trim(), stock: Math.max(0, Math.round(+f.stock.value || 0)), per_shot: f.kind.value == "supplies" ? Math.round(+f.pershot.value || 0) : 0, per_delivery: Math.round(+f.perdel.value || 0),
        ships_to: f.ships.value.toUpperCase().split(/[^A-Z]+/).filter((x) => x.length == 2).join(","), cadences: f.cad.value.split(/[^0-9.]+/).map((x) => Math.round(+x * 7)).filter((x) => x > 0 && x <= 366).slice(0, 6),
        price_from_cents: Math.round((+f.pfrom.value || 0) * 100), currency: f.cur.value.trim().toUpperCase(), style: SF.style, sourcing: SF.sourcing,
        image: f.image.value.trim(), icon: f.icon.value.trim(), color: f.color.value == "#5b6470" && !SF.color ? "" : f.color.value };
      if (!out.name && !out.url) { this.panel.toast("Give it a name or a link", false); return false; }
      if (out.manage_url && !/^https?:\/\//i.test(out.manage_url)) { this.panel.toast("The manage link must start https://", false); return false; }
      const r = await this.post("/api/subscriptions", out, "Subscription saved");
      if (r.ok) this.store.invalidate("subs");
      return r.ok;
    }, { wide: true, extra });
    const dlg = [...this.shadowRoot.querySelectorAll("form.dlg")].pop();
    const supSync = () => { dlg.querySelector(".sup").style.display = dlg.kind.value == "supplies" ? "" : "none"; };
    dlg.kind.onchange = supSync; supSync();
    dlg.querySelectorAll("[data-chip]").forEach((b) => b.onclick = () => { const [key, k] = b.dataset.chip.split(":"); const m = new Set(SF[key]); m.has(k) ? m.delete(k) : m.add(k); SF[key] = [...m]; b.classList.toggle("on", m.has(k)); });
    dlg.querySelector("#sfread").onclick = () => this.readPage(dlg, SF);
    const del = dlg.querySelector("#sfdel");
    if (del) del.onclick = async () => {
      if (!(await confirmDialog(this.shadowRoot, 'Delete "' + (SF.name || "this subscription") + '"? Bags it brought stay on the shelf.'))) return;
      const r = await this.post("/api/subscriptions/delete", { id: SF.id }, "Deleted");
      if (r.ok) { dlg.querySelector('[data-a="cancel"]').click(); this.store.invalidate("subs"); }
    };
    const share = dlg.querySelector("#sfshare");
    if (share) share.onclick = async () => {
      if (!(await confirmDialog(this.shadowRoot, 'Send "' + SF.name + '" to the shared library? Its name, links, kind, where it ships, deliveries offered, typical price, style and sourcing claims are sent. Your own price, dates, stock and notes are not.', { yes: "Send" }))) return;
      const r = await this.post("/api/subscriptions/share", { id: SF.id }, "Sent for review");
      if (r.ok) this.store.invalidate("subs");
    };
    if (SF.id) this.wireReview(dlg, SF, people);
    await p;
  }

  wireReview(dlg, SF, people) {
    const who = dlg.querySelector("#svwho");
    const revFor = (dr) => this.SREV.find((r) => r.subscription == SF.id && r.drinker == dr);
    let SV;
    const load = () => { const dr = who ? +who.value : 0, o = revFor(dr); SV = { drinker: dr, stars: o ? o.stars : 0, asp: ASP.map(([k]) => (o && o.aspects && o.aspects[k]) || 0) }; dlg.querySelector("#svnote").value = o ? o.note || "" : ""; draw(); };
    const draw = () => {
      dlg.querySelector("#svstars").innerHTML = [1, 2, 3, 4, 5].map((n) => `<button type="button" class="${n <= SV.stars ? "on" : ""}" data-sv="${n}">★</button>`).join("") + (SV.stars ? "" : '<span class="hint">tap to rate</span>');
      dlg.querySelector("#svasp").innerHTML = ASP.map(([, l], i) => `<label class="f">${l} <span>${SV.asp[i] ? SV.asp[i] + " / 5" : "not given"}</span><input type="range" min="0" max="5" step="1" value="${SV.asp[i]}" data-asp="${i}"></label>`).join("");
      dlg.querySelectorAll("[data-sv]").forEach((b) => b.onclick = () => { SV.stars = +b.dataset.sv; draw(); });
      dlg.querySelectorAll("[data-asp]").forEach((inp) => inp.oninput = () => { SV.asp[+inp.dataset.asp] = +inp.value; inp.previousElementSibling.textContent = +inp.value ? inp.value + " / 5" : "not given"; });
      dlg.querySelector("#svdel").style.display = revFor(SV.drinker) ? "" : "none";
    };
    if (who) who.onchange = load;
    dlg.querySelector("#svsave").onclick = async () => {
      if (!SV.stars) { this.panel.toast("Give it 1 to 5 stars", false); return; }
      const aspects = {}; ASP.forEach(([k], i) => { if (SV.asp[i]) aspects[k] = SV.asp[i]; });
      const r = await this.post("/api/subscriptions/review", { subscription: SF.id, drinker: SV.drinker, stars: SV.stars, aspects, note: dlg.querySelector("#svnote").value.trim() }, "Review saved");
      if (r.ok) { const rv = await this.api.getOr("/api/subscriptions/reviews", null, null); this.SREV = (rv && rv.reviews) || this.SREV; draw(); }
    };
    dlg.querySelector("#svdel").onclick = async () => {
      const o = revFor(SV.drinker);
      if (!o || !(await confirmDialog(this.shadowRoot, "Delete this review?"))) return;
      const r = await this.post("/api/subscriptions/review/delete", { id: o.id }, "Deleted");
      if (r.ok) { this.SREV = this.SREV.filter((x) => x.id != o.id); load(); }
    };
    load();
  }

  /** The controller reads the shop's page; poll until it is done (WebPage.h:1316-1324). */
  async readPage(dlg, SF) {
    const u = dlg.url.value.trim(), msg = dlg.querySelector("#sfreadmsg");
    if (!/^https?:\/\//i.test(u)) { this.panel.toast("Paste a link starting https://", false); return; }
    msg.textContent = "Reading the page…";
    const r = await this.api.post("/api/subscriptions/preview", { url: u });
    if (!r.ok) { msg.textContent = r.error || "Could not ask the controller to read it."; return; }
    let p = null;
    for (let i = 0; i < 20; i++) {
      await new Promise((z) => setTimeout(z, 1000));
      if (!dlg.isConnected) return;
      p = await this.api.getOr("/api/subscriptions/preview", null, null);
      if (p && p.url == u && p.state != "waiting") break;
    }
    if (!p || p.state == "waiting") { msg.textContent = "Still reading: try again in a moment."; return; }
    if (p.state == "failed") { msg.textContent = (p.problem || "The page could not be read") + ". Fill in the name; the card uses its initial."; if (!dlg.name.value) dlg.name.value = hostOf(u); return; }
    const keep = (n, val) => { if (val && !dlg[n].value) dlg[n].value = val; };
    SF.title = p.title; SF.site = p.site; SF.description = p.description; SF.platform = p.platform;
    keep("name", p.site || p.title); keep("image", p.image); keep("icon", p.icon); keep("manage", p.manage_url);
    if (p.color && !SF.color && !/^#(fff|ffffff|000|000000)$/i.test(p.color)) { dlg.color.value = p.color; SF.color = p.color; }
    msg.textContent = "Read: " + [p.title, PLAT[p.platform] ? "subscriptions by " + PLAT[p.platform] : ""].filter((x) => x).join(" · ") + (p.manage_url ? ". Check the Manage link." : "");
  }
}

customElements.define("brew-subscriptions", BrewSubscriptions);
