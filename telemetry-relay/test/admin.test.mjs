// Run with: node --test telemetry-relay/test
// The admin console (src/admin.js): the Access check, each endpoint, the audit log, and the relay's
// refusals and counters written to D1. GitHub, KV and D1 are stand-ins; D1 is SQLite (node:sqlite)
// with schema.sql applied, so the schema is tested too.
import { describe, test, beforeEach } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { generateKeyPairSync } from 'node:crypto';
import { DatabaseSync } from 'node:sqlite';

import worker, { forgetToken, kidOf } from '../src/worker.js';
import { accessIdentity, checkBody, forgetAccessKeys } from '../src/admin.js';
import { bundle } from '../tools/build-admin.mjs';

const enc = new TextEncoder();
const b64 = (bytes) => Buffer.from(bytes).toString('base64');
const b64url = (bytes) => Buffer.from(bytes).toString('base64url');
const LIBRARY = 'owner/Community-Coffee';
const REPORTS = 'owner/bbw-diagnostics';
const TEAM = 'brewteam';
const AUD = 'aud-0123456789abcdef';
const EMAIL = 'admin@example.com';
const BASE = 'https://relay.example';

// ----------------------------------------------------------------------------- stand-ins --

class MemoryKV {
  constructor() {
    this.map = new Map();
    this.meta = new Map();
  }
  async get(key) {
    return this.map.has(key) ? this.map.get(key) : null;
  }
  async put(key, value, options = {}) {
    this.map.set(key, String(value));
    if (options.metadata) this.meta.set(key, options.metadata);
  }
  async delete(key) {
    this.map.delete(key);
    this.meta.delete(key);
  }
  async list({ prefix = '', limit = 1000 } = {}) {
    const keys = [...this.map.keys()].filter((k) => k.startsWith(prefix ?? '')).sort().slice(0, limit)
      .map((name) => (this.meta.has(name) ? { name, metadata: this.meta.get(name) } : { name }));
    return { keys, list_complete: true };
  }
}

// D1's prepare/bind/run/all/first over an in-memory SQLite database with schema.sql applied.
class D1 {
  constructor() {
    this.db = new DatabaseSync(':memory:');
    this.db.exec(readFileSync(new URL('../schema.sql', import.meta.url), 'utf8'));
  }
  prepare(sql) {
    const db = this.db;
    let args = [];
    const statement = {
      bind(...values) {
        args = values;
        return statement;
      },
      async run() {
        db.prepare(sql).run(...args);
        return { success: true };
      },
      async all() {
        return { results: db.prepare(sql).all(...args).map((r) => ({ ...r })) };
      },
      async first() {
        const row = db.prepare(sql).get(...args);
        return row ? { ...row } : null;
      },
    };
    return statement;
  }
  audit() {
    return this.db.prepare('SELECT * FROM audit ORDER BY id').all().map((r) => ({ ...r, detail: JSON.parse(r.detail) }));
  }
  counters() {
    return this.db.prepare('SELECT day, path, outcome, n FROM counters ORDER BY path, outcome').all().map((r) => ({ ...r }));
  }
}

const appKey = generateKeyPairSync('rsa', {
  modulusLength: 2048,
  publicKeyEncoding: { type: 'spki', format: 'pem' },
  privateKeyEncoding: { type: 'pkcs8', format: 'pem' },
});

function makeEnv() {
  return {
    LIMITS: new MemoryKV(),
    KEYS: new MemoryKV(),
    AUDIT: new D1(),
    GITHUB_APP_ID: '123456',
    GITHUB_INSTALLATION_ID: '7890',
    GITHUB_APP_KEY: appKey.privateKey,
    REPORTS_REPO: REPORTS,
    LIBRARY_REPO: LIBRARY,
    ACCESS_TEAM: TEAM,
    ACCESS_AUD: AUD,
  };
}

// Cloudflare Access: the team's signing key, its certs endpoint, and tokens.
const accessPair = await crypto.subtle.generateKey(
  { name: 'RSASSA-PKCS1-v1_5', modulusLength: 2048, publicExponent: new Uint8Array([1, 0, 1]), hash: 'SHA-256' },
  true,
  ['sign', 'verify'],
);
const otherPair = await crypto.subtle.generateKey(
  { name: 'RSASSA-PKCS1-v1_5', modulusLength: 2048, publicExponent: new Uint8Array([1, 0, 1]), hash: 'SHA-256' },
  true,
  ['sign', 'verify'],
);
const accessJwk = await crypto.subtle.exportKey('jwk', accessPair.publicKey);
const CERTS = { keys: [{ kid: 'k1', kty: 'RSA', alg: 'RS256', use: 'sig', n: accessJwk.n, e: accessJwk.e }] };

async function accessToken(overrides = {}, { kid = 'k1', key = accessPair.privateKey } = {}) {
  const now = Math.floor(Date.now() / 1000);
  const claims = { aud: [AUD], email: EMAIL, exp: now + 600, iat: now, nbf: now, iss: `https://${TEAM}.cloudflareaccess.com`, sub: 'u1', ...overrides };
  const head = b64url(enc.encode(JSON.stringify({ alg: 'RS256', kid, typ: 'JWT' })));
  const body = b64url(enc.encode(JSON.stringify(claims)));
  const sig = new Uint8Array(await crypto.subtle.sign('RSASSA-PKCS1-v1_5', key, enc.encode(`${head}.${body}`)));
  return `${head}.${body}.${b64url(sig)}`;
}

// GitHub, as routes: [method, regex on the path, handler(match, url, init, headers) -> Response].
// Raw file reads are answered from github.files, keyed "<ref>:<path>" ("default:<path>" with no ref).
function mockGitHub() {
  const github = { calls: [], files: new Map(), routes: [], certsFetched: 0 };
  globalThis.fetch = async (input, init = {}) => {
    const url = new URL(typeof input === 'string' ? input : input.url);
    const headers = new Headers(init.headers);
    const method = init.method ?? 'GET';
    if (url.hostname === `${TEAM}.cloudflareaccess.com`) {
      assert.equal(url.pathname, '/cdn-cgi/access/certs');
      github.certsFetched++;
      return Response.json(CERTS);
    }
    assert.equal(url.hostname, 'api.github.com');
    if (method === 'POST' && url.pathname === '/app/installations/7890/access_tokens') {
      return Response.json({ token: 'ghs_admin', expires_at: new Date(Date.now() + 3600e3).toISOString() }, { status: 201 });
    }
    assert.equal(headers.get('authorization'), 'Bearer ghs_admin');
    const body = init.body ? JSON.parse(init.body) : undefined;
    github.calls.push({ method, path: url.pathname, query: url.search, body });
    if (method === 'GET' && headers.get('accept') === 'application/vnd.github.raw+json') {
      const m = /^\/repos\/([^/]+\/[^/]+)\/contents\/(.+)$/.exec(url.pathname);
      const key = `${m[1]}@${url.searchParams.get('ref') ?? 'default'}:${decodeURIComponent(m[2])}`;
      return github.files.has(key) ? new Response(github.files.get(key)) : new Response('{"message":"Not Found"}', { status: 404 });
    }
    for (const [routeMethod, pattern, handler] of github.routes) {
      const match = routeMethod === method ? pattern.exec(url.pathname) : null;
      if (match) return handler(match, url, body);
    }
    return Response.json({ message: `unexpected ${method} ${url.pathname}` }, { status: 599 });
  };
  github.on = (method, pattern, handler) => github.routes.push([method, pattern, handler]);
  github.file = (repo, ref, path, text) => github.files.set(`${repo}@${ref}:${path}`, typeof text === 'string' ? text : JSON.stringify(text));
  return github;
}

async function admin(env, path, { method, body, token, headers = {} } = {}) {
  method ??= body === undefined ? 'GET' : 'POST';
  const h = { 'cf-connecting-ip': '203.0.113.9', ...headers };
  if (token !== null) h['cf-access-jwt-assertion'] = token ?? (await accessToken());
  if (body !== undefined) {
    h['content-type'] = 'application/json';
    if (!('x-bbw-admin' in headers)) h['x-bbw-admin'] = '1';
  }
  return worker.fetch(new Request(`${BASE}${path}`, { method, headers: h, body: body === undefined ? undefined : JSON.stringify(body) }), env);
}

async function makeController() {
  const pair = await crypto.subtle.generateKey({ name: 'ECDSA', namedCurve: 'P-256' }, true, ['sign', 'verify']);
  const pub = new Uint8Array(await crypto.subtle.exportKey('raw', pair.publicKey));
  return { pair, pub, kid: await kidOf(pub) };
}

async function seal(controller, body) {
  const sig = new Uint8Array(await crypto.subtle.sign({ name: 'ECDSA', hash: 'SHA-256' }, controller.pair.privateKey, enc.encode(body)));
  return JSON.stringify({ v: 1, kid: controller.kid, alg: 'ES256', sig: b64(sig), body });
}

// A coffee review as intake.py opens it: branch, pull request, files, the signed record on main.
const COFFEE = { t: 'coffee', v: 1, at: '2026-10-10T22:51:50Z', local_id: 4, record: 7, name: 'Idjwi Island', roaster: 'Portrait', origin: 'DR Congo' };

async function coffeeReview(github, env, controller, { body = COFFEE, number = 12 } = {}) {
  const branch = `review/${controller.kid}-c4-7`;
  const meta = JSON.stringify({ kind: 'coffee', kid: controller.kid, local_id: 4, record: 7, library_id: 'portrait/idjwi-island' });
  const pr = {
    number,
    title: 'Change Portrait / Idjwi Island',
    state: 'open',
    body: `A controller has sent this coffee.\n<!-- intake ${meta} -->`,
    created_at: new Date(Date.now() - 3 * 3600e3).toISOString(),
    html_url: `https://github.com/${LIBRARY}/pull/${number}`,
    head: { ref: branch, sha: 'headsha1', repo: { full_name: LIBRARY } },
    base: { ref: 'main', sha: 'basesha1' },
  };
  const other = { ...pr, number: 3, title: 'Add Portrait / Older', state: 'closed', merged_at: '2026-09-01T00:00:00Z', head: { ...pr.head, ref: `review/${controller.kid}-c2-1` } };
  const stranger = { ...pr, number: 9, title: 'Fix README', head: { ref: 'docs', sha: 'x', repo: { full_name: LIBRARY } } };
  github.on('GET', new RegExp(`^/repos/${LIBRARY}/pulls$`), (m, url) =>
    Response.json(url.searchParams.get('state') === 'open' ? [pr, stranger] : [pr, other, stranger]));
  github.on('GET', new RegExp(`^/repos/${LIBRARY}/pulls/${number}$`), () => Response.json(pr));
  github.on('GET', new RegExp(`^/repos/${LIBRARY}/pulls/${number}/files$`), () =>
    Response.json([
      { filename: 'catalog/portrait/idjwi-island.json', status: 'modified' },
      { filename: `pending/coffees/${controller.kid}/c4-7.json`, status: 'removed' },
    ]));
  const entry = { id: 'portrait/idjwi-island', name: 'Idjwi Island', roaster: 'Portrait', origin: 'DR Congo' };
  github.file(LIBRARY, 'headsha1', 'catalog/portrait/idjwi-island.json', JSON.stringify(entry, null, 1));
  github.file(LIBRARY, 'main', 'catalog/portrait/idjwi-island.json', { id: 'portrait/idjwi-island', name: 'Idjwi Island', roaster: 'Portrait', origin: 'Congo' });
  github.file(LIBRARY, 'main', `pending/coffees/${controller.kid}/c4-7.json`, await seal(controller, JSON.stringify(body)));
  github.file(LIBRARY, 'main', `status/${controller.kid}.json`, { v: 1, coffees: [{ local_id: 2, record: 1, state: 'published', library_id: 'portrait/older' }, { local_id: 4, record: 7, state: 'review' }] });
  await env.KEYS.put(controller.kid, b64(controller.pub));
  return pr;
}

beforeEach(() => {
  forgetToken();
  forgetAccessKeys();
});

// ------------------------------------------------------------------------------ Access --

describe('admin access', () => {
  test('a valid Access token opens the console, and its email is the actor', async () => {
    mockGitHub();
    const env = makeEnv();
    const page = await admin(env, '/admin/');
    assert.equal(page.status, 200);
    assert.match(page.headers.get('content-type'), /text\/html/);
    assert.match(page.headers.get('content-security-policy'), /script-src 'self'/);
    assert.match(await page.text(), /Relay admin/);
    assert.equal((await admin(env, '/admin/admin.js')).status, 200);
    assert.equal((await admin(env, '/admin')).status, 301);
    assert.equal(await accessIdentity(new Request(BASE, { headers: { 'cf-access-jwt-assertion': await accessToken() } }), env), EMAIL);
  });

  test('a wrong audience is refused', async () => {
    mockGitHub();
    const response = await admin(makeEnv(), '/admin/api/queue', { token: await accessToken({ aud: ['someone-else'] }) });
    assert.equal(response.status, 403);
  });

  test('an expired token is refused', async () => {
    mockGitHub();
    const past = Math.floor(Date.now() / 1000) - 60;
    const response = await admin(makeEnv(), '/admin/', { token: await accessToken({ exp: past }) });
    assert.equal(response.status, 403);
  });

  test('no token is refused, pages and API alike', async () => {
    mockGitHub();
    const env = makeEnv();
    assert.equal((await admin(env, '/admin/', { token: null })).status, 403);
    assert.equal((await admin(env, '/admin/api/queue', { token: null })).status, 403);
    assert.equal((await admin(env, '/admin/api/block', { token: null, body: { kid: 'a'.repeat(20) } })).status, 403);
    assert.equal(await env.LIMITS.get(`blocked:${'a'.repeat(20)}`), null);
  });

  test('a token signed by another key, or a garbled one, is refused', async () => {
    mockGitHub();
    const env = makeEnv();
    assert.equal((await admin(env, '/admin/', { token: await accessToken({}, { key: otherPair.privateKey }) })).status, 403);
    assert.equal((await admin(env, '/admin/', { token: await accessToken({}, { kid: 'k9' }) })).status, 403);
    assert.equal((await admin(env, '/admin/', { token: 'a.b.c' })).status, 403);
    assert.equal((await admin(env, '/admin/', { token: await accessToken({ email: undefined }) })).status, 403);
    assert.equal((await admin(env, '/admin/', { token: await accessToken({ iss: 'https://evil.cloudflareaccess.com' }) })).status, 403);
  });

  test('the console stays shut until ACCESS_TEAM and ACCESS_AUD are set', async () => {
    mockGitHub();
    const env = { ...makeEnv(), ACCESS_AUD: 'REPLACE_WITH_ACCESS_AUD' };
    assert.equal((await admin(env, '/admin/')).status, 403);
  });

  test("the team's keys are fetched once and kept", async () => {
    const github = mockGitHub();
    const env = makeEnv();
    await admin(env, '/admin/');
    await admin(env, '/admin/');
    assert.equal(github.certsFetched, 1);
  });

  test('an action without the console header is refused (no cross-site posts)', async () => {
    mockGitHub();
    const env = makeEnv();
    const kid = 'b'.repeat(20);
    const response = await admin(env, '/admin/api/block', { body: { kid }, headers: { 'x-bbw-admin': '0' } });
    assert.equal(response.status, 403);
    const crossSite = await admin(env, '/admin/api/block', { body: { kid }, headers: { 'x-bbw-admin': '1', origin: 'https://evil.example' } });
    assert.equal(crossSite.status, 403);
    assert.equal(await env.LIMITS.get(`blocked:${kid}`), null);
  });
});

// ---------------------------------------------------------------------------- endpoints --

describe('admin endpoints', () => {
  test('queue lists open review pull requests only, with kind, kid and age', async () => {
    const github = mockGitHub();
    const env = makeEnv();
    const controller = await makeController();
    await coffeeReview(github, env, controller);
    const data = await (await admin(env, '/admin/api/queue')).json();
    assert.equal(data.ok, true);
    assert.equal(data.actor, EMAIL);
    assert.equal(data.items.length, 1);
    const [item] = data.items;
    assert.equal(item.number, 12);
    assert.equal(item.kind, 'coffee');
    assert.equal(item.kid, controller.kid);
    assert.equal(item.from_fork, false);
    assert.ok(item.age_s >= 3 * 3600 - 5 && item.age_s <= 3 * 3600 + 60);
  });

  test('review redoes the checks, and shows the diff and the kid\'s earlier records', async () => {
    const github = mockGitHub();
    const env = makeEnv();
    const controller = await makeController();
    await coffeeReview(github, env, controller);
    const d = await (await admin(env, '/admin/api/review?n=12')).json();
    assert.equal(d.ok, true, d.error);
    for (const c of d.checks) assert.equal(c.ok, true, `${c.check}: ${c.detail}`);
    assert.deepEqual(d.checks.map((c) => c.check), ['envelope', 'fields', 'algorithm', 'body', 'key', 'signature', 'values', 'matches the review']);
    assert.equal(d.listed.path, 'catalog/portrait/idjwi-island.json');
    assert.equal(d.listed.check.ok, true);
    assert.equal(d.signed.body.name, 'Idjwi Island');
    assert.deepEqual(d.diff.find((r) => r.field === 'origin'), { field: 'origin', before: 'Congo', after: 'DR Congo', change: 'changed' });
    assert.equal(d.diff.find((r) => r.field === 'name').change, 'same');
    assert.deepEqual(d.history.pulls.map((p) => p.number), [3]);
    assert.equal(d.history.pulls[0].merged, true);
    assert.equal(d.history.status.coffees.length, 2);
    assert.ok(d.server_side_only.length >= 3);
    assert.equal(d.blocked, false);
  });

  test('review flags a value outside the firmware\'s range, and a key not on file', async () => {
    const github = mockGitHub();
    const env = makeEnv();
    const controller = await makeController();
    await coffeeReview(github, env, controller, { body: { ...COFFEE, altitude_m: 9000 } });
    let d = await (await admin(env, '/admin/api/review?n=12')).json();
    const values = d.checks.find((c) => c.check === 'values');
    assert.equal(values.ok, false);
    assert.match(values.detail, /altitude_m = 9000 is outside 1 to 4000/);

    await env.KEYS.delete(controller.kid);
    d = await (await admin(env, '/admin/api/review?n=12')).json();
    assert.equal(d.checks.find((c) => c.check === 'key').ok, false);
    // The library's own keys/<kid>.json is the fallback, checked as a key record.
    const keyBody = JSON.stringify({ t: 'key', v: 1, alg: 'ES256', key: b64(controller.pub) });
    github.file(LIBRARY, 'main', `keys/${controller.kid}.json`, await seal(controller, keyBody));
    d = await (await admin(env, '/admin/api/review?n=12')).json();
    assert.match(d.checks.find((c) => c.check === 'key').detail, /keys\//);
    assert.equal(d.checks.find((c) => c.check === 'signature').ok, true);
  });

  test('review refuses a pull request that is not a review', async () => {
    const github = mockGitHub();
    github.on('GET', new RegExp(`^/repos/${LIBRARY}/pulls/9$`), () => Response.json({ number: 9, head: { ref: 'docs' } }));
    const response = await admin(makeEnv(), '/admin/api/review?n=9');
    assert.equal(response.status, 400);
  });

  test('the ported range checks follow verify_record.py', () => {
    const service = {
      t: 'service', v: 1, record: 41, local_id: 3, name: 'Trade Coffee', url: 'https://www.drinktrade.com/',
      manage_url: 'https://www.drinktrade.com/account', platform: 'ordergroove', kind: 'coffee', ships_to: ['US'],
      cadences: [14, 28], price_from_cents: 1500, currency: 'USD', style: ['multi_roaster'], sourcing: ['fair_trade'], color: '#aa3311',
    };
    assert.equal(checkBody(service), 'service');
    const refused = (body) => {
      try {
        checkBody(body);
      } catch (e) {
        return e.message;
      }
      return 'accepted';
    };
    assert.match(refused({ ...service, manage_url: 'https://www.drinktrade.com/account?ref=me' }), /query/);
    assert.match(refused({ ...service, manage_url: 'https://shop.example/portal/0123456789abcdef0123456789ab/x' }), /own link/);
    assert.match(refused({ ...service, image: 'https://x.example/i.png?token=abc' }), /credential/);
    assert.match(refused({ ...service, ships_to: ['usa'] }), /country/);
    assert.match(refused({ ...service, currency: undefined }), /currency/);
    const dialin = {
      t: 'dialin', v: 1, coffee: { local_id: 4, name: 'Idjwi Island' }, recipe: { dose_g: 18, yield_g: 36, target_time_s: 26 },
      grinder: { model_id: 1, unit: 'um', um_per_unit: 1, calibration: 0 }, grind: 246, result: { time_s: 25.6, basis: 'total', shots: 3 },
    };
    assert.equal(checkBody(dialin), 'dialin');
    assert.match(refused({ ...dialin, recipe: { dose_g: 180, yield_g: 36 } }), /outside/);
    assert.match(refused({ ...dialin, grinder: { ...dialin.grinder, divisor: 3 } }), /divisor/);
    assert.match(refused({ ...dialin, grinder: { ...dialin.grinder, sub_steps: 9, divisor: 10 } }), /no divisor/);
    assert.match(refused({ t: 'grinder', v: 1, record: 3, local_id: 2, model: 'Home Built 64' }), /unit is missing/);
    assert.match(refused({ t: 'machine', v: 1, record: 1, local_id: 1, maker: 'A', model: 'B', control: 'C', style: 'start_button', hold_preinfusion: 1, tap_preinfusion: 0, power_toggle: 0 }), /hold pre-infusion/);
    assert.match(refused({ t: 'coffee', v: 1, local_id: 4, record: 7, name: '<script>alert(1)</script>xxxxxxxx' }), /text of 1 to 23/);
    assert.match(refused({ t: 'other', v: 1 }), /unknown record type/);
  });

  test('approve squash-merges the pull request and is audited', async () => {
    const github = mockGitHub();
    const env = makeEnv();
    const controller = await makeController();
    await coffeeReview(github, env, controller);
    github.on('PUT', new RegExp(`^/repos/${LIBRARY}/pulls/12/merge$`), () => Response.json({ merged: true, sha: 'mergesha' }));
    const response = await admin(env, '/admin/api/approve', { method: 'POST', body: { number: 12, sha: 'headsha1' } });
    assert.equal(response.status, 200);
    const merge = github.calls.find((c) => c.method === 'PUT');
    assert.deepEqual(merge.body, { merge_method: 'squash', sha: 'headsha1' });
    const [row] = env.AUDIT.audit();
    assert.equal(row.actor, EMAIL);
    assert.equal(row.action, 'approve');
    assert.equal(row.target, '#12');
    assert.equal(row.result, 'ok');
    assert.equal(row.detail.sha, 'mergesha');
    assert.match(row.ip_hash, /^[0-9a-f]{16}$/);
    assert.ok(!row.ip_hash.includes('203'));
  });

  test('approve refuses a pull request changed since it was looked at, and audits that', async () => {
    const github = mockGitHub();
    const env = makeEnv();
    await coffeeReview(github, env, await makeController());
    const response = await admin(env, '/admin/api/approve', { method: 'POST', body: { number: 12, sha: 'oldsha' } });
    assert.equal(response.status, 409);
    assert.ok(!github.calls.some((c) => c.method === 'PUT'));
    const [row] = env.AUDIT.audit();
    assert.equal(row.result, 'failed');
    assert.match(row.error, /changed since/);
  });

  test('approve reports GitHub refusing the merge, audited as failed', async () => {
    const github = mockGitHub();
    const env = makeEnv();
    await coffeeReview(github, env, await makeController());
    github.on('PUT', new RegExp(`^/repos/${LIBRARY}/pulls/12/merge$`), () => Response.json({ message: 'Pull Request is not mergeable' }, { status: 405 }));
    const response = await admin(env, '/admin/api/approve', { method: 'POST', body: { number: 12 } });
    assert.equal(response.status, 502);
    assert.match((await response.json()).error, /not mergeable/);
    assert.equal(env.AUDIT.audit()[0].result, 'failed');
  });

  test('reject comments "Declined: <note>", then closes, and is audited', async () => {
    const github = mockGitHub();
    const env = makeEnv();
    await coffeeReview(github, env, await makeController());
    github.on('POST', new RegExp(`^/repos/${LIBRARY}/issues/12/comments$`), () => Response.json({ id: 1 }, { status: 201 }));
    github.on('PATCH', new RegExp(`^/repos/${LIBRARY}/pulls/12$`), () => Response.json({ state: 'closed' }));
    const response = await admin(env, '/admin/api/reject', { method: 'POST', body: { number: 12, note: 'already listed\nas portrait/idjwi' } });
    assert.equal(response.status, 200);
    const writes = github.calls.filter((c) => c.method !== 'GET');
    assert.deepEqual(writes.map((c) => c.method), ['POST', 'PATCH']);
    assert.equal(writes[0].body.body, 'Declined: already listed as portrait/idjwi');
    assert.deepEqual(writes[1].body, { state: 'closed' });
    const [row] = env.AUDIT.audit();
    assert.equal(row.action, 'reject');
    assert.equal(row.result, 'ok');
    assert.equal(row.detail.note, 'already listed as portrait/idjwi');
  });

  test('reject needs a note', async () => {
    mockGitHub();
    const env = makeEnv();
    const response = await admin(env, '/admin/api/reject', { method: 'POST', body: { number: 12, note: '  ' } });
    assert.equal(response.status, 400);
    assert.equal(env.AUDIT.audit()[0].result, 'failed');
  });

  test('hide moves the entry under hidden/ in one commit, starts the index rebuild, and is audited', async () => {
    const github = mockGitHub();
    const env = makeEnv();
    github.on('GET', new RegExp(`^/repos/${LIBRARY}/git/ref/heads/main$`), () => Response.json({ object: { sha: 'c1' } }));
    github.on('GET', new RegExp(`^/repos/${LIBRARY}/git/commits/c1$`), () => Response.json({ tree: { sha: 't1' } }));
    github.on('GET', new RegExp(`^/repos/${LIBRARY}/contents/catalog/portrait/idjwi-island.json$`), () => Response.json({ sha: 'blob1' }));
    github.on('POST', new RegExp(`^/repos/${LIBRARY}/git/trees$`), () => Response.json({ sha: 't2' }, { status: 201 }));
    github.on('POST', new RegExp(`^/repos/${LIBRARY}/git/commits$`), () => Response.json({ sha: 'c2' }, { status: 201 }));
    github.on('PATCH', new RegExp(`^/repos/${LIBRARY}/git/refs/heads/main$`), () => Response.json({ object: { sha: 'c2' } }));
    github.on('POST', new RegExp(`^/repos/${LIBRARY}/actions/workflows/intake.yml/dispatches$`), () => new Response(null, { status: 204 }));

    const response = await admin(env, '/admin/api/hide', { method: 'POST', body: { path: 'catalog/portrait/idjwi-island.json', reason: 'duplicate' } });
    assert.equal(response.status, 200, await response.clone().text());
    const tree = github.calls.find((c) => c.path.endsWith('/git/trees'));
    assert.deepEqual(tree.body, {
      base_tree: 't1',
      tree: [
        { path: 'hidden/catalog/portrait/idjwi-island.json', mode: '100644', type: 'blob', sha: 'blob1' },
        { path: 'catalog/portrait/idjwi-island.json', mode: '100644', type: 'blob', sha: null },
      ],
    });
    const commit = github.calls.find((c) => c.path.endsWith('/git/commits') && c.method === 'POST');
    assert.deepEqual(commit.body.parents, ['c1']);
    assert.ok(!commit.body.message.includes(EMAIL), 'the public commit names no one');
    assert.deepEqual(github.calls.find((c) => c.path.endsWith('/refs/heads/main')).body, { sha: 'c2', force: false });
    assert.deepEqual(github.calls.find((c) => c.path.endsWith('/dispatches')).body, { ref: 'main' });
    const [row] = env.AUDIT.audit();
    assert.equal(row.action, 'hide');
    assert.equal(row.target, 'catalog/portrait/idjwi-island.json');
    assert.equal(row.result, 'ok');
    assert.equal(row.detail.reason, 'duplicate');
  });

  test('hide refuses anything but a published entry', async () => {
    mockGitHub();
    const env = makeEnv();
    for (const path of ['status/abc.json', 'catalog/../keys/x.json', '.github/workflows/intake.yml', 'catalog/a.json']) {
      assert.equal((await admin(env, '/admin/api/hide', { method: 'POST', body: { path } })).status, 400, path);
    }
    assert.ok(env.AUDIT.audit().every((r) => r.action === 'hide' && r.result === 'failed'));
  });

  test('hide that moved the file but could not start the rebuild is audited as partial', async () => {
    const github = mockGitHub();
    const env = makeEnv();
    github.on('GET', /git\/ref\/heads\/main$/, () => Response.json({ object: { sha: 'c1' } }));
    github.on('GET', /git\/commits\/c1$/, () => Response.json({ tree: { sha: 't1' } }));
    github.on('GET', /contents\/grinders\/home-built-64.json$/, () => Response.json({ sha: 'blob1' }));
    github.on('POST', /git\/trees$/, () => Response.json({ sha: 't2' }, { status: 201 }));
    github.on('POST', /git\/commits$/, () => Response.json({ sha: 'c2' }, { status: 201 }));
    github.on('PATCH', /git\/refs\/heads\/main$/, () => Response.json({}));
    github.on('POST', /dispatches$/, () => Response.json({ message: 'Resource not accessible by integration' }, { status: 403 }));
    const response = await admin(env, '/admin/api/hide', { method: 'POST', body: { path: 'grinders/home-built-64.json' } });
    assert.equal(response.status, 200);
    assert.match((await response.json()).warning, /did not start/);
    assert.equal(env.AUDIT.audit()[0].result, 'partial');
  });

  test('block and unblock set and clear blocked:<kid>, each audited, and the relay obeys', async () => {
    const github = mockGitHub();
    const env = makeEnv();
    const controller = await makeController();
    let response = await admin(env, '/admin/api/block', { method: 'POST', body: { kid: controller.kid, note: 'spam' } });
    assert.equal(response.status, 200);
    const stored = JSON.parse(await env.LIMITS.get(`blocked:${controller.kid}`));
    assert.equal(stored.by, EMAIL);
    assert.equal(stored.note, 'spam');

    const blocked = await (await admin(env, '/admin/api/blocked')).json();
    assert.deepEqual(blocked.items.map((b) => b.kid), [controller.kid]);

    // The relay now refuses this kid, and that refusal is audited too.
    const put = await worker.fetch(new Request(`${BASE}/library/put`, {
      method: 'POST',
      body: JSON.stringify({ path: `keys/${controller.kid}.json`, record: await seal(controller, JSON.stringify({ t: 'key', v: 1, alg: 'ES256', key: b64(controller.pub) })) }),
    }), env);
    assert.equal(put.status, 403);

    response = await admin(env, '/admin/api/unblock', { method: 'POST', body: { kid: controller.kid } });
    assert.equal(response.status, 200);
    assert.equal(await env.LIMITS.get(`blocked:${controller.kid}`), null);
    assert.equal((await admin(env, '/admin/api/block', { method: 'POST', body: { kid: 'not-a-kid' } })).status, 400);

    const rows = env.AUDIT.audit();
    assert.deepEqual(rows.map((r) => [r.actor, r.action, r.result]), [
      [EMAIL, 'block', 'ok'],
      ['relay', 'refuse', 'refused'],
      [EMAIL, 'unblock', 'ok'],
      [EMAIL, 'block', 'failed'],
    ]);
    assert.equal(rows[1].error, 'this key is blocked');
    assert.equal(github.calls.length, 0);
  });

  test('health shows workflow runs, relay counters and KV hints', async () => {
    const github = mockGitHub();
    const env = makeEnv();
    const run = (id, conclusion) => ({ id, status: 'completed', conclusion, event: 'workflow_dispatch', head_branch: 'main', created_at: '2026-10-25T08:00:00Z', html_url: `https://github.com/x/${id}` });
    github.on('GET', /workflows\/intake.yml\/runs$/, () => Response.json({ workflow_runs: [run(1, 'success'), run(2, 'failure')] }));
    github.on('GET', /workflows\/review-outcome.yml\/runs$/, () => Response.json({ workflow_runs: [run(3, 'success')] }));
    github.on('GET', /workflows\/poke.yml\/runs$/, () => Response.json({ message: 'Not Found' }, { status: 404 }));
    // Some relay traffic first: a refused report and a refused library put.
    await worker.fetch(new Request(`${BASE}/report`, { method: 'POST', body: '{"v":1}' }), env);
    await worker.fetch(new Request(`${BASE}/report`, { method: 'POST', body: '{"v":1}' }), env);
    await env.KEYS.put('c'.repeat(20), 'x');

    const d = await (await admin(env, '/admin/api/health')).json();
    assert.equal(d.ok, true, d.error);
    assert.deepEqual(d.workflows.map((w) => [w.file, w.runs.length, w.failures ?? null]), [['intake.yml', 2, 1], ['review-outcome.yml', 1, 0], ['poke.yml', 0, null]]);
    assert.match(d.workflows[2].error, /not found/);
    assert.equal(d.counters.length, 1);
    assert.equal(d.counters[0].path, '/report');
    assert.equal(d.counters[0].n, 2);
    assert.match(d.counters[0].outcome, /^refused:envelope fields/);
    assert.equal(d.kv.reads_estimate, 2);
    assert.equal(d.kv.writes_estimate, 0);
    assert.equal(d.kv.keys_stored, 1);
    assert.equal(d.kv.writes_free, 1000);
  });

  test('devices lists the stored keys with first seen, and the diagnostics units with firmware', async () => {
    const github = mockGitHub();
    const env = makeEnv();
    // A key stored through the relay gets its first-seen time.
    const controller = await makeController();
    github.on('PUT', new RegExp(`^/repos/${LIBRARY}/contents/inbox/`), () => Response.json({}, { status: 201 }));
    const put = await worker.fetch(new Request(`${BASE}/library/put`, {
      method: 'POST',
      body: JSON.stringify({ path: `keys/${controller.kid}.json`, record: await seal(controller, JSON.stringify({ t: 'key', v: 1, alg: 'ES256', key: b64(controller.pub) })) }),
    }), env);
    assert.equal(put.status, 201);
    await env.KEYS.put('d'.repeat(20), 'old key, from before first-seen');
    await env.LIMITS.put(`blocked:${'d'.repeat(20)}`, '1');

    const now = new Date();
    const month = now.toISOString().slice(0, 7);
    const unitA = 'a'.repeat(20);
    const unitB = 'b'.repeat(20);
    github.on('GET', new RegExp(`^/repos/${REPORTS}/contents/reports$`), () =>
      Response.json([{ name: month, type: 'dir', sha: 'm1' }, { name: '2020-01', type: 'dir', sha: 'old' }]));
    github.on('GET', new RegExp(`^/repos/${REPORTS}/git/trees/m1$`), () =>
      Response.json({
        truncated: false,
        tree: [
          { path: unitA, type: 'tree' },
          { path: `${unitA}/20261001T080000Z-diag_daily.json`, type: 'blob' },
          { path: `${unitA}/20261002T080000Z-diag_crash.json`, type: 'blob' },
          { path: `${unitB}/20261003T090000Z-diag_daily.json`, type: 'blob' },
        ],
      }));
    github.file(REPORTS, 'default', `reports/${month}/${unitA}/20261002T080000Z-diag_crash.json`, { report: { t: 'diag_crash', fw: 'v0.1.19' } });
    github.file(REPORTS, 'default', `reports/${month}/${unitB}/20261003T090000Z-diag_daily.json`, { report: { t: 'diag_daily', fw: 'v0.1.20' } });

    const d = await (await admin(env, '/admin/api/devices')).json();
    assert.equal(d.ok, true, d.error);
    assert.equal(d.keys.total, 2);
    const fresh = d.keys.items.find((k) => k.kid === controller.kid);
    assert.ok(Date.parse(fresh.first_seen) > Date.now() - 60e3);
    const old = d.keys.items.find((k) => k.kid === 'd'.repeat(20));
    assert.equal(old.first_seen, null);
    assert.equal(old.blocked, true);
    assert.deepEqual(d.units.items.map((u) => [u.kid, u.reports, u.firmware, u.latest]), [
      [unitB, 1, 'v0.1.20', '2026-10-03T09:00:00Z'],
      [unitA, 2, 'v0.1.19', '2026-10-02T08:00:00Z'],
    ]);
    assert.ok(!github.calls.some((c) => c.path.endsWith('/git/trees/old')), 'only this month and last');
  });

  test('hidden lists what is under hidden/', async () => {
    const github = mockGitHub();
    github.on('GET', new RegExp(`^/repos/${LIBRARY}/git/trees/main$`), () => Response.json({ tree: [{ path: 'hidden', type: 'tree', sha: 'h1' }, { path: 'catalog', type: 'tree', sha: 'x' }] }));
    github.on('GET', new RegExp(`^/repos/${LIBRARY}/git/trees/h1$`), () => Response.json({ tree: [{ path: 'catalog', type: 'tree' }, { path: 'catalog/a/b.json', type: 'blob' }] }));
    const d = await (await admin(makeEnv(), '/admin/api/hidden')).json();
    assert.deepEqual(d.items, ['hidden/catalog/a/b.json']);
  });

  test('published lists every entry on main, named from the index', async () => {
    const github = mockGitHub();
    github.on('GET', new RegExp(`^/repos/${LIBRARY}/git/trees/main$`), () => Response.json({ tree: [
      { path: 'catalog/portrait/idjwi-island.json', type: 'blob' },
      { path: 'grinders/niche-zero.json', type: 'blob' },
      { path: 'links/understanding-espresso.json', type: 'blob' },
      { path: 'links/broken.json', type: 'blob' },
      { path: 'hidden/catalog/a/b.json', type: 'blob' },
      { path: 'index/coffees.json', type: 'blob' },
    ] }));
    const index = {
      'coffees.json': { coffees: [{ id: 'portrait/idjwi-island', name: 'Idjwi Island', roaster: 'Portrait', origin: 'DR Congo' }] },
      'grinders.json': { grinders: [{ id: 'niche-zero', model: 'Niche Zero', unit: 'dial', min: 1, max: 50 }] },
      'links.json': { links: [{ id: 'understanding-espresso', title: 'Understanding Espresso', kind: 'playlist', site: 'youtube.com', topics: ['dialing_in'] }] },
    };
    for (const [name, doc] of Object.entries(index)) github.file(LIBRARY, 'default', `index/${name}`, doc);
    const d = await (await admin(makeEnv(), '/admin/api/published')).json();
    assert.deepEqual(d.counts, { coffee: 1, grinder: 1, link: 2 });
    const byId = Object.fromEntries(d.items.map((it) => [it.id, it]));
    assert.equal(byId['portrait/idjwi-island'].name, 'Idjwi Island');
    assert.equal(byId['portrait/idjwi-island'].detail, 'Portrait · DR Congo');
    assert.equal(byId['niche-zero'].detail, 'dial · 1–50');
    assert.ok(byId['understanding-espresso'].in_index && !byId.broken.in_index);
    assert.ok(!d.items.some((it) => it.path.startsWith('hidden/') || it.path.startsWith('index/')));
  });

  test('audit lists newest first, with filters and paging', async () => {
    mockGitHub();
    const env = makeEnv();
    for (const kid of ['1'.repeat(20), '2'.repeat(20), '3'.repeat(20)]) {
      await admin(env, '/admin/api/block', { method: 'POST', body: { kid, note: `n${kid[0]}` } });
    }
    await worker.fetch(new Request(`${BASE}/report`, { method: 'POST', body: 'nope' }), env);

    let d = await (await admin(env, '/admin/api/audit')).json();
    assert.deepEqual(d.items.map((r) => r.action), ['refuse', 'block', 'block', 'block']);
    assert.equal(d.items[0].error, 'not JSON');
    assert.equal(typeof d.items[1].detail, 'object');

    d = await (await admin(env, '/admin/api/audit?action=block&limit=2')).json();
    assert.deepEqual(d.items.map((r) => r.target), ['3'.repeat(20), '2'.repeat(20)]);
    assert.equal(d.more, true);
    d = await (await admin(env, `/admin/api/audit?action=block&before=${d.items[1].id}`)).json();
    assert.deepEqual(d.items.map((r) => r.target), ['1'.repeat(20)]);

    d = await (await admin(env, '/admin/api/audit?actor=relay')).json();
    assert.equal(d.items.length, 1);
    d = await (await admin(env, '/admin/api/audit?q=n2')).json();
    assert.deepEqual(d.items.map((r) => r.target), ['2'.repeat(20)]);
    d = await (await admin(env, '/admin/api/audit?q=%25')).json();
    assert.equal(d.items.length, 0, 'a % in the search is a literal');
    d = await (await admin(env, '/admin/api/audit?since=2999-01-01')).json();
    assert.equal(d.items.length, 0);
  });

  test('unknown API names are 404', async () => {
    mockGitHub();
    assert.equal((await admin(makeEnv(), '/admin/api/nothing')).status, 404);
    assert.equal((await admin(makeEnv(), '/admin/api/approve')).status, 404, 'actions are POST only');
  });
});

// ----------------------------------------------------------------- the relay's refusals --

describe('relay refusals and counters in D1', () => {
  test('a refused report is audited with its reason, kid and path, and counted', async () => {
    mockGitHub();
    const env = makeEnv();
    const kid = 'e'.repeat(20);
    const envelope = { v: 1, kid, alg: 'ES256', pub: b64(new Uint8Array(65).fill(4)), sig: 'AAAA', body: '{"t":"diag_daily","v":1}' };
    const response = await worker.fetch(new Request(`${BASE}/report`, {
      method: 'POST',
      headers: { 'cf-connecting-ip': '198.51.100.7' },
      body: JSON.stringify(envelope),
    }), env);
    assert.equal(response.status, 401);
    const [row] = env.AUDIT.audit();
    assert.equal(row.actor, 'relay');
    assert.equal(row.action, 'refuse');
    assert.equal(row.target, kid);
    assert.equal(row.result, 'refused');
    assert.equal(row.error, 'kid does not match pub');
    assert.deepEqual(row.detail, { path: '/report', status: 401, kid });
    assert.match(row.ip_hash, /^[0-9a-f]{16}$/);
    assert.deepEqual(env.AUDIT.counters().map((c) => [c.path, c.outcome, c.n]), [['/report', 'refused:kid does not match pub', 1]]);
  });

  test('a library record before its key is audited (409), and a stored key is counted as ok:key', async () => {
    const github = mockGitHub();
    github.on('PUT', new RegExp(`^/repos/${LIBRARY}/contents/inbox/`), () => Response.json({}, { status: 201 }));
    const env = makeEnv();
    const controller = await makeController();
    const coffeePath = `coffees/${controller.kid}/c4-7.json`;
    let response = await worker.fetch(new Request(`${BASE}/library/put`, {
      method: 'POST',
      body: JSON.stringify({ path: coffeePath, record: await seal(controller, JSON.stringify(COFFEE)) }),
    }), env);
    assert.equal(response.status, 409);
    const [row] = env.AUDIT.audit();
    assert.equal(row.error, 'send the key first');
    assert.deepEqual(row.detail, { path: '/library/put', status: 409, kid: controller.kid, library_path: coffeePath });

    response = await worker.fetch(new Request(`${BASE}/library/put`, {
      method: 'POST',
      body: JSON.stringify({ path: `keys/${controller.kid}.json`, record: await seal(controller, JSON.stringify({ t: 'key', v: 1, alg: 'ES256', key: b64(controller.pub) })) }),
    }), env);
    assert.equal(response.status, 201);
    response = await worker.fetch(new Request(`${BASE}/library/put`, {
      method: 'POST',
      body: JSON.stringify({ path: coffeePath, record: await seal(controller, JSON.stringify(COFFEE)) }),
    }), env);
    assert.equal(response.status, 201);
    assert.equal(env.AUDIT.audit().length, 1, 'stored records are counted, not audited');
    assert.deepEqual(env.AUDIT.counters().map((c) => [c.outcome, c.n]), [['ok', 1], ['ok:key', 1], ['refused:send the key first', 1]]);
  });

  test('a 429 is audited and counted as 429', async () => {
    mockGitHub();
    const env = makeEnv();
    const controller = await makeController();
    await env.KEYS.put(controller.kid, b64(controller.pub));
    await env.LIMITS.put(`lib:${controller.kid}:${new Date().toISOString().slice(0, 10)}`, '60');
    const response = await worker.fetch(new Request(`${BASE}/library/put`, {
      method: 'POST',
      body: JSON.stringify({ path: `coffees/${controller.kid}/c4-7.json`, record: await seal(controller, JSON.stringify(COFFEE)) }),
    }), env);
    assert.equal(response.status, 429);
    assert.equal(env.AUDIT.audit()[0].result, '429');
    assert.deepEqual(env.AUDIT.counters().map((c) => c.outcome), ['429']);
  });

  test('a failing D1 never stops the relay answering', async () => {
    mockGitHub();
    const env = makeEnv();
    env.AUDIT = { prepare() { throw new Error('D1 down'); } };
    const errors = console.error;
    console.error = () => {};
    try {
      const response = await worker.fetch(new Request(`${BASE}/report`, { method: 'POST', body: '{"v":1}' }), env);
      assert.equal(response.status, 400);
    } finally {
      console.error = errors;
    }
  });

  test('the bundled pages match admin/', () => {
    assert.equal(readFileSync(new URL('../src/admin-pages.js', import.meta.url), 'utf8'), bundle(), 'run node tools/build-admin.mjs');
  });
});

describe('admin: Community Hub links', () => {
  test('a link entry is checked as the library checks it', async () => {
    const { checkListed } = await import('../src/admin.js');
    const ok = { id: 'understanding-espresso', url: 'https://www.youtube.com/playlist?list=PLx', title: 'Understanding Espresso', kind: 'playlist', topics: ['dialing_in'] };
    assert.doesNotThrow(() => checkListed('link', ok));
    assert.throws(() => checkListed('link', { ...ok, url: 'https://x.example/?token=1' }), /credential/);
    assert.throws(() => checkListed('link', { ...ok, topics: ['cats'] }), /topics/);
    assert.throws(() => checkListed('link', { ...ok, kind: 'meme' }), /kind/);
    const { topics, ...noTopics } = ok;
    assert.throws(() => checkListed('link', noTopics), /topics is missing/);
  });
});
