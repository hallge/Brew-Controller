// The relay's admin console: /admin (pages) and /admin/api/* (JSON), behind Cloudflare Access.
//
// Every request must carry a Cloudflare Access JWT in Cf-Access-Jwt-Assertion, checked here against
// the team's signing keys (RS256), its audience (ACCESS_AUD) and its expiry. Anything else is 403,
// so the console stays shut on any hostname Access does not cover (workers.dev included).
//
// Environment (besides the relay's own):
//   ACCESS_TEAM   the Zero Trust team name: <team>.cloudflareaccess.com
//   ACCESS_AUD    the Access application's audience tag
//   AUDIT         D1 database: tables audit and counters (schema.sql)
//
// Every admin action and every relay refusal is written to AUDIT.audit. The relay's outcomes per day
// go to AUDIT.counters. Neither write ever stops the relay answering: a failed write is only logged.

import { appPermissions, base64ToBytes, es256Valid, githubCall, GITHUB_API, kidOf, sha256Hex } from './worker.js';
import { ADMIN_PAGES } from './admin-pages.js';

const encoder = new TextEncoder();
const decoder = new TextDecoder();

// ----------------------------------------------------------------------------- answers --

class AdminError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

function fail(status, message) {
  throw new AdminError(status, message);
}

function json(status, data) {
  return new Response(JSON.stringify(data), {
    status,
    headers: { 'content-type': 'application/json', 'cache-control': 'no-store' },
  });
}

function forbidden() {
  return new Response('Forbidden', { status: 403, headers: { 'content-type': 'text/plain', 'cache-control': 'no-store' } });
}

// --------------------------------------------------------------------- Cloudflare Access --

const CERTS_TTL_MS = 60 * 60 * 1000; // the team's keys are kept this long
const CERTS_RETRY_MS = 60 * 1000; // an unknown key id refetches them at most this often
const CLOCK_SKEW_S = 60;

let accessCache = null; // { team, at, keys: Map(kid -> CryptoKey) }

export function forgetAccessKeys() {
  accessCache = null;
}

function base64urlToBytes(text) {
  if (typeof text !== 'string' || !/^[A-Za-z0-9_-]*$/.test(text)) return null;
  const padded = text.replace(/-/g, '+').replace(/_/g, '/') + '='.repeat((4 - (text.length % 4)) % 4);
  return base64ToBytes(padded);
}

function configured(value) {
  return typeof value === 'string' && value !== '' && !value.startsWith('REPLACE_');
}

async function accessKeys(team, wantKid) {
  const now = Date.now();
  if (accessCache && accessCache.team === team) {
    const age = now - accessCache.at;
    if (age < CERTS_TTL_MS && (accessCache.keys.has(wantKid) || age < CERTS_RETRY_MS)) return accessCache.keys;
  }
  const response = await fetch(`https://${team}.cloudflareaccess.com/cdn-cgi/access/certs`);
  if (!response.ok) throw new Error(`Access certs: HTTP ${response.status}`);
  const data = await response.json();
  const keys = new Map();
  for (const jwk of Array.isArray(data?.keys) ? data.keys : []) {
    if (!jwk || jwk.kty !== 'RSA' || typeof jwk.kid !== 'string') continue;
    try {
      const key = await crypto.subtle.importKey(
        'jwk',
        { kty: 'RSA', n: jwk.n, e: jwk.e, alg: 'RS256', ext: true },
        { name: 'RSASSA-PKCS1-v1_5', hash: 'SHA-256' },
        false,
        ['verify'],
      );
      keys.set(jwk.kid, key);
    } catch {
      // a key we cannot use is skipped
    }
  }
  accessCache = { team, at: now, keys };
  return keys;
}

// The signed-in person's email, or null. Never throws.
export async function accessIdentity(request, env, nowMs = Date.now()) {
  try {
    if (!configured(env.ACCESS_TEAM) || !configured(env.ACCESS_AUD)) return null;
    if (!/^[A-Za-z0-9-]{1,63}$/.test(env.ACCESS_TEAM)) return null;
    const token = request.headers.get('cf-access-jwt-assertion');
    if (!token) return null;
    const parts = token.split('.');
    if (parts.length !== 3) return null;
    const [headerBytes, payloadBytes, signature] = parts.map(base64urlToBytes);
    if (!headerBytes || !payloadBytes || !signature) return null;
    const header = JSON.parse(decoder.decode(headerBytes));
    const claims = JSON.parse(decoder.decode(payloadBytes));
    if (!header || header.alg !== 'RS256' || typeof header.kid !== 'string') return null;
    if (!claims || typeof claims !== 'object') return null;

    const keys = await accessKeys(env.ACCESS_TEAM, header.kid);
    const key = keys.get(header.kid);
    if (!key) return null;
    const signed = encoder.encode(`${parts[0]}.${parts[1]}`);
    if (!(await crypto.subtle.verify('RSASSA-PKCS1-v1_5', key, signature, signed))) return null;

    const audiences = Array.isArray(claims.aud) ? claims.aud : [claims.aud];
    if (!audiences.includes(env.ACCESS_AUD)) return null;
    const now = Math.floor(nowMs / 1000);
    if (typeof claims.exp !== 'number' || now >= claims.exp) return null;
    if (typeof claims.nbf === 'number' && now + CLOCK_SKEW_S < claims.nbf) return null;
    if (claims.iss !== undefined && claims.iss !== `https://${env.ACCESS_TEAM}.cloudflareaccess.com`) return null;
    if (typeof claims.email !== 'string' || !claims.email.includes('@')) return null;
    return claims.email;
  } catch {
    return null;
  }
}

// ------------------------------------------------------------------------- D1: audit, counts --

async function ipHash(request) {
  const address = request.headers.get('cf-connecting-ip');
  return address ? (await sha256Hex(address)).slice(0, 16) : '';
}

export async function writeAudit(env, row) {
  if (!env.AUDIT) return;
  try {
    await env.AUDIT.prepare(
      'INSERT INTO audit (ts, actor, action, target, result, error, ip_hash, detail) VALUES (?, ?, ?, ?, ?, ?, ?, ?)',
    )
      .bind(
        row.ts ?? new Date(Date.now()).toISOString(),
        String(row.actor ?? ''),
        String(row.action ?? ''),
        String(row.target ?? ''),
        String(row.result ?? ''),
        String(row.error ?? ''),
        String(row.ip_hash ?? ''),
        JSON.stringify(row.detail ?? {}),
      )
      .run();
  } catch (e) {
    console.error(`audit write failed: ${e && e.message ? e.message : e}`);
  }
}

export async function countOutcome(env, path, outcome, now = new Date(Date.now())) {
  if (!env.AUDIT) return;
  try {
    await env.AUDIT.prepare(
      'INSERT INTO counters (day, path, outcome, n) VALUES (?, ?, ?, 1) ON CONFLICT (day, path, outcome) DO UPDATE SET n = n + 1',
    )
      .bind(now.toISOString().slice(0, 10), path, outcome)
      .run();
  } catch (e) {
    console.error(`counter write failed: ${e && e.message ? e.message : e}`);
  }
}

// Called by worker.js after /report and /library/put (and for refusals on any route).
// status: the answer's status; refusal: the message when it was a refusal; info: what the handler
// learnt (kid, type) before it answered.
export async function recordRelayOutcome(request, env, pathname, status, refusal, info) {
  const counted = pathname === '/report' || pathname === '/library/put';
  let outcome;
  if (status === 201) outcome = info.type === 'key' ? 'ok:key' : 'ok';
  else if (status === 429) outcome = '429';
  else if (refusal) outcome = `refused:${refusal}`;
  else outcome = 'error';
  if (counted) await countOutcome(env, pathname, outcome);
  if (refusal) {
    await writeAudit(env, {
      actor: 'relay',
      action: 'refuse',
      target: info.kid ?? '',
      result: status === 429 ? '429' : 'refused',
      error: refusal,
      ip_hash: await ipHash(request),
      detail: { path: pathname, status, kid: info.kid ?? null, ...(info.libraryPath ? { library_path: info.libraryPath } : {}) },
    });
  }
}

// --------------------------------------------------------------------------- GitHub calls --

function repoPath(path) {
  return path.split('/').map(encodeURIComponent).join('/');
}

async function gh(env, method, path, body) {
  const init = { method };
  if (body !== undefined) {
    init.headers = { 'content-type': 'application/json' };
    init.body = JSON.stringify(body);
  }
  const response = await githubCall(env, `${GITHUB_API}${path}`, init);
  const text = await response.text();
  let data = null;
  if (text) {
    try {
      data = JSON.parse(text);
    } catch {
      data = text;
    }
  }
  return { status: response.status, ok: response.ok, data };
}

function githubError(what, r) {
  const message = r.data && typeof r.data === 'object' && r.data.message ? ` (${r.data.message})` : '';
  return new AdminError(502, `${what}: GitHub answered ${r.status}${message}`);
}

async function ghOk(env, method, path, body, what) {
  const r = await gh(env, method, path, body);
  if (!r.ok) throw githubError(what, r);
  return r.data;
}

// A file's text at a ref, or null if there is none.
async function ghRaw(env, repo, path, ref) {
  const query = ref ? `?ref=${encodeURIComponent(ref)}` : ''; // no ref: the default branch
  const response = await githubCall(
    env,
    `${GITHUB_API}/repos/${repo}/contents/${repoPath(path)}${query}`,
    { method: 'GET' },
    'application/vnd.github.raw+json',
  );
  if (response.status === 404) return null;
  if (!response.ok) throw githubError(`read ${path}`, { status: response.status, data: null });
  return response.text();
}

function parseJson(text) {
  if (typeof text !== 'string') return null;
  try {
    return JSON.parse(text);
  } catch {
    return null;
  }
}

// ------------------------------------------------------------------------- review requests --

// What intake.py names a review branch: review/<kid>-<letter><local id>-<record>.
const REVIEW_BRANCH = /^review\/([0-9a-f]{20})-([a-z])(\d{1,10})-(\d{1,10})$/;
const LETTER_KIND = { c: 'coffee', g: 'grinder', m: 'machine', s: 'service', l: 'link' };
const KIND_FOLDER = { coffee: 'catalog/', grinder: 'grinders/', machine: 'machines/', service: 'services/', link: 'links/' };
const KIND_SECTION = { coffee: 'coffees', grinder: 'grinders', machine: 'machines', service: 'services', link: 'links' };

function intakeMeta(body) {
  const match = /<!-- intake (\{.*?\}) -->/.exec(body ?? '');
  const meta = match ? parseJson(match[1]) : null;
  return meta && typeof meta === 'object' && !Array.isArray(meta) ? meta : {};
}

function describePull(pr, repo, nowMs) {
  const ref = pr.head?.ref ?? '';
  const branch = REVIEW_BRANCH.exec(ref);
  const meta = intakeMeta(pr.body);
  const letter = branch ? branch[2] : '';
  const kind = typeof meta.kind === 'string' && /^[a-z_]{1,20}$/.test(meta.kind) ? meta.kind : LETTER_KIND[letter] ?? 'unknown';
  const opened = pr.created_at ?? null;
  return {
    number: pr.number,
    title: pr.title ?? '',
    kind,
    kid: branch ? branch[1] : typeof meta.kid === 'string' ? meta.kid : '',
    letter,
    local_id: branch ? Number(branch[3]) : null,
    record: branch ? Number(branch[4]) : null,
    proposed_id: typeof meta.library_id === 'string' ? meta.library_id : '',
    branch: ref,
    opened,
    age_s: opened ? Math.max(0, Math.floor((nowMs - Date.parse(opened)) / 1000)) : null,
    url: pr.html_url ?? '',
    from_fork: pr.head?.repo?.full_name !== repo,
    state: pr.state,
    merged: Boolean(pr.merged_at),
    closed: pr.closed_at ?? null,
  };
}

async function apiQueue(env) {
  const repo = env.LIBRARY_REPO;
  const pulls = await ghOk(env, 'GET', `/repos/${repo}/pulls?state=open&per_page=100`, undefined, 'open pull requests');
  const now = Date.now();
  const items = (Array.isArray(pulls) ? pulls : [])
    .filter((pr) => (pr.head?.ref ?? '').startsWith('review/'))
    .map((pr) => describePull(pr, repo, now))
    .sort((a, b) => String(a.opened).localeCompare(String(b.opened)));
  return { items };
}

// The open review pull request numbered n, or a refusal.
async function reviewPull(env, n) {
  if (!Number.isInteger(n) || n < 1) fail(400, 'give a pull request number');
  const r = await gh(env, 'GET', `/repos/${env.LIBRARY_REPO}/pulls/${n}`);
  if (r.status === 404) fail(404, 'no such pull request');
  if (!r.ok) throw githubError('pull request', r);
  const pr = r.data;
  if (!(pr.head?.ref ?? '').startsWith('review/')) fail(400, 'not a review pull request');
  return pr;
}

// ------------------------------------------------- record checks (tools/verify_record.py) --
//
// A port of verify_record.py's checks, run again on the signed record behind a review. What is
// not redone here is listed in SERVER_SIDE_ONLY and shown with every review.

export const SERVER_SIDE_ONLY = [
  'Similar entries already in the library (a fuzzy name match over catalog/, grinders/, machines/, services/): intake.py writes them into the pull request text.',
  'Whether the contributor has sent a newer version since (record numbers in status/<kid>.json): intake.py set_status.',
  "The contributor's measurement count (dialins/, shots/): intake.py, in the pull request text.",
  "The index builder's own checks when the entry is published (build_index.py).",
  'Keys: the library checks against its keys/ folder. Here the key comes from the relay\'s KEYS first, then keys/<kid>.json on main (checked as a key record).',
  'Python and JavaScript differ slightly at the edges: Python accepts 2.0 as a whole number where JavaScript cannot tell 2.0 from 2, and accepts non-ASCII lowercase letters in a service id where this port does not.',
];

class Bad extends Error {}

function bad(message) {
  throw new Bad(message);
}

const TYPES = ['key', 'coffee', 'grinder', 'machine', 'dialin', 'shot', 'taste', 'service', 'service_review', 'link'];
// The Community Hub's links (library-template/tools/verify_record.py LINK_KINDS, LINK_TOPICS).
const LINK_KINDS = ['video', 'playlist', 'post', 'guide', 'article', 'podcast'];
const LINK_TOPICS = ['preparation', 'dialing_in', 'brewing', 'beans', 'gear', 'milk', 'maintenance', 'science'];
const CUP = ['acidity', 'sweetness', 'body', 'bitterness', 'finish'];
const FLAVOURS = ['fruity', 'floral', 'sweet', 'chocolate', 'spicy', 'roasty', 'fermented', 'earthy'];
const MACHINE_STYLES = ['toggle_button', 'start_button', 'manual', 'start_stop_buttons', 'hold_button'];
const SERVICE_KINDS = ['coffee', 'supplies', 'other'];
const SERVICE_STYLES = ['single_roaster', 'multi_roaster', 'curated', 'matched', 'single_origin', 'blends', 'decaf', 'espresso', 'filters', 'cleaning', 'equipment'];
const SERVICE_SOURCING = ['fair_trade', 'direct_trade', 'organic', 'farm_named', 'prices_published', 'sustainable_packaging', 'carbon_neutral', 'b_corp'];
const SERVICE_PLATFORMS = ['recharge', 'ordergroove', 'skio', 'loop', 'appstle', 'bold', 'shopify', 'own'];
const SERVICE_ASPECTS = ['value', 'quality', 'consistency', 'reliability', 'flexibility', 'transparency'];
const DIVISORS = [1, 2, 4, 5, 10, 20, 25, 50, 100];
const ROAST_LEVELS = ['light', 'medium-light', 'medium', 'medium-dark', 'dark'];

const has = (obj, name) => obj !== null && typeof obj === 'object' && Object.hasOwn(obj, name);
const isNumber = (v) => typeof v === 'number' && Number.isFinite(v);
const isInt = (v) => typeof v === 'number' && Number.isInteger(v);
const asObject = (v) => (v && typeof v === 'object' && !Array.isArray(v) ? v : {});
const shown = (v) => (typeof v === 'string' ? `'${v}'` : JSON.stringify(v));
// Python's str.isupper() on ASCII: at least one capital and no small letter.
const asciiUpper = (v, length) => typeof v === 'string' && v.length === length && /^[\x00-\x7f]*$/.test(v) && /[A-Z]/.test(v) && !/[a-z]/.test(v);

function number(obj, name, lo, hi, required = true) {
  if (!has(obj, name)) {
    if (required) bad(`${name} is missing`);
    return;
  }
  const value = obj[name];
  if (!isNumber(value) || !(lo <= value && value <= hi)) bad(`${name} = ${shown(value)} is outside ${lo} to ${hi}`);
}

function text(obj, name, limit, required = true) {
  if (!has(obj, name)) {
    if (required) bad(`${name} is missing`);
    return;
  }
  const value = obj[name];
  if (typeof value !== 'string' || !value || value.length > limit) bad(`${name} is not text of 1 to ${limit} characters`);
  if (/[^\x20-\x7e]/.test(value)) bad(`${name} has characters the controller cannot have sent`);
}

function checkCoffee(c, full = true) {
  text(c, 'name', 23);
  for (const [name, limit] of [['roaster', 23], ['origin', 23], ['region', 31], ['producer', 31], ['varietal', 31], ['process', 15], ['notes', 47]]) {
    text(c, name, limit, false);
  }
  if (has(c, 'roast_level') && !ROAST_LEVELS.includes(c.roast_level)) bad('roast_level is not one the controller offers');
  number(c, 'altitude_m', 1, 4000, false);
  text(c, 'library_id', 39, false);
  if (full) number(c, 'local_id', 1, 65535);
}

function checkRecipe(r) {
  number(r, 'dose_g', 5, 30);
  number(r, 'yield_g', 5, 200);
  number(r, 'target_time_s', 5, 60, false);
  number(r, 'preinfusion_hold_s', 0.3, 30, false);
  number(r, 'bloom_pause_s', 1, 45, false);
}

function checkReading(g) {
  const divisor = has(g, 'divisor') ? g.divisor : 1;
  const subSteps = has(g, 'sub_steps') ? g.sub_steps : 0;
  if (typeof divisor === 'boolean' || !DIVISORS.includes(divisor)) bad('grinder.divisor is not one the controller offers');
  if (!isInt(subSteps) || !(subSteps === 0 || (subSteps >= 2 && subSteps <= 99))) bad('grinder.sub_steps is outside 2 to 99');
  if (subSteps && divisor !== 1) bad('a ringed dial has no divisor');
}

function checkGrinder(g) {
  checkReading(g);
  text(g, 'unit', 7);
  text(g, 'model', 31, false);
  text(g, 'burrs', 23, false);
  number(g, 'model_id', 1, 65535, false);
  text(g, 'library_id', 39, false);
  number(g, 'um_per_unit', 0.01, 1000, false);
  number(g, 'calibration', 0, 65535);
}

// The link rules: https, printable, no spaces, at most 199 characters (255 for pictures); nothing of
// one customer's own (no query or fragment, no token-like path segment; no credential in a picture's query).
function link(obj, name, required = true, hub = false) {
  if (!has(obj, name)) {
    if (required) bad(`${name} is missing`);
    return;
  }
  const value = obj[name];
  const limit = name === 'image' || name === 'icon' ? 255 : 199;
  if (typeof value !== 'string' || !value.startsWith('https://') || value.length > limit || /[^\x21-\x7e]/.test(value)) {
    bad(`${name} is not an https link of at most ${limit} characters`);
  }
  if (name === 'image' || name === 'icon' || hub) { // pictures and Hub links (a video's ?v=) keep a query
    const at = value.indexOf('?');
    const query = at < 0 ? '' : value.slice(at + 1).toLowerCase();
    if (['token', 'key=', 'sig', 'auth', 'session', 'password'].some((word) => query.includes(word))) {
      bad(`${name} carries what looks like a credential`);
    }
  } else {
    if (value.includes('?') || value.includes('#')) bad(`${name} has a query or fragment: links are to a shop's general pages`);
    const rest = value.slice('https://'.length);
    const slash = rest.indexOf('/');
    const path = slash < 0 ? '' : rest.slice(slash + 1);
    for (const segment of path.split('/')) {
      const mixed = /[0-9]/.test(segment) && /[A-Za-z]/.test(segment);
      if (segment.toLowerCase().includes('token') || (segment.length >= 16 && mixed)) bad(`${name} looks like one customer's own link`);
    }
  }
}

function choices(obj, name, allowed, limit) {
  if (!has(obj, name)) return;
  const value = obj[name];
  if (!Array.isArray(value) || !value.length || value.length > limit || new Set(value).size !== value.length || value.some((v) => !allowed.includes(v))) {
    bad(`${name} is not a list taken from the controller's own`);
  }
}

function checkLink(l, full = true) {
  link(l, 'url', true, true);
  text(l, 'title', 79);
  if (!LINK_KINDS.includes(l.kind)) bad('kind is not one the controller offers');
  if (!has(l, 'topics')) bad('topics is missing');
  choices(l, 'topics', LINK_TOPICS, 3);
  text(l, 'why', 160, false);
  link(l, 'image', false);
  if (full) {
    number(l, 'local_id', 1, 65535);
    number(l, 'record', 0, 4294967295);
  }
}

function checkService(s, full = true) {
  text(s, 'name', 47);
  link(s, 'url');
  link(s, 'manage_url', false);
  if (has(s, 'platform') && !SERVICE_PLATFORMS.includes(s.platform)) bad('platform is not one the controller knows');
  if (!SERVICE_KINDS.includes(s.kind)) bad('kind is not coffee, supplies or other');
  if (has(s, 'ships_to')) {
    const ships = s.ships_to;
    if (!Array.isArray(ships) || !ships.length || ships.length > 10 || new Set(ships).size !== ships.length || ships.some((c) => !asciiUpper(c, 2))) {
      bad('ships_to is not a list of two-letter country codes');
    }
  }
  if (has(s, 'cadences')) {
    const cadences = s.cadences;
    if (!Array.isArray(cadences) || !cadences.length || cadences.length > 6 || cadences.some((d) => !isInt(d) || d < 1 || d > 366)) {
      bad('cadences are not days from 1 to 366');
    }
  }
  if (has(s, 'price_from_cents') !== has(s, 'currency')) bad('a typical price needs its currency, and a currency its price');
  if (has(s, 'price_from_cents')) {
    number(s, 'price_from_cents', 1, 1000000);
    if (!asciiUpper(s.currency, 3)) bad('currency is not a three-letter code');
  }
  choices(s, 'style', SERVICE_STYLES, SERVICE_STYLES.length);
  choices(s, 'sourcing', SERVICE_SOURCING, SERVICE_SOURCING.length);
  link(s, 'image', false);
  link(s, 'icon', false);
  if (has(s, 'color') && !(typeof s.color === 'string' && /^#[0-9a-fA-F]{6}$/.test(s.color))) bad('color is not #rrggbb');
  text(s, 'about', 160, false);
  if (full) {
    number(s, 'local_id', 1, 65535);
    number(s, 'record', 0, 4294967295);
  }
}

function serviceId(obj, name) {
  const value = has(obj, name) ? obj[name] : undefined;
  if (typeof value !== 'string' || !value || value.length > 39 || !/^[a-z0-9-]+$/.test(value)) bad(`${name} is not the id of a listed service`);
}

function checkMachine(m, full = true) {
  if (!MACHINE_STYLES.includes(m.style)) bad('machine.style is not one the controller offers');
  text(m, 'maker', 23, full);
  text(m, 'model', 31, full);
  text(m, 'control', 31, full);
  text(m, 'library_id', 39, false);
  if (has(m, 'clock_from') && m.clock_from !== 'first_drip') bad('machine.clock_from is not first_drip');
  if (full) {
    if (has(m, 'manual_start') && !['tap', 'first_drip'].includes(m.manual_start)) bad('manual_start is neither tap nor first_drip');
    for (const flag of ['hold_preinfusion', 'tap_preinfusion', 'power_toggle']) number(m, flag, 0, 1);
    number(m, 'self_stop_s', 1, 600, false);
    if (m.style !== 'toggle_button' && m.hold_preinfusion) bad('only a button the bot holds can hold pre-infusion');
  }
}

export function checkBody(body) {
  const kind = body.t;
  if (!TYPES.includes(kind)) bad(`unknown record type ${shown(kind)}`);
  if (body.v !== 1) bad('unknown record version');
  if (kind === 'coffee') {
    checkCoffee(body);
    number(body, 'record', 0, 4294967295);
  } else if (kind === 'grinder') {
    text(body, 'model', 31);
    text(body, 'burrs', 23, false);
    text(body, 'unit', 7);
    checkReading(body);
    number(body, 'um_per_unit', 0.01, 1000, false);
    number(body, 'local_id', 1, 65535);
    number(body, 'record', 0, 4294967295);
  } else if (kind === 'machine') {
    checkMachine(body);
    number(body, 'local_id', 1, 65535);
    number(body, 'record', 0, 4294967295);
  } else if (kind === 'dialin') {
    checkCoffee(asObject(body.coffee));
    checkRecipe(asObject(body.recipe));
    checkGrinder(asObject(body.grinder));
    number(body, 'grind', 1, 2000);
    number(body, 'frozen_days', 0, 3650, false);
    const result = asObject(body.result);
    number(result, 'time_s', 5, 90);
    number(result, 'shots', 1, 10);
    if (!['total', 'pour'].includes(result.basis)) bad('result.basis is neither total nor pour');
  } else if (kind === 'service') {
    checkService(body);
  } else if (kind === 'link') {
    checkLink(body);
  } else if (kind === 'service_review') {
    number(body, 'id', 1, 4294967295);
    serviceId(body, 'service');
    number(body, 'stars', 1, 5);
    number(body, 'drinker', 0, 8);
    const aspects = has(body, 'aspects') ? body.aspects : {};
    if (!aspects || typeof aspects !== 'object' || Array.isArray(aspects)) bad('aspects is not an object');
    for (const name of Object.keys(aspects)) {
      if (!SERVICE_ASPECTS.includes(name)) bad(`aspects.${name} is not one the controller asks for`);
      number(aspects, name, 1, 5);
    }
  } else if (kind === 'taste') {
    if (has(body, 'service')) serviceId(body, 'service');
    const coffee = body.coffee;
    if (!coffee || typeof coffee !== 'object' || Array.isArray(coffee)) bad('coffee is missing');
    checkCoffee(coffee, has(coffee, 'local_id'));
    number(body, 'id', 1, 4294967295);
    number(body, 'stars', 1, 5);
    number(body, 'drinker', 0, 8);
    const cup = has(body, 'cup') ? body.cup : {};
    if (!cup || typeof cup !== 'object' || Array.isArray(cup)) bad('cup is not an object');
    for (const name of Object.keys(cup)) {
      if (!CUP.includes(name)) bad(`cup.${name} is not one the controller asks for`);
      number(cup, name, 1, 5);
    }
    const flavours = has(body, 'flavours') ? body.flavours : [];
    if (!Array.isArray(flavours) || flavours.some((f) => !FLAVOURS.includes(f)) || new Set(flavours).size !== flavours.length) {
      bad("flavours are not the controller's flavour families");
    }
  } else if (kind === 'shot') {
    if (has(body, 'coffee')) checkCoffee(asObject(body.coffee), has(body.coffee, 'local_id'));
    checkRecipe(asObject(body.recipe));
    checkGrinder(asObject(body.grinder));
    number(body, 'grind', 1, 2000, false);
    number(body, 'frozen_days', 0, 3650, false);
    const result = asObject(body.result);
    number(result, 'yield_g', 0, 1000);
    number(result, 'time_s', 0, 90);
    number(result, 'pour_flow_gs', 0, 100, false);
    number(result, 'ratio', 0, 40, false);
    if (has(body, 'machine')) checkMachine(asObject(body.machine), false);
  }
  return kind;
}

// The entry a review would publish (the pull request's file), checked as the library lists it.
export function checkListed(kind, entry) {
  if (!entry || typeof entry !== 'object' || Array.isArray(entry)) bad('not a JSON object');
  if (typeof entry.id !== 'string' || !/^[a-z0-9][a-z0-9_-]*(\/[a-z0-9][a-z0-9_-]*)?$/.test(entry.id)) bad('id is not one the library uses');
  if (kind === 'coffee') checkCoffee(entry, false);
  else if (kind === 'grinder') {
    text(entry, 'model', 31);
    text(entry, 'burrs', 23, false);
    text(entry, 'unit', 7);
    checkReading(entry);
    number(entry, 'um_per_unit', 0.01, 1000, false);
  } else if (kind === 'machine') checkMachine(entry, true);
  else if (kind === 'service') checkService(entry, false);
  else if (kind === 'link') checkLink(entry, false);
}

// The public key for kid: the relay's KEYS first, then the library's keys/<kid>.json (itself a key
// record, so checked as one). Returns { pub, source } or null.
async function keyFor(env, kid) {
  const stored = await env.KEYS.get(kid);
  if (stored !== null) {
    const pub = base64ToBytes(stored);
    if (pub) return { pub, source: 'the relay (KEYS)' };
  }
  const text = await ghRaw(env, env.LIBRARY_REPO, `keys/${kid}.json`, 'main');
  const envelope = parseJson(text);
  const body = parseJson(envelope?.body);
  if (!body || body.t !== 'key' || body.alg !== 'ES256' || envelope.kid !== kid) return null;
  const pub = base64ToBytes(body.key);
  if (!pub || pub.length !== 65 || pub[0] !== 4 || (await kidOf(pub)) !== kid) return null;
  if (!(await es256Valid(pub, envelope.sig, envelope.body))) return null;
  return { pub, source: `the library (keys/${kid}.json)` };
}

// verify_record.py verify(), step by step. Returns { checks: [{check, ok, detail}], body }.
export async function verifyRecord(env, envelopeText) {
  const checks = [];
  const step = (check, ok, detail = '') => {
    checks.push({ check, ok, detail });
    return ok;
  };
  const envelope = parseJson(envelopeText);
  if (!step('envelope', Boolean(envelope && typeof envelope === 'object' && !Array.isArray(envelope) && envelope.v === 1), 'a version 1 envelope')) {
    return { checks, body: null };
  }
  const { kid, alg, sig, body: bodyText } = envelope;
  if (!step('fields', [kid, alg, sig, bodyText].every((v) => typeof v === 'string'), 'kid, alg, sig and body are text')) return { checks, body: null };
  if (!step('algorithm', alg === 'ES256', alg === 'ES256' ? 'ES256' : `signed with '${alg}', not ES256`)) return { checks, body: null };
  const body = parseJson(bodyText);
  if (!step('body', Boolean(body && typeof body === 'object' && !Array.isArray(body)), 'the body is a JSON object')) return { checks, body: null };

  let pub = null;
  if (body.t === 'key') {
    const raw = base64ToBytes(body.key);
    const ok = body.alg === 'ES256' && raw && raw.length === 65 && raw[0] === 4 && (await kidOf(raw)) === kid;
    if (!step('key', Boolean(ok), ok ? 'the kid is the fingerprint of the key it carries' : 'the kid is not the fingerprint of a P-256 key it carries')) {
      return { checks, body };
    }
    pub = raw;
  } else {
    const found = /^[0-9a-f]{20}$/.test(kid) ? await keyFor(env, kid) : null;
    if (!step('key', Boolean(found), found ? `on file in ${found.source}` : `key ${kid} is not on file`)) return { checks, body };
    pub = found.pub;
  }
  if (!step('signature', await es256Valid(pub, sig, bodyText), 'ECDSA P-256 over SHA-256 of the body as sent')) return { checks, body };
  try {
    const kind = checkBody(body);
    step('values', true, `a ${kind} record inside the firmware's ranges and link rules`);
  } catch (e) {
    if (!(e instanceof Bad)) throw e;
    step('values', false, e.message);
  }
  return { checks, body, kid };
}

function fieldDiff(before, after) {
  const a = asObject(before);
  const b = asObject(after);
  const fields = [...new Set([...Object.keys(a), ...Object.keys(b)])].sort();
  return fields.map((field) => {
    const was = Object.hasOwn(a, field) ? a[field] : undefined;
    const now = Object.hasOwn(b, field) ? b[field] : undefined;
    const change = was === undefined ? 'added' : now === undefined ? 'removed' : JSON.stringify(was) === JSON.stringify(now) ? 'same' : 'changed';
    return { field, before: was ?? null, after: now ?? null, change };
  });
}

async function apiReview(env, url) {
  const repo = env.LIBRARY_REPO;
  const pr = await reviewPull(env, Number(url.searchParams.get('n')));
  const item = describePull(pr, repo, Date.now());
  const files = await ghOk(env, 'GET', `/repos/${repo}/pulls/${pr.number}/files?per_page=30`, undefined, 'pull request files');

  // The entry it would publish, as the pull request has it now (the reviewer may have edited it).
  const folder = KIND_FOLDER[item.kind];
  const shownFiles = [];
  let listedPath = null;
  let listed = null;
  for (const f of (Array.isArray(files) ? files : []).slice(0, 10)) {
    const entry = { path: f.filename, status: f.status };
    if (folder && f.filename.startsWith(folder) && f.filename.endsWith('.json') && f.status !== 'removed' && !listedPath) {
      listedPath = f.filename;
      entry.text = await ghRaw(env, repo, f.filename, pr.head.sha);
      listed = parseJson(entry.text);
    }
    shownFiles.push(entry);
  }

  // The signed record behind it: pending/<section>/<kid>/<letter><local>-<record>.json on main.
  let signed = null;
  let verification = { checks: [{ check: 'record', ok: false, detail: 'no signed record found for this review' }], body: null };
  const section = KIND_SECTION[item.kind];
  if (section && item.kid && item.local_id !== null) {
    const path = `pending/${section}/${item.kid}/${item.letter}${item.local_id}-${item.record}.json`;
    const text = await ghRaw(env, repo, path, 'main');
    if (text !== null) {
      signed = { path, text };
      verification = await verifyRecord(env, text);
      const body = verification.body;
      if (body) {
        const same = verification.kid === item.kid && body.t === item.kind && body.local_id === item.local_id && body.record === item.record;
        verification.checks.push({
          check: 'matches the review',
          ok: same,
          detail: same ? 'kid, type, local id and record match the branch' : 'the record is not the one the branch names',
        });
      }
    }
  }

  let listedCheck = null;
  if (listedPath) {
    try {
      checkListed(item.kind, listed);
      listedCheck = { ok: true, detail: 'the entry is inside the ranges and link rules' };
    } catch (e) {
      if (!(e instanceof Bad)) throw e;
      listedCheck = { ok: false, detail: e.message };
    }
  }

  // The published entry at the same path, if any, and what merging would change.
  let published = null;
  let diff = null;
  if (listedPath) {
    const text = await ghRaw(env, repo, listedPath, 'main');
    if (text !== null) {
      published = { path: listedPath, entry: parseJson(text) };
      diff = fieldDiff(published.entry, listed);
    }
  }

  // The same kid's earlier records: its status file, and its other review pull requests.
  let status = null;
  let others = [];
  if (item.kid) {
    status = parseJson(await ghRaw(env, repo, `status/${item.kid}.json`, 'main'));
    const all = await ghOk(env, 'GET', `/repos/${repo}/pulls?state=all&per_page=100&sort=created&direction=desc`, undefined, 'earlier pull requests');
    const now = Date.now();
    others = (Array.isArray(all) ? all : [])
      .filter((p) => p.number !== pr.number && (p.head?.ref ?? '').startsWith(`review/${item.kid}-`))
      .map((p) => describePull(p, repo, now));
  }
  const blocked = item.kid ? (await env.LIMITS.get(`blocked:${item.kid}`)) !== null : false;

  return {
    item: { ...item, head_sha: pr.head?.sha ?? '', mergeable: pr.mergeable ?? null, body: pr.body ?? '' },
    files: shownFiles,
    listed: listedPath ? { path: listedPath, entry: listed, check: listedCheck } : null,
    signed: signed ? { path: signed.path, body: verification.body } : null,
    checks: verification.checks,
    server_side_only: SERVER_SIDE_ONLY,
    published,
    diff,
    history: { status, pulls: others },
    blocked,
  };
}

// ------------------------------------------------------------------------------- actions --

async function readAction(request) {
  let data;
  try {
    data = await request.json();
  } catch {
    fail(400, 'expected a JSON object');
  }
  if (!data || typeof data !== 'object' || Array.isArray(data)) fail(400, 'expected a JSON object');
  return data;
}

function cleanNote(note, limit) {
  return String(note ?? '')
    .replace(/[\x00-\x1f\x7f]+/g, ' ')
    .trim()
    .slice(0, limit);
}

async function actApprove(env, data) {
  const pr = await reviewPull(env, Number(data.number));
  const target = `#${pr.number}`;
  if (pr.state !== 'open') fail(409, 'this pull request is not open');
  if (pr.head?.repo?.full_name !== env.LIBRARY_REPO) fail(400, 'from a fork: the review workflow would not record it');
  if (data.sha !== undefined && data.sha !== pr.head.sha) fail(409, 'the pull request has changed since you looked: reload it');
  const r = await gh(env, 'PUT', `/repos/${env.LIBRARY_REPO}/pulls/${pr.number}/merge`, { merge_method: 'squash', sha: pr.head.sha });
  if (!r.ok || !r.data?.merged) throw githubError('merge', r);
  return { target, detail: { title: pr.title, branch: pr.head.ref, sha: r.data.sha } };
}

async function actReject(env, data) {
  const note = cleanNote(data.note, 200);
  if (!note) fail(400, 'give a note: it is shown to the contributor (the first 47 characters)');
  const pr = await reviewPull(env, Number(data.number));
  if (pr.state !== 'open') fail(409, 'this pull request is not open');
  await ghOk(env, 'POST', `/repos/${env.LIBRARY_REPO}/issues/${pr.number}/comments`, { body: `Declined: ${note}` }, 'comment');
  await ghOk(env, 'PATCH', `/repos/${env.LIBRARY_REPO}/pulls/${pr.number}`, { state: 'closed' }, 'close (the Declined comment is posted)');
  return { target: `#${pr.number}`, detail: { title: pr.title, branch: pr.head.ref, note } };
}

// What may be hidden: a published coffee, grinder, machine or service file.
const PUBLISHED_PATH = /^(?:catalog\/[a-z0-9][a-z0-9_-]{0,63}\/[a-z0-9][a-z0-9_-]{0,63}|(?:grinders|machines|services|links)\/[a-z0-9][a-z0-9_-]{0,63})\.json$/;
const INDEX_WORKFLOW = 'intake.yml'; // rebuilds the index (and takes in the inbox) on workflow_dispatch

// One commit on main that moves a file: Git Data API, retried once if main moved meanwhile.
async function moveOnMain(env, from, to, message) {
  const repo = env.LIBRARY_REPO;
  for (let attempt = 0; attempt < 2; attempt++) {
    const ref = await ghOk(env, 'GET', `/repos/${repo}/git/ref/heads/main`, undefined, 'main');
    const head = ref.object.sha;
    const commit = await ghOk(env, 'GET', `/repos/${repo}/git/commits/${head}`, undefined, 'main commit');
    const file = await gh(env, 'GET', `/repos/${repo}/contents/${repoPath(from)}?ref=${head}`);
    if (file.status === 404) fail(404, `${from} is not on main`);
    if (!file.ok || !file.data?.sha) throw githubError(`read ${from}`, file);
    const tree = await ghOk(env, 'POST', `/repos/${repo}/git/trees`, {
      base_tree: commit.tree.sha,
      tree: [
        { path: to, mode: '100644', type: 'blob', sha: file.data.sha },
        { path: from, mode: '100644', type: 'blob', sha: null },
      ],
    }, 'tree');
    const made = await ghOk(env, 'POST', `/repos/${repo}/git/commits`, { message, tree: tree.sha, parents: [head] }, 'commit');
    const moved = await gh(env, 'PATCH', `/repos/${repo}/git/refs/heads/main`, { sha: made.sha, force: false });
    if (moved.ok) return made.sha;
    if (moved.status !== 422 || attempt === 1) throw githubError('update main', moved);
  }
  return null;
}

async function rebuildIndex(env) {
  const r = await gh(env, 'POST', `/repos/${env.LIBRARY_REPO}/actions/workflows/${INDEX_WORKFLOW}/dispatches`, { ref: 'main' });
  return r.ok ? '' : `the index rebuild did not start: GitHub answered ${r.status}`;
}

async function actHide(env, data) {
  const path = String(data.path ?? '');
  if (!PUBLISHED_PATH.test(path)) fail(400, 'give a published file: catalog/<roaster>/<name>.json, grinders/<id>.json, machines/<id>.json or services/<id>.json');
  // The library is public: the commit names no one.
  const sha = await moveOnMain(env, path, `hidden/${path}`, `Hide ${path} (admin console)`);
  const warning = await rebuildIndex(env);
  return { target: path, detail: { commit: sha, reason: cleanNote(data.reason, 200), workflow: INDEX_WORKFLOW }, warning };
}

async function actUnhide(env, data) {
  const path = String(data.path ?? '');
  if (!path.startsWith('hidden/') || !PUBLISHED_PATH.test(path.slice('hidden/'.length))) fail(400, 'give a hidden file: hidden/<published path>');
  const back = path.slice('hidden/'.length);
  const sha = await moveOnMain(env, path, back, `Restore ${back} (admin console)`);
  const warning = await rebuildIndex(env);
  return { target: back, detail: { commit: sha, workflow: INDEX_WORKFLOW }, warning };
}

function readKid(data) {
  const kid = String(data.kid ?? '');
  if (!/^[0-9a-f]{20}$/.test(kid)) fail(400, 'a kid is 20 hex digits');
  return kid;
}

async function actBlock(env, data, actor) {
  const kid = readKid(data);
  const note = cleanNote(data.note, 200);
  await env.LIMITS.put(`blocked:${kid}`, JSON.stringify({ at: new Date(Date.now()).toISOString(), by: actor, note }));
  return { target: kid, detail: { note } };
}

async function actUnblock(env, data) {
  const kid = readKid(data);
  await env.LIMITS.delete(`blocked:${kid}`);
  return { target: kid, detail: {} };
}

const ACTIONS = {
  approve: actApprove,
  reject: actReject,
  hide: actHide,
  unhide: actUnhide,
  block: actBlock,
  unblock: actUnblock,
};

// ------------------------------------------------------------------------------ the views --

async function apiHidden(env) {
  const repo = env.LIBRARY_REPO;
  const root = await gh(env, 'GET', `/repos/${repo}/git/trees/main`);
  if (!root.ok) throw githubError('main tree', root);
  const hidden = (root.data?.tree ?? []).find((e) => e.path === 'hidden' && e.type === 'tree');
  if (!hidden) return { items: [] };
  const tree = await ghOk(env, 'GET', `/repos/${repo}/git/trees/${hidden.sha}?recursive=1`, undefined, 'hidden tree');
  const items = (tree.tree ?? []).filter((e) => e.type === 'blob' && e.path.endsWith('.json')).map((e) => `hidden/${e.path}`);
  return { items, truncated: Boolean(tree.truncated) };
}

// Everything published in the library: each file of catalog/, grinders/, machines/, services/ and
// links/ on main, named from the index (an entry the index left out is said to be so).
const PUBLISHED_KINDS = [
  ['catalog/', 'coffee', 'coffees.json', 'coffees'],
  ['grinders/', 'grinder', 'grinders.json', 'grinders'],
  ['machines/', 'machine', 'machines.json', 'machines'],
  ['services/', 'service', 'services.json', 'services'],
  ['links/', 'link', 'links.json', 'links'],
];

function publishedName(kind, e) {
  if (!e) return '';
  if (kind === 'coffee') return e.name ?? '';
  if (kind === 'grinder') return e.model ?? '';
  if (kind === 'machine') return [e.maker, e.model].filter(Boolean).join(' ');
  if (kind === 'service') return e.name ?? '';
  return e.title ?? '';
}

function publishedDetail(kind, e) {
  if (!e) return '';
  if (kind === 'coffee') return [e.roaster, e.origin, e.process, e.roast_level].filter(Boolean).join(' · ');
  if (kind === 'grinder') return [e.burrs, e.unit, e.max ? `${e.min ?? 1}–${e.max}` : ''].filter(Boolean).join(' · ');
  if (kind === 'machine') return [e.style, e.control].filter(Boolean).join(' · ');
  if (kind === 'service') return [e.kind, (e.ships_to ?? []).join(' '), e.reviews ? `★${e.reviews.stars} (${e.reviews.n})` : ''].filter(Boolean).join(' · ');
  return [e.kind, e.site, (e.topics ?? []).join(', '), e.added].filter(Boolean).join(' · ');
}

async function apiPublished(env) {
  const repo = env.LIBRARY_REPO;
  const tree = await ghOk(env, 'GET', `/repos/${repo}/git/trees/main?recursive=1`, undefined, 'main tree');
  const indexes = {};
  for (const [, kind, file, list] of PUBLISHED_KINDS) {
    const doc = parseJson(await ghRaw(env, repo, `index/${file}`));
    indexes[kind] = new Map((Array.isArray(doc?.[list]) ? doc[list] : []).filter((e) => e && typeof e.id === 'string').map((e) => [e.id, e]));
  }
  const items = [];
  for (const e of tree.tree ?? []) {
    if (e.type !== 'blob' || !e.path.endsWith('.json')) continue;
    const kinds = PUBLISHED_KINDS.find(([folder]) => e.path.startsWith(folder));
    if (!kinds) continue;
    const [folder, kind] = kinds;
    const id = e.path.slice(folder.length, -'.json'.length);
    const entry = indexes[kind].get(id);
    items.push({ kind, id, path: e.path, name: publishedName(kind, entry) || id, detail: publishedDetail(kind, entry), in_index: Boolean(entry) });
  }
  items.sort((a, b) => a.kind.localeCompare(b.kind) || a.name.localeCompare(b.name));
  const counts = {};
  for (const it of items) counts[it.kind] = (counts[it.kind] ?? 0) + 1;
  return { repo, items, counts, truncated: Boolean(tree.truncated) };
}

async function listAll(namespace, prefix, max = 5000) {
  const keys = [];
  let cursor;
  do {
    const page = await namespace.list({ prefix, cursor, limit: 1000 });
    keys.push(...page.keys);
    cursor = page.list_complete ? undefined : page.cursor;
  } while (cursor && keys.length < max);
  return { keys, complete: !cursor };
}

async function apiBlocked(env) {
  const { keys } = await listAll(env.LIMITS, 'blocked:');
  const items = [];
  for (const k of keys.slice(0, 50)) {
    const value = parseJson(await env.LIMITS.get(k.name));
    items.push({ kid: k.name.slice('blocked:'.length), at: value?.at ?? null, by: value?.by ?? '', note: value?.note ?? '' });
  }
  return { items, total: keys.length };
}

const WORKFLOWS = [
  ['intake.yml', 'Intake (also rebuilds the index)'],
  ['review-outcome.yml', 'Review outcome (also rebuilds the index)'],
  ['poke.yml', 'Something arrived (inbox branch)'],
];

// Cloudflare's free plan, per day: KV 100,000 reads, 1,000 writes, 1,000 deletes, 1,000 lists.
const KV_FREE = { reads: 100000, writes: 1000, lists: 1000 };

async function apiHealth(env) {
  const repo = env.LIBRARY_REPO;
  // What the App may do, as GitHub granted it: approve and decline need pull_requests: write.
  let permissions = {};
  try {
    permissions = await appPermissions(env);
  } catch {
    permissions = {};
  }
  const workflows = [];
  for (const [file, name] of WORKFLOWS) {
    const r = await gh(env, 'GET', `/repos/${repo}/actions/workflows/${file}/runs?per_page=10`);
    if (!r.ok) {
      workflows.push({ file, name, error: r.status === 404 ? 'not found on the default branch' : `GitHub answered ${r.status}`, runs: [] });
      continue;
    }
    const runs = (r.data?.workflow_runs ?? []).map((run) => ({
      id: run.id,
      status: run.status,
      conclusion: run.conclusion,
      event: run.event,
      branch: run.head_branch,
      created: run.created_at,
      url: run.html_url,
    }));
    workflows.push({ file, name, runs, failures: runs.filter((run) => run.conclusion === 'failure').length });
  }

  let counters = [];
  if (env.AUDIT) {
    const since = new Date(Date.now() - 6 * 86400e3).toISOString().slice(0, 10);
    const rows = await env.AUDIT.prepare('SELECT day, path, outcome, n FROM counters WHERE day >= ? ORDER BY day DESC, path, outcome')
      .bind(since)
      .all();
    counters = rows.results ?? [];
  }

  // KV use today, estimated from today's counters: every request reads its counter (a library put
  // also reads the block flag and the key, and a key record the address count); every stored report
  // or record writes its counter, a key record two more.
  const today = new Date(Date.now()).toISOString().slice(0, 10);
  const sum = (path, test) => counters.filter((c) => c.day === today && c.path === path && test(c.outcome)).reduce((n, c) => n + c.n, 0);
  const reportsOk = sum('/report', (o) => o === 'ok');
  const putsOk = sum('/library/put', (o) => o === 'ok');
  const keysOk = sum('/library/put', (o) => o === 'ok:key');
  const reportsAll = sum('/report', () => true);
  const putsAll = sum('/library/put', () => true);
  const writes = reportsOk + putsOk + 3 * keysOk;
  const reads = reportsAll + 4 * putsAll;
  const keyCount = await listAll(env.KEYS, undefined);
  const blocked = await listAll(env.LIMITS, 'blocked:');
  return {
    workflows,
    permissions,
    counters,
    kv: {
      today,
      writes_estimate: writes,
      writes_free: KV_FREE.writes,
      reads_estimate: reads,
      reads_free: KV_FREE.reads,
      lists_free: KV_FREE.lists,
      keys_stored: keyCount.keys.length,
      blocked: blocked.keys.length,
      note: 'Estimates from the relay counters (D1), not Cloudflare\'s own figures; the dashboard (Workers › KV › Metrics) has those. Each Health or Devices view uses a few KV list operations.',
    },
  };
}

function monthOf(date) {
  return date.toISOString().slice(0, 7);
}

const MAX_FIRMWARE_READS = 25; // Workers allow 50 subrequests per request on the free plan

async function apiDevices(env) {
  const { keys, complete } = await listAll(env.KEYS, undefined);
  const blocked = new Set((await listAll(env.LIMITS, 'blocked:')).keys.map((k) => k.name.slice('blocked:'.length)));
  const registered = keys
    .map((k) => ({ kid: k.name, first_seen: k.metadata?.first_seen ?? null, blocked: blocked.has(k.name) }))
    .sort((a, b) => String(b.first_seen ?? '').localeCompare(String(a.first_seen ?? '')));

  // Diagnostics units: kids under reports/<month>/ for this month and last, from the git trees.
  const now = new Date(Date.now());
  const months = [monthOf(now), monthOf(new Date(Date.UTC(now.getUTCFullYear(), now.getUTCMonth() - 1, 1)))];
  const repo = env.REPORTS_REPO;
  const units = new Map();
  let note = '';
  const top = await gh(env, 'GET', `/repos/${repo}/contents/reports`);
  if (top.status === 404) note = 'no reports yet';
  else if (!top.ok) throw githubError('reports folder', top);
  else {
    for (const month of months) {
      const dir = (Array.isArray(top.data) ? top.data : []).find((e) => e.name === month && e.type === 'dir');
      if (!dir) continue;
      const tree = await ghOk(env, 'GET', `/repos/${repo}/git/trees/${dir.sha}?recursive=1`, undefined, `reports/${month}`);
      if (tree.truncated) note = `reports/${month} is too large to list in full`;
      for (const e of tree.tree ?? []) {
        const m = /^([0-9a-f]{20})\/(\d{8}T\d{6}Z)-([a-z_]+)\.json$/.exec(e.path);
        if (e.type !== 'blob' || !m) continue;
        const unit = units.get(m[1]) ?? { kid: m[1], reports: 0, latest: '', latest_path: '', kinds: {} };
        unit.reports++;
        unit.kinds[m[3]] = (unit.kinds[m[3]] ?? 0) + 1;
        if (m[2] > unit.latest) {
          unit.latest = m[2];
          unit.latest_path = `reports/${month}/${e.path}`;
        }
        units.set(m[1], unit);
      }
    }
  }
  const list = [...units.values()].sort((a, b) => b.latest.localeCompare(a.latest));
  for (const unit of list.slice(0, MAX_FIRMWARE_READS)) {
    const record = parseJson(await ghRaw(env, repo, unit.latest_path, null));
    unit.firmware = typeof record?.report?.fw === 'string' ? record.report.fw : null;
    unit.url = `https://github.com/${repo}/blob/HEAD/${unit.latest_path}`;
  }
  if (list.length > MAX_FIRMWARE_READS) note = `${note ? `${note}; ` : ''}firmware read for the ${MAX_FIRMWARE_READS} most recent units only`;
  for (const unit of list) {
    const s = unit.latest;
    unit.latest = s ? `${s.slice(0, 4)}-${s.slice(4, 6)}-${s.slice(6, 8)}T${s.slice(9, 11)}:${s.slice(11, 13)}:${s.slice(13, 15)}Z` : null;
  }
  return { keys: { total: registered.length, complete, items: registered }, units: { months, items: list, note } };
}

async function apiAudit(env, url) {
  if (!env.AUDIT) fail(503, 'no AUDIT database bound');
  const p = url.searchParams;
  const where = [];
  const args = [];
  for (const column of ['action', 'actor', 'result']) {
    const value = p.get(column);
    if (value) {
      where.push(`${column} = ?`);
      args.push(value);
    }
  }
  const q = p.get('q');
  if (q) {
    const like = `%${q.replace(/[\\%_]/g, (c) => `\\${c}`)}%`;
    where.push("(target LIKE ? ESCAPE '\\' OR error LIKE ? ESCAPE '\\' OR detail LIKE ? ESCAPE '\\')");
    args.push(like, like, like);
  }
  if (p.get('since')) {
    where.push('ts >= ?');
    args.push(p.get('since'));
  }
  if (p.get('until')) {
    where.push('ts < ?');
    args.push(p.get('until'));
  }
  const before = Number(p.get('before'));
  if (Number.isInteger(before) && before > 0) {
    where.push('id < ?');
    args.push(before);
  }
  const limit = Math.min(200, Math.max(1, Number(p.get('limit')) || 50));
  const sql = `SELECT id, ts, actor, action, target, result, error, ip_hash, detail FROM audit${where.length ? ` WHERE ${where.join(' AND ')}` : ''} ORDER BY id DESC LIMIT ${limit}`;
  const rows = await env.AUDIT.prepare(sql).bind(...args).all();
  const items = (rows.results ?? []).map((r) => ({ ...r, detail: parseJson(r.detail) ?? r.detail }));
  return { items, more: items.length === limit };
}

const VIEWS = {
  queue: apiQueue,
  review: apiReview,
  health: apiHealth,
  devices: apiDevices,
  audit: apiAudit,
  hidden: apiHidden,
  published: apiPublished,
  blocked: apiBlocked,
};

// ------------------------------------------------------------------------------- routing --

const PAGE_HEADERS = {
  'cache-control': 'no-store',
  'content-security-policy': "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'",
  'x-content-type-options': 'nosniff',
  'referrer-policy': 'no-referrer',
};

function page(pathname) {
  const name = pathname === '/admin/' ? 'index.html' : pathname.slice('/admin/'.length);
  if (!Object.hasOwn(ADMIN_PAGES, name)) return null;
  const { type, body } = ADMIN_PAGES[name];
  return new Response(body, { status: 200, headers: { 'content-type': type, ...PAGE_HEADERS } });
}

// A cross-site page cannot send this: the custom header needs a CORS preflight nobody answers.
function sameSite(request) {
  if (request.headers.get('x-bbw-admin') !== '1') return false;
  const origin = request.headers.get('origin');
  return !origin || origin === new URL(request.url).origin;
}

export async function handleAdmin(request, env) {
  const actor = await accessIdentity(request, env);
  if (!actor) return forbidden();
  const url = new URL(request.url);
  const { pathname } = url;

  if (!pathname.startsWith('/admin/api/')) {
    if (request.method !== 'GET' && request.method !== 'HEAD') return new Response('GET only', { status: 405, headers: { allow: 'GET' } });
    if (pathname === '/admin') return new Response(null, { status: 301, headers: { location: '/admin/' } });
    return page(pathname) ?? new Response('Not found', { status: 404 });
  }

  const name = pathname.slice('/admin/api/'.length);
  if (request.method === 'GET' && Object.hasOwn(VIEWS, name)) {
    try {
      return json(200, { ok: true, actor, ...(await VIEWS[name](env, url)) });
    } catch (e) {
      if (e instanceof AdminError) return json(e.status, { ok: false, error: e.message });
      console.error(`admin ${name}: ${e && e.message ? e.message : e}`);
      return json(500, { ok: false, error: 'admin error' });
    }
  }
  if (request.method === 'POST' && Object.hasOwn(ACTIONS, name)) {
    if (!sameSite(request)) return json(403, { ok: false, error: 'send from the console' });
    const row = { actor, action: name, ip_hash: await ipHash(request) };
    let data = {};
    try {
      data = await readAction(request);
      const done = await ACTIONS[name](env, data, actor);
      await writeAudit(env, { ...row, target: done.target, result: done.warning ? 'partial' : 'ok', error: done.warning ?? '', detail: done.detail });
      return json(200, { ok: true, target: done.target, warning: done.warning || undefined, ...done.detail });
    } catch (e) {
      const status = e instanceof AdminError ? e.status : 500;
      const message = e instanceof AdminError ? e.message : 'admin error';
      if (!(e instanceof AdminError)) console.error(`admin ${name}: ${e && e.message ? e.message : e}`);
      const target = data.number !== undefined ? `#${data.number}` : String(data.path ?? data.kid ?? '');
      const asked = {};
      for (const k of ['number', 'path', 'kid', 'note', 'reason', 'sha']) if (data[k] !== undefined) asked[k] = String(data[k]).slice(0, 200);
      await writeAudit(env, { ...row, target, result: 'failed', error: message, detail: { request: asked } });
      return json(status, { ok: false, error: message });
    }
  }
  return json(404, { ok: false, error: 'not found' });
}
