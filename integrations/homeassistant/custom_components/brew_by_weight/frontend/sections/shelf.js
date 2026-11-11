// Coffees tab (0.4.0): the shelf, read-only. Each coffee on the controller's list with its bags (in
// use, unopened, on its way, in the freezer) and how it was rated; finished coffees below. The
// everyday actions only: bag finished, open a bag, a bag arrived. Adding or editing coffees and bags
// is done in the controller's own page.
import { BrewSection } from "../lib/section.js";
import { html, raw } from "../lib/styles.js";
import { age, ageNote, coffeeById, esc } from "../lib/format.js";
import { confirmDialog } from "../lib/forms.js";

export const meta = { id: "coffees", title: "Coffees", icon: "mdi:sack" };

const RL = ["", "light", "medium-light", "medium", "medium-dark", "dark"];
const fd = (n) => (n && n > 20000101 ? new Date(Math.floor(n / 10000), Math.floor(n / 100) % 100 - 1, n % 100).toLocaleDateString([], { month: "short", day: "numeric", year: "numeric" }) : "");
const frozenNow = (b) => !!(b && b.frozenDate && !b.thawDate);

class BrewShelf extends BrewSection {
  static get styles() {
    return `.coffee .top{display:flex;justify-content:space-between;gap:8px;align-items:flex-start}
    .coffee h3{margin:0}
    .bag{display:flex;justify-content:space-between;align-items:center;gap:8px;padding:6px 0;border-top:1px solid var(--divider-color);font-size:14px}
    .stars{color:var(--warning-color,#ffa600)}
    .now{border-color:var(--primary-color)}`;
  }

  async load() {
    const [L, R] = await Promise.all([this.store.get("library"), this.store.get("ratings").catch(() => null)]);
    Object.assign(this, { L, R });
  }

  render() {
    const L = this.L, coffees = L.coffees || [], bags = L.bags || [];
    const p = (L.presets || [])[L.activePreset] || {};
    const shelf = coffees.filter((c) => !c.finished), done = coffees.filter((c) => c.finished).sort((a, b) => b.finished - a.finished);
    // Bags of coffees that are not on the list (an unopened bag of a new coffee waits for it).
    return html`
      <div class="spread" style="margin-bottom:12px"><h2>On the shelf</h2>
        <button class="link" id="ctl">Add or edit coffees in the controller</button></div>
      ${shelf.length ? raw(`<div class="grid">${shelf.map((c) => this.coffeeHtml(c, bags.filter((b) => b.coffeeId == c.id), c.id == p.coffeeId)).join("")}</div>`)
        : html`<div class="card empty">No coffee on the shelf. Add one in the controller.</div>`}
      ${done.length ? raw(`<details style="margin-top:16px"><summary>Finished coffees (${done.length})</summary><div class="card">${done.map((c) =>
        `<div class="bag"><span>${esc(c.name)}${c.roaster ? ` <span class="hint">${esc(c.roaster)}</span>` : ""}</span><span class="hint">${this.ratedHtml(c)} finished ${esc(fd(c.finished))}</span></div>`).join("")}</div></details>`) : ""}`;
  }

  ratedHtml(c) {
    const rs = this.R && Array.isArray(this.R.ratings) ? this.R.ratings.filter((r) => r.coffee == c.id && r.stars) : [];
    if (!rs.length) return "";
    const avg = rs.reduce((a, r) => a + r.stars, 0) / rs.length;
    return `<span class="stars" title="${rs.length} rating${rs.length == 1 ? "" : "s"}">${"★".repeat(Math.round(avg))}</span>`;
  }

  coffeeHtml(c, bags, brewing) {
    const cur = bags.find((b) => b.id == c.bagId);
    const facts = [c.roaster, c.origin, c.region, c.process, RL[c.roastLevel || 0] ? RL[c.roastLevel] + " roast" : ""].filter((x) => x).join(" · ");
    const bagLine = (b) => {
      const st = b.onTheWay ? "On its way" + (b.expectedDate ? ", expected " + fd(b.expectedDate) : "") : b.unopened ? "Unopened" : b.id == c.bagId ? "In use" : "Open";
      const ag = b.roastDate && !b.onTheWay ? ", " + age(b.roastDate) + " days off roast" : "";
      const btn = b.onTheWay ? `<button data-arrived="${b.id}">It arrived</button>` : b.unopened ? `<button data-open="${b.id}">Open</button>` : "";
      return `<div class="bag"><span>${esc(st + ag)}${b.weightG ? ` <span class="hint">${b.weightG} g</span>` : ""}${frozenNow(b) ? ' <span class="pill">❄ frozen</span>' : ""}</span>${btn}</div>`;
    };
    return `<div class="card coffee ${brewing ? "now" : ""}">
      <div class="top"><div><h3>${esc(c.name)} ${this.ratedHtml(c)}</h3><div class="hint">${esc(facts)}</div></div>
        ${brewing ? '<span class="pill ok">brewing</span>' : ""}</div>
      <div class="hint" style="margin:6px 0">${esc(ageNote(cur ? { ...c, ...cur } : c) || "No roast date entered")}.</div>
      ${bags.filter((b) => b.id == c.bagId || b.unopened || b.onTheWay).map(bagLine).join("")}
      ${c.bagId ? `<div class="row" style="margin-top:8px"><button data-finish="${c.id}">Bag finished</button></div>` : ""}
      ${c.notes ? `<div class="hint" style="margin-top:6px">${esc(c.notes)}</div>` : ""}</div>`;
  }

  wire() {
    this.$("#ctl").onclick = () => this.panel.openController();
    const after = () => this.store.invalidate("library", "grind");
    this.$$("[data-finish]").forEach((b) => b.onclick = async () => {
      const c = coffeeById(this.L, +b.dataset.finish);
      if (!(await confirmDialog(this.shadowRoot, `Is the bag of ${c ? c.name : "this coffee"} finished? The controller takes it off its list until a new bag comes.`, { yes: "Bag finished" }))) return;
      const r = await this.post("/api/library/do", null, "Bag finished", { what: "coffee_finish", a: +b.dataset.finish });
      if (r.ok) after();
    });
    this.$$("[data-open]").forEach((b) => b.onclick = async () => {
      const r = await this.post("/api/library/do", null, "Bag opened: in use from today", { what: "bag_open", a: +b.dataset.open });
      if (r.ok) after();
    });
    this.$$("[data-arrived]").forEach((b) => b.onclick = async () => {
      const r = await this.post("/api/library/do", null, "On the shelf, unopened. Add its roast date in the controller.", { what: "bag_arrived", a: +b.dataset.arrived });
      if (r.ok) after();
    });
  }
}

customElements.define("brew-shelf", BrewShelf);
