// Talks to the controller through the integration's websocket commands (see ../CONTRACT.md).
// Every answer is plain data; a failed write resolves to {ok:false, error} and never throws,
// like the controller page's post() (WebPage.h:661-664).

const UNREACHABLE = { ok: false, error: "The controller could not be reached" };

export class BrewApi {
  constructor(hass, entryId) {
    this.hass = hass;
    this.entryId = entryId;
  }

  async request(method, path, { query, body } = {}) {
    const msg = { type: "brew_by_weight/request", entry_id: this.entryId, method, path };
    if (query) msg.query = clean(query);
    if (body !== undefined) msg.body = body;
    try {
      return await this.hass.callWS(msg);
    } catch (e) {
      return { status: 502, json: { ...UNREACHABLE, detail: String(e && (e.message || e.code) || e) } };
    }
  }

  /** GET JSON. Throws on an error answer (reads have nothing sensible to fall back to). */
  async get(path, query) {
    const r = await this.request("GET", path, { query });
    if (r.status >= 400 || r.json === undefined) {
      const err = new Error((r.json && r.json.error) || `${path} answered ${r.status}`);
      err.status = r.status;
      throw err;
    }
    return r.json;
  }

  /** GET JSON, or `fallback` on any error (for optional cards). */
  async getOr(path, query, fallback = null) {
    try { return await this.get(path, query); } catch (e) { return fallback; }
  }

  /** GET text (logs). Returns {text, headers}. */
  async text(path, query) {
    const r = await this.request("GET", path, { query });
    if (r.status >= 400) throw new Error((r.json && r.json.error) || `${path} answered ${r.status}`);
    return { text: r.text != null ? r.text : JSON.stringify(r.json), headers: r.headers || {} };
  }

  /**
   * POST. `query` goes in the address (most library actions), `body` as JSON.
   * Resolves to the controller's answer; {ok:false,error} on any failure.
   */
  async post(path, body, query) {
    const r = await this.request("POST", path, { query, body });
    const j = r.json;
    if (r.status >= 400) return j && typeof j === "object" ? { ok: false, ...j } : { ok: false, error: `Answered ${r.status}` };
    if (j && typeof j === "object" && !Array.isArray(j)) return "ok" in j ? j : { ok: true, ...j };
    return { ok: true, value: j };
  }

  /** Live /api/status about once a second. Returns an unsubscribe function (a promise of one). */
  subscribeStatus(callback) {
    return this.hass.connection.subscribeMessage(
      (ev) => callback(ev && ev.status ? ev.status : null, ev && ev.error),
      { type: "brew_by_weight/subscribe_status", entry_id: this.entryId },
    );
  }

  /** Download a file through the integration's authenticated view and hand it to the browser. */
  async download(kind, params = {}, filename = "download") {
    const qs = new URLSearchParams({ kind, ...clean(params) }).toString();
    const res = await this.hass.fetchWithAuth(`/api/brew_by_weight/download/${this.entryId}?${qs}`);
    if (!res.ok) throw new Error(`Download failed (${res.status})`);
    const blob = await res.blob();
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = filename;
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 10000);
  }
}

function clean(obj) {
  const out = {};
  for (const [k, v] of Object.entries(obj || {})) if (v !== undefined && v !== null) out[k] = typeof v === "boolean" ? (v ? 1 : 0) : v;
  return out;
}

/** The first Brew by Weight entry (the panel serves one controller). */
export async function firstEntry(hass) {
  const entries = await hass.callWS({ type: "brew_by_weight/entries" });
  return entries && entries.length ? entries[0] : null;
}
