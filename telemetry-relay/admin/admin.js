// The relay admin console: plain JS, no build step. Everything shown from a record or a pull request
// is put in as text (never as HTML): records come from anyone's controller.
'use strict';

const main = document.getElementById('main');

// ------------------------------------------------------------------------------ helpers --

function h(tag, attrs, ...children) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v === undefined || v === null || v === false) continue;
    if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
    else if (k === 'class') el.className = v;
    else el.setAttribute(k, v === true ? '' : String(v));
  }
  for (const c of children.flat()) {
    if (c === undefined || c === null || c === false) continue;
    el.append(c instanceof Node ? c : document.createTextNode(String(c)));
  }
  return el;
}

function show(...nodes) {
  main.replaceChildren(...nodes.flat());
}

function toast(message, bad) {
  const t = document.getElementById('toast');
  t.textContent = message;
  t.className = 'show' + (bad ? ' bad' : '');
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => (t.className = ''), 5000);
}

async function api(name, params, body) {
  const query = params ? '?' + new URLSearchParams(Object.entries(params).filter(([, v]) => v !== '' && v !== undefined && v !== null)) : '';
  const init = { credentials: 'same-origin', headers: { 'x-bbw-admin': '1' } };
  if (body !== undefined) {
    init.method = 'POST';
    init.headers['content-type'] = 'application/json';
    init.body = JSON.stringify(body);
  }
  const response = await fetch(`/admin/api/${name}${query}`, init);
  if (response.status === 403 && !(response.headers.get('content-type') || '').includes('json')) {
    throw new Error('Your Access session has ended: reload the page to sign in again.');
  }
  let data;
  try {
    data = await response.json();
  } catch {
    throw new Error(`HTTP ${response.status}`);
  }
  if (!data.ok) throw new Error(data.error || `HTTP ${response.status}`);
  if (data.actor) document.getElementById('who').textContent = data.actor;
  return data;
}

function age(seconds) {
  if (seconds === null || seconds === undefined) return '';
  if (seconds < 3600) return `${Math.floor(seconds / 60)} min`;
  if (seconds < 172800) return `${Math.floor(seconds / 3600)} h`;
  return `${Math.floor(seconds / 86400)} d`;
}

function when(iso) {
  if (!iso) return '';
  const d = new Date(iso);
  return Number.isNaN(d.getTime()) ? String(iso) : d.toLocaleString();
}

function table(headers, rows) {
  return h('div', { class: 'scroll' }, h('table', {}, h('thead', {}, h('tr', {}, headers.map((t) => h('th', {}, t)))), h('tbody', {}, rows)));
}

function pretty(value) {
  return h('pre', {}, typeof value === 'string' ? value : JSON.stringify(value, null, 2));
}

function shortKid(kid) {
  return h('span', { class: 'kid', title: kid }, kid);
}

function failed(error) {
  show(h('p', { class: 'bad' }, error.message || String(error)));
}

async function act(name, body, done) {
  try {
    const result = await api(name, null, body);
    toast(result.warning ? `Done, but ${result.warning}` : 'Done', Boolean(result.warning));
    if (done) done(result);
  } catch (e) {
    toast(e.message, true);
  }
}

function busy(button, work) {
  return async (event) => {
    event.preventDefault();
    button.disabled = true;
    try {
      await work();
    } finally {
      button.disabled = false;
    }
  };
}

// -------------------------------------------------------------------------------- queue --

async function viewQueue() {
  const data = await api('queue');
  const rows = data.items.map((it) =>
    h('tr', {},
      h('td', {}, h('span', { class: 'tag' }, it.kind)),
      h('td', {}, h('a', { href: `#review/${it.number}` }, it.title || `#${it.number}`), it.from_fork ? h('span', { class: 'bad' }, ' (fork)') : null),
      h('td', {}, shortKid(it.kid)),
      h('td', {}, when(it.opened)),
      h('td', { class: 'num' }, age(it.age_s))));
  show(
    h('h2', {}, `Open reviews (${data.items.length})`),
    data.items.length ? table(['Kind', 'Title', 'Kid', 'Opened', 'Age'], rows) : h('p', { class: 'muted' }, 'Nothing waiting for review.'),
  );
}

// ------------------------------------------------------------------------------- review --

async function viewReview(number) {
  const d = await api('review', { n: number });
  const it = d.item;
  const allOk = d.checks.every((c) => c.ok);

  const note = h('input', { type: 'text', maxlength: '200', placeholder: 'Why it is declined (the contributor sees the first 47 characters)' });
  const approve = h('button', { class: 'primary' }, 'Approve (squash merge)');
  approve.addEventListener('click', busy(approve, async () => {
    if (!confirm(`Publish "${it.title}"?${allOk ? '' : '\n\nSome checks failed.'}`)) return;
    await act('approve', { number: it.number, sha: it.head_sha }, () => (location.hash = '#queue'));
  }));
  const reject = h('button', { class: 'danger' }, 'Reject');
  reject.addEventListener('click', busy(reject, async () => {
    if (!note.value.trim()) return toast('Write a note first: the contributor is shown it.', true);
    if (!confirm(`Decline "${it.title}"?`)) return;
    await act('reject', { number: it.number, note: note.value }, () => (location.hash = '#queue'));
  }));
  const block = h('button', { class: 'danger' }, d.blocked ? 'Unblock this kid' : 'Block this kid');
  block.addEventListener('click', busy(block, async () => {
    if (!confirm(`${d.blocked ? 'Unblock' : 'Block'} ${it.kid}?`)) return;
    await act(d.blocked ? 'unblock' : 'block', { kid: it.kid, note: d.blocked ? undefined : `from review #${it.number}` }, () => route());
  }));

  const checks = h('ul', { class: 'checks' }, d.checks.map((c) =>
    h('li', {}, h('span', { class: c.ok ? 'ok' : 'bad' }, c.ok ? '✓ ' : '✗ '), h('strong', {}, c.check), ': ', c.detail)));
  if (d.listed && d.listed.check) {
    checks.append(h('li', {}, h('span', { class: d.listed.check.ok ? 'ok' : 'bad' }, d.listed.check.ok ? '✓ ' : '✗ '),
      h('strong', {}, 'entry to publish'), ': ', d.listed.check.detail));
  }

  const diff = d.diff
    ? table(['Field', 'Published', 'This review'], d.diff.map((r) =>
      h('tr', { class: r.change }, h('td', {}, r.field), h('td', { class: 'mono' }, r.before === null ? '' : JSON.stringify(r.before)),
        h('td', { class: 'mono' }, r.after === null ? '' : JSON.stringify(r.after)))))
    : h('p', { class: 'muted' }, 'Nothing published under this id yet: merging adds it.');

  const status = d.history.status;
  const statusRows = [];
  for (const section of ['coffees', 'grinders', 'machines', 'services']) {
    for (const e of (status && Array.isArray(status[section]) ? status[section] : [])) {
      statusRows.push(h('tr', {}, h('td', {}, section), h('td', { class: 'num' }, e.local_id), h('td', { class: 'num' }, e.record),
        h('td', {}, e.state), h('td', {}, e.library_id || ''), h('td', {}, e.note || '')));
    }
  }
  const pulls = d.history.pulls.map((p) =>
    h('tr', {}, h('td', {}, h('a', { href: p.url, target: '_blank', rel: 'noopener' }, `#${p.number}`)), h('td', {}, p.title),
      h('td', {}, p.state === 'open' ? 'open' : p.merged ? 'merged' : 'closed'), h('td', {}, when(p.opened))));

  show(
    h('p', {}, h('a', { href: '#queue' }, '← Queue')),
    h('h2', {}, it.title),
    h('div', { class: 'panel' },
      h('div', {}, h('span', { class: 'tag' }, it.kind), ' ', h('a', { href: it.url, target: '_blank', rel: 'noopener' }, `#${it.number} on GitHub`),
        ' · opened ', when(it.opened), ` (${age(it.age_s)} ago)`),
      h('div', {}, 'From ', shortKid(it.kid), d.blocked ? h('span', { class: 'bad' }, ' (blocked)') : null,
        ` · local id ${it.local_id ?? '?'}, record ${it.record ?? '?'}`),
      it.proposed_id ? h('div', {}, 'Proposed id ', h('code', {}, it.proposed_id)) : null,
      it.from_fork ? h('p', { class: 'bad' }, 'This comes from a fork: the review workflow would not record it.') : null),
    h('h3', {}, 'Checks, redone here'),
    checks,
    h('details', {}, h('summary', { class: 'muted' }, 'Checked only by the library tools, not here'),
      h('ul', {}, d.server_side_only.map((s) => h('li', { class: 'muted' }, s)))),
    h('h3', {}, 'Entry to publish', d.listed ? ` (${d.listed.path})` : ''),
    d.listed ? pretty(d.listed.entry) : h('p', { class: 'muted' }, 'No entry file in this pull request.'),
    h('h3', {}, 'Against the published entry'),
    diff,
    h('h3', {}, 'Signed record', d.signed ? ` (${d.signed.path})` : ''),
    d.signed ? pretty(d.signed.body) : h('p', { class: 'muted' }, 'Not found on main.'),
    h('h3', {}, 'Files in the pull request'),
    h('ul', {}, d.files.map((f) => h('li', {}, h('code', {}, f.path), ` (${f.status})`))),
    h('h3', {}, 'This kid, earlier'),
    statusRows.length ? table(['Section', 'Local id', 'Record', 'State', 'Library id', 'Note'], statusRows) : h('p', { class: 'muted' }, 'No status file.'),
    pulls.length ? table(['PR', 'Title', 'State', 'Opened'], pulls) : h('p', { class: 'muted' }, 'No other review pull requests.'),
    h('h3', {}, 'Decide'),
    h('div', { class: 'panel' },
      h('div', { class: 'row' }, approve),
      h('div', { class: 'row' }, note, reject),
      h('div', { class: 'row' }, block)),
  );
}

// ------------------------------------------------------------------------------- health --

async function viewHealth() {
  const d = await api('health');
  const runs = d.workflows.map((w) =>
    h('div', { class: 'panel' },
      h('h3', {}, w.name, ' ', h('code', {}, w.file), w.failures ? h('span', { class: 'bad' }, ` · ${w.failures} failed of the last ${w.runs.length}`) : null),
      w.error ? h('p', { class: 'warn' }, w.error)
        : w.runs.length ? table(['When', 'Event', 'Status', 'Result'], w.runs.map((r) =>
          h('tr', {}, h('td', {}, h('a', { href: r.url, target: '_blank', rel: 'noopener' }, when(r.created))), h('td', {}, r.event),
            h('td', {}, r.status), h('td', { class: r.conclusion === 'success' ? 'ok' : r.conclusion === 'failure' ? 'bad' : '' }, r.conclusion || '')))) : h('p', { class: 'muted' }, 'No runs.')));

  const days = [...new Set(d.counters.map((c) => c.day))];
  const counterRows = [];
  for (const day of days) {
    for (const c of d.counters.filter((x) => x.day === day)) {
      const cls = c.outcome.startsWith('ok') ? 'ok' : c.outcome === '429' ? 'warn' : 'bad';
      counterRows.push(h('tr', {}, h('td', {}, day), h('td', {}, c.path), h('td', { class: cls }, c.outcome), h('td', { class: 'num' }, c.n)));
    }
  }
  const kv = d.kv;
  const pct = (a, b) => `${Math.round((100 * a) / b)}%`;
  const perm = d.permissions || {};
  const need = [['pull_requests', 'write', 'approve and decline'], ['contents', 'write', 'records, hide'], ['actions', 'write', 'workflow runs, index rebuild']];
  show(
    h('h2', {}, 'GitHub App permissions'),
    h('div', { class: 'panel' }, ...need.map(([k, lvl, what]) => h('div', { class: perm[k] === lvl ? 'ok' : 'bad' },
      `${k}: ${perm[k] || 'none'}`, perm[k] === lvl ? '' : ` (needs ${lvl}, for ${what})`))),
    h('h2', {}, 'Workflows'),
    runs,
    h('h2', {}, 'Relay, last 7 days'),
    counterRows.length ? table(['Day', 'Path', 'Outcome', 'Count'], counterRows) : h('p', { class: 'muted' }, 'Nothing counted yet.'),
    h('h2', {}, 'KV, today (UTC)'),
    h('div', { class: 'panel' },
      h('div', {}, `Writes ≈ ${kv.writes_estimate} of ${kv.writes_free} free (${pct(kv.writes_estimate, kv.writes_free)})`),
      h('div', {}, `Reads ≈ ${kv.reads_estimate} of ${kv.reads_free} free (${pct(kv.reads_estimate, kv.reads_free)})`),
      h('div', {}, `Keys stored: ${kv.keys_stored} · blocked kids: ${kv.blocked}`),
      h('p', { class: 'muted' }, kv.note)),
  );
}

// ------------------------------------------------------------------------------ devices --

async function viewDevices() {
  const d = await api('devices');
  const keyRows = d.keys.items.map((k) => {
    const button = h('button', { class: k.blocked ? '' : 'danger' }, k.blocked ? 'Unblock' : 'Block');
    button.addEventListener('click', busy(button, async () => {
      if (!confirm(`${k.blocked ? 'Unblock' : 'Block'} ${k.kid}?`)) return;
      await act(k.blocked ? 'unblock' : 'block', { kid: k.kid }, () => route());
    }));
    return h('tr', {}, h('td', {}, shortKid(k.kid)), h('td', {}, k.first_seen ? when(k.first_seen) : h('span', { class: 'muted' }, 'before first-seen was kept')),
      h('td', {}, k.blocked ? h('span', { class: 'bad' }, 'blocked') : ''), h('td', {}, button));
  });
  const unitRows = d.units.items.map((u) =>
    h('tr', {}, h('td', {}, shortKid(u.kid)), h('td', {}, u.url ? h('a', { href: u.url, target: '_blank', rel: 'noopener' }, when(u.latest)) : when(u.latest)),
      h('td', {}, u.firmware || ''), h('td', { class: 'num' }, u.reports),
      h('td', {}, Object.entries(u.kinds).map(([k, n]) => `${k} ${n}`).join(', '))));
  show(
    h('h2', {}, `Library keys (${d.keys.total}${d.keys.complete ? '' : '+'})`),
    keyRows.length ? table(['Kid', 'First seen', '', ''], keyRows) : h('p', { class: 'muted' }, 'No keys yet.'),
    h('h2', {}, `Diagnostics units (${d.units.items.length})`),
    h('p', { class: 'muted' }, `Reports under reports/${d.units.months.join(' and reports/')}.`, d.units.note ? ` ${d.units.note}.` : ''),
    unitRows.length ? table(['Kid', 'Latest report', 'Firmware', 'Reports', 'Kinds'], unitRows) : h('p', { class: 'muted' }, 'No reports in these months.'),
  );
}

// ------------------------------------------------------------------------------- manage --

async function viewManage() {
  const [hidden, blocked] = await Promise.all([api('hidden'), api('blocked')]);

  const kind = h('select', {}, [['catalog', 'Coffee'], ['grinders', 'Grinder'], ['machines', 'Machine'], ['services', 'Service']].map(([v, t]) => h('option', { value: v }, t)));
  const id = h('input', { type: 'text', placeholder: 'id, e.g. portrait-coffee/idjwi-island' });
  const reason = h('input', { type: 'text', maxlength: '200', placeholder: 'Reason (kept in the audit log only)' });
  const hide = h('button', { class: 'danger' }, 'Hide');
  hide.addEventListener('click', busy(hide, async () => {
    const path = `${kind.value}/${id.value.trim().replace(/\.json$/, '')}.json`;
    if (!confirm(`Move ${path} to hidden/ and rebuild the index?`)) return;
    await act('hide', { path, reason: reason.value }, () => route());
  }));

  const hiddenRows = hidden.items.map((path) => {
    const button = h('button', {}, 'Restore');
    button.addEventListener('click', busy(button, async () => {
      if (!confirm(`Move ${path} back and rebuild the index?`)) return;
      await act('unhide', { path }, () => route());
    }));
    return h('tr', {}, h('td', {}, h('code', {}, path)), h('td', {}, button));
  });

  const kid = h('input', { type: 'text', placeholder: 'kid (20 hex digits)', maxlength: '20' });
  const why = h('input', { type: 'text', maxlength: '200', placeholder: 'Note' });
  const block = h('button', { class: 'danger' }, 'Block');
  block.addEventListener('click', busy(block, async () => {
    await act('block', { kid: kid.value.trim(), note: why.value }, () => route());
  }));
  const blockedRows = blocked.items.map((b) => {
    const button = h('button', {}, 'Unblock');
    button.addEventListener('click', busy(button, async () => {
      if (!confirm(`Unblock ${b.kid}?`)) return;
      await act('unblock', { kid: b.kid }, () => route());
    }));
    return h('tr', {}, h('td', {}, shortKid(b.kid)), h('td', {}, when(b.at)), h('td', {}, b.by || ''), h('td', {}, b.note || ''), h('td', {}, button));
  });

  show(
    h('h2', {}, 'Hide a published entry'),
    h('div', { class: 'panel' },
      h('p', { class: 'muted' }, 'Moves the file under hidden/ in one commit on main, then runs Intake, which rebuilds the index without it.'),
      h('div', { class: 'row' }, kind, id),
      h('div', { class: 'row' }, reason, hide)),
    h('h3', {}, `Hidden (${hidden.items.length})`),
    hiddenRows.length ? table(['File', ''], hiddenRows) : h('p', { class: 'muted' }, 'Nothing hidden.'),
    h('h2', {}, 'Block a kid'),
    h('div', { class: 'panel' },
      h('p', { class: 'muted' }, 'A blocked kid can send nothing more to the library through the relay (403).'),
      h('div', { class: 'row' }, kid, why, block)),
    h('h3', {}, `Blocked (${blocked.total})`),
    blockedRows.length ? table(['Kid', 'Since', 'By', 'Note', ''], blockedRows) : h('p', { class: 'muted' }, 'No one is blocked.'),
  );
}

// -------------------------------------------------------------------------------- audit --

async function viewAudit() {
  const action = h('select', {}, ['', 'refuse', 'approve', 'reject', 'hide', 'unhide', 'block', 'unblock'].map((a) => h('option', { value: a }, a || 'any action')));
  const result = h('select', {}, ['', 'ok', 'partial', 'failed', 'refused', '429'].map((r) => h('option', { value: r }, r || 'any result')));
  const actor = h('input', { type: 'text', placeholder: 'actor (email, or relay)' });
  const q = h('input', { type: 'text', placeholder: 'search target, error, detail' });
  const since = h('input', { type: 'date' });
  const search = h('button', { class: 'primary' }, 'Search');
  const more = h('button', {}, 'Older');
  const body = h('tbody', {});
  let oldest = null;

  async function load(append) {
    const params = { action: action.value, result: result.value, actor: actor.value.trim(), q: q.value.trim(), since: since.value, before: append ? oldest : '' };
    const d = await api('audit', params);
    if (!append) body.replaceChildren();
    for (const r of d.items) {
      const detail = r.detail && typeof r.detail === 'object' ? JSON.stringify(r.detail) : String(r.detail || '');
      const cls = r.result === 'ok' ? 'ok' : r.result === 'partial' || r.result === '429' ? 'warn' : 'bad';
      body.append(h('tr', {}, h('td', {}, when(r.ts)), h('td', {}, r.actor), h('td', {}, r.action), h('td', { class: 'mono' }, r.target),
        h('td', { class: cls }, r.result), h('td', {}, r.error), h('td', { class: 'mono' }, detail), h('td', { class: 'mono' }, r.ip_hash)));
      oldest = r.id;
    }
    more.disabled = !d.more;
  }
  search.addEventListener('click', busy(search, () => load(false).catch((e) => toast(e.message, true))));
  more.addEventListener('click', busy(more, () => load(true).catch((e) => toast(e.message, true))));

  show(
    h('h2', {}, 'Audit'),
    h('div', { class: 'row' }, action, result, actor, q, since, search),
    h('div', { class: 'scroll' }, h('table', {}, h('thead', {}, h('tr', {}, ['When', 'Actor', 'Action', 'Target', 'Result', 'Error', 'Detail', 'IP hash'].map((t) => h('th', {}, t)))), body)),
    h('div', { class: 'row' }, more),
  );
  await load(false);
}

// ------------------------------------------------------------------------------- routing --

async function route() {
  const hash = location.hash.replace(/^#/, '') || 'queue';
  const [name, arg] = hash.split('/');
  for (const a of document.querySelectorAll('nav a')) a.classList.toggle('on', a.getAttribute('href') === `#${name === 'review' ? 'queue' : name}`);
  show(h('p', { class: 'muted' }, 'Loading…'));
  try {
    if (name === 'review') await viewReview(Number(arg));
    else if (name === 'health') await viewHealth();
    else if (name === 'devices') await viewDevices();
    else if (name === 'manage') await viewManage();
    else if (name === 'audit') await viewAudit();
    else await viewQueue();
  } catch (e) {
    failed(e);
  }
}

window.addEventListener('hashchange', route);
route();
