// Community: the feed of reviewed links (topics, suggest a link with the page read by the
// controller, your suggestions), share a shot to Reddit (image and text made in the browser),
// trending recipes, for what you are brewing, grinders in the library, what you share
// (WebPage.h:497-530, 960-1040).
import { BrewSection } from "../lib/section.js";
import { html, raw } from "../lib/styles.js";
import { f1, f2, fs, fsn, grindOf, coffeeById, recipeById, esc } from "../lib/format.js";
import { shotChart, theme, when } from "../lib/charts.js";
import { shotData } from "../lib/shotview.js";
import { confirmDialog } from "../lib/forms.js";

export const meta = { id: "community", title: "Community", icon: "mdi:account-group", optional: true };

const KINDS = [["video", "Video"], ["playlist", "Playlist"], ["post", "Post"], ["guide", "Guide"], ["article", "Article"], ["podcast", "Podcast"]];
const TOPICS = [["preparation", "Preparation"], ["dialing_in", "Dialling in"], ["brewing", "Brewing"], ["beans", "Beans"], ["gear", "Gear"], ["milk", "Milk"], ["maintenance", "Upkeep"], ["science", "Science"]];
const STATE = { queued: "waiting to send", sent: "sent", review: "with a reviewer", published: "approved: in the feed", declined: "declined", refused: "refused by the library's checks", local: "not sent" };
const METHOD = { straight: "straight through", preinfusion: "held pre-infusion", bloom: "bloom" };
const label = (list, k) => (list.find((x) => x[0] == k) || [k, k])[1];
const ytId = (u) => { const m = /(?:youtube\.com\/watch\?(?:.*&)?v=|youtu\.be\/|youtube\.com\/shorts\/)([\w-]{11})/.exec(u || ""); return m ? m[1] : ""; };
const thumb = (l) => l.image || (ytId(l.url) ? "https://i.ytimg.com/vi/" + ytId(l.url) + "/hqdefault.jpg" : "");
const fd = (n) => (n && n > 20000101 ? new Date(Math.floor(n / 10000), Math.floor(n / 100) % 100 - 1, n % 100).toLocaleDateString([], { month: "short", day: "numeric", year: "numeric" }) : "");
const UI = { topic: "", suggest: false, sel: [], shot: 0, photo: null, sub: "espresso", titleEdited: false };

class BrewCommunity extends BrewSection {
  static get styles() {
    return `.recos{display:grid;grid-template-columns:repeat(auto-fill,minmax(220px,1fr));gap:10px}
    .reco{border:1px solid var(--divider-color);border-radius:10px;padding:10px 12px;display:block;color:var(--primary-text-color);text-decoration:none}
    .reco img{width:100%;aspect-ratio:16/9;object-fit:cover;border-radius:8px;margin-bottom:8px;background:var(--secondary-background-color)}
    .reco .src{font-size:12px;color:var(--secondary-text-color);display:block}
    .chips{display:flex;flex-wrap:wrap;gap:6px} .chips button.on{background:var(--primary-color);color:var(--text-primary-color,#fff);border-color:var(--primary-color)}
    .two{display:grid;grid-template-columns:1fr 1fr;gap:12px} @media (max-width:800px){.two{grid-template-columns:1fr}}
    .pair{display:flex;gap:10px;align-items:center;padding:6px 0;border-bottom:1px solid var(--divider-color)} .pair .what{flex:1}`;
  }

  async load() {
    const [L, SH, S] = await Promise.all([this.store.get("library"), this.store.get("shots"), this.store.get("settings")]);
    const [LK, TR, GM, SHR] = await Promise.all([
      this.api.getOr("/api/links", null, null), this.api.getOr("/api/trending", null, null),
      this.api.getOr("/api/community/grinders", null, null), this.api.getOr("/api/share", null, null),
    ]);
    const p = (L.presets || [])[L.activePreset] || {};
    const now = p.coffeeId && p.recipeId ? await this.api.getOr("/api/community", { coffee: p.coffeeId, recipe: p.recipeId }, null) : null;
    Object.assign(this, { L, SH: Array.isArray(SH) ? SH : [], S, LK, TR, GM: (GM && GM.grinders) || [], SHR, now, g: grindOf(L) });
    if (this.arg) { UI.shot = +this.arg; this.arg = null; this._scrollReddit = true; }
    if (!UI.shot && this.SH.length) UI.shot = this.SH[0].id;
  }

  render() {
    const listed = (this.LK && this.LK.listed) || [], shown = listed.filter((l) => !UI.topic || (l.topics || []).includes(UI.topic)), mine = (this.LK && this.LK.mine) || [];
    const rs = (this.TR && this.TR.recipes) || [], L = this.L, p = (L.presets || [])[L.activePreset] || {}, c = coffeeById(L, p.coffeeId), r = recipeById(L, p.recipeId);
    const sharing = this.S && this.S.shareEnabled, sh = this.SHR, g = this.g, m = this.now, gm = this.GM.filter((x) => x.id);
    return html`<div class="grid">
      <div class="card" style="grid-column:1/-1"><div class="spread"><h3>Community feed</h3><button id="sug">${UI.suggest ? "Close" : "Suggest a link"}</button></div>
        <div class="hint">Videos, posts and guides about espresso that owners suggested and a reviewer approved.</div>
        <div class="chips" style="margin-top:8px">${raw([["", "All"], ...TOPICS].filter((t) => !t[0] || listed.some((l) => (l.topics || []).includes(t[0]))).map(([k, n]) => `<button class="${k == UI.topic ? "on" : ""}" data-topic="${k}">${n}</button>`).join(""))}</div>
        <div class="recos" style="margin-top:10px">${raw(!this.LK ? '<div class="empty">The feed cannot be read from the controller.</div>' : !listed.length ? '<div class="empty" style="grid-column:1/-1">Nothing in the feed yet. It fills as owners suggest links and a reviewer approves them: suggest the first one.</div>' :
          shown.map((l) => { const th = thumb(l); return `<a class="reco" href="${esc(l.url)}" target="_blank" rel="noopener noreferrer">${th ? `<img src="${esc(th)}" alt="" loading="lazy" referrerpolicy="no-referrer">` : ""}<span class="src">${esc(label(KINDS, l.kind))} · ${esc(l.site || "")}</span><b>${esc(l.title)}</b>${l.why ? `<div class="hint" style="margin-top:6px">${esc(l.why)}</div>` : ""}<div class="hint" style="margin-top:6px">${(l.topics || []).map((t) => esc(label(TOPICS, t))).join(" · ")}${l.added ? " · added " + esc(l.added) : ""}</div></a>`; }).join(""))}</div>
        ${UI.suggest ? html`<div style="margin-top:14px;border-top:1px solid var(--divider-color);padding-top:12px">
          <div class="row"><input id="lkurl" type="url" placeholder="https://www.youtube.com/watch?v=..." style="flex:1 1 300px"><button id="lkread">Read the page</button></div>
          <div class="hint" id="lkmsg"></div>
          <div class="form" style="margin-top:8px"><label class="f">Title<input id="lktitle" maxlength="79"></label><label class="f">What it is<select id="lkkind">${raw(KINDS.map(([k, n]) => `<option value="${k}">${n}</option>`).join(""))}</select></label></div>
          <div class="hint" style="margin:8px 0 6px">What it is about (up to three)</div><div class="chips" id="lktopics">${raw(TOPICS.map(([k, n]) => `<button class="${UI.sel.includes(k) ? "on" : ""}" data-lktopic="${k}">${n}</button>`).join(""))}</div>
          <label class="f" style="margin-top:10px">Why it is worth it (optional)<textarea id="lkwhy" maxlength="160" rows="2"></textarea></label>
          <div class="row" style="margin-top:8px"><button class="primary" id="lksend">Send for review</button><span class="hint">A reviewer checks it first. It goes with this controller's key: no account, nothing personal.</span></div></div>` : ""}
        ${mine.length ? html`<div class="muted" style="margin-top:12px">Your suggestions</div>${raw(mine.map((x) => `<div class="pair"><span class="what"><b>${esc(x.title)}</b> <span class="hint">${esc(STATE[x.state] || x.state)}${x.note ? ": " + esc(x.note) : ""}</span></span><button class="danger" data-lkdel="${x.id}">Delete</button></div>`).join(""))}` : ""}
      </div>

      <div class="card" style="grid-column:1/-1" id="rdcard"><div class="spread"><h3>Share a shot to Reddit</h3><span class="hint">Nothing is sent from the controller</span></div>
        <div class="form"><label class="f">Shot<select id="rdshot">${raw(this.SH.slice(0, 40).map((s) => `<option value="${s.id}" ${s.id == UI.shot ? "selected" : ""}>${esc(when(s))} · ${esc(s.coffee || s.recipe)} · ${f1(s.final_weight_g)} g in ${f1(s.shot_time_s)} s</option>`).join(""))}</select></label>
          <label class="f">Your photo (optional)<input type="file" id="rdphoto" accept="image/*"></label>
          <label class="f">Community<select id="rdsub">${raw([["espresso", "r/espresso"], ["Coffee", "r/Coffee"], ["BrevilleCoffee", "r/BrevilleCoffee"]].map(([k, n]) => `<option value="${k}" ${UI.sub == k ? "selected" : ""}>${n}</option>`).join(""))}</select></label></div>
        <label class="f" style="margin-top:8px">Title<input id="rdtitle" maxlength="300"></label>
        <div class="two" style="margin-top:10px"><canvas id="rdimg" width="1200" height="1200" style="width:100%;height:auto;border-radius:10px;border:1px solid var(--divider-color)"></canvas>
          <div><label class="f">Post text<textarea id="rdtext" rows="14"></textarea></label>
          <div class="row" style="margin-top:8px"><button class="primary" id="rddl">Download image</button><button id="rdcopy">Copy text</button><button id="rdopen">Open Reddit</button></div>
          <div class="hint">Reddit takes no picture through a link: download the image, then on Reddit attach it to the post and paste the text.</div></div></div>
        <canvas id="rdcurve" style="display:none;width:560px"></canvas></div>

      <div class="card"><div class="spread"><h3>Trending recipes</h3><span class="hint">${this.TR && this.TR.days ? "the last " + this.TR.days + " days" : ""}</span></div>
        ${raw(rs.length ? `<div class="recos">${rs.map((x, i) => `<div class="reco"><span class="src">#${i + 1}</span><b>${x.dose_g} g in, ${x.yield_g} g out</b><div class="hint">1:${x.ratio} in about ${x.time_s} s, ${esc(METHOD[x.method] || x.method)}</div><div class="hint">${x.contributors} people, ${x.dialins} dial-in${x.dialins == 1 ? "" : "s"}, ${x.coffees} coffee${x.coffees == 1 ? "" : "s"}</div><button style="margin-top:8px" data-trend="${i}">Make it a recipe</button></div>`).join("")}</div>` : '<div class="empty">Coming as more people share: the recipes owners settled on lately, once at least two people settled on the same one.</div>')}</div>

      <div class="card"><div class="spread"><h3>For what you are brewing</h3><span class="hint">${c ? c.name : ""}</span></div>
        ${raw(!c || !r ? '<div class="hint">No coffee is attached to what is being brewed.</div>' : !m ? '<div class="hint">The community figures cannot be read right now.</div>' : !m.found ? `<div class="hint">${esc("Nobody has shared a settled dial-in for " + c.name + " with a recipe like " + r.name + " yet.")}</div>` :
          `<div class="tiles"><div class="tile"><div class="k">Start your grinder at</div><div class="v">${m.start && m.start.setting ? esc(fs(g, m.start.setting)) : "not known"}</div></div><div class="tile"><div class="k">A good shot</div><div class="v">${f1(m.time_s)} s</div></div><div class="tile"><div class="k">Based on</div><div class="v">${m.contributors} ${m.contributors == 1 ? "person" : "people"}</div></div></div><div class="row" style="margin-top:8px"><a class="link" data-go="coffees">Use it from the Coffees tab</a></div>`)}</div>

      <div class="card"><div class="spread"><h3>Grinders in the shared library</h3><a class="link" data-go="grinder">Your grinder</a></div>
        <div class="hint">${gm.length ? gm.length + " grinder models, each with how its setting reads and its range: " + gm.slice(0, 12).map((x) => x.model).join(" · ") + (gm.length > 12 ? " and " + (gm.length - 12) + " more" : "") + ". Owners of the same model pool what they learn." : "The shared grinder list cannot be read yet."}</div></div>

      <div class="card"><div class="spread"><h3>What you share</h3><span class="pill">${sharing ? "sharing" : "not sharing"}</span></div>
        <div class="hint">${!sh ? "" : !sharing ? "You are not sharing yet. Turn it on in Settings, Shared library, to send your coffees, dial-ins and (if you choose) ratings. Nothing personal leaves the controller, and no account is needed." : `${sh.sent} record${sh.sent == 1 ? "" : "s"} sent, ${sh.waiting} waiting. ${sh.problem ? "Not reaching the library: " + sh.problem + ". " : sh.last_ok ? "Last reached " + new Date(sh.last_ok * 1000).toLocaleString() + ". " : ""}What each coffee shares is on its page under Coffees.`}</div>
        <div class="row" style="margin-top:8px"><a class="link" data-go="settings">Sharing settings</a></div></div>
    </div>`;
  }

  wire() {
    this.$$("[data-go]").forEach((a) => a.onclick = () => this.panel.navigate(a.dataset.go));
    this.$$("[data-topic]").forEach((b) => b.onclick = () => { UI.topic = b.dataset.topic; this._paint(); });
    this.$("#sug").onclick = () => { UI.suggest = !UI.suggest; UI.sel = []; this._paint(); };
    this.$$("[data-lkdel]").forEach((b) => b.onclick = async () => {
      if (!(await confirmDialog(this.shadowRoot, "Delete this suggestion from the list here? What a reviewer already approved stays in the feed."))) return;
      const r = await this.post("/api/links/delete", null, "Deleted", { id: +b.dataset.lkdel });
      if (r.ok) this.refresh();
    });
    if (UI.suggest) this.wireSuggest();
    this.$$("[data-trend]").forEach((b) => b.onclick = () => {
      const x = this.TR.recipes[+b.dataset.trend];
      this.store.prefill = { name: x.dose_g + " g in, " + x.yield_g + " g out", doseG: x.dose_g, useRatio: false, targetWeightG: x.yield_g, targetTimeS: Math.round(x.time_s) };
      this.panel.navigate("recipes");
    });
    // Reddit
    this.$("#rdshot").onchange = (e) => { UI.shot = +e.target.value; UI.titleEdited = false; this.rdBuild(); };
    this.$("#rdsub").onchange = (e) => { UI.sub = e.target.value; };
    this.$("#rdtitle").oninput = () => { UI.titleEdited = true; };
    this.$("#rdphoto").onchange = (e) => {
      const f = e.target.files && e.target.files[0];
      if (!f) { UI.photo = null; this.rdBuild(); return; }
      const im = new Image(); im.onload = () => { UI.photo = im; this.rdBuild(); }; im.src = URL.createObjectURL(f);
    };
    this.$("#rddl").onclick = () => this.$("#rdimg").toBlob((b) => { const a = document.createElement("a"); a.href = URL.createObjectURL(b); a.download = "espresso-shot-" + UI.shot + ".png"; a.click(); setTimeout(() => URL.revokeObjectURL(a.href), 4000); }, "image/png");
    this.$("#rdcopy").onclick = async () => { try { await navigator.clipboard.writeText(this.$("#rdtext").value); this.panel.toast("Text copied", true); } catch (e) { this.$("#rdtext").select(); this.panel.toast("Selected: copy it with Ctrl+C", false); } };
    this.$("#rdopen").onclick = () => window.open("https://www.reddit.com/r/" + UI.sub + "/submit?title=" + encodeURIComponent(this.$("#rdtitle").value), "_blank", "noopener");
    if (this.SH.length) this.rdBuild();
    if (this._scrollReddit) { this._scrollReddit = false; setTimeout(() => this.$("#rdcard").scrollIntoView({ behavior: "smooth" }), 200); }
  }

  wireSuggest() {
    this.$$("[data-lktopic]").forEach((b) => b.onclick = () => {
      const k = b.dataset.lktopic, i = UI.sel.indexOf(k);
      if (i >= 0) UI.sel.splice(i, 1); else if (UI.sel.length < 3) UI.sel.push(k); else { this.panel.toast("Three topics at most", false); return; }
      b.classList.toggle("on", UI.sel.includes(k));
    });
    let image = "";
    this.$("#lkread").onclick = async () => {
      const u = this.$("#lkurl").value.trim(), msg = this.$("#lkmsg");
      if (!/^https?:\/\//i.test(u)) { this.panel.toast("Paste a link starting https://", false); return; }
      const yt = ytId(u);
      if (yt) { this.$("#lkkind").value = /list=/.test(u) && !/v=/.test(u) ? "playlist" : "video"; image = "https://i.ytimg.com/vi/" + yt + "/hqdefault.jpg"; } else if (/list=/.test(u)) this.$("#lkkind").value = "playlist";
      msg.textContent = "Reading the page…";
      const r = await this.api.post("/api/subscriptions/preview", { url: u });
      if (r && r.ok === false) { msg.textContent = r.error || "Could not read it: type the title."; return; }
      for (let i = 0; i < 40; i++) {
        await new Promise((x) => setTimeout(x, 750));
        if (!this.isConnected) return;
        const g = await this.api.getOr("/api/subscriptions/preview", null, null);
        if (!g) continue;
        if (g.state == "done") { if (g.title && !this.$("#lktitle").value) this.$("#lktitle").value = g.title.slice(0, 79); if (g.image && !image) image = g.image; msg.textContent = g.site ? "Read from " + g.site + ". Check the title." : "Read. Check the title."; return; }
        if (g.state == "failed") { msg.textContent = "Could not read it (" + (g.problem || "no answer") + "): type the title."; return; }
      }
      msg.textContent = "No answer from the page: type the title.";
    };
    this.$("#lksend").onclick = async () => {
      const u = this.$("#lkurl").value.trim(), t = this.$("#lktitle").value.trim();
      if (!u) { this.panel.toast("Paste the link first", false); return; }
      if (!t) { this.panel.toast("Give it a title", false); return; }
      if (!UI.sel.length) { this.panel.toast("Pick at least one topic", false); return; }
      const r = await this.post("/api/links/suggest", { url: u, title: t, kind: this.$("#lkkind").value, topics: UI.sel, why: this.$("#lkwhy").value.trim(), image }, "Sent for review: it shows in the feed once approved");
      if (r.ok) { UI.suggest = false; UI.sel = []; this.refresh(); }
    };
  }

  /** The 1200 x 1200 picture and the post's text (WebPage.h:1014-1037), in Home Assistant's theme. */
  async rdBuild() {
    const id = UI.shot, s = this.SH.find((x) => x.id == id);
    if (!s) return;
    let d = null;
    if (s.has_data) { try { d = await shotData(this.api, id); } catch (e) { d = null; } }
    if (UI.shot != id || !this.isConnected) return;
    const L = this.L, c = coffeeById(L, s.coffee_id), gr = L.grinder || {}, g = this.g;
    const st = getComputedStyle(this), col = (k, def) => st.getPropertyValue(k).trim() || def;
    const BG = col("--primary-background-color", "#111"), CARD = col("--card-background-color", "#1c1c1c"), FG = col("--primary-text-color", "#eee"), DIM = col("--secondary-text-color", "#999");
    const dose = s.dose_g > 0 ? s.dose_g : 0, out = s.final_weight_g, ratio = dose ? (out / dose).toFixed(1) : "";
    const days = c && c.roastDate && s.timestamp ? Math.round((Date.parse(s.timestamp) - new Date(Math.floor(c.roastDate / 10000), Math.floor(c.roastDate / 100) % 100 - 1, c.roastDate % 100)) / 864e5) : null;
    const name = c ? c.name : s.coffee || "Espresso";
    const title = this.$("#rdtitle");
    if (!UI.titleEdited) title.value = `${name}${c && c.roaster ? " (" + c.roaster + ")" : ""}: ${dose ? f1(dose) + " g in, " : ""}${f1(out)} g out in ${f1(s.shot_time_s)} s`;
    const cv = this.$("#rdimg"), x = cv.getContext("2d"), W = 1200;
    x.fillStyle = BG; x.fillRect(0, 0, W, W);
    x.fillStyle = FG; x.font = "600 52px sans-serif"; x.fillText(name.slice(0, 34), 48, 96);
    x.fillStyle = DIM; x.font = "28px sans-serif"; x.fillText([c && c.roaster, c && c.origin, c && c.process].filter(Boolean).join(" · ").slice(0, 70), 48, 140);
    x.fillStyle = CARD; x.fillRect(40, 170, W - 80, 500);
    if (d && d.time_s && d.time_s.length > 1) {
      const curve = this.$("#rdcurve");
      curve.style.display = "block";
      shotChart(curve, s, d.time_s, d.weight_g, d.flow_g_s, null, theme(this), 235);
      x.drawImage(curve, 40, 185, W - 80, 470);
      curve.style.display = "none";
    } else { x.fillStyle = DIM; x.font = "28px sans-serif"; x.fillText("No curve stored for this shot", 80, 430); }
    const lines = [["Dose", dose ? f1(dose) + " g" : "not set"], ["Yield", f1(out) + " g" + (ratio ? " (1:" + ratio + ")" : "")], ["Time", f1(s.shot_time_s) + " s" + (s.first_drop_s > 0 ? ", first drops " + f1(s.first_drop_s) + " s" : "")], ["Flow", f2(s.pour_flow_g_s) + " g/s, peak " + f2(s.peak_flow_g_s)],
      ["Grind", (s.grind_um ? fs(g, s.grind_um) : "not recorded") + (gr.model ? " on " + gr.model : "")], ["Start", s.bloom_pause_s ? "bloom " + s.bloom_pause_s + " s" : s.preinfusion_hold_s ? "held pre-infusion" : "machine pre-infusion"], ["Roasted", c && c.roastDate ? fd(c.roastDate) + (days != null && days >= 0 ? " (" + days + " days)" : "") : ""]].filter((l) => l[1]);
    const P = UI.photo, px = P ? 600 : W - 80;
    x.fillStyle = CARD; x.fillRect(40, 700, px - 40 + (P ? 0 : 40), 440); x.font = "30px sans-serif";
    lines.forEach((l, i) => { x.fillStyle = DIM; x.fillText(l[0], 70, 756 + i * 56); x.fillStyle = FG; x.fillText(String(l[1]).slice(0, P ? 26 : 52), 230, 756 + i * 56); });
    if (P) { const sx = 640, sy = 700, r = Math.max((W - 40 - sx) / P.width, 440 / P.height), w = P.width * r, h = P.height * r; x.save(); x.beginPath(); x.rect(sx, sy, W - 40 - sx, 440); x.clip(); x.drawImage(P, sx + (W - 40 - sx - w) / 2, sy + (440 - h) / 2, w, h); x.restore(); }
    x.fillStyle = DIM; x.font = "22px sans-serif"; x.fillText("Brew by Weight controller · github.com/hallge/Brew-Controller", 48, 1180);
    const md = [`**Coffee:** ${name}${c && c.roaster ? " from " + c.roaster : ""}${c && (c.origin || c.process) ? " (" + [c.origin, c.process].filter(Boolean).join(", ") + ")" : ""}${c && c.roastDate ? ", roasted " + fd(c.roastDate) + (days != null && days >= 0 ? " (" + days + " days before this shot)" : "") : ""}`,
      `**Recipe:** ${dose ? f1(dose) + " g in, " : ""}${f1(out)} g out${ratio ? " (1:" + ratio + ")" : ""} in ${f1(s.shot_time_s)} s${s.first_drop_s > 0 ? "; first drops at " + f1(s.first_drop_s) + " s" : ""}${s.bloom_pause_s ? "; bloom " + s.bloom_pause_s + " s" : ""}`,
      `**Grind:** ${s.grind_um ? fs(g, s.grind_um) : "not recorded"}${gr.model ? " on a " + gr.model : ""}`,
      `**Flow:** ${f2(s.pour_flow_g_s)} g/s on average while pouring, peak ${f2(s.peak_flow_g_s)} g/s`,
      s.taste ? `**Taste:** ${s.taste}${s.rating ? ", " + s.rating + "/5" : ""}` : "", c && c.notes ? `**Tasting notes on the bag:** ${c.notes}` : "",
      "", "*Stopped by weight with an open-source brew-by-weight controller: https://github.com/hallge/Brew-Controller*"].filter((l, i, a) => l || a[i + 1]);
    this.$("#rdtext").value = md.join("\n\n").replace(/\n\n\n+/g, "\n\n");
  }
}

customElements.define("brew-community", BrewCommunity);
