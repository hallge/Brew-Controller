// Subscriptions tab (0.4.0): coffee and supplies that come by post, read-only. What comes next, what
// it costs, how many filters are left, and a Manage link to the shop. The everyday actions only:
// Arrived and Skip. Adding, editing, sharing and reviewing services is done in the controller's
// own page; updates from the shops come through the Brew Controller helper there.
import { BrewSection } from "../lib/section.js";
import { html, raw } from "../lib/styles.js";
import { esc, money } from "../lib/format.js";
import { confirmDialog } from "../lib/forms.js";

export const meta = { id: "subscriptions", title: "Subscriptions", icon: "mdi:truck-delivery", optional: true };

const KIND = { coffee: "Coffee", supplies: "Supplies", other: "Subscription" };
const fd = (n) => (n && n > 20000101 ? new Date(Math.floor(n / 10000), Math.floor(n / 100) % 100 - 1, n % 100).toLocaleDateString([], { weekday: "short", month: "short", day: "numeric" }) : "");
const every = (d) => (!d ? "" : d % 7 == 0 ? (d == 7 ? "every week" : "every " + d / 7 + " weeks") : d >= 28 && d <= 31 ? "every month" : "every " + d + " days");
const until = (n) => (n == null ? "" : n < 0 ? -n + (n == -1 ? " day" : " days") + " overdue" : n == 0 ? "today" : n == 1 ? "tomorrow" : "in " + n + " days");
const https = (u) => (/^https:\/\//i.test(u || "") ? u : "");

class BrewDeliveries extends BrewSection {
  static get styles() {
    return `.subs{display:grid;grid-template-columns:repeat(auto-fill,minmax(280px,1fr));gap:12px}
    .sub{display:flex;flex-direction:column;gap:6px}
    .sub.off{opacity:.65}
    .sub .top{display:flex;gap:10px;align-items:center}
    .sub .ini{width:36px;height:36px;border-radius:8px;display:flex;align-items:center;justify-content:center;color:#fff;font-weight:600;flex:none}
    .sub .top img{width:36px;height:36px;border-radius:8px;object-fit:cover;flex:none}
    .meter{height:6px;border-radius:3px;background:var(--secondary-background-color);overflow:hidden}
    .meter i{display:block;height:100%;background:var(--success-color,#43a047)} .meter.warn i{background:var(--warning-color,#ffa600)}
    .acts{display:flex;gap:6px;flex-wrap:wrap;margin-top:4px} .acts a{text-decoration:none}`;
  }

  async load() { this.S = await this.store.get("subs"); }

  render() {
    const S = this.S, all = S.subscriptions || [];
    const active = all.filter((s) => (s.status || "active") == "active").sort((a, b) => (a.days_until ?? 1e9) - (b.days_until ?? 1e9));
    const rest = all.filter((s) => (s.status || "active") != "active");
    return html`
      <div class="tiles" style="margin-bottom:16px">
        <div class="tile"><div class="k">Each month, about</div><div class="v">${S.monthly_cents ? money(S.monthly_cents) : "--"}</div></div>
        <div class="tile"><div class="k">Active</div><div class="v">${active.length}</div></div>
        <div class="tile"><div class="k">Shots a day lately</div><div class="v">${S.shots_per_day != null ? (+S.shots_per_day).toFixed(1) : "--"}</div></div>
      </div>
      <div class="spread" style="margin-bottom:8px"><h2>Subscriptions</h2><button class="link" id="ctl">Add, edit or update from the shops in the controller</button></div>
      ${all.length ? raw(`<div class="subs">${[...active, ...rest].map((s) => this.cardHtml(s)).join("")}</div>`)
        : html`<div class="card empty">No subscriptions yet. Coffee and supplies that come by post are added in the controller.</div>`}`;
  }

  cardHtml(s) {
    const off = s.status && s.status != "active", name = s.name || s.title || s.site || "Subscription";
    const col = /^#[0-9a-f]{6}$/i.test(s.color || "") ? s.color : "#5b6470";
    const logo = https(s.icon) || https(s.image);
    const bits = [KIND[s.kind] || "", every(s.every_days), s.price_cents ? money(s.price_cents) : ""].filter((x) => x).join(" · ");
    let sup = "";
    if (s.kind == "supplies" && s.stock != null) {
      const full = Math.max(s.per_delivery || 0, s.stock || 0, 1), pct = Math.min(100, Math.round(100 * (s.stock || 0) / full));
      sup = `<div class="hint">${s.stock} ${esc(s.item || "left")}${s.stock_days_left != null ? ", about " + s.stock_days_left + " days" : ""}${s.stock_short ? ": runs out before the next delivery" : ""}</div>
        <div class="meter ${s.stock_short ? "warn" : ""}"><i style="width:${pct}%"></i></div>`;
    }
    const manage = https(s.manage_url) || https(s.url);
    return `<div class="card sub ${off ? "off" : ""}">
      <div class="top">${logo ? `<img src="${esc(logo)}" alt="" loading="lazy" referrerpolicy="no-referrer" onerror="this.remove()">` : `<span class="ini" style="background:${col}">${esc(name.charAt(0).toUpperCase())}</span>`}
        <div style="min-width:0;flex:1"><b>${esc(name)}</b><div class="hint">${esc(bits)}</div></div>
        ${off ? `<span class="pill">${s.status == "paused" ? "Paused" : "Cancelled"}</span>` : s.days_until != null && s.days_until < 0 ? '<span class="pill bad">Overdue</span>' : s.due_soon ? '<span class="pill warn">Soon</span>' : ""}</div>
      ${s.next_date && !off ? `<div>Next: <b>${esc(fd(s.next_date))}</b> <span class="hint">${esc(until(s.days_until))}</span></div>` : ""}
      ${s.last_arrived ? `<div class="hint">Last arrived ${esc(fd(s.last_arrived))}</div>` : ""}
      ${sup}
      ${s.note ? `<div class="hint">${esc(s.note)}</div>` : ""}
      <div class="acts">${!off && s.next_date ? `<button data-arr="${s.id}">Arrived</button><button data-skip="${s.id}">Skip one</button>` : ""}
        ${manage ? `<a class="btn" href="${esc(manage)}" target="_blank" rel="noopener noreferrer">Manage</a>` : ""}</div></div>`;
  }

  wire() {
    this.$("#ctl").onclick = () => this.panel.openController();
    this.$$("[data-arr]").forEach((b) => b.onclick = async () => {
      const s = (this.S.subscriptions || []).find((x) => x.id == b.dataset.arr);
      const r = await this.post("/api/subscriptions/arrived", { id: +b.dataset.arr },
        s && s.kind == "coffee" ? "Arrived: the next date moved on. Put the bag on the shelf in the controller." : "Arrived: the next date moved on");
      if (r.ok) this.store.invalidate("subs", "library");
    });
    this.$$("[data-skip]").forEach((b) => b.onclick = async () => {
      if (!(await confirmDialog(this.shadowRoot, "Skip the next delivery here? This only moves the date on in the controller: skip it at the shop too.", { yes: "Skip one" }))) return;
      const r = await this.post("/api/subscriptions/skip", { id: +b.dataset.skip }, "Skipped one: the next date moved on");
      if (r.ok) this.store.invalidate("subs");
    });
  }
}

customElements.define("brew-deliveries", BrewDeliveries);
