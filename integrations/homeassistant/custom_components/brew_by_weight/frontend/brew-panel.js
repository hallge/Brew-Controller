// The Brew panel shell: toolbar, tabs, routing, live status, toast (see CONTRACT.md).
// A summary app: Summary first, then read-only drill-ins. Setup is in the controller's own page.
// Home Assistant creates <brew-panel> and sets `hass`, `narrow`, `route` and `panel` on it.
import { BrewApi, firstEntry } from "./lib/api.js";
import { BrewStore } from "./lib/section.js";
import { sharedStyles } from "./lib/styles.js";
import { stateWords, esc } from "./lib/format.js";

const TABS = [
  { id: "summary", title: "Summary", icon: "mdi:view-dashboard" },
  { id: "shots", title: "Shots", icon: "mdi:coffee" },
  { id: "insights", title: "Insights", icon: "mdi:chart-scatter-plot", optional: true },
  // `file`: the module in sections/ when it is not named after the tab (0.4.0 read-only tabs)
  { id: "coffees", title: "Coffees", icon: "mdi:sack", file: "shelf" },
  { id: "subscriptions", title: "Subscriptions", icon: "mdi:truck-delivery", optional: true, file: "deliveries" },
];
// Older links and bookmarks (0.3.x tabs) land on the Summary.
const OLD_TABS = new Set(["home", "recipes", "grinder", "machine", "discover", "community", "settings", "logs"]);
const USER_DATA_KEY = "brew_by_weight_hidden_tabs";
const CONTROLLER_PANEL = "/local_brew_controller";

class BrewPanel extends HTMLElement {
  constructor() {
    super();
    this.attachShadow({ mode: "open" });
    this._hidden = new Set();
    this._tab = null;
    this._section = null;
    this._lastShotId = null;
    this._status = null;
    this._onHash = () => this._route();
  }

  set hass(hass) {
    const first = !this._hass;
    this._hass = hass;
    if (this.api) this.api.hass = hass;
    if (this._section) this._section.hass = hass;
    if (this._menu) this._menu.hass = hass;
    if (first) this._start();
  }
  get hass() { return this._hass; }
  set narrow(v) { this._narrow = v; if (this._menu) this._menu.narrow = v; this.toggleAttribute("narrow", !!v); }
  get narrow() { return this._narrow; }
  set route(_v) {}
  set panel(_v) {}

  connectedCallback() {
    window.addEventListener("hashchange", this._onHash);
    if (this.api && !this._unsub) this._subscribe();
  }
  disconnectedCallback() {
    window.removeEventListener("hashchange", this._onHash);
    this._unsubscribe();
  }

  async _start() {
    this._paintShell();
    try {
      const entry = await firstEntry(this._hass);
      if (!entry) { this._content.innerHTML = `<div class="card">No Brew by Weight controller is set up in Home Assistant.</div>`; return; }
      this.entry = entry;
      this.api = new BrewApi(this._hass, entry.entry_id);
      this.store = new BrewStore(this.api);
      await this._loadHidden();
      this._paintTabs();
      this._subscribe();
      this._route();
    } catch (e) {
      this._content.innerHTML = `<div class="card err">Could not reach the integration: ${esc(e.message || e)}</div>`;
    }
  }

  _paintShell() {
    this.shadowRoot.innerHTML = `<style>${sharedStyles}${SHELL_CSS}</style>
      <div class="toolbar">
        <span class="menu"></span>
        <div class="title">Brew Controller</div>
        <span class="pill" id="state" title="Machine state"></span>
        <button class="link ctl" id="tabsbtn" title="Choose the tabs shown to you" aria-label="Tabs shown"><ha-icon icon="mdi:tune-variant"></ha-icon></button>
        <button class="link ctl" id="ctl" title="Brewing, setup, pairing and firmware are in the controller's own page">Open the controller</button>
      </div>
      <nav id="tabs"></nav>
      <main id="content"><div class="empty">Loading…</div></main>
      <div id="toast"></div>`;
    this._content = this.shadowRoot.getElementById("content");
    // Home Assistant's own sidebar button, shown when the sidebar is folded (phone, narrow window).
    const menu = document.createElement("ha-menu-button");
    menu.hass = this._hass;
    menu.narrow = this._narrow;
    this.shadowRoot.querySelector(".menu").appendChild(menu);
    this._menu = menu;
    this.shadowRoot.getElementById("ctl").onclick = () => this.openController();
    this.shadowRoot.getElementById("tabsbtn").onclick = () => this._tabsMenu();
  }

  // The optional tabs, per Home Assistant user (the 0.3 Settings tab did this).
  _tabsMenu() {
    const old = this.shadowRoot.getElementById("tabsmenu");
    if (old) { old.remove(); return; }
    const m = document.createElement("div");
    m.id = "tabsmenu";
    m.innerHTML = `<div class="hint">Tabs shown to you</div>` + this.optionalTabs.map((t) =>
      `<label class="c"><input type="checkbox" data-id="${t.id}" ${this._hidden.has(t.id) ? "" : "checked"}> ${esc(t.title)}</label>`).join("");
    m.querySelectorAll("input").forEach((i) => i.onchange = () =>
      this.setHiddenTabs([...m.querySelectorAll("input")].filter((x) => !x.checked).map((x) => x.dataset.id)));
    this.shadowRoot.querySelector(".toolbar").appendChild(m);
  }

  _visibleTabs() { return TABS.filter((t) => !(t.optional && this._hidden.has(t.id))); }

  _paintTabs() {
    const nav = this.shadowRoot.getElementById("tabs");
    nav.innerHTML = this._visibleTabs().map((t) => `<a href="#${t.id}" data-id="${t.id}"><ha-icon icon="${t.icon}"></ha-icon><span>${t.title}</span></a>`).join("");
    this._markTab();
  }
  _markTab() {
    this.shadowRoot.querySelectorAll("#tabs a").forEach((a) => a.classList.toggle("on", a.dataset.id === this._tab));
    const on = this.shadowRoot.querySelector("#tabs a.on");
    if (on && on.scrollIntoView) on.scrollIntoView({ block: "nearest", inline: "nearest" });
  }

  async _route() {
    if (!this.api) return;
    const h = (location.hash || "#summary").slice(1);
    let [id, ...rest] = h.split("/");
    if (OLD_TABS.has(id)) { id = "summary"; rest = []; }
    const arg = rest.join("/") || null;
    const tab = this._visibleTabs().find((t) => t.id === id) ? id : "summary";
    if (tab === this._tab && arg === this._arg && this._section) { return; }
    this._tab = tab;
    this._arg = arg;
    this._markTab();
    const file = TABS.find((t) => t.id === tab).file || tab;
    let el;
    try {
      await import(new URL(`./sections/${file}.js`, import.meta.url).href);
      el = document.createElement(`brew-${file}`);
    } catch (e) {
      el = document.createElement("div");
      el.className = "card";
      el.textContent = `This tab is not available yet (${tab}).`;
      console.warn("brew panel:", tab, e);
    }
    if (this._tab !== tab) return; // the user moved on while it loaded
    Object.assign(el, { hass: this._hass, api: this.api, store: this.store, panel: this, arg });
    this._content.innerHTML = "";
    this._content.appendChild(el);
    this._section = el;
    if (this._status && el.onStatus) el.onStatus(this._status);
  }

  // --- live status ----------------------------------------------------------------------------
  async _subscribe() {
    if (this._unsub || !this.api) return;
    try {
      this._unsub = await this.api.subscribeStatus((status, error) => this._onStatus(status, error));
    } catch (e) {
      this._unsub = null;
      this._setStatePill(null, "No live status");
    }
  }
  _unsubscribe() {
    if (this._unsub) { try { this._unsub(); } catch (e) { /* closed */ } }
    this._unsub = null;
  }
  _onStatus(status, error) {
    if (!status) { this._setStatePill(null, error ? "Controller not reachable" : ""); return; }
    this._status = status;
    this._setStatePill(status.state);
    if (this._lastShotId !== null && status.last_shot_id !== this._lastShotId) {
      this.store.invalidate("shots", "library", "grind", "maintenance", "start");
    }
    this._lastShotId = status.last_shot_id;
    if (this._section && this._section.onStatus) this._section.onStatus(status);
  }
  _setStatePill(state, text) {
    const p = this.shadowRoot.getElementById("state");
    if (!p) return;
    p.textContent = state ? stateWords(state) : (text || "");
    p.className = "pill " + (!state ? "bad" : /READY|SHOT_COMPLETE/.test(state) ? "ok" : /FAILED|ERROR|DISCONNECTED|STALE|TIMEOUT|INVALID/.test(state) ? "bad" : "");
    p.style.display = p.textContent ? "" : "none";
  }

  // --- tabs the user hides (per Home Assistant user) -------------------------------------------
  async _loadHidden() {
    try {
      const r = await this._hass.callWS({ type: "frontend/get_user_data", key: USER_DATA_KEY });
      this._hidden = new Set((r && r.value) || []);
    } catch (e) {
      try { this._hidden = new Set(JSON.parse(localStorage.getItem(USER_DATA_KEY) || "[]")); } catch (e2) { this._hidden = new Set(); }
    }
  }
  async setHiddenTabs(ids) {
    this._hidden = new Set(ids);
    try { await this._hass.callWS({ type: "frontend/set_user_data", key: USER_DATA_KEY, value: [...this._hidden] }); }
    catch (e) { try { localStorage.setItem(USER_DATA_KEY, JSON.stringify([...this._hidden])); } catch (e2) { /* ignore */ } }
    this._paintTabs();
  }
  get hiddenTabs() { return [...this._hidden]; }
  get optionalTabs() { return TABS.filter((t) => t.optional); }

  // --- for sections ------------------------------------------------------------------------------
  navigate(tab, arg) { location.hash = "#" + tab + (arg != null ? "/" + arg : ""); }
  openController() {
    history.pushState(null, "", CONTROLLER_PANEL);
    window.dispatchEvent(new CustomEvent("location-changed", { detail: { replace: false } }));
  }
  get status() { return this._status; }
  toast(text, ok) {
    if (!text) return;
    const t = this.shadowRoot.getElementById("toast");
    t.textContent = text;
    t.className = ok === true ? "ok on" : ok === false ? "bad on" : "on";
    clearTimeout(this._toastT);
    this._toastT = setTimeout(() => { t.className = ""; }, ok === false ? 6000 : 3500);
  }
}

const SHELL_CSS = `
:host { display: flex; flex-direction: column; min-height: 100vh; background: var(--primary-background-color); }
.toolbar { display: flex; align-items: center; gap: 12px; height: var(--header-height, 56px); padding: 0 12px 0 4px;
  background: var(--app-header-background-color, var(--primary-background-color)); color: var(--app-header-text-color, var(--primary-text-color));
  border-bottom: var(--app-header-border-bottom, 1px solid var(--divider-color)); position: sticky; top: 0; z-index: 3; }
.toolbar .title { font-size: 20px; flex: 1; margin-left: 12px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
:host([narrow]) .toolbar .title { margin-left: 0; }
.ctl { font-size: 14px; white-space: nowrap; }
nav { display: flex; gap: 4px; overflow-x: auto; padding: 0 8px; position: sticky; top: var(--header-height, 56px); z-index: 2;
  background: var(--app-header-background-color, var(--primary-background-color)); border-bottom: 1px solid var(--divider-color); scrollbar-width: none; }
nav::-webkit-scrollbar { display: none; }
nav a { display: inline-flex; align-items: center; gap: 6px; padding: 10px 12px; text-decoration: none; color: var(--secondary-text-color);
  border-bottom: 2px solid transparent; white-space: nowrap; font-size: 14px; }
nav a.on { color: var(--primary-color); border-bottom-color: var(--primary-color); }
nav ha-icon { --mdc-icon-size: 18px; }
main { padding: 16px; max-width: 1400px; width: 100%; margin: 0 auto; flex: 1; }
:host([narrow]) main { padding: 8px; }
:host([narrow]) nav a span { display: none; }
:host([narrow]) nav a.on span { display: inline; }
#tabsmenu { position: absolute; right: 12px; top: calc(var(--header-height, 56px) - 4px); z-index: 5; display: flex; flex-direction: column; gap: 8px;
  background: var(--card-background-color, #fff); color: var(--primary-text-color); border: 1px solid var(--divider-color); border-radius: 12px;
  padding: 12px 16px; box-shadow: 0 4px 16px rgba(0,0,0,.25); font-size: 14px; }
#tabsbtn ha-icon { --mdc-icon-size: 20px; }
#toast { position: fixed; left: 50%; bottom: 24px; transform: translateX(-50%) translateY(120px); transition: transform .2s;
  background: var(--primary-text-color); color: var(--primary-background-color); padding: 10px 16px; border-radius: 8px; z-index: 10; max-width: 90vw; }
#toast.on { transform: translateX(-50%) translateY(0); }
#toast.ok { background: var(--success-color, #43a047); color: #fff; }
#toast.bad { background: var(--error-color, #db4437); color: #fff; }
`;

customElements.define("brew-panel", BrewPanel);
