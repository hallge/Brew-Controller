// Machine: the software part of the controller page's Espresso machine section: upkeep
// (backflush, descale), the machine as the controller knows it (read only), the power state it
// believes with a correction that presses nothing, and the schedule (read only). Power presses,
// the schedule, make and model, and power-bot settings drive the hardware: the controller page
// does those (WebPage.h:420-436, 1864-1941).
import { BrewSection } from "../lib/section.js";
import { html, raw } from "../lib/styles.js";
import { esc } from "../lib/format.js";
import { upkeepHtml, wireUpkeep } from "../lib/upkeep.js";

export const meta = { id: "machine", title: "Machine", icon: "mdi:coffee-maker" };

const STYLE = {
  toggle_button: "One button: press to start, press to stop", start_button: "A button starts; the machine stops itself",
  start_stop_buttons: "Two buttons: start and stop", hold_button: "One button held for the whole shot", manual: "By hand: lever, knob or paddle",
};
const DAYS = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"]; // bit 0 Sunday .. bit 6 Saturday (core/PowerControl.h:31)

class BrewMachine extends BrewSection {
  async load() {
    const [maint, power, machine] = await Promise.all([
      this.store.get("maintenance"), this.store.get("power").catch(() => null), this.store.get("machine").catch(() => null),
    ]);
    Object.assign(this, { maint, power, machine });
  }

  render() {
    const m = (this.machine && this.machine.current) || {}, p = this.power;
    const sched = (p && p.schedule) || [];
    const hm = (min) => String(Math.floor(min / 60)).padStart(2, "0") + ":" + String(min % 60).padStart(2, "0");
    const days = (bits) => ((bits & 127) == 127 ? "every day" : DAYS.filter((_, i) => bits & (1 << i)).join(" ") || "no days");
    return html`
    <div class="grid">
      <div class="card"><div class="spread"><h3>Upkeep</h3><span class="hint">${this.maint ? this.maint.shots_total + " shots in all" : ""}</span></div>
        <div id="rows">${raw(upkeepHtml(this.maint, ["backflush", "descale"]))}</div>
        <div class="hint" style="margin-top:8px">A shot counts from 12 s and 2 g. The controller cannot tell from the cup when the machine needs descaling, so it counts.</div></div>

      <div class="card"><div class="spread"><h3>Machine power</h3>${p && p.paired ? html`<span class="pill ${p.belief == "on" ? "ok" : ""}">${p.belief == "on" ? "On" : p.belief == "off" ? "Off" : "Not known"}</span>` : html`<span class="pill">No power bot</span>`}</div>
        ${p && p.paired ? html`
          <div class="hint">${p.last ? "Last press: " + p.last + "." : ""}</div>
          <div class="row" style="margin-top:8px"><span class="hint">Not right? It is really</span><button data-believe="believe_on">on</button><button data-believe="believe_off">off</button></div>
          <div class="hint">Saying how it is presses nothing.</div>
          <div class="muted" style="margin-top:12px">Schedule</div>
          ${sched.length ? html`<table><tr><th>When</th><th>Does</th><th>Days</th><th></th></tr>${raw(sched.filter((s) => s.enabled || s.minute).map((s) => `<tr><td>${hm(s.minute)}</td><td>${s.on ? "On" : "Off"}</td><td>${esc(days(s.days))}</td><td>${s.enabled ? "" : '<span class="muted">paused</span>'}</td></tr>`).join(""))}</table>` : html`<div class="hint">No times set.</div>`}`
          : html`<div class="hint">No power bot is paired, so the machine is switched by hand.</div>`}
        <div class="hw" style="margin-top:10px">Switching on and off, the schedule and the power-bot options are in the <a data-ctl>controller page</a>.</div></div>

      <div class="card"><h3>Make and model</h3>
        ${m.maker ? html`<table>
          <tr><td class="muted">Machine</td><td>${m.maker} ${m.model}</td></tr>
          ${m.control ? html`<tr><td class="muted">Control</td><td>${m.control}</td></tr>` : ""}
          ${m.style ? html`<tr><td class="muted">How a shot is run</td><td>${STYLE[m.style] || m.style}</td></tr>` : ""}
          ${m.share_state ? html`<tr><td class="muted">Shared library</td><td>${m.share_state}</td></tr>` : ""}
        </table>` : html`<div class="hint">No machine chosen yet.</div>`}
        <div class="hw" style="margin-top:10px">How the controller drives the machine is changed in the <a data-ctl>controller page</a>.</div></div>
    </div>`;
  }

  wire() {
    this.$$("[data-ctl]").forEach((a) => a.onclick = () => this.panel.openController());
    wireUpkeep(this, this.$("#rows"), () => this.store.invalidate("maintenance"));
    this.$$("[data-believe]").forEach((b) => b.onclick = async () => {
      const r = await this.post("/api/power", { action: b.dataset.believe }, "Noted: nothing was pressed");
      if (r.ok) this.store.invalidate("power");
    });
  }
}

customElements.define("brew-machine", BrewMachine);
