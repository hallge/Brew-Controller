// Brew Controller diagnostics relay: a Cloudflare Worker.
//
// POST /report takes one signed report envelope from a controller, checks it and commits it to the
// private reports repository as a GitHub App. The contract is docs/TELEMETRY.md.
//
// Environment:
//   LIMITS                  KV namespace for the per-key daily counts
//   GITHUB_APP_ID           the App's id (secret or var)
//   GITHUB_INSTALLATION_ID  the App's installation on the reports repository
//   GITHUB_APP_KEY          the App's private key, PEM (PKCS#8, or the PKCS#1 file GitHub hands out)
//   REPORTS_REPO            "owner/name" of the private repository

const MAX_BYTES = 32 * 1024;
const KINDS = { diag_daily: 4, diag_crash: 10 }; // the kind and its limit per key per UTC day
const ALG_SIGNED = 'ES256';
const ALG_EMULATOR = 'EMULATOR-UNSIGNED';
const ENVELOPE_FIELDS = ['v', 'kid', 'alg', 'pub', 'sig', 'body'];
const GITHUB_API = 'https://api.github.com';
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

function base64ToBytes(text) {
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

  const sig = base64ToBytes(envelope.sig);
  if (!sig || sig.length !== 64) refuse(401, 'bad signature');
  let ok = false;
  try {
    const key = await crypto.subtle.importKey('raw', pub, { name: 'ECDSA', namedCurve: 'P-256' }, false, ['verify']);
    ok = await crypto.subtle.verify({ name: 'ECDSA', hash: 'SHA-256' }, key, sig, encoder.encode(envelope.body));
  } catch {
    ok = false; // a point not on the curve lands here
  }
  if (!ok) refuse(401, 'bad signature');
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

let cachedToken = null; // { token, expiresAt (ms) }, kept for the isolate's life

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
  return cachedToken.token;
}

async function commit(env, path, record, kind, kid) {
  const content = bytesToBase64(encoder.encode(JSON.stringify(record, null, 1) + '\n'));
  const url = `${GITHUB_API}/repos/${env.REPORTS_REPO}/contents/${path}`;
  for (let attempt = 0; attempt < 2; attempt++) {
    const token = await installationToken(env);
    const response = await fetch(url, {
      method: 'PUT',
      headers: { ...githubHeaders(`Bearer ${token}`), 'content-type': 'application/json' },
      body: JSON.stringify({ message: `${kind} from ${kid}`, content }),
    });
    if (response.status === 401 && attempt === 0) {
      forgetToken(); // revoked or expired early: get a fresh one once
      continue;
    }
    return response.status;
  }
  return 401;
}

// -------------------------------------------------------------------------------- handler --

async function handleReport(request, env) {
  const envelope = await readEnvelope(request);
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
  } catch {
    return answer(502, 'could not store the report');
  }
  if (status !== 201 && status !== 200) return answer(502, 'could not store the report');

  // Counted only once stored, so a failed store does not use up the day's allowance.
  await env.LIMITS.put(key, String(count + 1), { expirationTtl: 2 * 24 * 60 * 60 });
  return answer(201);
}

export default {
  async fetch(request, env) {
    const { pathname } = new URL(request.url);
    if (pathname !== '/report') return answer(404, 'not found');
    if (request.method !== 'POST') {
      const response = answer(405, 'POST only');
      response.headers.set('allow', 'POST');
      return response;
    }
    try {
      return await handleReport(request, env);
    } catch (e) {
      if (e instanceof Refusal) return answer(e.status, e.message);
      return answer(500, 'relay error');
    }
  },
};
