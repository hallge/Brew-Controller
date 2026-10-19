import zlib from 'node:zlib';
// Usage: BC_DECODER=<path to a prepared copy of Beanconqueror's brew-handoff.decoder.ts> node --experimental-transform-types --no-warnings beanconqueror-handoff-check.mjs
// The decoder is GPL-3.0 and is NOT copied into this repository. See README.md in this folder for how it was prepared.
const { collectHandoffPayload, decodeHandoffPayload } = await import(process.env.BC_DECODER);

// Controller-shaped shot. SIMULATED curve (not real data), quantised like CurvePoint: 0.01 s, 0.01 g.
const pts = []; let w = 0;
for (let cs = 0; cs <= 3300; cs += 10) {            // 10 Hz like a Lunar
  const t = cs / 100;
  const flow = t < 8 ? 0 : Math.min(1.9, (t - 8) * 0.6);
  w += flow * 0.1 + (cs > 800 ? (Math.random() - 0.5) * 0.08 : 0);
  pts.push({ cs, cg: Math.round(w * 100) });
}
const final = pts.at(-1).cg / 100;
function envelope(over = {}) {
  let tPrev = 0, wPrev = 0; const t = [], wt = [];
  for (const p of pts) { t.push((p.cs - tPrev) * 10); tPrev = p.cs; wt.push(+(((p.cg - wPrev) / 100) * 10).toFixed(2)); wPrev = p.cg; }
  const env = {
    v: 1, app: { name: 'BBW controller', version: '0.0' },
    brew: { date: '2026-10-12T08:30:00Z', doseIn: { value: 18, unit: 'g' }, waterIn: { value: 36, unit: 'ml' },
            beverageOut: { value: final, unit: 'g' }, brewTime: 33, grindSize: '246', grinderName: 'Mahlkonig E64 WS',
            preparationMethod: 'Espresso', firstDropTime: 8.4, rating: 4, note: '' },
    bean: { name: 'Idjwi Island', roaster: 'Portrait', roastingDate: '2026-03-03' },
    flow: { fidelity: 'full', t, waterDispensed: wt.map(() => 0), weight: wt },
    imported: { source: 'bbw-controller', sourceName: 'BBW controller', schema: 1 },
  };
  return structuredClone(Object.assign(env, over));
}
function link(env) {
  const b64 = zlib.gzipSync(Buffer.from(JSON.stringify(env))).toString('base64url');
  const chunks = b64.match(/.{1,400}/g);
  return { url: `beanconqueror://ADD_BREW?len=${b64.length}&` + chunks.map((c, i) => `shareBrew${i}=${c}`).join('&'), len: b64.length, n: chunks.length };
}
async function tryIt(name, env) {
  try { const l = link(env); const d = await decodeHandoffPayload(collectHandoffPayload(l.url));
    console.log('OK  ', name.padEnd(34), `chars=${l.len} chunks=${l.n} urlLen=${l.url.length} samples=${d.flow?.t.length ?? 0}`); return d; }
  catch (e) { console.log('FAIL', name.padEnd(34), e.message); }
}
const base = envelope();
console.log('samples', pts.length, 'final', final, 'json bytes', JSON.stringify(base).length);
const d = await tryIt('controller-shaped shot', base);
console.log('  decoded dose/out/time:', d.brew.doseIn, d.brew.beverageOut, d.brew.brewTime, 'rating', d.brew.rating, 'grindSize', JSON.stringify(d.brew.grindSize));
const noWater = envelope(); delete noWater.brew.waterIn; await tryIt('no waterIn', noWater);
const r5 = envelope(); r5.brew.rating = 4.5; await tryIt('fractional rating', r5);
const unit = envelope(); unit.brew.beverageOut.unit = 'ml'; await tryIt('beverageOut in ml', unit);
const grind = envelope(); grind.brew.grindSize = 246; await tryIt('grindSize numeric', grind);
const nof = envelope(); delete nof.flow; await tryIt('summary only (no flow)', nof);
const big = envelope(); const f = big.flow; for (let i = 0; i < 6; i++) for (const k of ['t','waterDispensed','weight']) f[k] = f[k].concat(f[k]); await tryIt('>10,000 samples', big);
const dt = envelope(); dt.brew.date = '2026-10-12T08:30:00'; await tryIt('date without zone', dt);
const bad = envelope(); bad.brew.brewTime = -1; await tryIt('negative brewTime', bad);
const ver = envelope(); ver.v = 2; await tryIt('v:2', ver);
const m = envelope(); m.metrics = [{ key: 'bbw_flow', name: 'Flow', unit: 'g/s', kind: 'measured', t: [0, 1000], v: [0, 1.2] }]; await tryIt('with metrics (flow g/s)', m);
// decigram accumulation round-trip error vs original
let acc = 0, maxErr = 0; base.flow.weight.forEach((x, i) => { acc += x / 10; maxErr = Math.max(maxErr, Math.abs(acc - pts[i].cg / 100)); });
console.log('max weight round-trip error g (fractional decigrams):', maxErr.toFixed(4));
