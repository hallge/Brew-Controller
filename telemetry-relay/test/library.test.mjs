// Run with: node --test telemetry-relay/test
// The shared coffee library's endpoints: POST /library/put and GET /library/get.
import { describe, test, beforeEach } from 'node:test';
import assert from 'node:assert/strict';
import { generateKeyPairSync } from 'node:crypto';

import worker, { forgetToken, kidOf, libraryPathKind, libraryReadable } from '../src/worker.js';

const enc = new TextEncoder();
const b64 = (bytes) => Buffer.from(bytes).toString('base64');
const REPO = 'owner/Community-Coffee';

// ----------------------------------------------------------------------------- fixtures --

class MemoryKV {
  constructor() {
    this.map = new Map();
    this.writes = 0;
  }
  async get(key) {
    return this.map.has(key) ? this.map.get(key) : null;
  }
  async put(key, value) {
    this.writes++;
    this.map.set(key, String(value));
  }
}

const rsa = generateKeyPairSync('rsa', {
  modulusLength: 2048,
  publicKeyEncoding: { type: 'spki', format: 'pem' },
  privateKeyEncoding: { type: 'pkcs8', format: 'pem' },
});

function makeEnv() {
  return {
    LIMITS: new MemoryKV(),
    KEYS: new MemoryKV(),
    GITHUB_APP_ID: '123456',
    GITHUB_INSTALLATION_ID: '7890',
    GITHUB_APP_KEY: rsa.privateKey,
    REPORTS_REPO: 'owner/bbw-diagnostics',
    LIBRARY_REPO: REPO,
  };
}

// A stand-in for api.github.com: installation tokens, inbox commits and published files on main.
function mockGitHub({ putStatus = 201 } = {}) {
  const github = { inbox: new Map(), published: new Map(), puts: [], reads: [] };
  globalThis.fetch = async (input, init = {}) => {
    const url = new URL(typeof input === 'string' ? input : input.url);
    const headers = new Headers(init.headers);
    if (init.method === 'POST' && url.pathname === '/app/installations/7890/access_tokens') {
      return Response.json({ token: 'ghs_lib', expires_at: new Date(Date.now() + 3600e3).toISOString() }, { status: 201 });
    }
    const prefix = `/repos/${REPO}/contents/`;
    assert.equal(headers.get('authorization'), 'Bearer ghs_lib');
    if (init.method === 'PUT' && url.pathname.startsWith(prefix)) {
      const path = decodeURIComponent(url.pathname.slice(prefix.length));
      const payload = JSON.parse(init.body);
      assert.equal(payload.branch, 'inbox');
      github.puts.push(path);
      if (putStatus !== 201) return new Response('{}', { status: putStatus });
      if (github.inbox.has(path)) return new Response('{"message":"already exists"}', { status: 422 });
      github.inbox.set(path, Buffer.from(payload.content, 'base64').toString('utf8'));
      return Response.json({ content: { path } }, { status: 201 });
    }
    if (init.method === 'GET' && url.pathname.startsWith(prefix)) {
      assert.equal(url.searchParams.get('ref'), 'main');
      assert.equal(headers.get('accept'), 'application/vnd.github.raw+json');
      const path = decodeURIComponent(url.pathname.slice(prefix.length));
      github.reads.push(path);
      if (!github.published.has(path)) return new Response('{"message":"Not Found"}', { status: 404 });
      return new Response(github.published.get(path), { status: 200 });
    }
    return new Response('unexpected', { status: 599 });
  };
  return github;
}

// The Workers Cache API, in memory.
function mockCache() {
  const store = new Map();
  globalThis.caches = {
    default: {
      async match(request) {
        const hit = store.get(request.url);
        return hit ? hit.clone() : undefined;
      },
      async put(request, response) {
        store.set(request.url, response);
      },
    },
  };
  return store;
}

async function makeController() {
  const pair = await crypto.subtle.generateKey({ name: 'ECDSA', namedCurve: 'P-256' }, true, ['sign', 'verify']);
  const pub = new Uint8Array(await crypto.subtle.exportKey('raw', pair.publicKey));
  return { pair, pub, kid: await kidOf(pub) };
}

// sealRecord (src/core/Share.h): {"v":1,"kid","alg","sig","body"}, signed over the body text.
async function seal(controller, body, overrides = {}) {
  const sig = new Uint8Array(
    await crypto.subtle.sign({ name: 'ECDSA', hash: 'SHA-256' }, controller.pair.privateKey, enc.encode(body)),
  );
  return JSON.stringify({ v: 1, kid: controller.kid, alg: 'ES256', sig: b64(sig), body, ...overrides });
}

// buildKeyRecord (src/core/Share.h): t, v, at, alg, key.
function keyBody(controller) {
  return JSON.stringify({ t: 'key', v: 1, at: '2026-10-23T08:00:00Z', alg: 'ES256', key: b64(controller.pub) });
}

function coffeeBody(extra = {}) {
  return JSON.stringify({ t: 'coffee', v: 1, record: 2, at: '2026-10-23T08:01:00Z', coffee: { local_id: 1, name: 'Kenya AA' }, ...extra });
}

async function keyRecord(controller) {
  return seal(controller, keyBody(controller));
}

async function put(env, path, record, { ip = '203.0.113.7', raw } = {}) {
  const request = new Request('https://relay.example/library/put', {
    method: 'POST',
    headers: { 'content-type': 'application/json', 'cf-connecting-ip': ip },
    body: raw ?? JSON.stringify({ path, record }),
  });
  const response = await worker.fetch(request, env, {});
  return { status: response.status, json: await response.json() };
}

async function get(env, path) {
  const query = path === undefined ? '' : `?path=${encodeURIComponent(path)}`;
  const response = await worker.fetch(new Request(`https://relay.example/library/get${query}`), env, {});
  return { status: response.status, text: await response.text(), headers: response.headers };
}

// Scoped in a suite: test/index.mjs runs this file and relay.test.mjs together, and their hooks
// must not reach each other's tests.
describe('library', () => {
  let env;
  let github;
  let me;
  beforeEach(async () => {
    forgetToken();
    env = makeEnv();
    github = mockGitHub();
    delete globalThis.caches;
    me = await makeController();
  });

  // ------------------------------------------------------------------------------- put --

  test('a key record, then a coffee by the same kid, are both committed under inbox/', async () => {
    const key = await keyRecord(me);
    let r = await put(env, `keys/${me.kid}.json`, key);
    assert.equal(r.status, 201);
    assert.deepEqual(r.json, { ok: true });
    assert.equal(await env.KEYS.get(me.kid), b64(me.pub), 'the key is kept by kid');

    const coffee = await seal(me, coffeeBody());
    r = await put(env, `coffees/${me.kid}/c1-2.json`, coffee);
    assert.equal(r.status, 201);

    assert.deepEqual(github.puts, [`inbox/keys/${me.kid}.json`, `inbox/coffees/${me.kid}/c1-2.json`]);
    assert.equal(github.inbox.get(`inbox/keys/${me.kid}.json`), key, 'stored exactly as received');
    assert.equal(github.inbox.get(`inbox/coffees/${me.kid}/c1-2.json`), coffee);
    // Hashed address, never the address itself.
    for (const name of env.LIMITS.map.keys()) assert.ok(!name.includes('203.0.113.7'), name);
  });

  test('every firmware path shape is accepted (src/main.cpp)', () => {
    const kid = 'a'.repeat(20);
    const good = {
      [`keys/${kid}.json`]: 'key',
      [`coffees/${kid}/c3-17.json`]: 'coffee',
      [`grinders/${kid}/g1-0.json`]: 'grinder',
      [`services/${kid}/s2-40.json`]: 'service',
      [`machines/${kid}/m1-9.json`]: 'machine',
      [`tastes/${kid}-r12.json`]: 'taste',
      [`tastes/${kid}-r12-40.json`]: 'taste',
      [`svcreviews/${kid}-v3-41.json`]: 'service_review',
      [`svcreviews/${kid}-v3.json`]: 'service_review',
      [`shots/2026/10/${kid}-301.json`]: 'shot',
      [`shots/undated/${kid}-5.json`]: 'shot',
      [`dialins/2026/01/${kid}-77.json`]: 'dialin',
    };
    for (const [path, type] of Object.entries(good)) assert.deepEqual(libraryPathKind(path), { kid, type }, path);
    const bad = [
      `coffees/${kid}/x.json`,
      `coffees/${kid}/c01-2.json`,
      `machines/${kid}/m2-1.json`,
      `shots/2026-10/${kid}-1.json`,
      `shots/2026/13/${kid}-1.json`,
      `../keys/${kid}.json`,
      `/keys/${kid}.json`,
      `keys/${kid}.json/../x.json`,
      `keys/${kid.toUpperCase()}.json`,
      `status/${kid}.json`,
      `tastes/${kid}-r12-.json`,
      `tastes/${kid}-r12-4-5.json`,
    ];
    for (const path of bad) assert.equal(libraryPathKind(path), null, path);
  });

  test('a record before its key is 409 "send the key first"', async () => {
    const r = await put(env, `coffees/${me.kid}/c1-2.json`, await seal(me, coffeeBody()));
    assert.equal(r.status, 409);
    assert.deepEqual(r.json, { ok: false, error: 'send the key first' });
    assert.equal(github.puts.length, 0);
  });

  test('a path whose kid is not the record\'s is 403', async () => {
    await put(env, `keys/${me.kid}.json`, await keyRecord(me));
    const other = await makeController();
    const r = await put(env, `coffees/${other.kid}/c1-2.json`, await seal(me, coffeeBody()));
    assert.equal(r.status, 403);
    // Nor can one controller send another's key under its own name.
    const r2 = await put(env, `keys/${me.kid}.json`, await keyRecord(other));
    assert.equal(r2.status, 403);
    assert.equal(github.puts.length, 1);
  });

  test('a path outside the patterns is 400', async () => {
    await put(env, `keys/${me.kid}.json`, await keyRecord(me));
    for (const path of [`coffees/${me.kid}/notes.json`, `index/${me.kid}.json`, `../main/keys/${me.kid}.json`]) {
      const r = await put(env, path, await seal(me, coffeeBody()));
      assert.equal(r.status, 400, path);
    }
    // A record in a folder for another type.
    const r = await put(env, `grinders/${me.kid}/g1-2.json`, await seal(me, coffeeBody()));
    assert.equal(r.status, 400);
    assert.equal(github.puts.length, 1);
  });

  test('a tampered signature is 401, for a key record and for a record after it', async () => {
    const flip = (record) => {
      const envelope = JSON.parse(record);
      const sig = Buffer.from(envelope.sig, 'base64');
      sig[10] ^= 0x01;
      envelope.sig = sig.toString('base64');
      return JSON.stringify(envelope);
    };
    let r = await put(env, `keys/${me.kid}.json`, flip(await keyRecord(me)));
    assert.equal(r.status, 401);
    assert.equal(await env.KEYS.get(me.kid), null);

    await put(env, `keys/${me.kid}.json`, await keyRecord(me));
    r = await put(env, `coffees/${me.kid}/c1-2.json`, flip(await seal(me, coffeeBody())));
    assert.equal(r.status, 401);
    // A body changed after signing.
    const envelope = JSON.parse(await seal(me, coffeeBody()));
    envelope.body = envelope.body.replace('Kenya', 'Kenja');
    r = await put(env, `coffees/${me.kid}/c1-2.json`, JSON.stringify(envelope));
    assert.equal(r.status, 401);
    assert.equal(github.puts.length, 1);
  });

  test('a key record whose kid is not the key\'s fingerprint is 401', async () => {
    const other = await makeController();
    // Signed by me, carrying my key, but naming another kid in the envelope and path.
    const record = await seal(me, keyBody(me), { kid: other.kid });
    const r = await put(env, `keys/${other.kid}.json`, record);
    assert.equal(r.status, 401);
    assert.equal(github.puts.length, 0);
  });

  test('an emulator record is 403', async () => {
    await put(env, `keys/${me.kid}.json`, await keyRecord(me));
    const record = JSON.stringify({ v: 1, kid: me.kid, alg: 'EMULATOR-UNSIGNED', sig: '', body: coffeeBody() });
    const r = await put(env, `coffees/${me.kid}/c1-2.json`, record);
    assert.equal(r.status, 403);
    assert.equal(github.puts.length, 1);
  });

  test('an oversize record or request is 413', async () => {
    await put(env, `keys/${me.kid}.json`, await keyRecord(me));
    const big = await seal(me, coffeeBody({ pad: 'x'.repeat(8300) }));
    assert.ok(big.length > 8192 && big.length < 16384);
    let r = await put(env, `coffees/${me.kid}/c1-2.json`, big);
    assert.equal(r.status, 413);
    r = await put(env, null, null, { raw: JSON.stringify({ path: `coffees/${me.kid}/c1-2.json`, record: 'x'.repeat(17000) }) });
    assert.equal(r.status, 413);
    assert.equal(github.puts.length, 1);
  });

  test('the 61st record of a day is 429, and only stored records count', async () => {
    assert.equal((await put(env, `keys/${me.kid}.json`, await keyRecord(me))).status, 201);
    for (let n = 1; n < 60; n++) {
      const r = await put(env, `coffees/${me.kid}/c1-${n}.json`, await seal(me, coffeeBody({ record: n })));
      assert.equal(r.status, 201, `record ${n + 1}`);
    }
    const r = await put(env, `coffees/${me.kid}/c1-60.json`, await seal(me, coffeeBody({ record: 60 })));
    assert.equal(r.status, 429);
    assert.equal(github.puts.length, 60);
  });

  test('a failed store answers 502 and is not counted', async () => {
    await put(env, `keys/${me.kid}.json`, await keyRecord(me));
    const before = new Map(env.LIMITS.map);
    github = mockGitHub({ putStatus: 500 });
    const errors = [];
    const original = console.error;
    console.error = (line) => errors.push(String(line));
    try {
      const r = await put(env, `coffees/${me.kid}/c1-2.json`, await seal(me, coffeeBody()));
      assert.equal(r.status, 502);
    } finally {
      console.error = original;
    }
    assert.deepEqual(env.LIMITS.map, before);
    assert.deepEqual(errors, ['library store failed: GitHub answered 500']);
  });

  test('20 key records per network address per day, then 429', async () => {
    for (let n = 0; n < 20; n++) {
      const c = await makeController();
      assert.equal((await put(env, `keys/${c.kid}.json`, await keyRecord(c))).status, 201);
    }
    const c = await makeController();
    assert.equal((await put(env, `keys/${c.kid}.json`, await keyRecord(c))).status, 429);
    assert.equal((await put(env, `keys/${c.kid}.json`, await keyRecord(c), { ip: '198.51.100.9' })).status, 201);
  });

  test('a blocked kid is 403', async () => {
    await put(env, `keys/${me.kid}.json`, await keyRecord(me));
    await env.LIMITS.put(`blocked:${me.kid}`, '1');
    const r = await put(env, `coffees/${me.kid}/c1-2.json`, await seal(me, coffeeBody()));
    assert.equal(r.status, 403);
    const k = await put(env, `keys/${me.kid}.json`, await keyRecord(me));
    assert.equal(k.status, 403);
    assert.equal(github.puts.length, 1);
  });

  test('GitHub\'s 422 (already there) counts as sent', async () => {
    github = mockGitHub({ putStatus: 422 });
    let r = await put(env, `keys/${me.kid}.json`, await keyRecord(me));
    assert.equal(r.status, 201);
    assert.deepEqual(r.json, { ok: true });
    assert.equal(await env.KEYS.get(me.kid), b64(me.pub), 'the key is kept: it is in the inbox');
    r = await put(env, `coffees/${me.kid}/c1-2.json`, await seal(me, coffeeBody()));
    assert.equal(r.status, 201);
  });

  test('put is POST only, and a body that is not {path, record} is 400', async () => {
    const response = await worker.fetch(new Request('https://relay.example/library/put'), env, {});
    assert.equal(response.status, 405);
    assert.equal(response.headers.get('allow'), 'POST');
    assert.equal((await put(env, null, null, { raw: 'not json' })).status, 400);
    assert.equal((await put(env, null, null, { raw: '{"path":"keys/x.json"}' })).status, 400);
  });

  // ------------------------------------------------------------------------------- get --

  test('get serves a status file and an index file, and caches them', async () => {
    const cache = mockCache();
    const status = `status/${me.kid}.json`;
    const index = 'index/dialin/kenya-aa.json';
    github.published.set(status, '{"verdicts":[]}');
    github.published.set(index, '{"grind":[1,2]}');

    let r = await get(env, status);
    assert.equal(r.status, 200);
    assert.equal(r.text, '{"verdicts":[]}');
    assert.equal(r.headers.get('cache-control'), 'public, max-age=120');
    r = await get(env, index);
    assert.equal(r.status, 200);
    assert.equal(r.text, '{"grind":[1,2]}');
    assert.equal(r.headers.get('cache-control'), 'public, max-age=600');
    assert.equal(cache.size, 2);

    r = await get(env, index);
    assert.equal(r.text, '{"grind":[1,2]}');
    assert.deepEqual(github.reads, [status, index], 'the second read came from the cache');
  });

  test('get refuses other paths', async () => {
    const kid = me.kid;
    for (const path of [
      undefined,
      `keys/${kid}.json`,
      `inbox/keys/${kid}.json`,
      'status/abc.json',
      `status/${kid}.txt`,
      'index/../keys/x.json',
      'index/a/../../b.json',
      '/index/a.json',
      'index/.json',
      'index/a.json?ref=inbox',
      'index//a.json',
    ]) {
      const r = await get(env, path);
      assert.equal(r.status, 400, String(path));
    }
    assert.equal(github.reads.length, 0);
    assert.equal(libraryReadable('index/fit/abc.json'), 600);
  });

  test('get answers 404 for a missing file, and does not cache it', async () => {
    const cache = mockCache();
    const r = await get(env, `status/${me.kid}.json`);
    assert.equal(r.status, 404);
    assert.equal(cache.size, 0);
  });

  test('get is GET only', async () => {
    const response = await worker.fetch(
      new Request(`https://relay.example/library/get?path=status/${me.kid}.json`, { method: 'POST', body: '{}' }),
      env,
      {},
    );
    assert.equal(response.status, 405);
    assert.equal(response.headers.get('allow'), 'GET');
  });
});
