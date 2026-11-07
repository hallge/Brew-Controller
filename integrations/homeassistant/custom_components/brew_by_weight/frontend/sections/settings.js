// Settings: the software cards of the controller page's Setup (WebPage.h:531-638): tabs shown,
// drinkers, controller screen, Home Assistant, backup download, shared library, diagnostics,
// report a problem, relay address, and the firmware version (read only). Devices, pusher keys,
// brew and press timing, Wi-Fi, restore and firmware updates operate the hardware: the controller
// page does those.
import { BrewSection } from "../lib/section.js";
import { html, raw } from "../lib/styles.js";
import { ago, f1, esc } from "../lib/format.js";
import { confirmDialog } from "../lib/forms.js";

export const meta = { id: "settings", title: "Settings", icon: "mdi:cog" };

const BUG_KINDS = [["brewing", "Brewing: starting, stopping, weights"], ["grind_advice", "Grind advice or a starting point"], ["scale", "The scale"], ["pushers", "Button pushers (bots)"], ["machine_power", "Switching the machine on or off"], ["web_page", "The web page or this panel"], ["sharing", "Sharing and the community library"], ["firmware_update", "Firmware updates"], ["other", "Something else"]];
const SHARE_KEYS = [["shareCoffees", "Coffees I add or change (name, roaster, origin, process, roast level, notes)"], ["shareDialIns", "Settled dial-ins: grind, dose, yield, brew time, grinder, roast date"], ["shareShots", "Every shot that counts for grind advice, with its flow"], ["shareRatings", "My taste ratings, with those shots"]];

const haRoom = (a) => { if (!a || a.source == "none") return "no reading yet"; const p = []; if (a.rh_pct != null) p.push(f1(a.rh_pct) + " % RH"); if (a.temp_c != null) p.push(f1(a.temp_c) + " °C"); return p.join(", ") + " (" + (a.source == "ha" ? "Home Assistant" : "pushed") + ", " + Math.round((a.age_s || 0) / 60) + " min ago)"; };

class BrewSettings extends BrewSection {
  static get styles() { return `fieldset{border:1px solid var(--divider-color);border-radius:10px;margin:8px 0;padding:8px 12px} legend{color:var(--secondary-text-color);font-size:13px} pre{white-space:pre-wrap;word-break:break-word;max-height:360px;overflow:auto;font-size:12px;background:var(--secondary-background-color);padding:8px;border-radius:8px}`; }

  async load() {
    const [S, RT, SH] = await Promise.all([this.store.get("settings"), this.store.get("ratings").catch(() => null), this.store.get("share").catch(() => null)]);
    const [HA, DG, CR, UP] = await Promise.all([
      this.api.getOr("/api/homeassistant", null, null), this.api.getOr("/api/diagnostics/report", null, null),
      this.api.getOr("/api/crash", null, null), this.api.getOr("/api/update", null, null),
    ]);
    Object.assign(this, { S, RT, SH, HA, DG, CR, UP });
  }

  render() {
    const S = this.S || {}, RT = this.RT, SH = this.SH, HA = this.HA, DG = this.DG, CR = this.CR, UP = this.UP;
    const hidden = new Set(this.panel.hiddenTabs || []), isAdmin = !this.hass.user || this.hass.user.is_admin;
    const dm = CR && CR.dump;
    return html`<div class="grid">
      <div class="card"><h3>Tabs shown</h3><div class="hint">Extra tabs of this panel, for your Home Assistant user.</div>
        ${raw(this.panel.optionalTabs.map((t) => `<div><label class="c"><input type="checkbox" data-tab="${t.id}" ${hidden.has(t.id) ? "" : "checked"}> ${t.title}</label></div>`).join(""))}</div>

      <div class="card"><h3>Drinkers</h3>
        <label class="c"><input type="checkbox" id="named" ${S.namedDrinkers ? "checked" : ""}> Named drinkers <span class="hint">(each rating says who gave it; each drinker gets their own taste and recommendations)</span></label>
        <div class="form" style="margin-top:8px">${raw(((RT && RT.drinkers) || Array(8).fill("")).map((n, i) => `<label class="f">Drinker ${i + 1}<input data-drinker="${i}" maxlength="16" value="${esc(n)}"></label>`).join(""))}</div>
        <div class="row" style="margin-top:8px"><button id="drsave">Save names</button><span class="hint">Clearing a name keeps that drinker's ratings.</span></div></div>

      <div class="card"><h3>Controller screen</h3><label class="c"><input type="checkbox" id="flip" ${S.displayFlipped ? "checked" : ""}> Turned upside down, for a mounting with the USB-C connector and the touch key on the right</label></div>

      <div class="card"><div class="spread"><h3>Home Assistant</h3><span class="hint">${HA ? (HA.url ? (HA.mqtt_url ? (HA.mqtt_connected ? "MQTT connected" : "MQTT not connected") : "reading the room") : "off") : ""}</span></div>
        <div class="hint">What the controller itself reads from and sends to Home Assistant: the room's humidity and temperature recorded with each shot, a webhook after each shot, and MQTT discovery.</div>
        ${HA ? html`<form id="haf" class="form" style="margin-top:8px">
          <label class="f">Home Assistant address<input name="url" value="${HA.url || ""}" placeholder="http://homeassistant.local:8123"></label>
          <label class="f">Long-lived access token (blank keeps current)<input name="token" type="password" autocomplete="off" placeholder="${HA.token_set ? "set" : "not set"}"></label>
          <label class="f">Humidity entity<input name="rh" value="${HA.rh_entity || ""}"></label>
          <label class="f">Temperature entity<input name="temp" value="${HA.temp_entity || ""}"></label>
          <label class="f">Webhook id for each finished shot (blank keeps current)<input name="hook" type="password" autocomplete="off" placeholder="${HA.webhook_set ? "set" : "not set"}"></label>
          <label class="f">MQTT broker (empty: off)<input name="mqtt" value="${HA.mqtt_url || ""}" placeholder="mqtt://homeassistant.local:1883"></label>
          <label class="f">MQTT user<input name="muser" value="${HA.mqtt_user || ""}" autocomplete="off"></label>
          <label class="f">MQTT password (blank keeps current)<input name="mpass" type="password" autocomplete="off" placeholder="${HA.mqtt_password_set ? "set" : "not set"}"></label>
          <div style="grid-column:1/-1" class="row"><label class="c"><input type="checkbox" name="cleartok"> Remove the token</label><label class="c"><input type="checkbox" name="clearhook"> Remove the webhook</label><label class="c"><input type="checkbox" name="clearmpass"> Remove the MQTT password</label></div></form>
          <div class="hint" id="hamsg">Room: ${haRoom(HA.ambient)}${HA.problem ? ". Last problem: " + HA.problem : ""}${HA.mqtt_problem ? ". MQTT: " + HA.mqtt_problem : ""}${HA.last_webhook_code ? ". Last webhook answer: " + HA.last_webhook_code : ""}</div>
          <div class="row" style="margin-top:8px"><button class="primary" id="hasave">Save</button><button id="hatest">Test the reading</button></div>` : html`<div class="hint">Not available from the controller.</div>`}</div>

      <div class="card"><h3>Backup</h3><div class="hint">One file with this controller's settings, coffees, recipes, pairing and shots. Download one before the controller is reflashed. Restoring one is done in the controller page.</div>
        <div><label class="c"><input type="checkbox" id="bkhist" checked> Include the shot history</label></div>
        <div><label class="c"><input type="checkbox" id="bksec" ${isAdmin ? "" : "disabled"}> Include the Wi-Fi and bot passwords, tokens and the signing key <span class="hint">(keep that file private)</span></label></div>
        <div class="row" style="margin-top:8px"><button class="primary" id="bkdl" ${isAdmin ? "" : "disabled"}>Download backup</button>${isAdmin ? "" : html`<span class="hint">Only an administrator can download a backup.</span>`}</div></div>

      <div class="card"><div class="spread"><h3>Shared library</h3><span class="pill ${S.shareEnabled ? "ok" : ""}">${!S.shareEnabled ? "off" : SH && SH.can_send ? "sending" : "queueing"}</span></div>
        ${SH ? html`<div class="hint">${SH.key_ready ? "This controller's key id: " + SH.key_id + "." : "No device key: nothing can be shared."}</div>
          <div class="hint">${SH.via_relay ? "Through the relay (" + (SH.relay || "") + "). " : ""}${!SH.configured ? (SH.repository ? "No token stored yet." : "Not connected: records wait on the controller.") : SH.problem ? "Not reaching the library: " + SH.problem + "." : SH.can_send ? "Connected" + (SH.last_ok ? ", last reached " + new Date(SH.last_ok * 1000).toLocaleString() : "") + "." : "Waiting to try again."}${SH.token_set ? " A token is stored." : ""}</div>
          <div class="hint">${SH.waiting} waiting to send${SH.capacity ? " (room for " + SH.capacity + ")" : ""}, ${SH.sent} sent.</div>` : ""}
        <details ${SH && SH.repository ? "open" : ""}><summary>Use your own library repository (optional)</summary>
          <div class="form"><label class="f">Library repository (owner/name)<input id="shrepo" maxlength="63" value="${(SH && SH.repository) || ""}" autocomplete="off"></label>
          <label class="f">Token for that repository only (blank keeps the one stored)<input id="shtok" type="password" maxlength="127" autocomplete="new-password"></label></div></details>
        <div style="margin-top:6px"><label class="c"><input type="checkbox" id="shareEnabled" ${S.shareEnabled ? "checked" : ""}> Share from this controller</label></div>
        <fieldset><legend>What it shares</legend>${raw(SHARE_KEYS.map(([k, l]) => `<div><label class="c"><input type="checkbox" data-sk="${k}" ${S[k] ? "checked" : ""}> ${l}</label></div>`).join(""))}</fieldset>
        <div class="row"><button class="primary" id="shsave">Save</button><button id="shqueue">Queue coffees not sent yet</button><button id="shagain">Send everything again</button></div>
        <div class="hint">Every record is signed with this controller's own key: its short id, never a name, an address or anything about the network.</div></div>

      <div class="card"><div class="spread"><h3>Diagnostics</h3><span class="pill">${DG && DG.on ? "on" : "off"}</span></div>
        <div class="hint">A short technical report once a day, and after a crash, so problems in other homes get found and fixed. Never sent: addresses, the Wi-Fi name, passwords, tokens, coffee, roaster or drinker names, notes, subscriptions or where you are.</div>
        <label class="c" style="margin-top:6px"><input type="checkbox" id="diagon" ${S.diagnosticsOn ? "checked" : ""}> Send diagnostics reports</label>
        <div class="hint">${DG && DG.on ? ["Last sent: " + ago(DG.last_sent), DG.waiting ? DG.waiting + " waiting" : "", DG.last_problem ? "last try: " + DG.last_problem : "", DG.key_id ? "key " + DG.key_id : ""].filter((x) => x).join(" · ") : "Nothing is sent."}</div>
        ${CR && CR.t ? html`<div class="hint"><b>Last crash:</b> ${CR.fw || ""} ${CR.reset || ""}${dm ? ", in " + dm.task + " at " + dm.pc + (dm.cause ? " (" + dm.cause + ")" : "") : ""}${CR.at ? " · " + ago(CR.at) : ""}</div>` : ""}
        <div class="row" style="margin-top:8px"><button id="dgprev">What would be sent</button><button id="dgsend" ${DG && DG.on ? "" : "disabled"}>Send now</button></div>
        <pre id="dgpre" style="display:none"></pre>
        <h3 style="margin-top:14px">Report a problem</h3><div class="hint">Something not working? Say what happened: it goes to the maintainer with the log attached.</div>
        <div class="form" style="margin-top:6px"><label class="f">What it is about<select id="bgkind">${raw(BUG_KINDS.map(([k, l]) => `<option value="${k}" ${k == "other" ? "selected" : ""}>${l}</option>`).join(""))}</select></label>
          <label class="f">How to reach you (optional)<input id="bgcontact" maxlength="64" placeholder="a GitHub name or an email"></label>
          <label class="f" style="grid-column:1/-1">What happened<textarea id="bgtext" rows="4" maxlength="1000" placeholder="What you did, what you expected, and what happened instead."></textarea></label></div>
        <div class="row" style="margin-top:6px"><button class="primary" id="bgsend" ${DG && DG.on ? "" : "disabled"}>Send the report</button><span class="hint" id="bgn">0 / 1000</span><span class="hint">${!(DG && DG.on) ? "Turn diagnostics on to send a report." : DG.bug_waiting ? "Your report goes with the next one, in a moment." : DG.bug_sent ? "Last report sent " + new Date(DG.bug_sent * 1000).toLocaleString() + "." : ""}</span></div>
        <details style="margin-top:8px"><summary>Relay address (for testing; diagnostics and the shared library both use it)</summary>
          <div class="row"><input id="relay" maxlength="95" placeholder="blank: the built-in relay" value="${DG && !DG.relay_default ? DG.relay || "" : ""}" style="flex:1 1 260px"><button id="relaysave">Save</button></div></details></div>

      <div class="card"><h3>Firmware</h3>
        ${UP ? html`<div>Running <b>${UP.running || UP.version || "?"}</b>${UP.latest ? html`; latest release <b>${UP.latest}</b>${UP.available ? html` <span class="pill warn">update available</span>` : ""}` : ""}.</div>` : html`<div class="hint">Not available from the controller.</div>`}
        <div class="hw" style="margin-top:8px">Checking for and installing updates, and choosing the version it starts, are in the <a data-ctl>controller page</a>.</div></div>

      <div class="card"><h3>In the controller page</h3><div class="hint">These operate the hardware, so they stay in the controller's own page: devices and pairing, test presses, pusher keys, the brew and press timing settings, the machine's make and model, the power schedule, Wi-Fi, restoring a backup and firmware updates.</div>
        <div class="row" style="margin-top:8px"><button data-ctl>Open the controller page</button></div></div>
    </div>`;
  }

  async saveSettings(body, ok) {
    const r = await this.post("/api/settings", body, ok || "Saved");
    if (r.ok) this.store.invalidate("settings");
    return r;
  }

  wire() {
    this.$$("[data-ctl]").forEach((a) => a.onclick = () => this.panel.openController());
    this.$$("[data-tab]").forEach((cb) => cb.onchange = () => this.panel.setHiddenTabs(this.$$("[data-tab]").filter((x) => !x.checked).map((x) => x.dataset.tab)));
    this.$("#named").onchange = (e) => this.saveSettings({ namedDrinkers: e.target.checked }).then(() => this.store.invalidate("ratings"));
    this.$("#drsave").onclick = async () => { const r = await this.post("/api/drinkers", { names: [...Array(8)].map((_, i) => (this.$(`[data-drinker="${i}"]`) || {}).value || "") }, "Names saved"); if (r.ok) this.store.invalidate("ratings"); };
    this.$("#flip").onchange = (e) => this.saveSettings({ displayFlipped: e.target.checked });
    // Home Assistant card
    const haf = this.$("#haf");
    if (haf) {
      this.$("#hasave").onclick = async () => {
        const r = await this.post("/api/homeassistant", { url: haf.url.value.trim(), token: haf.token.value.trim(), rh_entity: haf.rh.value.trim(), temp_entity: haf.temp.value.trim(), webhook_id: haf.hook.value.trim(), mqtt_url: haf.mqtt.value.trim(), mqtt_user: haf.muser.value.trim(), mqtt_password: haf.mpass.value, clear_token: haf.cleartok.checked, clear_webhook: haf.clearhook.checked, clear_mqtt_password: haf.clearmpass.checked }, "Saved");
        if (r.ok) setTimeout(() => this.refresh(), 800);
      };
      this.$("#hatest").onclick = async () => {
        const m = this.$("#hamsg"); m.textContent = "Reading…";
        const t = await this.api.getOr("/api/ambient/test", null, null);
        m.textContent = !t ? "The controller did not answer" : t.ok ? "Room: " + haRoom(t.reading) + (t.problem ? ". " + t.problem : "") : "Not read: " + (t.problem || "unknown");
      };
    }
    // backup
    this.$("#bkdl").onclick = () => this.api.download("backup", { history: this.$("#bkhist").checked ? 1 : 0, secrets: this.$("#bksec").checked ? 1 : 0 }, "brew-controller.bbwbak").catch((e) => this.panel.toast(e.message, false));
    // shared library
    this.$("#shsave").onclick = async () => {
      const repo = this.$("#shrepo").value.trim(), tok = this.$("#shtok").value.trim(), linkChanged = !!tok || !this.SH || repo !== (this.SH.repository || "");
      if (linkChanged) { const r = await this.post("/api/share/link", { repository: repo, token: tok }, null); if (!r.ok) { this.panel.toast(r.error || "Not saved", false); return; } }
      const body = { shareEnabled: this.$("#shareEnabled").checked };
      this.$$("[data-sk]").forEach((cb) => { body[cb.dataset.sk] = cb.checked; });
      const r = await this.saveSettings(body);
      if (r.ok) this.store.invalidate("share");
    };
    this.$("#shqueue").onclick = async () => { const r = await this.post("/api/share/coffees", null, null); this.panel.toast(r.ok ? (r.queued || 0) + " coffees queued" : r.error || "Not saved", !!r.ok); if (r.ok) this.store.invalidate("share", "library"); };
    this.$("#shagain").onclick = async () => {
      if (!(await confirmDialog(this.shadowRoot, "Forget what was sent before and send every coffee and grinder you have described again?", { yes: "Send again" }))) return;
      const r = await this.post("/api/share/again", null, null); this.panel.toast(r.ok ? (r.queued || 0) + " queued" : r.error || "Not saved", !!r.ok); if (r.ok) this.store.invalidate("share", "library");
    };
    // diagnostics
    this.$("#diagon").onchange = async (e) => { const r = await this.saveSettings({ diagnosticsOn: e.target.checked }, e.target.checked ? "Diagnostics on" : "Diagnostics off: nothing is sent"); if (r.ok) setTimeout(() => this.refresh(), 500); };
    this.$("#dgprev").onclick = async () => { const p = this.$("#dgpre"); if (p.style.display != "none") { p.style.display = "none"; return; } const t = await this.api.getOr("/api/diagnostics/preview", null, null); p.textContent = t ? JSON.stringify(t, null, 1) : "Not available."; p.style.display = ""; };
    this.$("#dgsend").onclick = async () => { const r = await this.post("/api/diagnostics/send", null, "Sending a report"); if (r.ok) setTimeout(() => this.refresh(), 8000); };
    const bt = this.$("#bgtext"); bt.oninput = () => { this.$("#bgn").textContent = bt.value.length + " / 1000"; };
    this.$("#bgsend").onclick = async () => {
      const t = bt.value.trim();
      if (!t) { this.panel.toast("Say what happened first", false); return; }
      const r = await this.post("/api/diagnostics/bug", { kind: this.$("#bgkind").value, text: t, contact: this.$("#bgcontact").value.trim() }, "Thank you: the report is on its way");
      if (r.ok) { bt.value = ""; this.$("#bgn").textContent = "0 / 1000"; }
    };
    this.$("#relaysave").onclick = async () => { const r = await this.post("/api/diagnostics/relay", { url: this.$("#relay").value.trim() }, "Saved"); if (r.ok) this.refresh(); };
  }
}

customElements.define("brew-settings", BrewSettings);
