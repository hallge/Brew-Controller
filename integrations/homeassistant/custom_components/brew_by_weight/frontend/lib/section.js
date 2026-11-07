// Base class for the panel's tabs, and the shared cache of controller data (see ../CONTRACT.md).
import { sharedStyles } from "./styles.js";

const LOADERS = {
  settings: (api) => api.get("/api/settings"),
  library: (api) => api.get("/api/library"),
  grind: (api) => api.get("/api/grind"),
  shots: (api) => api.get("/api/shots", { limit: 500 }),
  maintenance: (api) => api.get("/api/maintenance"),
  ratings: (api) => api.get("/api/coffee/ratings"),
  share: (api) => api.get("/api/share"),
  subs: (api) => api.get("/api/subscriptions"),
  start: (api) => api.get("/api/start"),
  power: (api) => api.get("/api/power"),
  machine: (api) => api.get("/api/machine"),
};

export class BrewStore {
  constructor(api) {
    this.api = api;
    this.cache = new Map();
    this.listeners = new Set();
  }
  /** Load once and cache. Concurrent callers share one request. */
  get(key) {
    if (!this.cache.has(key)) {
      const loader = LOADERS[key];
      if (!loader) return Promise.reject(new Error(`unknown store key ${key}`));
      const p = loader(this.api).catch((e) => { this.cache.delete(key); throw e; });
      this.cache.set(key, p);
    }
    return this.cache.get(key);
  }
  /** Forget cached data after a write; listeners (open tabs) refresh. */
  invalidate(...keys) {
    keys.forEach((k) => this.cache.delete(k));
    this.listeners.forEach((fn) => fn(keys));
  }
  onInvalidate(fn) { this.listeners.add(fn); return () => this.listeners.delete(fn); }
}

export class BrewSection extends HTMLElement {
  constructor() {
    super();
    this.attachShadow({ mode: "open" });
    this._loaded = false;
  }

  /** Extra CSS for this tab (override). */
  static get styles() { return ""; }

  connectedCallback() {
    this._unsub = this.store.onInvalidate(() => this.isConnected && this.refresh());
    this.refresh();
  }
  disconnectedCallback() { if (this._unsub) this._unsub(); }

  async refresh() {
    try {
      await this.load();
      this._error = null;
    } catch (e) {
      this._error = e && e.message ? e.message : String(e);
    }
    this._loaded = true;
    this._paint();
  }

  _paint() {
    const css = `<style>${sharedStyles}${this.constructor.styles}</style>`;
    if (this._error) {
      this.shadowRoot.innerHTML = `${css}<div class="card err">Could not load: ${this._error.replace(/[<>&]/g, "")}</div>`;
      return;
    }
    this.shadowRoot.innerHTML = css + this.render();
    this.wire();
  }

  /** Fetch what the tab needs (override). */
  async load() {}
  /** Return the tab's HTML (override). Use html`` from styles.js to escape. */
  render() { return ""; }
  /** Attach event handlers after render (override). */
  wire() {}
  /** Live status while the tab is shown (override where useful). */
  onStatus(_status) {}

  $(sel) { return this.shadowRoot.querySelector(sel); }
  $$(sel) { return [...this.shadowRoot.querySelectorAll(sel)]; }

  /** POST and tell the user how it went. Returns the controller's answer. */
  async post(path, body, okText, query) {
    const r = await this.api.post(path, body, query);
    if (okText !== null) this.panel.toast(r.ok ? (okText || "Saved") : (r.error || "Not saved"), !!r.ok);
    return r;
  }
}
