// Checks the web page's hand-off to Beanconqueror, its import of a Beanconqueror backup, and its mapping of a
// visualizer.coffee result, by running the page's own functions (the marked blocks in src/net/WebPage.h).
//
//     node --experimental-transform-types --no-warnings test/host/page_interop_check.mjs
//
// Optional, for the strongest check:
//   BC_DECODER=file:///.../brew-handoff.decoder.ts   Beanconqueror's own decoder, prepared as described in
//                                                    research/validation/README.md (it is GPL-3.0 and is not in this repo)
//   BC_FIXTURE=<path>                                a Beanconqueror brew export (Visualizer's test/files/beanconqueror_real.json is one)
//   BBW_URL=http://localhost:8080                    a running controller or emulator: its last shot's real data is used (read only)
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const here = path.dirname(fileURLToPath(import.meta.url));
const page = fs.readFileSync(path.join(here, '..', '..', 'src', 'net', 'WebPage.h'), 'utf8');
const block = n => { const a = page.indexOf(`/*${n}*/`), b = page.indexOf(`/*END-${n}*/`); if (a < 0 || b < 0) throw new Error('missing block ' + n); return page.slice(a, b); };
const code = ['BC-HANDOFF', 'BC-IMPORT', 'VZ-LOOKUP'].map(block).join('\n');
const api = new Function(`${code}\nreturn { bcEnvelope, bcLink, bcBeanToCoffee, bcParseExport, vzToCoffee, VZROAST, dateIso, isoToYmd, clip, b64url };`)();

let fails = 0, checks = 0;
const ok = (name, cond, extra = '') => { checks++; if (!cond) { fails++; console.log('FAIL ' + name + (extra ? '  ' + extra : '')); } };

// ---- a controller shot, shaped like /api/shots + /api/shot/data (a 33 s shot at 10 Hz, 0.01 g and 0.01 s resolution)
const time_s = [], weight_g = []; let w = 0;
for (let cs = 0; cs <= 3300; cs += 10) { time_s.push(cs / 100); const t = cs / 100; w += (t < 8 ? 0 : Math.min(1.9, (t - 8) * 0.6)) * 0.1 + (cs > 800 ? Math.sin(cs) * 0.02 : 0); weight_g.push(Math.round(w * 100) / 100); }
let shot = { id: 7, timestamp: '2026-10-12T08:30:00Z', recipe: 'Double', final_weight_g: weight_g.at(-1), target_weight_g: 36, shot_time_s: 33, first_drop_s: 8.4, dose_g: 18,
  rating: 4, taste: 'balanced', end_reason: 'weight', grind_um: 246, grind_text: '246', has_data: true, tds_pct: 9.2, extraction_pct: 18.4 };
let data = { time_s, weight_g };
if (process.env.BBW_URL) {                                  // real data from a running controller, read only
  try {
    const list = await (await fetch(process.env.BBW_URL + '/api/shots')).json();
    const real = list.find(x => x.has_data);
    if (real) { shot = Object.assign({}, real, { grind_text: String(real.grind_um || '') }); data = await (await fetch(process.env.BBW_URL + '/api/shot/data?id=' + real.id)).json(); console.log(`using real shot ${real.id}: ${data.time_s.length} readings`); }
  } catch (e) { console.log('BBW_URL not reachable; using the synthetic shot'); }
}
const coffee = { name: 'Idjwi Island', roaster: 'Portrait', origin: 'DR Congo', region: 'Kivu', producer: 'Co-op', varietal: 'Bourbon', process: 'washed', altitudeM: 1900, notes: 'plum, cocoa', roastDate: 20260303 };
const env = api.bcEnvelope(shot, data, coffee, { model: 'Mahlkonig E64 WS' }, '');

// ---- structure, against the contract in Beanconqueror's docs/brew-import-api.md
ok('envelope version 1', env.v === 1);
ok('required brew fields present', env.brew.date && env.brew.waterIn?.unit === 'ml' && env.brew.beverageOut?.unit === 'g' && typeof env.brew.brewTime === 'number' && env.brew.preparationMethod === 'Espresso');
ok('dose in grams', !(shot.dose_g > 0) || (env.brew.doseIn?.value === shot.dose_g && env.brew.doseIn.unit === 'g'));
ok('rating is an integer, or absent when unrated', shot.rating ? Number.isInteger(env.brew.rating) : !('rating' in env.brew));
ok('grind setting is a string', typeof env.brew.grindSize === 'string');
ok('bean mapped', env.bean.name === 'Idjwi Island' && env.bean.roaster === 'Portrait' && env.bean.farmer === 'Co-op' && env.bean.roastingDate === '2026-03-03' && env.bean.elevation === '1900');
ok('flow is delta coded and matches in length', env.flow.t.length === data.time_s.length && env.flow.weight.length === env.flow.t.length && env.flow.waterDispensed.length === env.flow.t.length);
let acc = 0, tacc = 0, maxErr = 0; env.flow.weight.forEach((d, i) => { acc += d / 10; tacc += env.flow.t[i]; maxErr = Math.max(maxErr, Math.abs(acc - data.weight_g[i]), Math.abs(tacc / 1000 - data.time_s[i])); });
ok('the curve survives the delta coding to 0.01 g / 0.01 s', maxErr < 0.011, 'max error ' + maxErr);
ok('the note carries taste and TDS when there are some', (!shot.taste || new RegExp(shot.taste).test(env.brew.note || '')) && (!shot.tds_pct || /TDS/.test(env.brew.note || '')));
// a shot with no clock is sent with the date it is given; rating 0 and no data are left out
const bare = api.bcEnvelope(Object.assign({}, shot, { timestamp: '', rating: 0, first_drop_s: 0, dose_g: 0, taste: '', tds_pct: 0, extraction_pct: 0 }), null, null, null, '2026-10-13T00:00:00.000Z');
ok('no clock: dated as asked; no rating, no flow, no bean', bare.brew.date === '2026-10-13T00:00:00.000Z' && !('rating' in bare.brew) && !bare.flow && !bare.bean && !('doseIn' in bare.brew) && !('firstDripTime' in bare.brew));
// more than 10,000 readings are thinned and said to be
const huge = api.bcEnvelope(shot, { time_s: Array.from({ length: 30000 }, (_, i) => i * 0.01), weight_g: Array.from({ length: 30000 }, (_, i) => i * 0.001) }, null, null, '');
ok('over 10,000 readings: downsampled', huge.flow.fidelity === 'downsampled' && huge.flow.t.length <= 10000);

// ---- the link
const link = await api.bcLink(env);
const q = new URLSearchParams(link.split('?')[1]);
const chunks = [...q.keys()].filter(k => /^shareBrew\d+$/.test(k)).map(k => q.get(k));
ok('link scheme and chunking', link.startsWith('beanconqueror://ADD_BREW?len=') && chunks.length >= 1 && chunks.every(c => c.length <= 400) && chunks.join('').length === +q.get('len'));
ok('payload is unpadded base64url', /^[A-Za-z0-9_-]+$/.test(chunks.join('')));
console.log(`link: ${link.length} characters in ${chunks.length} chunks`);
if (process.env.BC_DECODER) {
  const { collectHandoffPayload, decodeHandoffPayload } = await import(process.env.BC_DECODER);
  try {
    const d = await decodeHandoffPayload(collectHandoffPayload(link));
    ok('Beanconqueror’s own decoder accepts the link', d.v === 1 && d.brew.beverageOut.value === env.brew.beverageOut.value);
    ok('and reads the flow back', d.flow && d.flow.t.length === env.flow.t.length);
  } catch (e) { ok('Beanconqueror’s own decoder accepts the link', false, e.message); }
} else console.log('BC_DECODER not set: the decoder check was skipped');

// ---- importing a Beanconqueror backup (BEANS)
let baseBean = { name: 'Holm', roaster: 'onoma', roast: 'AMERICAN_ROAST', roastingDate: '2022-12-08T06:24:32.574Z', aromatics: 'Schokolade, Haselnuss, Rund', weight: 250, openDate: '2022-12-20T08:00:00.000Z',
  bean_information: [{ country: 'Country 1', region: 'Region 1', farmer: 'Farmer 1', variety: 'Variety 1', processing: 'Processing 1', elevation: '1,850 masl' }], config: { uuid: 'abc' } };
if (process.env.BC_FIXTURE) { try { baseBean = JSON.parse(fs.readFileSync(process.env.BC_FIXTURE, 'utf8')).bean; console.log('using the real Beanconqueror fixture bean'); } catch (e) { /* keep the built-in one */ } }
const r1 = api.bcBeanToCoffee(baseBean);
ok('bean to coffee', r1.coffee.name === baseBean.name.slice(0, 23) && r1.coffee.roaster === baseBean.roaster.slice(0, 23) && r1.coffee.origin === (baseBean.bean_information[0].country || '').slice(0, 23) && r1.coffee.process === (baseBean.bean_information[0].processing || '').slice(0, 15));
ok('roast date and level', r1.coffee.roastDate === api.isoToYmd(baseBean.roastingDate) && r1.coffee.roastLevel >= 0 && r1.coffee.roastLevel <= 5);
ok('bag details', r1.bag.roastDate === r1.coffee.roastDate && r1.bag.frozenDate === 0 && (!baseBean.weight || r1.bag.weightG === Math.round(baseBean.weight)) && (!baseBean.openDate || r1.bag.openedDate === api.isoToYmd(baseBean.openDate)));
ok('long names are cut to what the controller keeps', api.bcBeanToCoffee(Object.assign({}, baseBean, { name: 'A very long coffee name indeed that will not fit' })).coffee.name.length === 23);
ok('elevation read as metres', api.bcBeanToCoffee(Object.assign({}, baseBean, { bean_information: [{ elevation: '1,850 masl' }] })).coffee.altitudeM === 1850 && api.bcBeanToCoffee(Object.assign({}, baseBean, { bean_information: [{ elevation: 'high' }] })).coffee.altitudeM === 0 && api.bcBeanToCoffee(Object.assign({}, baseBean, { bean_information: [{ elevation: '9000' }] })).coffee.altitudeM === 0);
const frozenBean = api.bcBeanToCoffee(Object.assign({}, baseBean, { frozenDate: '2023-01-02T00:00:00.000Z', unfrozenDate: '2023-02-01T00:00:00.000Z', finished: true }));
ok('freezer dates and finished flag', frozenBean.bag.frozenDate === 20230102 && frozenBean.bag.thawDate === 20230201 && frozenBean.finished);
const parsed = api.bcParseExport({ BEANS: [baseBean, Object.assign({}, baseBean, { name: 'Done', finished: true }), { roaster: 'nameless' }] }, false);
ok('export: finished and nameless beans skipped', parsed.beans.length === 1 && parsed.skipped.length === 2 && parsed.total === 3);
ok('export: finished beans can be included', api.bcParseExport({ BEANS: [Object.assign({}, baseBean, { finished: true })] }, true).beans.length === 1);
ok('a single-brew export is read too', api.bcParseExport({ bean: baseBean }, false).beans.length === 1);
ok('an unrelated file has no beans', api.bcParseExport({ SETTINGS: [] }, false).beans.length === 0);
ok('quotes and newlines cannot reach the controller’s fields', !/["\n]/.test(api.bcBeanToCoffee(Object.assign({}, baseBean, { name: 'Say "hi"\nthere' })).coffee.name));

// ---- a visualizer.coffee canonical coffee (shape from a real answer of its API)
const vz = { id: '44bf466b', canonical_roaster_id: 'c76c', name: '/025 Ethiopia Mr. Ocholo Bedecho', roast_level: 'Light', country: 'Ethiopia', region: 'West Guji', farmer: 'Mr. Ocholo Bedecho',
  variety: 'Ethiopian Heirlooms', elevation: null, processing: 'Washed', tasting_notes: 'Bergamot, Floral, Jasmine, Black Tea', canonical_roaster_name: 'A Matter of Concrete' };
const v = api.vzToCoffee(vz);
ok('visualizer: fields mapped', v.roaster === 'A Matter of Concrete' && v.origin === 'Ethiopia' && v.region === 'West Guji' && v.process === 'Washed' && v.roastLevel === 1 && v.altitudeM === 0);
ok('visualizer: long values cut', v.name.length <= 23 && v.notes.length <= 47 && v.producer.length <= 31);
for (const [text, level] of [['Light', 1], ['Medium-Light', 2], ['Medium', 3], ['Medium-Dark', 4], ['Dark', 5], ['', 0], ['espresso', 0], ['City+', 3], ['Full City', 4], ['French', 5]]) ok(`visualizer roast "${text}" -> ${level}`, api.VZROAST(text) === level, String(api.VZROAST(text)));

console.log(fails ? `\n${fails} of ${checks} FAILED` : `page interop: ${checks} checks, 0 failed`);
process.exit(fails ? 1 : 0);
