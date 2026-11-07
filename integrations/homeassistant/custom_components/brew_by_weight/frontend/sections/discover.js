// Discover: coffees and subscriptions recommended for a drinker, the shared library's coffees
// (published, waiting for review: approve or decline) and recipe book (WebPage.h:481-496, 955-959,
// 1053-1096).
import { BrewSection } from "../lib/section.js";
import { html, raw } from "../lib/styles.js";
import { coffeeById, esc } from "../lib/format.js";

export const meta = { id: "discover", title: "Discover", icon: "mdi:compass", optional: true };

const RL = ["", "light", "medium-light", "medium", "medium-dark", "dark"];
const starTxt = (n) => "★".repeat(n) + "☆".repeat(5 - n);
const moreTxt = (n) => { const k = Math.max(1, 3 - (n || 0)); return k + " more coffee" + (k == 1 ? "" : "s"); };
const lbNum = (v) => (v == null ? "" : String(Math.round(v * 10) / 10));
const recipeLine = (r) => `${lbNum(r.dose_g)} g in, ${lbNum(r.yield_g)} g out` + (r.target_time_s ? `, ${lbNum(r.target_time_s)} s` : "") + (r.preinfusion_hold_s ? `, hold ${lbNum(r.preinfusion_hold_s)} s` : "") + (r.bloom_pause_s ? `, bloom ${lbNum(r.bloom_pause_s)} s` : "");
const UI = { drinker: 0, ctab: 0, rtab: 0, cq: "", rq: "" };

class BrewDiscover extends BrewSection {
  static get styles() {
    return `.recos{display:grid;grid-template-columns:repeat(auto-fill,minmax(220px,1fr));gap:10px}
    .reco{border:1px solid var(--divider-color);border-radius:10px;padding:10px 12px;display:block;color:var(--primary-text-color);text-decoration:none}
    .reco .src{font-size:12px;color:var(--secondary-text-color);display:block} .reco b{display:block;margin:2px 0}
    a.reco{cursor:pointer} a.reco:hover{border-color:var(--primary-color)}
    .seg button.on{background:var(--primary-color);color:var(--text-primary-color,#fff);border-color:var(--primary-color)}
    .it{display:flex;flex-wrap:wrap;gap:8px;align-items:center;padding:8px 0;border-bottom:1px solid var(--divider-color)} .it .what{flex:1 1 240px}`;
  }

  async load() {
    const [L, RT] = await Promise.all([this.store.get("library"), this.store.get("ratings").catch(() => null)]);
    const hidden = new Set(this.panel.hiddenTabs || []);
    const [reco, subReco, CC, LP, LR] = await Promise.all([
      this.api.getOr("/api/taste/recommend", { drinker: UI.drinker }, null),
      hidden.has("subscriptions") ? null : this.api.getOr("/api/subscriptions/recommend", { drinker: UI.drinker }, null),
      this.api.getOr("/api/community/coffees", null, null), this.api.getOr("/api/library/pending", null, null), this.api.getOr("/api/community/recipes", null, null),
    ]);
    Object.assign(this, { L, RT, reco, subReco, CC: (CC && CC.coffees) || [], LP, LR: (LR && LR.items) || [] });
  }

  render() {
    const RT = this.RT, named = RT && RT.named, people = named ? RT.drinkers.map((n, i) => [i + 1, n]).filter((x) => x[1]) : [];
    const r = this.reco, L = this.L, from = { shelf: "Your shelf", library: "Library", community: "Library ratings" };
    const have = new Set(L.coffees.map((c) => c.libraryId).filter((x) => x));
    const cq = UI.cq.toLowerCase(), chit = this.CC.filter((c) => !cq || [c.name, c.roaster, c.origin, c.region, c.process, c.varietal].join(" ").toLowerCase().includes(cq));
    const rq = UI.rq.toLowerCase(), rhit = this.LR.filter((x) => !rq || [(x.recipe || {}).name, recipeLine(x.recipe || {})].join(" ").toLowerCase().includes(rq));
    return html`
    ${people.length ? html`<div class="row seg" style="margin-bottom:12px"><span class="hint">For:</span>${raw([[0, "Household"], ...people].map(([id, n]) => `<button class="${id == UI.drinker ? "on" : ""}" data-drinker="${id}">${esc(n)}</button>`).join(""))}</div>` : ""}
    <div class="card" style="margin-bottom:16px"><h3>Coffees you might like</h3>
      ${!r ? html`<div class="hint">Recommendations cannot be read from the controller.</div>` : !r.shown ? html`<div class="empty">Rate ${moreTxt(r.coffees)} on the Coffees tab and recommendations appear here.</div>` :
        raw(`<div class="recos">${(r.items || []).length ? r.items.map((it) => {
          const like = it.like && coffeeById(L, it.like);
          const extra = [it.people_stars ? `drinkers like you: ★${it.people_stars}` : "", it.community_n ? `library: ★${it.community_stars} from ${it.community_n}` : "", it.sure < 0.4 ? "a guess from its description" : ""].filter(Boolean).join(" · ");
          const desc = [it.roaster, it.origin, it.process].filter(Boolean).map(esc).join(" · ");
          return `<${it.coffee ? `a data-coffee="${it.coffee}"` : "div"} class="reco"><span class="src">${from[it.from] || ""}</span><b>${esc(it.name)}</b>${desc ? `<div class="hint">${desc}</div>` : ""}${it.why && it.why.length ? `<div style="margin-top:4px"><b>${esc(it.why.join(", "))}</b></div>` : ""}${like ? `<div class="hint">Like the ${esc(like.name)} you gave ${starTxt(it.like_stars)}</div>` : ""}${extra ? `<div class="hint">${extra}</div>` : ""}</${it.coffee ? "a" : "div"}>`;
        }).join("") : '<div class="empty" style="grid-column:1/-1">Nothing new to recommend yet.</div>'}</div>
        ${(r.buy_again || []).length ? `<div class="hint" style="margin-top:8px">Buy again: ${r.buy_again.map((b) => `${esc(b.name)} ${starTxt(b.stars)}`).join(", ")}</div>` : ""}`)}
    </div>
    ${this.subReco ? html`<div class="card" style="margin-bottom:16px"><div class="spread"><h3>Subscriptions you might like</h3><a class="link" data-go="subscriptions">Subscriptions</a></div>
      ${raw((this.subReco.items || []).length ? `<div class="recos">${this.subReco.items.map((it) => `<div class="reco"><b>${esc(it.service.name)}</b>${it.why && it.why.length ? `<div class="hint">${esc(it.why.join(", "))}</div>` : ""}${it.people ? `<div class="hint">${esc(it.people)}</div>` : ""}</div>`).join("")}</div>` : '<div class="hint">Nothing to suggest yet.</div>')}</div>` : ""}
    <div class="grid">
      <div class="card"><div class="spread"><h3>Coffee library</h3><div class="row seg"><button class="${UI.ctab == 0 ? "on" : ""}" data-ctab="0">Published ${this.CC.length || ""}</button><button class="${UI.ctab == 1 ? "on" : ""}" data-ctab="1">Waiting for review</button></div></div>
        ${UI.ctab == 0 ? html`<input id="cq" type="search" placeholder="Search the library" value="${UI.cq}" style="width:100%;margin:8px 0">
          ${raw(!this.CC.length ? '<div class="empty">No coffees published yet.</div>' : !chit.length ? '<div class="empty">No shared coffee matches that.</div>' : chit.slice(0, 40).map((c) => `<div class="it"><span class="what"><b>${esc(c.name)}</b> <span class="hint">${esc([c.roaster, c.origin, c.process].filter((x) => x).join(", "))}</span></span>${have.has(c.id) ? '<span class="hint">on your shelf</span>' : `<button data-adopt="${esc(c.id)}">Add to my coffees</button>`}</div>`).join("") + (chit.length > 40 ? `<div class="hint">and ${chit.length - 40} more: search to narrow it down.</div>` : ""))}` : raw(this.pendingHtml("coffee"))}
      </div>
      <div class="card"><div class="spread"><h3>Recipe book</h3><div class="row seg"><button class="${UI.rtab == 0 ? "on" : ""}" data-rtab="0">Published ${this.LR.length || ""}</button><button class="${UI.rtab == 1 ? "on" : ""}" data-rtab="1">Waiting for review</button></div></div>
        ${UI.rtab == 0 ? html`<input id="rq" type="search" placeholder="Search recipes" value="${UI.rq}" style="width:100%;margin:8px 0">
          ${raw(!this.LR.length ? '<div class="empty">No recipes published yet. Share one of yours from the Recipes tab.</div>' : !rhit.length ? '<div class="empty">No recipe matches that.</div>' : rhit.map((x) => { const rr = x.recipe || {}; return `<div class="it"><span class="what"><b>${esc(rr.name || "Recipe")}</b> <span class="hint">${esc(recipeLine(rr))}</span>${x.contributors > 1 ? `<span class="hint"> · ${x.contributors} owners</span>` : ""}</span><button data-book="${this.LR.indexOf(x)}">Make it a recipe</button></div>`; }).join(""))}` : raw(this.pendingHtml("recipe"))}
      </div>
    </div>`;
  }

  pendingHtml(type) {
    const LP = this.LP, idx = (LP && LP.index && LP.index.items) || [], mine = new Map(((LP && LP.mine) || []).map((m) => [m.target, m.approve]));
    const why = !LP ? "The controller cannot be reached." : !LP.linked ? "Link the shared library in Settings to see what is waiting." : LP.can_review ? (type == "coffee" ? "Check the details against the bag, then approve or decline." : "Pull it once if you can, then approve or decline.") : "You can review once something you shared has been published.";
    const list = idx.filter((x) => x.type == type);
    return `<div class="hint" style="margin:8px 0">${why}</div>` + (!list.length ? '<div class="empty">Nothing waiting for review.</div>' : list.map((x) => {
      const own = LP.kid && x.from == LP.kid, done = mine.has(x.id), c = x.coffee || {}, r = x.recipe || {};
      const title = type == "coffee" ? esc(c.name || "Coffee") : esc(r.name || "Recipe"), line = type == "coffee" ? esc([c.roaster, c.origin, c.region, c.process, c.roast_level].filter(Boolean).join(" · ")) : esc(recipeLine(r));
      const act = own ? '<span class="pill">yours, waiting</span>' : done ? `<span class="pill ${mine.get(x.id) ? "ok" : ""}">you ${mine.get(x.id) ? "approved" : "declined"}</span>` : LP.can_review ? `<button class="primary" data-approve="${esc(x.id)}">Approve</button><button data-decline="${esc(x.id)}">Decline…</button>` : "";
      return `<div class="it"><span class="what"><b>${title}</b><span class="hint" style="display:block">${line}</span>${type == "coffee" && c.notes ? `<span class="hint" style="display:block">${esc(c.notes)}</span>` : ""}<span class="hint">${x.approvals || 0} of 2 approvals${x.declines ? ", " + x.declines + " declined" : ""}</span></span>${act}
        <div class="row dn" data-dn="${esc(x.id)}" style="display:none;flex-basis:100%"><input maxlength="119" placeholder="Why? The sender sees this." style="flex:1"><button data-declinesend="${esc(x.id)}">Send</button></div></div>`;
    }).join(""));
  }

  wire() {
    this.$$("[data-drinker]").forEach((b) => b.onclick = () => { UI.drinker = +b.dataset.drinker; this.refresh(); });
    this.$$("[data-go]").forEach((a) => a.onclick = () => this.panel.navigate(a.dataset.go));
    this.$$("[data-coffee]").forEach((a) => a.onclick = () => this.panel.navigate("coffees", a.dataset.coffee));
    this.$$("[data-ctab]").forEach((b) => b.onclick = () => { UI.ctab = +b.dataset.ctab; this._paint(); });
    this.$$("[data-rtab]").forEach((b) => b.onclick = () => { UI.rtab = +b.dataset.rtab; this._paint(); });
    const cq = this.$("#cq"); if (cq) cq.oninput = () => { UI.cq = cq.value; clearTimeout(this._t); this._t = setTimeout(() => { this._paint(); const n = this.$("#cq"); n.focus(); n.setSelectionRange(n.value.length, n.value.length); }, 250); };
    const rq = this.$("#rq"); if (rq) rq.oninput = () => { UI.rq = rq.value; clearTimeout(this._t); this._t = setTimeout(() => { this._paint(); const n = this.$("#rq"); n.focus(); n.setSelectionRange(n.value.length, n.value.length); }, 250); };
    this.$$("[data-adopt]").forEach((b) => b.onclick = async () => {
      const c = this.CC.find((x) => x.id == b.dataset.adopt);
      if (!c) return;
      const r = await this.post("/api/coffee/adopt", { libraryId: c.id, name: c.name, roaster: c.roaster || "", origin: c.origin || "", region: c.region || "", producer: c.producer || "", varietal: c.varietal || "", process: c.process || "", notes: c.notes || "", roastLevel: Math.max(0, RL.indexOf(c.roast_level || "")), altitudeM: c.altitude_m || 0 }, "Added to your shelf");
      if (r.ok) this.store.invalidate("library");
    });
    this.$$("[data-book]").forEach((b) => b.onclick = () => {
      const r = (this.LR[+b.dataset.book] || {}).recipe;
      if (!r) return;
      this.store.prefill = { name: (r.name || "").slice(0, 23), doseG: r.dose_g, useRatio: false, targetWeightG: r.yield_g, ...(r.target_time_s ? { targetTimeS: r.target_time_s } : {}), ...(r.preinfusion_hold_s ? { preinfusion: 1, preinfusionHoldS: r.preinfusion_hold_s } : {}), ...(r.bloom_pause_s ? { bloomPauseS: r.bloom_pause_s } : {}) };
      this.panel.navigate("recipes");
    });
    const review = async (id, approve, note) => {
      const r = await this.post("/api/library/review", { target: id, verdict: approve ? "approve" : "decline", note: note || "" }, approve ? "Approved: sent with your key" : "Declined: sent with your note");
      if (r.ok) { this.LP.mine = (this.LP.mine || []).concat([{ target: id, approve: !!approve }]); this._paint(); }
    };
    this.$$("[data-approve]").forEach((b) => b.onclick = () => review(b.dataset.approve, true));
    this.$$("[data-decline]").forEach((b) => b.onclick = () => { const d = this.shadowRoot.querySelector(`[data-dn="${CSS.escape(b.dataset.decline)}"]`); d.style.display = d.style.display ? "" : "none"; if (!d.style.display) d.querySelector("input").focus(); });
    this.$$("[data-declinesend]").forEach((b) => b.onclick = () => { const note = b.parentElement.querySelector("input").value.trim(); if (!note) { this.panel.toast("Say why it is declined", false); return; } review(b.dataset.declinesend, false, note); });
  }
}

customElements.define("brew-discover", BrewDiscover);
