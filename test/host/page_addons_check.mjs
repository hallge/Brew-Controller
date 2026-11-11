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
  page.slice(a, b) + '\nreturn { loadAddons, adEnable, adMap, adSet, adSource, adModuleDo, adModuleUpload };')($, esc, f1, f2, get, post, done, say, ask, document);

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
// No card declared: no steering or comparison sections.
ok('no card, no steering section', !html.includes('Steering') && !html.includes('/api/addons/file'));

// An add-on that declares a card (interface 2): per-recipe mode, the numbers, the knob, the comparison.
answers['/api/addons'].addons[0].card = {
  steer: {
    title: 'Who steers the grind', note: 'Off until you turn it on <i>',
    modes: ['Off', 'Shadow', 'Grinder', 'Controller'], capMode: 2, capLabel: 'Cap (s)',
    recipes: [{ key: '2', label: 'Triple <b>', mode: 2, cap: 3, note: 'gate open' }, { key: '1', label: 'Double', mode: 0 }],
    finest: { label: 'Finest allowed gap (um)', value: '60' },
    coarsest: { label: 'Coarsest allowed gap (um)', value: '' },
    knob: { label: 'Knob press', value: 1, options: ['Arms the grind', 'Starts the shot'] },
  },
  compare: {
    title: 'Advice against the grinder', file: 'compare.csv', columns: ['Method', 'Shots', 'Mean error'],
    groups: [{ name: 'Coffee <x>', rows: [['Controller', '7', '1.2 s'], ['Grinder', '5', 'not enough shots']] }],
  },
};
await api.loadAddons();
const card = $('adlist').innerHTML;
ok('the steering title and note come from the add-on, escaped', card.includes('Who steers the grind') && card.includes('Off until you turn it on &lt;i&gt;') && !card.includes('<i>'));
ok('one mode switch per recipe with the add-on\'s own labels', (card.match(/data-adm="test-grinder"/g) || []).length === 2 &&
   card.includes('>Controller</option>') && /<option value="2" selected>Grinder<\/option>/.test(card));
ok('the cap is shown for the recipe in that mode and disabled for the others', card.includes('Cap (s)') && /value="3"\s+onchange="adSet\('test-grinder','cap','2'/.test(card) &&
   /value=""\s+disabled onchange="adSet\('test-grinder','cap','1'/.test(card));
ok('finest and coarsest gap fields', card.includes('Finest allowed gap (um)') && card.includes('value="60"') && card.includes('Coarsest allowed gap (um)'));
ok('the knob setting', card.includes('Knob press') && /<option value="1" selected>Starts the shot<\/option>/.test(card));
ok('the comparison table, escaped, with the add-on\'s columns', card.includes('Advice against the grinder') && card.includes('Coffee &lt;x&gt;') && card.includes('<th class="l">Mean error</th>') && card.includes('not enough shots'));
ok('the CSV download is limited to the add-on\'s own file route', card.includes('href="/api/addons/file?id=test-grinder&name=compare.csv"'));
posts.length = 0;
await api.adSet('test-grinder', 'mode', '2', '1');
ok('a mode change posts the field, the recipe key and the value', posts.some(p => p.u === '/api/addons/do?id=test-grinder&cmd=set&arg=' + encodeURIComponent('mode:2:1')));
await api.adSet('test-grinder', 'finest', '', '62');
ok('a number change posts with an empty key', posts.some(p => p.u === '/api/addons/do?id=test-grinder&cmd=set&arg=' + encodeURIComponent('finest::62')));
delete answers['/api/addons'].addons[0].card;

// Built for another interface: cannot be switched on.
answers['/api/addons'].addons[0].api_ok = false;
await api.loadAddons();
ok('another interface: the switch is disabled', $('adlist').innerHTML.includes('disabled') && $('adlist').innerHTML.includes('another add-on interface'));

// ---- Add-on modules (docs/ADDONS.md, "Add-ons as modules") ----
// No add-on compiled in, but the firmware loads modules: the card is shown for "Add-ons from" and the upload.
answers = {
  '/api/library': { recipes: [] },
  '/api/addons': { compiled: 0, modules: 0, addons: [], module_source: { repo: '', token_set: false, state: 'idle', message: '', restart_needed: false, offers: [] } },
};
await api.loadAddons();
let mod = $('admod').innerHTML;
ok('modules supported: the card is shown with none compiled in', $('adcard').style.display === '');
ok('"Add-ons from" has the repository and the token, which is never shown', mod.includes('Add-ons from') && mod.includes('id="admrepo"') && mod.includes('id="admtok"') &&
   mod.includes('type="password"') && mod.includes('not set'));
ok('no repository name is built in', !/hallge|Brew-Controller/.test(mod));
ok('the upload and the check are offered, no restart notice yet', mod.includes('Upload a module file') && mod.includes('Check for add-on updates') && !mod.includes('Restart now'));
// A repository, a token that is set, offers, a restart needed.
answers['/api/addons'].module_source = {
  repo: 'owner/<name>', token_set: true, state: 'done', message: 'v1: 2 add-on module(s) available', restart_needed: true, checked_epoch: 1,
  offers: [{ id: 'new-one', tag: 'v1', bytes: 90000, installed: false, differs: false },
           { id: 'old-one', tag: 'v1', bytes: 52000, installed: true, differs: true },
           { id: 'same-one', tag: 'v1', bytes: 1000, installed: true, differs: false }],
};
await api.loadAddons();
mod = $('admod').innerHTML;
ok('the repository is escaped and the token only reported as set', mod.includes('value="owner/&lt;name&gt;"') && mod.includes('set; blank keeps it, - forgets it') && !mod.includes('owner/<name>'));
ok('an offer is installable, a different one is an update, the same one has no button',
   mod.includes("adModuleInstall('new-one')") && mod.includes('>Install<') && mod.includes("adModuleInstall('old-one')") && mod.includes('>Update<') &&
   !mod.includes("adModuleInstall('same-one')") && mod.includes('installed, the same') && mod.includes('a different version is installed'));
ok('a stored module asks for a restart', mod.includes('loads at the next restart') && mod.includes('Restart now'));
// A busy delivery disables the check; a failure says so.
answers['/api/addons'].module_source.state = 'downloading';
await api.loadAddons();
ok('while downloading the check is disabled', /Check for add-on updates<\/button>/.test($('admod').innerHTML) && /disabled>Check/.test($('admod').innerHTML));
answers['/api/addons'].module_source.state = 'failed';
answers['/api/addons'].module_source.message = 'check failed: no release found';
await api.loadAddons();
ok('a failed check shows its reason', $('admod').innerHTML.includes('check failed: no release found') && $('admod').innerHTML.includes('>failed<'));
// Module add-ons in the list: loaded, stopped after a crash, needing a rebuild.
const modAddon = (id, m) => ({ id, name: id, version: '0.1', kind: 'grinder', api_ok: m.loaded, enabled: false, link: 'off', status: '', fields: [], options: {}, map: [], module: m });
answers['/api/addons'] = {
  compiled: 0, modules: 3, module_source: { repo: '', token_set: false, state: 'idle', message: '', offers: [] },
  addons: [
    modAddon('loaded-mod', { state: 'loaded', loaded: true, crashed: false, problem: '', version: '0.0.1', file_bytes: 8100, load_ms: 41, chip_used: 600 }),
    modAddon('crashed-mod', { state: 'stopped after a crash', loaded: false, crashed: true, problem: 'stopped after a crash', version: '2.0', file_bytes: 9000, load_ms: 0, chip_used: 0 }),
    modAddon('old-mod', { state: 'needs a rebuild', loaded: false, crashed: false, problem: 'built for add-on interface 1, this firmware has 2: needs a rebuild <x>', version: '1.0', file_bytes: 9000, load_ms: 0, chip_used: 0 }),
  ],
};
await api.loadAddons();
const list = $('adlist').innerHTML;
ok('a loaded module shows its load cost', list.includes('Module 0.0.1: loaded, loaded in 41 ms, 600 bytes of chip RAM'));
ok('a crashed module says so and offers a retry', list.includes('stopped after a crash') && list.includes("adModuleDo('retry','crashed-mod')"));
ok('a module needing a rebuild says why, escaped, with no retry', list.includes('needs a rebuild') && list.includes('needs a rebuild &lt;x&gt;') && !list.includes("adModuleDo('retry','old-mod')") &&
   !list.includes('Built for another add-on interface: it cannot'));
ok('every module can have its file removed', (list.match(/adModuleDo\('remove'/g) || []).length === 3);
posts.length = 0;
await api.adModuleDo('retry', 'crashed-mod');
ok('retry posts the module id', posts.some(p => p.u === '/api/addons/module/retry?id=crashed-mod'));
await api.adModuleDo('remove', 'old-mod');
ok('remove posts the module id', posts.some(p => p.u === '/api/addons/module/remove?id=old-mod'));
$('admrepo').value = ' owner/name ';
$('admtok').value = ' tok ';
await api.adSource();
ok('saving the source posts the repository and the token, trimmed', posts.some(p => p.u === '/api/addons/source' && p.body.repo === 'owner/name' && p.body.token === 'tok'));
// Without modules support (the emulator): still hidden with none compiled in.
answers = { '/api/addons': { compiled: 0, addons: [] }, '/api/library': { recipes: [] } };
await api.loadAddons();
ok('no modules, none compiled in: hidden', $('adcard').style.display === 'none');

console.log(`page addons: ${checks} checks, ${failed} failed`);
process.exit(failed ? 1 : 0);
