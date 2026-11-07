// Upkeep rows (backflush, descale, grinder clean): shots since, remind after, done today
// (WebPage.h:1864-1874). Used by the Machine and Grinder tabs.
import { esc, ago } from "./format.js";

export const JOB_NAMES = { backflush: "Backflush", descale: "Descale", grinder_clean: "Grinder clean" };

export function upkeepHtml(maint, jobs) {
  if (!maint || !Array.isArray(maint.jobs)) return `<div class="hint">Upkeep counters are not available.</div>`;
  return maint.jobs.filter((j) => jobs.includes(j.id)).map((j) => `
    <div class="spread" style="padding:8px 0;border-bottom:1px solid var(--divider-color);flex-wrap:wrap">
      <div><b>${esc(JOB_NAMES[j.id] || j.id)}</b> ${j.due ? '<span class="pill warn">due</span>' : ""}
        <div class="hint">${j.since_shots} shot${j.since_shots == 1 ? "" : "s"} since it was last done${j.last_epoch ? " (" + esc(ago(j.last_epoch)) + ")" : ""}${j.every_shots ? ", reminder every " + j.every_shots : ", no reminder set"}.</div></div>
      <div class="row"><label class="row" style="gap:6px"><span class="hint">Remind after</span><input type="number" min="0" max="5000" style="width:80px" value="${j.every_shots || 0}" data-every="${j.id}"><span class="hint">shots</span></label>
        <button data-done="${j.id}">Done today</button></div>
    </div>`).join("");
}

/** Wire the rows; `after` runs once a change is saved. */
export function wireUpkeep(section, root, after) {
  root.querySelectorAll("[data-done]").forEach((b) => b.onclick = async () => {
    const r = await section.post("/api/maintenance", null, (JOB_NAMES[b.dataset.done] || "Job") + " marked done", { job: b.dataset.done, action: "done" });
    if (r.ok) after();
  });
  root.querySelectorAll("[data-every]").forEach((inp) => inp.onchange = async () => {
    const n = Math.max(0, Math.min(5000, Math.round(+inp.value || 0)));
    const r = await section.post("/api/maintenance", null, n ? "Reminder every " + n + " shots" : "Reminder off", { job: inp.dataset.every, action: "every", every: n });
    if (r.ok) after();
  });
}
