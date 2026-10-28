// Brew Controller diagnostics relay: a Cloudflare Worker.
//
// POST /report takes one signed report envelope from a controller, checks it and commits it to the
// private reports repository as a GitHub App. The contract is docs/TELEMETRY.md.
// POST /library/put and GET /library/get carry the shared coffee library (see the section below).
//
// Environment:
//   LIMITS                  KV namespace for the per-key daily counts
//   GITHUB_APP_ID           the App's id (secret or var)
//   GITHUB_INSTALLATION_ID  the App's installation on the reports repository
//   GITHUB_APP_KEY          the App's private key, PEM (PKCS#8, or the PKCS#1 file GitHub hands out)
//   REPORTS_REPO            "owner/name" of the private repository
//   KEYS                    KV namespace: library contributors' public keys, by kid
//   LIBRARY_REPO            "owner/name" of the coffee library (records go to its inbox branch)
//   AUDIT                   D1 database (optional): relay counters per day and the audit log
//   ACCESS_TEAM, ACCESS_AUD the Cloudflare Access team and audience guarding /admin (src/admin.js)
// A kid is blocked from the library by a key "blocked:<kid>" in LIMITS.

import { handleAdmin, recordRelayOutcome } from './admin.js';

const MAX_BYTES = 32 * 1024;
const KINDS = { diag_daily: 4, diag_crash: 10 }; // the kind and its limit per key per UTC day
const ALG_SIGNED = 'ES256';
const ALG_EMULATOR = 'EMULATOR-UNSIGNED';
const ENVELOPE_FIELDS = ['v', 'kid', 'alg', 'pub', 'sig', 'body'];
export const GITHUB_API = 'https://api.github.com';
const USER_AGENT = 'bbw-telemetry-relay';
const TOKEN_MARGIN_MS = 5 * 60 * 1000; // renew the installation token this long before it expires

// A MAC address (colons or dashes) or a dotted IPv4 address left in a body means the controller's
// cleaning missed something: such a report is refused rather than stored.
const MAC_PATTERN = /(?<![0-9A-Fa-f])[0-9A-Fa-f]{2}([:-])[0-9A-Fa-f]{2}(?:\1[0-9A-Fa-f]{2}){4}(?![0-9A-Fa-f])/;
const OCTET = '(?:25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)';
const IPV4_PATTERN = new RegExp(`(?<![\\d.])${OCTET}(?:\\.${OCTET}){3}(?![\\d.])`);

const encoder = new TextEncoder();

// --------------------------------------------------------------------------------- answers --

function answer(status, error) {
  const body = error ? { ok: false, error } : { ok: true };
  return new Response(JSON.stringify(body), {
    status,
    headers: { 'content-type': 'application/json' },
  });
}

class Refusal extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

function refuse(status, message) {
  throw new Refusal(status, message);
}

// ---------------------------------------------------------------------------------- bytes --

export function base64ToBytes(text) {
  if (typeof text !== 'string' || !/^[A-Za-z0-9+/]*={0,2}$/.test(text) || text.length % 4 !== 0) {
    return null;
  }
  try {
    const binary = atob(text);
    const bytes = new Uint8Array(binary.length);
    for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
    return bytes;
  } catch {
    return null;
  }
}

function bytesToBase64(bytes) {
  let binary = '';
  for (let i = 0; i < bytes.length; i += 0x8000) {
    binary += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  }
  return btoa(binary);
}

function base64url(bytes) {
  return bytesToBase64(bytes).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

function hex(bytes) {
  return Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join('');
}

export async function kidOf(pubBytes) {
  const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', pubBytes));
  return hex(digest.subarray(0, 10));
}

// ------------------------------------------------------------------------------- checking --

async function readEnvelope(request) {
  const declared = Number(request.headers.get('content-length'));
  if (Number.isFinite(declared) && declared > MAX_BYTES) refuse(413, 'report too large');

  const raw = new Uint8Array(await request.arrayBuffer());
  if (raw.length > MAX_BYTES) refuse(413, 'report too large');

  let envelope;
  try {
    envelope = JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(raw));
  } catch {
    refuse(400, 'not JSON');
  }
  if (!envelope || typeof envelope !== 'object' || Array.isArray(envelope)) refuse(400, 'not an envelope');

  const keys = Object.keys(envelope);
  if (keys.length !== ENVELOPE_FIELDS.length || !ENVELOPE_FIELDS.every((k) => keys.includes(k))) {
    refuse(400, 'envelope fields must be exactly v, kid, alg, pub, sig, body');
  }
  if (envelope.v !== 1) refuse(400, 'unknown envelope version');
  if (typeof envelope.kid !== 'string' || !/^[0-9a-f]{20}$/.test(envelope.kid)) refuse(400, 'bad kid');
  if (envelope.alg !== ALG_SIGNED && envelope.alg !== ALG_EMULATOR) refuse(400, 'unknown alg');
  if (typeof envelope.sig !== 'string') refuse(400, 'bad sig');
  if (typeof envelope.body !== 'string') refuse(400, 'body must be a string');
  return envelope;
}

function readBody(envelope) {
  let body;
  try {
    body = JSON.parse(envelope.body);
  } catch {
    refuse(400, 'body is not JSON');
  }
  if (!body || typeof body !== 'object' || Array.isArray(body)) refuse(400, 'body is not an object');
  if (!Object.hasOwn(KINDS, body.t)) refuse(400, 'unknown report kind');
  if (body.v !== 1) refuse(400, 'unknown report version');
  return body;
}

async function checkKey(envelope) {
  const pub = base64ToBytes(envelope.pub);
  if (!pub || pub.length !== 65 || pub[0] !== 0x04) refuse(400, 'pub must be an uncompressed P-256 point');
  if ((await kidOf(pub)) !== envelope.kid) refuse(401, 'kid does not match pub');

  // The emulator has no key anyone can check: its reports are kept apart, never verified.
  if (envelope.alg === ALG_EMULATOR) return;

  if (!(await es256Valid(pub, envelope.sig, envelope.body))) refuse(401, 'bad signature');
}

// ECDSA P-256 over SHA-256 of the text exactly as sent; sig is base64 of r|s (64 bytes).
export async function es256Valid(pub, sigText, text) {
  const sig = base64ToBytes(sigText);
  if (!sig || sig.length !== 64) return false;
  try {
    const key = await crypto.subtle.importKey('raw', pub, { name: 'ECDSA', namedCurve: 'P-256' }, false, ['verify']);
    return await crypto.subtle.verify({ name: 'ECDSA', hash: 'SHA-256' }, key, sig, encoder.encode(text));
  } catch {
    return false; // a point not on the curve lands here
  }
}

function checkClean(envelope) {
  if (MAC_PATTERN.test(envelope.body)) refuse(422, 'body holds a MAC address');
  if (IPV4_PATTERN.test(envelope.body)) refuse(422, 'body holds an IP address');
}

// ------------------------------------------------------------------------------- counting --

// Made by the PC emulator: unsigned, or signed by an emulator with a real key and saying so in its
// body ("emu":1). Its shots are simulated: kept apart from boards' reports.
function fromEmulator(envelope, body) {
  return envelope.alg === ALG_EMULATOR || (body && body.emu === 1);
}

function limitKey(envelope, kind, now, emu = envelope.alg === ALG_EMULATOR) {
  const day = now.toISOString().slice(0, 10);
  const space = emu ? 'emu' : 'dev';
  return `${space}:${envelope.kid}:${day}:${kind}`;
}

async function countSoFar(env, key) {
  const stored = await env.LIMITS.get(key);
  const n = Number.parseInt(stored ?? '0', 10);
  return Number.isFinite(n) ? n : 0;
}

// ------------------------------------------------------------------------------ the store --

export function reportPath(envelope, kind, now, emu = envelope.alg === ALG_EMULATOR) {
  const iso = now.toISOString(); // 2026-10-23T08:15:30.123Z
  const month = iso.slice(0, 7);
  const stamp = `${iso.slice(0, 4)}${iso.slice(5, 7)}${iso.slice(8, 10)}T${iso.slice(11, 13)}${iso.slice(14, 16)}${iso.slice(17, 19)}Z`;
  const top = emu ? 'emulator' : 'reports';
  return `${top}/${month}/${envelope.kid}/${stamp}-${kind}.json`;
}

// Wraps a PKCS#1 RSA key (BEGIN RSA PRIVATE KEY, as GitHub downloads it) into PKCS#8 for WebCrypto.
function derLength(n) {
  if (n < 0x80) return [n];
  const out = [];
  while (n > 0) {
    out.unshift(n & 0xff);
    n >>= 8;
  }
  return [0x80 | out.length, ...out];
}

function pkcs1ToPkcs8(pkcs1) {
  const algorithm = [0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00];
  const octets = [0x04, ...derLength(pkcs1.length)];
  const inner = [0x02, 0x01, 0x00, ...algorithm, ...octets];
  const total = inner.length + pkcs1.length;
  const out = new Uint8Array(1 + derLength(total).length + total);
  out.set([0x30, ...derLength(total), ...inner]);
  out.set(pkcs1, out.length - pkcs1.length);
  return out;
}

async function importAppKey(pem) {
  const text = String(pem ?? '').replace(/\\n/g, '\n');
  const isPkcs1 = text.includes('BEGIN RSA PRIVATE KEY');
  const der = base64ToBytes(text.replace(/-----[A-Z ]+-----/g, '').replace(/\s+/g, ''));
  if (!der) throw new Error('GITHUB_APP_KEY is not a PEM key');
  return crypto.subtle.importKey(
    'pkcs8',
    isPkcs1 ? pkcs1ToPkcs8(der) : der,
    { name: 'RSASSA-PKCS1-v1_5', hash: 'SHA-256' },
    false,
    ['sign'],
  );
}

export async function appJwt(env, nowMs = Date.now()) {
  const now = Math.floor(nowMs / 1000);
  const part = (o) => base64url(encoder.encode(JSON.stringify(o)));
  // GitHub allows at most 10 minutes; iat is set back a minute for clock drift.
  const unsigned = `${part({ alg: 'RS256', typ: 'JWT' })}.${part({ iat: now - 60, exp: now + 540, iss: String(env.GITHUB_APP_ID) })}`;
  const key = await importAppKey(env.GITHUB_APP_KEY);
  const signature = new Uint8Array(await crypto.subtle.sign('RSASSA-PKCS1-v1_5', key, encoder.encode(unsigned)));
  return `${unsigned}.${base64url(signature)}`;
}

let cachedToken = null; // { token, expiresAt (ms), permissions }, kept for the isolate's life
let grantedPermissions = null; // what GitHub granted the App with the last token, for the admin console

export function forgetToken() {
  cachedToken = null;
}

function githubHeaders(auth) {
  return {
    authorization: auth,
    accept: 'application/vnd.github+json',
    'user-agent': USER_AGENT,
    'x-github-api-version': '2022-11-28',
  };
}

async function installationToken(env) {
  if (cachedToken && Date.now() < cachedToken.expiresAt - TOKEN_MARGIN_MS) return cachedToken.token;
  const jwt = await appJwt(env);
  const response = await fetch(`${GITHUB_API}/app/installations/${env.GITHUB_INSTALLATION_ID}/access_tokens`, {
    method: 'POST',
    headers: githubHeaders(`Bearer ${jwt}`),
  });
  if (!response.ok) throw new Error(`installation token: HTTP ${response.status}`);
  const data = await response.json();
  const expiresAt = Date.parse(data.expires_at);
  cachedToken = { token: data.token, expiresAt: Number.isFinite(expiresAt) ? expiresAt : Date.now() + 50 * 60 * 1000 };
  grantedPermissions = data.permissions && typeof data.permissions === 'object' ? data.permissions : null;
  return cachedToken.token;
}

// The App's permissions as granted with the current token ({} before the first token).
export async function appPermissions(env) {
  await installationToken(env);
  return grantedPermissions ?? {};
}

// One GitHub API call as the App, with one retry on a fresh token if GitHub says 401, or 403 to a
// change (a token from before the owner granted the App more permissions keeps the old ones).
export async function githubCall(env, url, init = {}, accept) {
  const writes = (init.method ?? 'GET') !== 'GET';
  for (let attempt = 0; attempt < 2; attempt++) {
    const fresh = !cachedToken;
    const token = await installationToken(env);
    const headers = { ...githubHeaders(`Bearer ${token}`), ...(init.headers ?? {}) };
    if (accept) headers.accept = accept;
    const response = await fetch(url, { ...init, headers });
    if (attempt === 0 && (response.status === 401 || (response.status === 403 && writes && !fresh))) {
      forgetToken(); // revoked, expired early, or older than a permission change: a fresh one, once
      continue;
    }
    return response;
  }
  return new Response('', { status: 401 });
}

// Creates a file through the contents API. Answers GitHub's status (422: the path is taken).
async function putFile(env, repo, path, text, message, branch) {
  const payload = { message, content: bytesToBase64(encoder.encode(text)) };
  if (branch) payload.branch = branch;
  const response = await githubCall(env, `${GITHUB_API}/repos/${repo}/contents/${path}`, {
    method: 'PUT',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify(payload),
  });
  return response.status;
}

async function commit(env, path, record, kind, kid) {
  return putFile(env, env.REPORTS_REPO, path, JSON.stringify(record, null, 1) + '\n', `${kind} from ${kid}`);
}

// -------------------------------------------------------------------------------- handler --

async function handleReport(request, env, ctx, info = {}) {
  const envelope = await readEnvelope(request);
  info.kid = envelope.kid; // as claimed: checked below
  const body = readBody(envelope);
  await checkKey(envelope);
  checkClean(envelope);

  const now = new Date(Date.now());
  const kind = body.t;
  const emu = fromEmulator(envelope, body);
  const key = limitKey(envelope, kind, now, emu);
  const count = await countSoFar(env, key);
  if (count >= KINDS[kind]) refuse(429, 'too many reports today');

  // What is stored keeps the signed string, so the report can be checked again later.
  const record = {
    received: now.toISOString(),
    kid: envelope.kid,
    alg: envelope.alg,
    pub: envelope.pub,
    sig: envelope.sig,
    body: envelope.body,
    report: body,
  };
  // Two reports in the same second (a controller clearing its backlog) take the next free second.
  let status;
  try {
    for (let step = 0; step < 5; step++) {
      const when = new Date(now.getTime() + step * 1000);
      status = await commit(env, reportPath(envelope, kind, when, emu), record, kind, envelope.kid);
      if (status !== 422) break; // 422: that path is already taken
    }
  } catch (e) {
    // Why, for `wrangler tail`: never a secret, only what GitHub answered or what could not be done.
    console.error(`store failed: ${e && e.message ? e.message : e}`);
    return answer(502, 'could not store the report');
  }
  if (status !== 201 && status !== 200) {
    console.error(`store failed: GitHub answered ${status} for ${env.REPORTS_REPO}`);
    return answer(502, 'could not store the report');
  }

  // Counted only once stored, so a failed store does not use up the day's allowance.
  await env.LIMITS.put(key, String(count + 1), { expirationTtl: 2 * 24 * 60 * 60 });
  return answer(201);
}

// ------------------------------------------------------------------------ shared library --
//
// POST /library/put files one signed library record (src/core/Share.h sealRecord) in the coffee
// library's inbox, so an owner needs no GitHub token. GET /library/get reads the library's published
// status and index files. The library's own intake checks every record again before using it.

const LIBRARY_MAX_BYTES = 16 * 1024; // the whole request
const LIBRARY_MAX_RECORD = 8 * 1024; // the record (the library intake's MAX_RECORD_BYTES)
const LIBRARY_PER_KID = 60; // records per kid per UTC day
const LIBRARY_KEYS_PER_ADDRESS = 20; // key records per network address per UTC day
const LIBRARY_FIELDS = ['v', 'kid', 'alg', 'sig', 'body'];
const LIBRARY_INBOX = 'inbox';
const LIBRARY_CACHE_INDEX_S = 600;
const LIBRARY_CACHE_STATUS_S = 120;
const DAY_S = 24 * 60 * 60;

// The inbox paths the firmware writes (src/main.cpp), and the record type each folder holds.
// Numbers are printed with %u; the month folder is "yyyy/mm", or "undated" with no clock.
const KID = '([0-9a-f]{20})';
const NUM = '(?:0|[1-9]\\d{0,9})';
const MONTH = '(?:\\d{4}/(?:0[1-9]|1[0-2])|undated)';
const LIBRARY_PATHS = [
  [new RegExp(`^keys/${KID}\\.json$`), 'key'],
  [new RegExp(`^coffees/${KID}/c${NUM}-${NUM}\\.json$`), 'coffee'],
  [new RegExp(`^grinders/${KID}/g${NUM}-${NUM}\\.json$`), 'grinder'],
  [new RegExp(`^services/${KID}/s${NUM}-${NUM}\\.json$`), 'service'],
  [new RegExp(`^machines/${KID}/m1-${NUM}\\.json$`), 'machine'],
  [new RegExp(`^links/${KID}/l${NUM}-${NUM}\\.json$`), 'link'],
  // -<record>: each version of a rating under its own name (an edit is not "already there").
  [new RegExp(`^tastes/${KID}-r${NUM}(?:-${NUM})?\\.json$`), 'taste'],
  [new RegExp(`^svcreviews/${KID}-v${NUM}(?:-${NUM})?\\.json$`), 'service_review'],
  [new RegExp(`^shots/${MONTH}/${KID}-${NUM}\\.json$`), 'shot'],
  [new RegExp(`^dialins/${MONTH}/${KID}-${NUM}\\.json$`), 'dialin'],
];

// What GET /library/get may read: a controller's status, or a published index file.
const STATUS_PATH = /^status\/[0-9a-f]{20}\.json$/;
const INDEX_PATH = /^index\/(?:[A-Za-z0-9_-][A-Za-z0-9._-]*\/)*[A-Za-z0-9_-][A-Za-z0-9._-]*\.json$/;

export function libraryPathKind(path) {
  for (const [pattern, type] of LIBRARY_PATHS) {
    const match = pattern.exec(path);
    if (match) return { kid: match[1], type };
  }
  return null;
}

export function libraryReadable(path) {
  if (typeof path !== 'string' || path.includes('..')) return null;
  if (STATUS_PATH.test(path)) return LIBRARY_CACHE_STATUS_S;
  if (INDEX_PATH.test(path)) return LIBRARY_CACHE_INDEX_S;
  return null;
}

export async function sha256Hex(text) {
  return hex(new Uint8Array(await crypto.subtle.digest('SHA-256', encoder.encode(text))));
}

async function readLibraryPut(request) {
  const declared = Number(request.headers.get('content-length'));
  if (Number.isFinite(declared) && declared > LIBRARY_MAX_BYTES) refuse(413, 'request too large');
  const raw = new Uint8Array(await request.arrayBuffer());
  if (raw.length > LIBRARY_MAX_BYTES) refuse(413, 'request too large');

  let put;
  try {
    put = JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(raw));
  } catch {
    refuse(400, 'not JSON');
  }
  if (!put || typeof put !== 'object' || typeof put.path !== 'string' || typeof put.record !== 'string') {
    refuse(400, 'expected {"path": "...", "record": "..."}');
  }
  if (encoder.encode(put.record).length > LIBRARY_MAX_RECORD) refuse(413, 'record too large');
  return put;
}

function readLibraryEnvelope(text) {
  let envelope;
  try {
    envelope = JSON.parse(text);
  } catch {
    refuse(400, 'record is not JSON');
  }
  if (!envelope || typeof envelope !== 'object' || Array.isArray(envelope)) refuse(400, 'not an envelope');
  const keys = Object.keys(envelope);
  if (keys.length !== LIBRARY_FIELDS.length || !LIBRARY_FIELDS.every((k) => keys.includes(k))) {
    refuse(400, 'envelope fields must be exactly v, kid, alg, sig, body');
  }
  if (envelope.v !== 1) refuse(400, 'unknown envelope version');
  if (typeof envelope.kid !== 'string' || !/^[0-9a-f]{20}$/.test(envelope.kid)) refuse(400, 'bad kid');
  if (typeof envelope.alg !== 'string' || typeof envelope.sig !== 'string') refuse(400, 'bad alg or sig');
  if (typeof envelope.body !== 'string') refuse(400, 'body must be a string');
  return envelope;
}

async function handleLibraryPut(request, env, ctx, info = {}) {
  const put = await readLibraryPut(request);
  info.libraryPath = put.path.slice(0, 120);
  const target = libraryPathKind(put.path);
  if (!target) refuse(400, 'not a library inbox path');
  info.kid = target.kid; // as claimed: checked below
  info.type = target.type;
  const envelope = readLibraryEnvelope(put.record);
  if (envelope.alg === ALG_EMULATOR) refuse(403, 'the emulator has no key: its records are not shared');
  if (envelope.alg !== ALG_SIGNED) refuse(400, 'unknown alg');
  if (target.kid !== envelope.kid) refuse(403, 'the path names another key');
  if ((await env.LIMITS.get(`blocked:${envelope.kid}`)) !== null) refuse(403, 'this key is blocked');

  let body;
  try {
    body = JSON.parse(envelope.body);
  } catch {
    refuse(400, 'body is not JSON');
  }
  if (!body || typeof body !== 'object' || Array.isArray(body)) refuse(400, 'body is not an object');
  if (body.t !== target.type) refuse(400, `a ${target.type} path must hold a ${target.type} record`);

  // Keys: a key record vouches for itself; anything else is checked against the key on file.
  const isKey = target.type === 'key';
  let keyText;
  if (isKey) {
    if (body.alg !== ALG_SIGNED) refuse(400, 'key record for another algorithm');
    const pub = base64ToBytes(body.key);
    if (!pub || pub.length !== 65 || pub[0] !== 0x04) refuse(400, 'key must be an uncompressed P-256 point');
    if ((await kidOf(pub)) !== envelope.kid) refuse(401, 'kid is not the fingerprint of the key');
    if (!(await es256Valid(pub, envelope.sig, envelope.body))) refuse(401, 'bad signature');
    keyText = body.key;
  } else {
    const stored = await env.KEYS.get(envelope.kid);
    if (stored === null) refuse(409, 'send the key first');
    const pub = base64ToBytes(stored);
    if (!pub || !(await es256Valid(pub, envelope.sig, envelope.body))) refuse(401, 'bad signature');
  }

  // Limits, per UTC day: records per kid, and key records per network address (kept only hashed).
  const day = new Date(Date.now()).toISOString().slice(0, 10);
  const kidKey = `lib:${envelope.kid}:${day}`;
  const kidCount = await countSoFar(env, kidKey);
  if (kidCount >= LIBRARY_PER_KID) refuse(429, 'too many records today');
  let addressKey;
  let addressCount = 0;
  if (isKey) {
    const address = request.headers.get('cf-connecting-ip') ?? 'unknown';
    addressKey = `libkey:${await sha256Hex(address)}:${day}`;
    addressCount = await countSoFar(env, addressKey);
    if (addressCount >= LIBRARY_KEYS_PER_ADDRESS) refuse(429, 'too many keys today');
  }

  let status;
  try {
    status = await putFile(
      env,
      env.LIBRARY_REPO,
      `${LIBRARY_INBOX}/${put.path}`,
      put.record, // exactly as received: the signature is over the body inside it
      `${target.type} from ${envelope.kid}`,
      LIBRARY_INBOX,
    );
  } catch (e) {
    console.error(`library store failed: ${e && e.message ? e.message : e}`);
    return answer(502, 'could not store the record');
  }
  // 422: already there, which counts as sent (a controller resending after a lost answer).
  if (status !== 201 && status !== 200 && status !== 422) {
    console.error(`library store failed: GitHub answered ${status}`);
    return answer(502, 'could not store the record');
  }

  // Counted, and the key kept, only once stored.
  const ttl = { expirationTtl: 2 * DAY_S };
  await env.LIMITS.put(kidKey, String(kidCount + 1), ttl);
  if (isKey) {
    await env.LIMITS.put(addressKey, String(addressCount + 1), ttl);
    // First seen: the admin console's Devices view lists it.
    if ((await env.KEYS.get(envelope.kid)) !== keyText) {
      await env.KEYS.put(envelope.kid, keyText, { metadata: { first_seen: new Date(Date.now()).toISOString() } });
    }
  }
  return answer(201);
}

async function handleLibraryGet(request, env, ctx) {
  const url = new URL(request.url);
  const path = url.searchParams.get('path');
  const maxAge = libraryReadable(path);
  if (maxAge === null) refuse(400, 'only status/<kid>.json and index/...json can be read');

  const cache = globalThis.caches?.default;
  const cacheKey = new Request(`${url.origin}/library/get?path=${encodeURIComponent(path)}`);
  if (cache) {
    const hit = await cache.match(cacheKey);
    if (hit) return hit;
  }

  let response;
  try {
    response = await githubCall(
      env,
      `${GITHUB_API}/repos/${env.LIBRARY_REPO}/contents/${path}?ref=main`,
      { method: 'GET' },
      'application/vnd.github.raw+json',
    );
  } catch (e) {
    console.error(`library read failed: ${e && e.message ? e.message : e}`);
    return answer(502, 'could not read the library');
  }
  if (response.status === 404) return answer(404, 'not found');
  if (!response.ok) {
    console.error(`library read failed: GitHub answered ${response.status}`);
    return answer(502, 'could not read the library');
  }

  const fresh = new Response(await response.text(), {
    status: 200,
    headers: { 'content-type': 'application/json', 'cache-control': `public, max-age=${maxAge}` },
  });
  if (cache) {
    const storing = cache.put(cacheKey, fresh.clone());
    if (ctx && typeof ctx.waitUntil === 'function') ctx.waitUntil(storing);
    else await storing;
  }
  return fresh;
}

// -------------------------------------------------------------------------------- routing --

const ROUTES = {
  '/report': ['POST', handleReport],
  '/library/put': ['POST', handleLibraryPut],
  '/library/get': ['GET', handleLibraryGet],
};

// Counts the outcome and audits a refusal (D1), after the answer when the runtime allows it.
async function record(request, env, ctx, pathname, status, refusal, info) {
  const writing = recordRelayOutcome(request, env, pathname, status, refusal, info).catch((e) => {
    console.error(`record failed: ${e && e.message ? e.message : e}`);
  });
  if (ctx && typeof ctx.waitUntil === 'function') ctx.waitUntil(writing);
  else await writing;
}

export default {
  async fetch(request, env, ctx) {
    const { pathname } = new URL(request.url);
    if (pathname === '/admin' || pathname.startsWith('/admin/')) return handleAdmin(request, env, ctx);
    if (!Object.hasOwn(ROUTES, pathname)) return answer(404, 'not found');
    const [method, handle] = ROUTES[pathname];
    if (request.method !== method) {
      const response = answer(405, `${method} only`);
      response.headers.set('allow', method);
      return response;
    }
    const info = {};
    let response;
    let refusal = null;
    try {
      response = await handle(request, env, ctx, info);
    } catch (e) {
      if (e instanceof Refusal) {
        refusal = e.message;
        response = answer(e.status, e.message);
      } else {
        response = answer(500, 'relay error');
      }
    }
    if (env.AUDIT) await record(request, env, ctx, pathname, response.status, refusal, info);
    return response;
  },
};
