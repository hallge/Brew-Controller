// Logs: what is recorded (detail level, keep on flash), download, clear, and the live log with
// follow and filter (WebPage.h:639-649, 789-795).
import { BrewSection } from "../lib/section.js";
import { html, raw } from "../lib/styles.js";
import { esc } from "../lib/format.js";
import { confirmDialog } from "../lib/forms.js";

export const meta = { id: "logs", title: "Logs", icon: "mdi:text-box-search", optional: true };

const LEVELS = [[0, "Errors only"], [1, "Warnings and errors"], [2, "Normal: every event"], [3, "Debug: also once a second during a shot"]];
const UI = { text: "", next: 0, follow: true, filter: "" };

class BrewLogs extends BrewSection {
  static get styles() {
    return `pre{height:60vh;overflow:auto;margin:0;font:12px/1.45 ui-monospace,Consolas,monospace;white-space:pre-wrap;word-break:break-all;background:var(--secondary-background-color);padding:8px;border-radius:8px}
    .w{color:var(--warning-color,#ffa600)} .d{color:var(--secondary-text-color)}`;
  }

  async load() { this.cfg = await this.api.getOr("/api/log/config", null, null); }

  render() {
    const c = this.cfg;
    return html`
    <div class="card" style="margin-bottom:16px"><h3>What is recorded</h3>
      ${c ? html`<div class="form"><label class="f">Detail<select id="lvl">${raw(LEVELS.map(([v, l]) => `<option value="${v}" ${v == c.level ? "selected" : ""}>${l}</option>`).join(""))}</select></label>
        <label class="c"><input type="checkbox" id="keep" ${c.keep ? "checked" : ""}> Keep on flash, so it survives a restart</label></div>
        <div class="hint" style="margin-top:6px">${c.keep ? "On flash: " + Math.round(c.flash_bytes / 1024) + " kB of at most " + Math.round(c.flash_limit / 1024) + " kB (the oldest half is dropped when it is full)." : "Not kept on flash: only the last " + Math.round(c.memory_bytes / 1024) + " kB, in memory, lost at a restart."}</div>` : html`<div class="hint">Not available from the controller.</div>`}
      <div class="row" style="margin-top:8px"><button class="primary" id="save">Save</button><button id="dl">Download the whole log</button><button class="danger" id="clear" style="margin-left:auto">Clear stored log</button></div></div>
    <div class="card"><div class="spread"><h3>Live</h3><div class="row"><label class="c"><input type="checkbox" id="follow" ${UI.follow ? "checked" : ""}> Follow</label><input id="filter" placeholder="filter, e.g. scale or W" style="width:170px" value="${UI.filter}"><button id="cv">Clear view</button></div></div>
      <pre id="view"></pre></div>`;
  }

  draw() {
    const v = this.$("#view");
    if (!v) return;
    const f = UI.filter.toLowerCase();
    v.innerHTML = UI.text.split("\n").filter((l) => l && (!f || l.toLowerCase().includes(f))).map((l) => { const k = l.charAt(11); return `<span${k == "E" || k == "W" ? ' class="w"' : k == "D" ? ' class="d"' : ""}>${esc(l)}</span>`; }).join("\n");
    if (UI.follow) v.scrollTop = v.scrollHeight;
  }

  /** Reads what is new since the last read (X-Log-Next), up to 8 pieces at a time. */
  async poll() {
    if (this._busy || !this.isConnected) return;
    this._busy = true;
    try {
      for (let i = 0; i < 8; i++) {
        const { text, headers } = await this.api.text("/api/log", { from: UI.next });
        const n = +(headers["x-log-next"] || 0);
        if (n < UI.next) UI.text = "";
        UI.next = n;
        if (!text) break;
        UI.text = (UI.text + text).slice(-200000);
      }
      this.draw();
    } catch (e) { /* the controller may be busy; try again */ }
    this._busy = false;
  }

  wire() {
    this.$("#save").onclick = async () => { const r = await this.post("/api/log/config", { level: +this.$("#lvl").value, keep: this.$("#keep").checked }, "Saved"); if (r.ok) this.refresh(); };
    this.$("#dl").onclick = () => this.api.download("log", {}, "brew-controller-log.txt").catch((e) => this.panel.toast(e.message, false));
    this.$("#clear").onclick = async () => { if (!(await confirmDialog(this.shadowRoot, "Delete the stored log?"))) return; const r = await this.post("/api/log/clear", null, "Log cleared"); if (r.ok) { UI.text = ""; UI.next = 0; this.refresh(); } };
    this.$("#follow").onchange = (e) => { UI.follow = e.target.checked; };
    this.$("#filter").oninput = (e) => { UI.filter = e.target.value; this.draw(); };
    this.$("#cv").onclick = () => { UI.text = ""; this.draw(); };
    this.draw();
    clearInterval(this._timer);
    this._timer = setInterval(() => this.poll(), 2000);
    this.poll();
  }

  disconnectedCallback() { super.disconnectedCallback(); clearInterval(this._timer); }
}

customElements.define("brew-logs", BrewLogs);
