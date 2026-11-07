// Form helpers for the Brew panel: a confirm dialog, a modal form dialog, and field builders.
import { esc } from "./format.js";

const DIALOG_CSS = `
.dlg-back{position:fixed;inset:0;background:rgba(0,0,0,.45);display:flex;align-items:center;justify-content:center;z-index:30;padding:16px}
.dlg{background:var(--card-background-color,#fff);color:var(--primary-text-color);border-radius:16px;max-width:560px;width:100%;
  max-height:90vh;overflow:auto;padding:20px;box-shadow:0 10px 40px rgba(0,0,0,.4)}
.dlg h3{margin:0 0 12px;font-size:18px;font-weight:500}
.dlg .acts{display:flex;justify-content:flex-end;gap:8px;margin-top:16px;flex-wrap:wrap}
`;

function host(root) {
  // A dialog lives in the section's shadow root so the panel's theme variables apply.
  const holder = document.createElement("div");
  holder.innerHTML = `<style>${DIALOG_CSS}</style>`;
  root.appendChild(holder);
  return holder;
}

/** Ask a yes/no question. Resolves true for Yes (Enter), false for No (Esc, click outside). */
export function confirmDialog(root, text, { yes = "Yes", no = "Cancel", danger } = {}) {
  danger = danger ?? /^(Delete|Forget|Remove|Clear)/.test(text);
  return new Promise((resolve) => {
    const h = host(root);
    h.insertAdjacentHTML("beforeend", `<div class="dlg-back"><div class="dlg" role="dialog" aria-modal="true">
      <div>${esc(text)}</div><div class="acts"><button data-a="no">${esc(no)}</button><button class="${danger ? "danger" : "primary"}" data-a="yes">${esc(yes)}</button></div></div></div>`);
    const end = (v) => { document.removeEventListener("keydown", key, true); h.remove(); resolve(v); };
    const key = (e) => { if (e.key === "Escape") { e.preventDefault(); end(false); } else if (e.key === "Enter") { e.preventDefault(); end(true); } };
    document.addEventListener("keydown", key, true);
    h.querySelector(".dlg-back").addEventListener("click", (e) => { if (e.target.classList.contains("dlg-back")) end(false); });
    h.querySelector('[data-a="no"]').onclick = () => end(false);
    const y = h.querySelector('[data-a="yes"]');
    y.onclick = () => end(true);
    y.focus();
  });
}

/**
 * A modal form. `body` is HTML (use the field helpers); `onSubmit(form)` returns true (or a
 * truthy promise) to close, false to keep it open. Resolves with the submit result or null.
 */
export function formDialog(root, title, body, onSubmit, { submit = "Save", cancel = "Cancel", wide, extra } = {}) {
  return new Promise((resolve) => {
    const h = host(root);
    h.insertAdjacentHTML("beforeend", `<div class="dlg-back"><form class="dlg" style="${wide ? "max-width:820px" : ""}" novalidate>
      <h3>${esc(title)}</h3>${body}<div class="acts">${extra || ""}<button type="button" data-a="cancel">${esc(cancel)}</button><button class="primary" type="submit">${esc(submit)}</button></div></form></div>`);
    const form = h.querySelector("form");
    const end = (v) => { document.removeEventListener("keydown", key, true); h.remove(); resolve(v); };
    const key = (e) => { if (e.key === "Escape") { e.preventDefault(); end(null); } };
    document.addEventListener("keydown", key, true);
    h.querySelector('[data-a="cancel"]').onclick = () => end(null);
    form.addEventListener("submit", async (e) => {
      e.preventDefault();
      const btn = form.querySelector('button[type="submit"]');
      btn.disabled = true;
      try {
        const r = await onSubmit(form);
        if (r) end(r);
      } finally { btn.disabled = false; }
    });
    const first = form.querySelector("input,select,textarea");
    if (first) first.focus();
    if (extra) form._dialogHolder = h;
  });
}

// --- fields --------------------------------------------------------------------------------------
const attr = (o) => Object.entries(o || {}).filter(([, v]) => v !== undefined && v !== null && v !== false).map(([k, v]) => (v === true ? k : `${k}="${esc(v)}"`)).join(" ");

export const textField = (name, label, value, a = {}) => `<label class="f">${esc(label)}<input name="${name}" value="${esc(value ?? "")}" ${attr(a)}></label>`;
export const numberField = (name, label, value, a = {}) => `<label class="f">${esc(label)}<input name="${name}" type="number" value="${value ?? ""}" ${attr({ step: "any", ...a })}></label>`;
export const dateField = (name, label, value, a = {}) => `<label class="f">${esc(label)}<input name="${name}" type="date" value="${esc(value || "")}" ${attr(a)}></label>`;
export const areaField = (name, label, value, a = {}) => `<label class="f" style="grid-column:1/-1">${esc(label)}<textarea name="${name}" rows="${a.rows || 3}" ${attr({ ...a, rows: undefined })}>${esc(value ?? "")}</textarea></label>`;
export const checkField = (name, label, checked) => `<label class="c"><input type="checkbox" name="${name}" ${checked ? "checked" : ""}> ${esc(label)}</label>`;
/** options: [[value, label]] or [value]. */
export const selectField = (name, label, options, value, a = {}) => `<label class="f">${esc(label)}<select name="${name}" ${attr(a)}>${options.map((o) => { const [v, l] = Array.isArray(o) ? o : [o, o]; return `<option value="${esc(v)}" ${String(v) === String(value) ? "selected" : ""}>${esc(l)}</option>`; }).join("")}</select></label>`;

/** Read a form into an object: numbers for type=number, booleans for checkboxes, strings else. */
export function readForm(form) {
  const out = {};
  form.querySelectorAll("input,select,textarea").forEach((el) => {
    if (!el.name) return;
    if (el.type === "checkbox") out[el.name] = el.checked;
    else if (el.type === "number") out[el.name] = el.value === "" ? null : +el.value;
    else out[el.name] = el.value;
  });
  return out;
}

/** Stars as buttons (rating 1-5); value 0 = none. */
export const starButtons = (name, value) => `<span class="row" data-stars="${name}">${[1, 2, 3, 4, 5].map((i) => `<button type="button" class="${value >= i ? "primary" : ""}" data-v="${i}" aria-label="${i} of 5">${i <= value ? "★" : "☆"}</button>`).join("")}</span>`;
