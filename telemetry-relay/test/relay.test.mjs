// Run with: node --test telemetry-relay/test
import { test, beforeEach } from 'node:test';
import assert from 'node:assert/strict';
import { generateKeyPairSync, createPublicKey, verify as nodeVerify } from 'node:crypto';

import worker, { forgetToken, kidOf, reportPath } from '../src/worker.js';

const enc = new TextEncoder();
const b64 = (bytes) => Buffer.from(bytes).toString('base64');

// ----------------------------------------------------------------------------- fixtures --

class MemoryKV {
  constructor() {
    this.map = new Map();
  }
  async get(key) {
    return this.map.has(key) ? this.map.get(key) : null;
  }
  async put(key, value) {
    this.map.set(key, String(value));
  }
}

const rsa = generateKeyPairSync('rsa', {
  modulusLength: 2048,
  publicKeyEncoding: { type: 'spki', format: 'pem' },
  privateKeyEncoding: { type: 'pkcs8', format: 'pem' },
});

function makeEnv(overrides = {}) {
  return {
    LIMITS: new MemoryKV(),
    GITHUB_APP_ID: '123456',
    GITHUB_INSTALLATION_ID: '7890',
    GITHUB_APP_KEY: rsa.privateKey,
    REPORTS_REPO: 'owner/bbw-diagnostics',
    ...overrides,
  };
}

// A stand-in for api.github.com: hands out installation tokens and stores PUT files.
function mockGitHub({ putStatus = 201 } = {}) {
  const github = { files: new Map(), tokenCalls: 0, puts: [], jwts: [] };
  globalThis.fetch = async (input, init = {}) => {
    const url = new URL(typeof input === 'string' ? input : input.url);
    const auth = new Headers(init.headers).get('authorization') ?? '';
    if (init.method === 'POST' && url.pathname === '/app/installations/7890/access_tokens') {
      github.tokenCalls++;
      github.jwts.push(auth.replace(/^Bearer /, ''));
      return Response.json(
        { token: `ghs_test${github.tokenCalls}`, expires_at: new Date(Date.now() + 3600e3).toISOString() },
        { status: 201 },
      );
    }
    const prefix = '/repos/owner/bbw-diagnostics/contents/';
    if (init.method === 'PUT' && url.pathname.startsWith(prefix)) {
      assert.match(auth, /^Bearer ghs_test\d+$/);
      const path = decodeURIComponent(url.pathname.slice(prefix.length));
      const payload = JSON.parse(init.body);
      github.puts.push(path);
      if (putStatus !== 201) return new Response('{}', { status: putStatus });
      if (github.files.has(path)) return new Response('{"message":"already exists"}', { status: 422 });
      github.files.set(path, JSON.parse(Buffer.from(payload.content, 'base64').toString('utf8')));
      return Response.json({ content: { path } }, { status: 201 });
    }
    return new Response('unexpected', { status: 599 });
  };
  return github;
}

const device = await crypto.subtle.generateKey({ name: 'ECDSA', namedCurve: 'P-256' }, true, ['sign', 'verify']);
const devicePub = new Uint8Array(await crypto.subtle.exportKey('raw', device.publicKey));
const deviceKid = await kidOf(devicePub);

function dailyBody(extra = {}) {
  return JSON.stringify({
    t: 'diag_daily',
    v: 1,
    fw: 'v0.1.20',
    at: 1792806690,
    since: 1792720290,
    hw: { scale: { driver: 'acaia', name: 'LUNAR' } },
    warnings: [{ n: 7, p: '<power bot>: connect failed after # ms: timed out (code #)' }],
    recent: ['[65179.623] W <brew bot>: no supported pusher service/characteristics usable'],
    ...extra,
  });
}

async function signedEnvelope(body, overrides = {}) {
  const sig = new Uint8Array(await crypto.subtle.sign({ name: 'ECDSA', hash: 'SHA-256' }, device.privateKey, enc.encode(body)));
  return { v: 1, kid: deviceKid, alg: 'ES256', pub: b64(devicePub), sig: b64(sig), body, ...overrides };
}

function post(envelope, { path = '/report', method = 'POST', raw } = {}) {
  const init = { method, headers: { 'content-type': 'application/json' } };
  if (method !== 'GET') init.body = raw ?? JSON.stringify(envelope);
  return new Request(`https://relay.example${path}`, init);
}

async function send(env, envelope, options) {
  const response = await worker.fetch(post(envelope, options), env);
  return { status: response.status, json: await response.json() };
}

let env;
let github;
beforeEach(() => {
  forgetToken();
  env = makeEnv();
  github = mockGitHub();
});

// --------------------------------------------------------------------------------- tests --

test('a good signed report is committed under reports/', async () => {
  const envelope = await signedEnvelope(dailyBody());
  const { status, json } = await send(env, envelope);
  assert.equal(status, 201);
  assert.deepEqual(json, { ok: true });

  assert.equal(github.puts.length, 1);
  const [path] = github.puts;
  assert.match(path, new RegExp(`^reports/\\d{4}-\\d{2}/${deviceKid}/\\d{8}T\\d{6}Z-diag_daily\\.json$`));
  const stored = github.files.get(path);
  assert.equal(stored.body, envelope.body, 'the signed string is kept as sent');
  assert.equal(stored.sig, envelope.sig);
  assert.equal(stored.report.fw, 'v0.1.20');
});

test('the GitHub App JWT is RS256, signed by the App key, issued by the App id', async () => {
  await send(env, await signedEnvelope(dailyBody()));
  const [jwt] = github.jwts;
  const [h, p, s] = jwt.split('.');
  const header = JSON.parse(Buffer.from(h, 'base64url'));
  const claims = JSON.parse(Buffer.from(p, 'base64url'));
  assert.deepEqual(header, { alg: 'RS256', typ: 'JWT' });
  assert.equal(claims.iss, '123456');
  assert.ok(claims.exp - claims.iat <= 600);
  const ok = nodeVerify('sha256', Buffer.from(`${h}.${p}`), createPublicKey(rsa.publicKey), Buffer.from(s, 'base64url'));
  assert.ok(ok, 'JWT signature verifies with the App public key');
});

test('a PKCS#1 key (as GitHub downloads it) works too', async () => {
  const pkcs1 = generateKeyPairSync('rsa', {
    modulusLength: 2048,
    publicKeyEncoding: { type: 'spki', format: 'pem' },
    privateKeyEncoding: { type: 'pkcs1', format: 'pem' },
  });
  assert.match(pkcs1.privateKey, /BEGIN RSA PRIVATE KEY/);
  env = makeEnv({ GITHUB_APP_KEY: pkcs1.privateKey });
  const { status } = await send(env, await signedEnvelope(dailyBody()));
  assert.equal(status, 201);
  const [h, p, s] = github.jwts[0].split('.');
  assert.ok(nodeVerify('sha256', Buffer.from(`${h}.${p}`), createPublicKey(pkcs1.publicKey), Buffer.from(s, 'base64url')));
});

test('the installation token is cached across reports', async () => {
  await send(env, await signedEnvelope(dailyBody()));
  await send(env, await signedEnvelope(dailyBody({ at: 1792806691 })));
  assert.equal(github.tokenCalls, 1);
});

test('reports in the same second take the next free second', async () => {
  const fixed = Date.UTC(2026, 9, 23, 8, 15, 30, 500);
  const realNow = Date.now;
  Date.now = () => fixed;
  try {
    for (let i = 0; i < 3; i++) assert.equal((await send(env, await signedEnvelope(dailyBody({ at: i })))).status, 201);
  } finally {
    Date.now = realNow;
  }
  assert.deepEqual(
    [...github.files.keys()].map((p) => p.split('/').pop()),
    ['20261023T081530Z-diag_daily.json', '20261023T081531Z-diag_daily.json', '20261023T081532Z-diag_daily.json'],
  );
});

test('a tampered body is refused with 401', async () => {
  const envelope = await signedEnvelope(dailyBody());
  envelope.body = envelope.body.replace('v0.1.20', 'v0.1.21');
  const { status, json } = await send(env, envelope);
  assert.equal(status, 401);
  assert.equal(json.ok, false);
  assert.equal(json.error, 'bad signature');
  assert.equal(github.puts.length, 0);
});

test('a kid that is not the hash of pub is refused with 401', async () => {
  const envelope = await signedEnvelope(dailyBody(), { kid: '0123456789abcdef0123' });
  const { status, json } = await send(env, envelope);
  assert.equal(status, 401);
  assert.equal(json.error, 'kid does not match pub');
});

test('a body over 32 KB is refused with 413', async () => {
  const envelope = await signedEnvelope(dailyBody({ recent: ['x'.repeat(33 * 1024)] }));
  const { status, json } = await send(env, envelope);
  assert.equal(status, 413);
  assert.equal(json.ok, false);
});

test('a body holding a MAC address is refused with 422', async () => {
  const envelope = await signedEnvelope(dailyBody({ recent: ['[12.0] W scale c4:4f:33:12:ab:9e: lost'] }));
  const { status, json } = await send(env, envelope);
  assert.equal(status, 422);
  assert.equal(json.error, 'body holds a MAC address');
});

test('a body holding an IPv4 address is refused with 422', async () => {
  const envelope = await signedEnvelope(dailyBody({ recent: ['[12.0] I wifi: got 192.168.1.42'] }));
  const { status, json } = await send(env, envelope);
  assert.equal(status, 422);
  assert.equal(json.error, 'body holds an IP address');
});

test('versions, timestamps, hex addresses and UUIDs are not mistaken for MACs or IPs', async () => {
  const envelope = await signedEnvelope(
    dailyBody({
      recent: ['[65179.623] W update: v0.1.20 at 0x42012345', '[1.5] I ble: service 0000fee7-0000-1000-8000-00805f9b34fb'],
    }),
  );
  const { status } = await send(env, envelope);
  assert.equal(status, 201);
});

test('an unknown report kind is refused with 400', async () => {
  const envelope = await signedEnvelope(dailyBody({ t: 'diag_weekly' }));
  const { status, json } = await send(env, envelope);
  assert.equal(status, 400);
  assert.equal(json.error, 'unknown report kind');
});

test('not JSON, missing or extra envelope fields, and an unknown alg are refused with 400', async () => {
  assert.equal((await send(env, null, { raw: '{not json' })).status, 400);
  const good = await signedEnvelope(dailyBody());
  const { sig, ...missing } = good;
  assert.equal((await send(env, missing)).status, 400);
  assert.equal((await send(env, { ...good, extra: 1 })).status, 400);
  assert.equal((await send(env, { ...good, alg: 'RS256' })).status, 400);
  assert.equal((await send(env, { ...good, v: 2 })).status, 400);
  assert.equal(github.puts.length, 0);
});

test('the 5th daily report of a day is refused with 429; crashes have their own allowance', async () => {
  for (let i = 0; i < 4; i++) {
    const { status } = await send(env, await signedEnvelope(dailyBody({ at: 1792806690 + i })));
    assert.equal(status, 201, `report ${i + 1}`);
  }
  const fifth = await send(env, await signedEnvelope(dailyBody({ at: 1792806699 })));
  assert.equal(fifth.status, 429);
  assert.equal(fifth.json.ok, false);
  assert.equal(fifth.json.error, 'too many reports today');
  assert.equal(github.files.size, 4);

  const crash = JSON.stringify({ t: 'diag_crash', v: 1, fw: 'v0.1.19', at: 1792806700, reset: 'CRASH (exception)' });
  assert.equal((await send(env, await signedEnvelope(crash))).status, 201);
});

test('the daily count starts again on the next UTC day', async () => {
  const realNow = Date.now;
  try {
    Date.now = () => Date.UTC(2026, 9, 23, 23, 59, 0);
    for (let i = 0; i < 4; i++) await send(env, await signedEnvelope(dailyBody({ at: i })));
    assert.equal((await send(env, await signedEnvelope(dailyBody()))).status, 429);
    Date.now = () => Date.UTC(2026, 9, 24, 0, 0, 1);
    assert.equal((await send(env, await signedEnvelope(dailyBody()))).status, 201);
  } finally {
    Date.now = realNow;
  }
});

test('10 crash reports a day are allowed, the 11th is refused with 429', async () => {
  const crash = JSON.stringify({ t: 'diag_crash', v: 1, fw: 'v0.1.19', at: 1792806700 });
  const day = new Date().toISOString().slice(0, 10);
  env.LIMITS.map.set(`dev:${deviceKid}:${day}:diag_crash`, '9');
  assert.equal((await send(env, await signedEnvelope(crash))).status, 201);
  assert.equal((await send(env, await signedEnvelope(crash))).status, 429);
});

test('an emulator report skips the signature and is filed under emulator/ only', async () => {
  const body = dailyBody();
  const envelope = { v: 1, kid: deviceKid, alg: 'EMULATOR-UNSIGNED', pub: b64(devicePub), sig: '', body };
  const { status } = await send(env, envelope);
  assert.equal(status, 201);
  assert.equal(github.puts.length, 1);
  assert.match(github.puts[0], new RegExp(`^emulator/\\d{4}-\\d{2}/${deviceKid}/\\d{8}T\\d{6}Z-diag_daily\\.json$`));
  // Its count is kept apart from the same key's signed reports.
  assert.ok([...env.LIMITS.map.keys()].every((k) => k.startsWith('emu:')));
});

test('an emulator report still needs a kid that matches its pub, and a clean body', async () => {
  const envelope = { v: 1, kid: '0123456789abcdef0123', alg: 'EMULATOR-UNSIGNED', pub: b64(devicePub), sig: '', body: dailyBody() };
  assert.equal((await send(env, envelope)).status, 401);
  const dirty = { ...envelope, kid: deviceKid, body: dailyBody({ recent: ['10.0.0.1'] }) };
  assert.equal((await send(env, dirty)).status, 422);
});

test('a failed store answers 502 and does not use up the allowance', async () => {
  github = mockGitHub({ putStatus: 500 });
  const { status } = await send(env, await signedEnvelope(dailyBody()));
  assert.equal(status, 502);
  assert.equal(env.LIMITS.map.size, 0);
});

test('other paths are 404 and other methods are 405', async () => {
  assert.equal((await send(env, {}, { path: '/' })).status, 404);
  assert.equal((await send(env, {}, { path: '/reports' })).status, 404);
  const response = await worker.fetch(post(null, { method: 'GET' }), env);
  assert.equal(response.status, 405);
  assert.equal(response.headers.get('allow'), 'POST');
});

test('report paths use UTC month and second', () => {
  const at = new Date(Date.UTC(2026, 0, 2, 3, 4, 5));
  assert.equal(
    reportPath({ alg: 'ES256', kid: 'abcdefabcdefabcdefab' }, 'diag_crash', at),
    'reports/2026-01/abcdefabcdefabcdefab/20260102T030405Z-diag_crash.json',
  );
});
