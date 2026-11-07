// Coffees: the shelf, finished bags, the coffee form, bags, how you liked it (ratings), brew it
// with (recipes), sharing, the shared library and directory, Visualizer lookup, Beanconqueror
// import, community dial-in (WebPage.h:285-332, 885-925, 1041-1145, 1350-1359, 1977-2010).
import { BrewSection } from "../lib/section.js";
import { html, raw } from "../lib/styles.js";
import { fs, f1, grindOf, d2i, i2d, today, age, frozenFor, coffeeById, esc } from "../lib/format.js";
import { confirmDialog } from "../lib/forms.js";

export const meta = { id: "coffees", title: "Coffees", icon: "mdi:sack" };

const CF = ["name", "roaster", "origin", "region", "producer", "varietal", "process", "roastLevel", "altitudeM", "roastDate", "frozenDate", "thawDate", "notes"];
const DK = ["roastDate", "frozenDate", "thawDate"];
const RLV = [[0, "not set"], [1, "light"], [2, "medium-light"], [3, "medium"], [4, "medium-dark"], [5, "dark"]];
const RL = ["", "light", "medium-light", "medium", "medium-dark", "dark"];
const ATTR = ["Acidity", "Sweetness", "Body", "Bitterness", "Finish"];
const FAM = ["Fruity", "Floral", "Sweet / caramel", "Chocolate / nutty", "Spicy", "Roasty / smoky", "Fermented / winey", "Earthy / green"];
const SHN = { local: "on this controller only", queued: "waiting to send", sent: "sent", review: "in review", published: "published", declined: "declined", refused: "refused by the library" };
const fd = (n) => (n && n > 20000101 ? new Date(Math.floor(n / 10000), Math.floor(n / 100) % 100 - 1, n % 100).toLocaleDateString([], { month: "short", day: "numeric", year: "numeric" }) : "(date not known)");
const clip = (s, n) => String(s == null ? "" : s).replace(/["\r\n]+/g, " ").trim().slice(0, n);

const UI = { C: -1, q: "", RS: null, dir: { q: "", origin: "", process: "", roast: "", page: 0 }, vz: [], showBag: false };

class BrewCoffees extends BrewSection {
  static get styles() {
    return `.split{display:grid;grid-template-columns:minmax(240px,320px) 1fr;gap:16px;align-items:start}
    @media (max-width:860px){.split{grid-template-columns:1fr}}
    .item{display:block;width:100%;text-align:left;border:1px solid var(--divider-color);border-radius:10px;padding:8px 10px;margin:6px 0;background:transparent;color:var(--primary-text-color);font-weight:400}
    .item.on{border-color:var(--primary-color);background:rgba(var(--rgb-primary-color,3,169,244),.08)}
    .item b{display:block} .item span{font-size:13px;color:var(--secondary-text-color)}
    .stack > .card{margin-bottom:16px}
    fieldset{border:1px solid var(--divider-color);border-radius:10px;margin:0 0 12px;padding:10px 12px} legend{color:var(--secondary-text-color);font-size:13px;padding:0 6px}
    .pair{display:flex;gap:10px;align-items:center;padding:8px 0;border-bottom:1px solid var(--divider-color)} .pair .what{flex:1}
    .stars button{font-size:20px;padding:2px 8px} .stars button.on{color:var(--warning-color,#ffa600);border-color:var(--warning-color,#ffa600)}
    .fam button.on{background:var(--primary-color);color:var(--text-primary-color,#fff);border-color:var(--primary-color)}
    .bagrow{display:flex;flex-wrap:wrap;gap:8px;align-items:center;padding:6px 0;border-bottom:1px solid var(--divider-color)} .bagrow > span{flex:1 1 220px}`;
  }

  async load() {
    const [L, S, RT, share] = await Promise.all([this.store.get("library"), this.store.get("settings"), this.store.get("ratings").catch(() => null), this.store.get("share").catch(() => null)]);
    const subs = await this.store.get("subs").catch(() => null);
    const CC = await this.api.getOr("/api/community/coffees", null, null);
    Object.assign(this, { L, S, RT, share, subs, CC: (CC && CC.coffees) || [], g: grindOf(L) });
    if (this.arg) { UI.C = +this.arg; this.arg = null; UI.RS = null; }
    if (UI.C < 0 || (UI.C && !coffeeById(L, UI.C))) UI.C = ((L.presets || [])[L.activePreset] || {}).coffeeId || ((L.coffees || [])[0] || {}).id || 0;
    this.community = null;
    const c = coffeeById(L, UI.C);
    if (c) {
      const ps = (L.presets || []).map((p, i) => ({ p, i })).filter((x) => x.p.coffeeId == c.id);
      const x = ps.find((y) => y.i == L.activePreset) || ps[0];
      if (x) this.community = { x, m: await this.api.getOr("/api/community", { coffee: c.id, recipe: x.p.recipeId }, null) };
    }
    const hidden = new Set(this.panel.hiddenTabs || []);
    this.dirOn = !hidden.has("community");
    this.dirData = this.dirOn ? await this.api.getOr("/api/directory", UI.dir, null) : null;
  }

  waitingBag(c) { return (this.L.bags || []).filter((b) => b.coffeeId == c.id && b.unopened).sort((a, b) => (a.roastDate || 9e9) - (b.roastDate || 9e9))[0] || null; }

  render() {
    const L = this.L, c = coffeeById(L, UI.C) || { roastLevel: 0 }, isNew = !UI.C;
    return html`<div class="split">
      <div class="card">
        <div class="spread"><h3>Coffees</h3><button class="primary" id="add">Add a coffee</button></div>
        <input id="q" type="search" placeholder="Search name, roaster, origin, process" value="${UI.q}" style="width:100%">
        <div id="list">${raw(this.listHtml())}</div>
      </div>
      <div class="stack">
        <div class="card">${raw(this.formHtml(c, isNew))}</div>
        ${!isNew ? raw(this.ratingHtml(c)) : ""}
        ${!isNew ? raw(this.pairsHtml(c)) : ""}
        ${!isNew ? raw(this.communityHtml(c)) : ""}
        <div class="card"><h3>Add coffees from elsewhere</h3>
          <div class="row"><input id="vzq" placeholder="Look up a coffee or roaster on visualizer.coffee" style="flex:1 1 240px"><button id="vzgo">Search</button></div>
          <div id="vzres" class="hint" style="margin-top:6px"></div>
          <div class="row" style="margin-top:12px"><label class="f" style="flex:1 1 240px">Bring in coffees from a Beanconqueror backup<input type="file" id="bcfile" accept=".json,application/json"></label><label class="c"><input type="checkbox" id="bcfin"> include finished beans</label></div>
          <div class="hint">Adds the beans you do not have yet, with their roast and opening dates, weights and freezer time. The file stays in your browser; shots are not imported.</div></div>
        ${this.dirOn ? raw(this.directoryHtml()) : ""}
      </div></div>`;
  }

  listHtml() {
    const L = this.L, q = UI.q.toLowerCase();
    const hit = (c) => !q || [c.name, c.roaster, c.origin, c.region, c.process, c.varietal].join(" ").toLowerCase().includes(q);
    const m = L.coffees.filter((c) => (!c.finished || this.waitingBag(c)) && hit(c)), fin = L.coffees.filter((c) => c.finished && !this.waitingBag(c) && hit(c));
    const item = (c, extra) => {
      const n = L.presets.filter((p) => p.coffeeId == c.id).length, u = L.bags.filter((b) => b.coffeeId == c.id && b.unopened).length, otw = L.bags.filter((b) => b.coffeeId == c.id && b.onTheWay);
      const d = c.finished || !c.bagId ? null : age(c.roastDate);
      const bits = extra || [c.roaster, n ? n + (n == 1 ? " recipe" : " recipes") : "not on the controller", d != null ? d + " days off roast" : "", u ? (u == 1 ? "1 unopened bag" : u + " unopened bags") : "", otw.length ? "on its way" + (otw[0].expectedDate ? ", expected " + fd(otw[0].expectedDate) : "") : "", c.share && c.share != "local" ? SHN[c.share] + (c.shareChanged ? ", changed since" : "") : ""];
      return `<button class="item ${c.id == UI.C ? "on" : ""}" data-c="${c.id}"><b>${esc(c.name)}</b><span>${esc(bits.filter((x) => x).join(" · "))}</span></button>`;
    };
    let h = m.map((c) => item(c)).join("") || `<div class="hint">${q ? "No coffee on the shelf matches." : "No coffee on the shelf."}</div>`;
    if (fin.length) h += `<details ${fin.some((c) => c.id == UI.C) || q ? "open" : ""}><summary>Finished bags (${fin.length})</summary>${fin.map((c) => item(c, [c.roaster, "finished " + fd(c.finished)])).join("")}</details>`;
    const onShelf = L.coffees.filter((c) => !c.finished).length, nf = L.coffees.length - onShelf;
    h += `<div class="hint">${onShelf} ${onShelf == 1 ? "coffee" : "coffees"} on the shelf${nf ? ", " + nf + " finished" : ""} (room for ${L.maxCoffees} in all).</div>`;
    const have = new Set(L.coffees.map((c) => c.libraryId).filter((x) => x));
    const sm = this.CC.filter((c) => !have.has(c.id) && (!q || [c.name, c.roaster, c.origin, c.region, c.process, c.varietal].join(" ").toLowerCase().includes(q))).slice(0, 8);
    if (sm.length) h += `<div class="muted" style="margin-top:12px">In the shared library</div>` + sm.map((c) => `<div class="pair"><span class="what"><b>${esc(c.name)}</b> <span class="hint">${esc([c.roaster, c.origin, c.process].filter((x) => x).join(", "))}</span></span><button data-adopt="${esc(c.id)}">Add</button></div>`).join("");
    return h;
  }

  formHtml(c, isNew) {
    const v = (k) => (DK.includes(k) ? i2d(c[k]) : c[k] == null ? "" : k == "altitudeM" && !c[k] ? "" : c[k]);
    const wb = !isNew ? this.waitingBag(c) : null;
    const f = (k, label, a = "") => `<label class="f">${label}<input name="${k}" value="${esc(v(k))}" ${a}></label>`;
    const st = c.share || "local", on = this.S && this.S.shareEnabled;
    const can = on && (st == "local" || st == "refused" || c.shareChanged);
    return `<div class="spread"><h3>${isNew ? "New coffee" : "Coffee"}</h3>${isNew ? "" : `<button class="danger" id="cdel">Delete</button>`}</div>
      <form id="cf">
      <fieldset><legend>The coffee</legend><div class="form">${f("name", "Name", 'maxlength="23" placeholder="e.g. Idjwi Island"')}${f("roaster", "Roaster", 'maxlength="23"')}
        <label class="f">Roast level<select name="roastLevel">${RLV.map(([n, l]) => `<option value="${n}" ${n == (c.roastLevel || 0) ? "selected" : ""}>${l}</option>`).join("")}</select></label>
        <label class="f" style="grid-column:1/-1">Tasting notes<input name="notes" maxlength="47" value="${esc(v("notes"))}" placeholder="e.g. blueberry, cocoa"></label></div></fieldset>
      <fieldset><legend>The bag in use</legend><div class="form">${f("roastDate", "Roast date printed on the bag", 'type="date"')}${f("frozenDate", "Put in the freezer", 'type="date"')}${f("thawDate", "Taken out (empty if still frozen)", 'type="date"')}</div></fieldset>
      <fieldset><legend>Where it is from (optional)</legend><div class="form">${f("origin", "Origin (country)", 'maxlength="23"')}${f("region", "Region", 'maxlength="31"')}${f("producer", "Producer or washing station", 'maxlength="31"')}${f("varietal", "Varietal", 'maxlength="31"')}${f("process", "Process", 'maxlength="15" placeholder="washed, natural..."')}${f("altitudeM", "Altitude (m)", 'type="number" min="0" max="4000" step="10"')}</div></fieldset>
      </form>
      <div class="row"><button class="primary" id="csave">Save</button>${isNew ? "" : `<button id="cbag">${c.finished ? "New bag: back on the shelf…" : "New bag…"}</button>${!c.finished && c.bagId ? `<button id="cfin">Bag finished</button>` : ""}`}
        <span class="hint">${wb && (c.finished || !c.bagId) ? "Waiting unopened: open it below when you start it." : c.finished ? "Bag finished " + fd(c.finished) + ". Off the controller until a new bag is started." : ""}</span></div>
      ${UI.showBag && !isNew ? `<div class="row" style="margin-top:10px;padding:10px;border-radius:10px;background:var(--secondary-background-color)"><label class="f">Roast date printed on the new bag<input type="date" id="nbroast"></label><label class="f">Opened (optional)<input type="date" id="nbopen"></label><button class="primary" id="nbstart">Start this bag</button><button id="nbcancel">Cancel</button></div>` : ""}
      ${isNew ? "" : this.bagsHtml(c)}
      ${isNew ? "" : `<div class="row" style="margin-top:12px;border-top:1px solid var(--divider-color);padding-top:10px"><span class="pill">${esc(SHN[st] || st)}</span><span class="hint" style="flex:1">${on ? (st == "published" ? "Approved: in the library" + (c.libraryId ? " as " + esc(c.libraryId) : "") + "." : st == "local" ? "Not sent to the shared library yet." : "") : "Sharing is switched off (Settings), so this coffee stays here."}${c.reviewNote ? ' Reviewer: "' + esc(c.reviewNote) + '"' : ""}${c.shareChanged ? " You have changed it since that version was sent." : ""}</span>${can ? `<button id="csend">${st == "local" ? "Send to the shared library" : st == "refused" ? "Send again" : "Send the new version"}</button>` : ""}</div>`}`;
  }

  bagsHtml(c) {
    const bs = (this.L.bags || []).filter((b) => b.coffeeId == c.id).sort((a, b) => b.id - a.id);
    if (!bs.length) return "";
    const subs = (this.subs && this.subs.subscriptions) || [];
    return `<div class="muted" style="margin-top:14px">Bags of ${esc(c.name)}</div>` + bs.map((b) => {
      const f = frozenFor({ frozenDate: b.frozenDate, thawDate: b.thawDate });
      const bits = [b.roastDate ? "roasted " + i2d(b.roastDate) : "roast date not entered", b.openedDate ? "opened " + i2d(b.openedDate) : "", f ? f + " days frozen" + (b.frozenDate && !b.thawDate ? " (still there)" : "") : "", b.weightG ? b.weightG + " g" : ""].filter((x) => x).join(", ");
      const from = b.subscriptionId ? subs.find((x) => x.id == b.subscriptionId) : null, fromTxt = from ? ` <span class="hint">from ${esc(from.name)}</span>` : "";
      if (b.onTheWay) return `<div class="bagrow"><span>${b.expectedDate ? "expected " + i2d(b.expectedDate) : "not come yet"}${b.weightG ? ", " + b.weightG + " g" : ""} <span class="pill">on its way</span>${fromTxt}</span><button class="primary" data-bag="bag_arrived" data-a="${b.id}">It arrived</button><button class="danger" data-bagdel="${b.id}">Forget</button></div>`;
      if (b.unopened) return `<div class="bagrow"><span>${esc(bits)} <span class="pill warn">unopened</span>${fromTxt}</span><button class="primary" data-bag="bag_open" data-a="${b.id}">Open this bag</button><button class="danger" data-bagdel="${b.id}">Forget</button></div>`;
      return `<div class="bagrow"><span>${esc(bits)}${b.current ? ' <span class="pill">in use</span>' : ""}</span>${b.current ? "" : `<button data-bag="bag_use" data-a="${c.id}" data-b="${b.id}">Use this bag</button><button class="danger" data-bagdel="${b.id}">Forget</button>`}</div>`;
    }).join("");
  }

  ratingHtml(c) {
    const RT = this.RT;
    if (!RT) return "";
    const named = RT.named, people = named ? RT.drinkers.map((n, i) => [i + 1, n]).filter((x) => x[1]) : [];
    const ratingFor = (bag, dr) => RT.ratings.find((r) => r.coffee == c.id && r.bag == bag && r.drinker == dr);
    if (!UI.RS || UI.RS.coffee != c.id || (UI.RS.bag == null && UI.RS.forBag != (c.bagId || 0))) {
      const dr = named && people.length ? (UI.RS && UI.RS.drinker && RT.drinkers[UI.RS.drinker - 1] ? UI.RS.drinker : people[0][0]) : 0, old = ratingFor(c.bagId || 0, dr);
      UI.RS = old ? { coffee: c.id, id: old.id, drinker: dr, stars: old.stars, attr: [...old.attr], fl: old.flavours } : { coffee: c.id, id: 0, drinker: dr, stars: 0, attr: [0, 0, 0, 0, 0], fl: 0 };
      UI.RS.forBag = c.bagId || 0;
    }
    const RS = UI.RS, dn = (id) => (!id ? "Household" : RT.drinkers[id - 1] || "Drinker " + id);
    const bag = (this.L.bags || []).find((b) => b.id == c.bagId);
    const mine = RT.ratings.filter((r) => r.coffee == c.id);
    return `<div class="card"><div class="spread"><h3>How you liked it</h3><span class="hint">${bag && bag.roastDate ? "the bag roasted " + fd(bag.roastDate) : ""}</span></div>
      ${named ? (people.length ? `<div class="row"><span class="hint">Who is rating:</span>${people.map(([id, n]) => `<button class="${id == RS.drinker ? "primary" : ""}" data-drinker="${id}">${esc(n)}</button>`).join("")}</div>` : `<div class="hint">Named drinkers is on, but no names are set: add them in Settings.</div>`) : ""}
      <div class="row stars" style="margin:8px 0">${[1, 2, 3, 4, 5].map((n) => `<button class="${n <= RS.stars ? "on" : ""}" data-star="${n}" aria-label="${n} stars">★</button>`).join("")}${RS.stars ? "" : '<span class="hint">tap to rate</span>'}</div>
      <div class="form">${ATTR.map((l, i) => `<label class="f">${l} <span>${RS.attr[i] ? RS.attr[i] + " / 5" : "not given"}</span><input type="range" min="0" max="5" step="1" value="${RS.attr[i]}" data-attr="${i}"></label>`).join("")}</div>
      <div class="muted" style="margin-top:8px">Flavours you tasted</div>
      <div class="row fam">${FAM.map((l, i) => `<button class="${RS.fl & (1 << i) ? "on" : ""}" data-fam="${i}">${l}</button>`).join("")}</div>
      <div class="row" style="margin-top:10px"><button class="primary" id="rsave">Save rating</button><button id="rclear">Clear</button><span class="hint">${RS.id ? "Changing your rating of this bag" + (named ? " (" + esc(dn(RS.drinker)) + ")" : "") : ""}</span></div>
      <div style="margin-top:8px">${mine.length ? mine.map((r) => {
        const b = (this.L.bags || []).find((x) => x.id == r.bag), attrs = r.attr.map((v, i) => (v ? ATTR[i].toLowerCase() + " " + v : "")).filter(Boolean).join(", "), fl = FAM.filter((_, i) => r.flavours & (1 << i)).map((x) => x.toLowerCase()).join(", ");
        return `<div class="pair"><span class="what"><b style="color:var(--warning-color)">${"★".repeat(r.stars) + "☆".repeat(5 - r.stars)}</b> <span class="hint">${named || r.drinker ? esc(dn(r.drinker)) + " · " : ""}${r.t ? new Date(r.t * 1000).toLocaleDateString() : ""}${b && b.roastDate ? " · bag roasted " + fd(b.roastDate) : ""}</span><div class="hint">${esc([attrs, fl].filter(Boolean).join(" · ") || "stars only")}</div></span><button data-redit="${r.id}">Edit</button><button class="danger" data-rdel="${r.id}">Delete</button></div>`;
      }).join("") : '<div class="hint">Not rated yet.</div>'}</div></div>`;
  }

  pairsHtml(c) {
    const L = this.L, g = this.g;
    return `<div class="card"><div class="spread"><h3>Brew it with</h3><a class="link" data-go="recipes">Recipes</a></div>
      ${L.recipes.map((r) => {
        const i = L.presets.findIndex((p) => p.coffeeId == c.id && p.recipeId == r.id), on = i >= 0, p = on ? L.presets[i] : null;
        return `<div class="pair"><input type="checkbox" ${on ? "checked" : ""} ${c.finished ? "disabled" : ""} data-pair="${on ? "del:" + i : "add:" + r.id}" aria-label="Brew with ${esc(r.name)}"><span class="what"><b>${esc(r.name)}</b> <span class="hint">${r.doseG} g in, ${r.targetWeightG} g out, ${r.targetTimeS} s${r.bloomPauseS ? ", bloom " + r.bloomPauseS + " s" : ""}</span></span>${on ? `<span class="hint">grinder ${p.grindUm ? esc(fs(g, p.grindUm)) : "not set yet"}</span>` : ""}</div>`;
      }).join("")}
      <div class="hint">Each ticked line is one entry on the controller (${L.presets.length} of ${L.maxPresets} used). The grind setting and the learned stop offset belong to the pair.</div></div>`;
  }

  communityHtml(c) {
    const cm = this.community, g = this.g;
    if (!cm) return `<div class="card"><h3>Community dial-in</h3><div class="hint">${c.libraryId ? "Tick a recipe for this coffee to see what others settled on for it." : esc(c.name) + " is not in the shared library yet. Once it is published, where other owners settle shows here."}</div></div>`;
    const m = cm.m, x = cm.x, r = (this.L.recipes || []).find((q) => q.id == x.p.recipeId) || {};
    if (!m) return "";
    let body;
    if (!c.libraryId) body = `<div class="hint">${esc(c.name)} is not in the shared library yet. Once it is published, where other owners settle shows here.</div>`;
    else if (!m.found) body = `<div class="hint">${esc("Nobody has shared a settled dial-in for " + c.name + " with a recipe like " + r.name + " (" + r.doseG + " g in, " + r.targetWeightG + " g out) yet." + (m.reachable ? "" : " The shared library cannot be reached from this controller yet."))}</div>`;
    else {
      const s = m.start;
      body = `<div class="tiles"><div class="tile"><div class="k">Start your grinder at</div><div class="v">${s.setting ? esc(fs(g, s.setting)) + " ± " + s.range : "not known"}</div></div><div class="tile"><div class="k">What a good shot looked like</div><div class="v">${f1(m.time_s)} s</div><div class="k">${r.doseG} g in, ${r.targetWeightG} g out</div></div><div class="tile"><div class="k">Based on</div><div class="v">${m.contributors} ${m.contributors == 1 ? "person" : "people"}</div><div class="k">${m.dialins} settled dial-in${m.dialins == 1 ? "" : "s"}</div></div></div>` +
        (s.setting ? `<div class="row" style="margin-top:10px"><button data-usestart="${x.i}:${s.setting}">Use ${esc(fs(g, s.setting))} as my starting grind</button><span class="hint">${x.p.grindUm ? "Your setting for it now: " + esc(fs(g, x.p.grindUm)) + "." : "No setting entered for it yet."}</span></div>` : "");
    }
    const sim = m.similar && m.similar.found ? `<div class="tile" style="margin-top:10px"><div class="k">A guess from your own coffees</div><div class="v">${esc(fs(g, m.similar.setting))}</div><div class="k">From ${esc(m.similar.from_coffee)} (${esc(fs(g, m.similar.from_setting))}), your nearest coffee. Grind advice takes over from your first shot.</div></div><div class="row" style="margin-top:8px"><button data-usestart="${x.i}:${m.similar.setting}">Use ${esc(fs(g, m.similar.setting))} as my starting grind</button></div>` : "";
    return `<div class="card"><h3>Community dial-in</h3>${body}${sim}</div>`;
  }

  directoryHtml() {
    const d = this.dirData;
    if (!d) return `<div class="card"><h3>Coffee directory</h3><div class="hint">The directory cannot be read from the controller.</div></div>`;
    const facet = (key, name, all) => `<select data-dir="${name}"><option value="">${all}</option>${Object.entries((d.facets || {})[key] || {}).map(([k, n]) => `<option value="${esc(k)}" ${UI.dir[name] == k ? "selected" : ""}>${esc(k)} (${n})</option>`).join("")}</select>`;
    const pages = d.page_size ? Math.ceil(d.matched / d.page_size) : 1;
    return `<div class="card"><div class="spread"><h3>Coffee directory</h3><span class="hint">${d.count ? d.count + " coffees" : ""}</span></div>
      <div class="hint">Every coffee published in the shared library, kept on the controller.</div>
      <div class="row" style="margin-top:8px"><input type="search" id="dirq" placeholder="name, roaster, origin, notes" value="${esc(UI.dir.q)}" style="flex:2 1 200px">${facet("origin", "origin", "Any origin")}${facet("process", "process", "Any process")}${facet("roast_level", "roast", "Any roast")}</div>
      <div style="margin-top:8px">${!d.kept_pages ? '<div class="empty">The directory has not been read from the shared library yet.</div>' : !d.coffees.length ? '<div class="empty">No coffee in the directory matches that.</div>' :
        d.coffees.map((c) => `<div class="pair"><span class="what"><b>${esc(c.name)}</b> <span class="hint">${esc([c.roaster, c.origin, c.process, c.roast_level].filter(Boolean).join(" · "))}</span>${c.stars ? ` <span style="color:var(--warning-color)">★${c.stars}</span> <span class="hint">(${c.n})</span>` : ""}${c.notes ? '<div class="hint">' + esc(c.notes) + "</div>" : ""}</span>${c.on_shelf ? '<span class="hint">on your shelf</span>' : `<button data-diradopt="${esc(c.id)}">Add to my coffees</button>`}</div>`).join("")}</div>
      ${pages > 1 ? `<div class="row" style="margin-top:8px"><button id="dirprev" ${UI.dir.page ? "" : "disabled"}>Previous</button><span class="hint">${d.matched} found · page ${UI.dir.page + 1} of ${pages}</span><button id="dirnext" ${UI.dir.page + 1 < pages ? "" : "disabled"}>Next</button></div>` : d.matched ? `<div class="hint">${d.matched} found</div>` : ""}</div>`;
  }

  // --- actions --------------------------------------------------------------------------------------
  async act(what, a, b, okText) {
    const r = await this.post("/api/library/do", null, okText === undefined ? "Saved" : okText, { what, a: a || 0, b: b || 0 });
    if (r.ok) this.store.invalidate("library", "grind");
    return r;
  }
  async adopt(c) {
    const r = await this.post("/api/coffee/adopt", { libraryId: c.id, name: c.name, roaster: c.roaster || "", origin: c.origin || "", region: c.region || "", producer: c.producer || "", varietal: c.varietal || "", process: c.process || "", notes: c.notes || "", roastLevel: Math.max(0, RL.indexOf(c.roast_level || "")), altitudeM: c.altitude_m || 0 }, "Added to your shelf");
    if (r.ok) { UI.C = r.id; this.store.invalidate("library"); }
  }

  wire() {
    this.$("#add").onclick = () => { UI.C = 0; UI.RS = null; this._paint(); };
    const q = this.$("#q");
    q.oninput = () => { UI.q = q.value; this.$("#list").innerHTML = this.listHtml(); this.wireList(); };
    this.wireList();
    this.$$("[data-go]").forEach((a) => a.onclick = () => this.panel.navigate(a.dataset.go));
    const L = this.L, c = coffeeById(L, UI.C);
    // the form
    this.$("#csave").onclick = async () => {
      const f = this.$("#cf"), b = { id: UI.C || 0 };
      CF.forEach((k) => { const v = f[k].value; b[k] = DK.includes(k) ? d2i(v) : (k == "roastLevel" || k == "altitudeM") ? +v || 0 : v.trim(); });
      if (!b.name) { this.panel.toast("A coffee needs a name", false); return; }
      if (!UI.C && L.coffees.some((x) => x.name.toLowerCase() == b.name.toLowerCase() && (x.roaster || "").toLowerCase() == b.roaster.toLowerCase()) && !(await confirmDialog(this.shadowRoot, '"' + b.name + '" is already on the shelf. Add it again?', { yes: "Add" }))) return;
      const r = await this.post("/api/coffee", b, "Coffee saved");
      if (r.ok) { UI.C = r.id; this.store.invalidate("library", "grind"); }
    };
    const del = this.$("#cdel");
    if (del) del.onclick = async () => { if (!(await confirmDialog(this.shadowRoot, 'Delete "' + c.name + '" from the shelf? Its shots stay in the history.'))) return; const r = await this.act("coffee_delete", UI.C, 0, "Deleted"); if (r.ok) UI.C = -1; };
    const cbag = this.$("#cbag"); if (cbag) cbag.onclick = () => { UI.showBag = true; this._paint(); };
    const nbc = this.$("#nbcancel"); if (nbc) nbc.onclick = () => { UI.showBag = false; this._paint(); };
    const nbs = this.$("#nbstart");
    if (nbs) nbs.onclick = async () => {
      const d = d2i(this.$("#nbroast").value), o = d2i(this.$("#nbopen").value);
      if (!d) { this.panel.toast("Enter the roast date printed on the bag", false); return; }
      if (d > today()) { this.panel.toast("That date has not come yet", false); return; }
      if (c.roastDate == d && !(await confirmDialog(this.shadowRoot, "The bag in use has this roast date. Start another bag anyway?", { yes: "Start it" }))) return;
      const r = await this.post("/api/bag", { coffeeId: c.id, roastDate: d, openedDate: o }, "New bag started");
      if (r.ok) { UI.showBag = false; this.store.invalidate("library", "grind"); }
    };
    const fin = this.$("#cfin");
    if (fin) fin.onclick = async () => { if (!(await confirmDialog(this.shadowRoot, 'Is the bag of "' + c.name + '" finished? It moves to Finished bags with everything kept, and leaves the controller until you start a new bag.', { yes: "Bag finished" }))) return; this.act("coffee_finish", UI.C, 0, c.name + " moved to Finished bags"); };
    this.$$("[data-bag]").forEach((b) => b.onclick = () => this.act(b.dataset.bag, +b.dataset.a, +(b.dataset.b || 0), b.dataset.bag == "bag_open" ? "Bag opened: tracking starts now" : b.dataset.bag == "bag_arrived" ? "On the shelf, unopened: add its roast date" : "Saved"));
    this.$$("[data-bagdel]").forEach((b) => b.onclick = async () => { if (await confirmDialog(this.shadowRoot, "Forget this bag? Its shots stay in the history.")) this.act("bag_delete", +b.dataset.bagdel, 0, "Forgotten"); });
    const send = this.$("#csend");
    if (send) send.onclick = async () => { const r = await this.post("/api/share/coffee", null, "Queued for the shared library", { id: UI.C }); if (r.ok) this.store.invalidate("library", "share"); };
    // rating
    if (c && UI.RS) {
      const RS = UI.RS;
      this.$$("[data-drinker]").forEach((b) => b.onclick = () => { const id = +b.dataset.drinker, old = this.RT.ratings.find((r) => r.coffee == c.id && r.bag == (c.bagId || 0) && r.drinker == id); UI.RS = old ? { coffee: c.id, id: old.id, drinker: id, stars: old.stars, attr: [...old.attr], fl: old.flavours } : { coffee: c.id, id: 0, drinker: id, stars: 0, attr: [0, 0, 0, 0, 0], fl: 0 }; UI.RS.forBag = c.bagId || 0; this._paint(); });
      this.$$("[data-star]").forEach((b) => b.onclick = () => { RS.stars = +b.dataset.star; this._paint(); });
      this.$$("[data-attr]").forEach((inp) => inp.oninput = () => { RS.attr[+inp.dataset.attr] = +inp.value; inp.previousElementSibling.textContent = +inp.value ? inp.value + " / 5" : "not given"; });
      this.$$("[data-fam]").forEach((b) => b.onclick = () => { RS.fl ^= 1 << +b.dataset.fam; this._paint(); });
      const rs = this.$("#rsave");
      if (rs) rs.onclick = async () => {
        if (!RS.stars) { this.panel.toast("Give it 1 to 5 stars first", false); return; }
        const r = await this.post("/api/coffee/rate", { id: RS.id || 0, coffee: c.id, bag: RS.bag != null ? RS.bag : (c.bagId || 0), drinker: RS.drinker, stars: RS.stars, attr: RS.attr, flavours: RS.fl }, "Rating saved");
        if (r.ok) { RS.id = r.id; this.store.invalidate("ratings"); }
      };
      const rc = this.$("#rclear"); if (rc) rc.onclick = () => { UI.RS = null; this._paint(); };
      this.$$("[data-redit]").forEach((b) => b.onclick = () => { const r = this.RT.ratings.find((x) => x.id == +b.dataset.redit); if (!r) return; UI.RS = { coffee: c.id, id: r.id, drinker: r.drinker, stars: r.stars, attr: [...r.attr], fl: r.flavours, bag: r.bag }; this._paint(); });
      this.$$("[data-rdel]").forEach((b) => b.onclick = async () => { if (!(await confirmDialog(this.shadowRoot, "Delete this rating?"))) return; const r = await this.post("/api/coffee/rate/delete", null, "Deleted", { id: +b.dataset.rdel }); if (r.ok) { if (UI.RS && UI.RS.id == +b.dataset.rdel) UI.RS = null; this.store.invalidate("ratings"); } });
    }
    // brew it with
    this.$$("[data-pair]").forEach((cb) => cb.onchange = () => { const [k, v] = cb.dataset.pair.split(":"); k == "del" ? this.act("preset_delete", +v) : this.act("preset_add", +v, UI.C); });
    this.$$("[data-usestart]").forEach((b) => b.onclick = async () => {
      const [i, um] = b.dataset.usestart.split(":").map(Number), p = L.presets[i];
      if (p.grindUm && p.grindUm != um && !(await confirmDialog(this.shadowRoot, "Replace your setting of " + fs(this.g, p.grindUm) + " with " + fs(this.g, um) + "?"))) return;
      this.act("preset_grind", i, um, "Starting grind set");
    });
    // visualizer.coffee (open CORS; needs this device online) (WebPage.h:1999-2009)
    const vz = async () => {
      const qq = this.$("#vzq").value.trim(), out = this.$("#vzres");
      if (qq.length < 3) { this.panel.toast("Type at least three letters of the coffee or roaster", false); return; }
      out.textContent = "Searching…";
      try {
        const r = await fetch("https://visualizer.coffee/api/canonical_coffee_bags?items=8&q=" + encodeURIComponent(qq), { headers: { Accept: "application/json" } });
        if (!r.ok) throw new Error("Visualizer answered " + r.status);
        UI.vz = (await r.json()).data || [];
      } catch (e) { UI.vz = []; out.textContent = "Could not reach visualizer.coffee (" + e.message + ")."; return; }
      out.innerHTML = UI.vz.length ? UI.vz.map((b, i) => `<div class="pair"><span class="what"><b>${esc(b.name)}</b> <span class="hint">${esc([b.canonical_roaster_name, b.country, b.processing].filter((x) => x).join(", "))}</span></span><button data-vz="${i}">Fill the form</button></div>`).join("") : "Nothing found.";
      out.querySelectorAll("[data-vz]").forEach((btn) => btn.onclick = () => this.vzUse(UI.vz[+btn.dataset.vz]));
    };
    this.$("#vzgo").onclick = vz;
    this.$("#vzq").onkeydown = (e) => { if (e.key == "Enter") vz(); };
    this.$("#bcfile").onchange = (e) => this.importBeanconqueror(e.target);
    // directory
    if (this.dirOn) {
      let t = 0;
      const dq = this.$("#dirq");
      if (dq) dq.oninput = () => { clearTimeout(t); t = setTimeout(() => { UI.dir.q = dq.value.trim(); UI.dir.page = 0; this.refresh(); }, 300); };
      this.$$("[data-dir]").forEach((s) => s.onchange = () => { UI.dir[s.dataset.dir] = s.value; UI.dir.page = 0; this.refresh(); });
      const pv = this.$("#dirprev"); if (pv) pv.onclick = () => { UI.dir.page--; this.refresh(); };
      const nx = this.$("#dirnext"); if (nx) nx.onclick = () => { UI.dir.page++; this.refresh(); };
      this.$$("[data-diradopt]").forEach((b) => b.onclick = () => { const cc = (this.dirData.coffees || []).find((x) => x.id == b.dataset.diradopt); if (cc) this.adopt(cc); });
    }
  }

  wireList() {
    this.$$("[data-c]").forEach((b) => b.onclick = () => { UI.C = +b.dataset.c; UI.RS = null; UI.showBag = false; this.refresh(); });
    this.$$("[data-adopt]").forEach((b) => b.onclick = () => { const cc = this.CC.find((x) => x.id == b.dataset.adopt); if (cc) this.adopt(cc); });
  }

  vzUse(b) {
    const alt = (/\d[\d.,]*/.exec(String(b.elevation || "")) || [""])[0].replace(/[.,]/g, ""), altM = +alt >= 1 && +alt <= 4000 ? +alt : 0;
    const s = String(b.roast_level || "").toLowerCase();
    const rl = /medium.?dark|full city|vienna|dark|french|italian/.test(s) ? (/medium.?dark|full city/.test(s) ? 4 : 5) : /medium.?light|light.?medium/.test(s) ? 2 : /light|cinnamon|nordic/.test(s) ? 1 : /medium|city/.test(s) ? 3 : 0;
    const c = { name: clip(b.name, 23), roaster: clip(b.canonical_roaster_name, 23), origin: clip(b.country, 23), region: clip(b.region, 31), producer: clip(b.farmer, 31), varietal: clip(b.variety, 31), process: clip(b.processing, 15), notes: clip(b.tasting_notes, 47), altitudeM: altM, roastLevel: rl };
    UI.C = 0; UI.RS = null;
    this._paint();
    const f = this.$("#cf");
    CF.forEach((k) => { if (k in c && f[k]) f[k].value = c[k] || ""; });
    this.panel.toast("Filled in from visualizer.coffee. Check it, add the roast date from your bag, then Save.");
  }

  // Beanconqueror backup import (WebPage.h:1978-1996)
  async importBeanconqueror(input) {
    const file = input.files && input.files[0];
    if (!file) return;
    let json;
    try { json = JSON.parse(await file.text()); } catch (e) { this.panel.toast("That is not a Beanconqueror backup file (it is not JSON)", false); return; }
    const BCROAST = { CINNAMON_ROAST: 1, NEW_ENGLAND_ROAST: 1, HALF_CITY_ROAST: 1, MODERATE_LIGHT_ROAST: 2, AMERICAN_ROAST: 2, CITY_ROAST: 3, CITY_PLUS_ROAST: 3, FULL_CITY_ROAST: 4, FULL_CITY_PLUS_ROAST: 4, VIEANNA_ROAST: 5, VIENNA_ROAST: 5, ITALIAN_ROAST: 5, FRENCH_ROAST: 5 };
    const isoToYmd = (s) => { const m = /^(\d{4})-(\d\d)-(\d\d)/.exec(s || ""); return m ? +(m[1] + m[2] + m[3]) : 0; };
    const withFinished = this.$("#bcfin").checked;
    const list = Array.isArray(json.BEANS) ? json.BEANS : json.bean ? [json.bean] : [];
    const beans = [], skipped = [];
    for (const b of list) {
      const i = (b.bean_information && b.bean_information[0]) || {};
      const alt = (/\d[\d.,]*/.exec(String(i.elevation || "")) || [""])[0].replace(/[.,]/g, ""), altM = +alt >= 1 && +alt <= 4000 ? +alt : 0;
      const coffee = { name: clip(b.name, 23), roaster: clip(b.roaster, 23), origin: clip(i.country, 23), region: clip(i.region, 31), producer: clip(i.farmer || i.farm, 31), varietal: clip(i.variety, 31), process: clip(i.processing, 15), notes: clip(b.aromatics, 47), altitudeM: altM, roastLevel: BCROAST[b.roast] || 0, roastDate: isoToYmd(b.roastingDate) };
      const bag = { roastDate: coffee.roastDate, openedDate: isoToYmd(b.openDate), frozenDate: isoToYmd(b.frozenDate), thawDate: isoToYmd(b.unfrozenDate), weightG: b.weight > 0 && b.weight < 65535 ? Math.round(b.weight) : 0 };
      if (!coffee.name) { skipped.push("a bean with no name"); continue; }
      if (b.finished && !withFinished) { skipped.push(coffee.name + " (finished)"); continue; }
      beans.push({ coffee, bag });
    }
    if (!beans.length) { this.panel.toast("No beans found in that file" + (skipped.length ? " (" + skipped.length + " skipped)" : "")); return; }
    const L = this.L, have = new Set(L.coffees.map((c) => (c.name + "|" + (c.roaster || "")).toLowerCase()));
    let added = 0, dup = 0, full = 0;
    for (const x of beans) {
      const key = (x.coffee.name + "|" + x.coffee.roaster).toLowerCase();
      if (have.has(key)) { dup++; continue; }
      if (L.coffees.length + added >= L.maxCoffees) { full++; continue; }
      const res = await this.api.post("/api/coffee", x.coffee);
      if (!res.ok && !res.id) continue;
      added++; have.add(key);
      if (x.bag.openedDate || x.bag.weightG || x.bag.frozenDate) {
        const lib = await this.api.getOr("/api/library", null, null), cc = lib && lib.coffees.find((q) => q.id == res.id);
        if (cc && cc.bagId) await this.api.post("/api/bag", { id: cc.bagId, ...x.bag });
      }
    }
    input.value = "";
    this.store.invalidate("library");
    this.panel.toast("From Beanconqueror: " + added + " coffee" + (added == 1 ? "" : "s") + " added" + (dup ? ", " + dup + " already on the shelf" : "") + (full ? ", " + full + " left out: the shelf is full" : "") + (skipped.length ? ", " + skipped.length + " skipped" : "") + ". Shots are not imported.", true);
  }
}

customElements.define("brew-coffees", BrewCoffees);
