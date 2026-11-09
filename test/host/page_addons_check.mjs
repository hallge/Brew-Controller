// The web page's Add-ons card (the ADDONS block of src/net/WebPage.h), run against made-up answers:
// hidden with no add-ons, a secret never shown, the recipe map, the shot's own target.
//    node test/host/page_addons_check.mjs
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const page = fs.readFileSync(path.join(here, '..', '..', 'src', 'net', 'WebPage.h'), 'utf8');
const a = page.indexOf('/*ADDONS*/'), b = page.indexOf('/*END-ADDONS*/');
if (a < 0 || b < 0) throw new Error('missing block ADDONS');
let checks = 0, failed = 0;
const ok = (what, cond) => { checks++; if (!cond) { failed++; console.log('FAIL ' + what); } };

const els = {};
const $ = id => (els[id] ||= { style: {}, innerHTML: '', textContent: '' });
const esc = s => String(s == null ? '' : s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const f1 = v => (+v).toFixed(1), f2 = v => (+v).toFixed(2);
const posts = [];
let answers = {};
const get = async u => { if (!(u in answers)) throw new Error('no ' + u); return answers[u]; };
const post = async (u, body) => { posts.push({ u, body }); return { ok: true }; };
const done = () => true, say = () => {}, ask = async () => true;
const document = { querySelectorAll: () => [] };
const api = new Function('$', 'esc', 'f1', 'f2', 'get', 'post', 'done', 'say', 'ask', 'document',
  page.slice(a, b) + '\nreturn { loadAddons, adEnable, adMap };')($, esc, f1, f2, get, post, done, say, ask, document);

// No add-ons in this firmware: the card stays hidden.
answers = { '/api/addons': { compiled: 0, addons: [] }, '/api/library': { recipes: [] } };
await api.loadAddons();
ok('hidden with none compiled in', $('adcard').style.display === 'none');

answers = {
  '/api/library': { recipes: [{ id: 1, name: 'Double' }, { id: 2, name: 'Triple <b>' }] },
  '/api/addons': { compiled: 1, addons: [{
    id: 'test-grinder', name: 'Test grinder', version: '1', kind: 'grinder', api_ok: true, enabled: true,
    link: 'connected', status: 'grind 4', shot_feedback_possible: false,
    fields: [{ key: 'grinder_url', label: 'Grinder address', type: 'url', value: 'https://10.0.0.9', help: '' },
             { key: 'owner_password', label: 'Owner password', type: 'secret', set: true, help: '' }],
    options: { dose_sets_target: true, select_recipe: false, shot_feedback: false },
    map: [{ grinder_recipe: 2, recipe_id: 2, source: 'auto', recipe: 'Triple <b>' }],
    last_grind: { age_s: 120, grinder_recipe: 2, dose_g: 24.058, gap_um: 67.7, mapped: true, recipe_id: 2, setting: 68, shot_target_g: 48.12, note: '' },
    chip_ram_used: 120, chip_low_during: 21000 }] },
};
await api.loadAddons();
const html = $('adlist').innerHTML;
ok('shown with one compiled in', $('adcard').style.display === '');
ok('its address is shown', html.includes('value="https://10.0.0.9"'));
ok('a secret is never shown, only that it is set', html.includes('type="password"') && html.includes('blank keeps it') && !/Secret|value="[^"]*"[^>]*type="password"/.test(html));
ok('the recipe map shows the automatic match', html.includes('matched by its dose and yield') && /<option value="2" selected>Triple &lt;b&gt;<\/option>/.test(html));
ok('names are escaped', !html.includes('Triple <b>'));
ok('the last grind and the next shot\'s own target', html.includes('24.06 g') && html.includes('67.7 µm') && html.includes('next shot stops at 48.12 g') && html.includes('setting 68'));
ok('shot feedback is not offered when the add-on has none', !html.includes('shot_feedback'));
ok('chip RAM is shown', html.includes('120 bytes'));
await api.adEnable('test-grinder', false);
ok('switching off posts on=0', posts.some(p => p.u === '/api/addons/enable?id=test-grinder&on=0'));
await api.adMap('test-grinder', 3, 1);
ok('mapping posts the grinder recipe and recipe id', posts.some(p => p.u === '/api/addons/map?id=test-grinder&grinder=3&recipe=1'));
// Built for another interface: cannot be switched on.
answers['/api/addons'].addons[0].api_ok = false;
await api.loadAddons();
ok('another interface: the switch is disabled', $('adlist').innerHTML.includes('disabled') && $('adlist').innerHTML.includes('another add-on interface'));

console.log(`page addons: ${checks} checks, ${failed} failed`);
process.exit(failed ? 1 : 0);
