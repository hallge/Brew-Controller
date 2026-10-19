// Checks the web page's own setting formatter and parser (src/net/WebPage.h) against the same cases
// the firmware's host tests use for core/SettingFormat.h, so that the page and the controller's screen
// can never read one number two ways.
//     node test/host/page_settings_check.mjs
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const here = path.dirname(fileURLToPath(import.meta.url));
const page = fs.readFileSync(path.join(here, '..', '..', 'src', 'net', 'WebPage.h'), 'utf8');

// The whole script must at least parse.
const script = page.match(/<script>([\s\S]*?)<\/script>/)[1];
try { new Function(script); } catch (e) { console.log('FAIL the page script does not parse:', e.message); process.exit(1); }

// The helpers sit between "let GRD=" and "let U=".
const a = page.indexOf('let GRD=');
const b = page.indexOf("let U='", a);
if (a < 0 || b < 0) { console.log('FAIL helpers not found'); process.exit(1); }
const helpers = page.slice(a, b);
const make = new Function(`let U='um';${helpers}; return { setGrd:(g)=>{GRD=g}, fsn, fdn, fdf, fdu, ps, fs };`);
const h = make();

let fails = 0, checks = 0;
const eq = (name, got, want) => { checks++; if (got !== want) { fails++; console.log(`FAIL ${name}: got ${JSON.stringify(got)}, want ${JSON.stringify(want)}`); } };

const PLAIN = { divisor: 1, subSteps: 0 }, TENTHS = { divisor: 10, subSteps: 0 }, QUARTERS = { divisor: 4, subSteps: 0 },
  HALVES = { divisor: 2, subSteps: 0 }, RINGS = { divisor: 1, subSteps: 9 };

h.setGrd(PLAIN);
eq('plain 246', h.fsn(246), '246'); eq('plain 0', h.fsn(0), '');
eq('parse 246', h.ps('246'), 246); eq('parse " 12 "', h.ps(' 12 '), 12); eq('parse 12.4', h.ps('12.4'), 12);
for (const bad of ['', 'abc', '0', '2001', '-3', '12x']) eq(`plain rejects ${JSON.stringify(bad)}`, h.ps(bad), null);

h.setGrd(TENTHS);
eq('tenths 35', h.fsn(35), '3.5'); eq('tenths 40', h.fsn(40), '4.0'); eq('tenths 5', h.fsn(5), '0.5');
eq('parse 3.5', h.ps('3.5'), 35); eq('parse 3,5', h.ps('3,5'), 35); eq('parse 4', h.ps('4'), 40);
eq('parse 3.54', h.ps('3.54'), 35); eq('parse 3.56', h.ps('3.56'), 36);
eq('delta -15 tenths', h.fdn(-15), '1.5');

h.setGrd(QUARTERS);
eq('quarters 7', h.fsn(7), '1.75'); eq('quarters 8', h.fsn(8), '2.00');
eq('parse 1.75', h.ps('1.75'), 7); eq('parse 1.8', h.ps('1.8'), 7); eq('delta 3 quarters', h.fdn(3), '0.75');

h.setGrd(HALVES);
eq('halves 3', h.fsn(3), '1.5'); eq('parse 1.5', h.ps('1.5'), 3); eq('halves 1999 too big', h.ps('1999'), null); eq('halves 1000', h.ps('1000'), 2000);

h.setGrd(RINGS);
eq('rings 1', h.fsn(1), '1:1'); eq('rings 9', h.fsn(9), '1:9'); eq('rings 10', h.fsn(10), '2:1'); eq('rings 93', h.fsn(93), '11:3');
eq('parse 2:1', h.ps('2:1'), 10); eq('parse 12-3', h.ps('12-3'), 102); eq('parse 12/3', h.ps('12/3'), 102); eq('parse 7', h.ps('7'), 55);
for (const bad of ['12:0', '12:10', '0:3', '12:x', '300:9']) eq(`rings rejects ${bad}`, h.ps(bad), null);
eq('rings delta unit', h.fdu(), 'steps');
h.setGrd(PLAIN); eq('plain delta unit', h.fdu(), 'um');

// Every setting reads back as itself, in every format.
for (const [name, g] of [['plain', PLAIN], ['tenths', TENTHS], ['quarters', QUARTERS], ['halves', HALVES], ['rings', RINGS],
                         ['twentieths', { divisor: 20, subSteps: 0 }], ['hundredths', { divisor: 100, subSteps: 0 }]]) {
  h.setGrd(g);
  let bad = 0;
  for (let v = 1; v <= 2000; v++) if (h.ps(h.fsn(v)) !== v) bad++;
  eq(`round trip ${name}`, bad, 0);
}
// Every grind setting the controller sends that has an input on the page must be in the page's list
// of settings it loads and saves (F), or the page shows a default and never saves it. (The Ratings
// selector was missing from it until 16 Oct 2026.)
{
  const list = JSON.parse(page.match(/const F=(\[[^\]]*\]);/)[1]);
  const web = fs.readFileSync(path.join(here, '..', '..', 'src', 'net', 'Web.cpp'), 'utf8');
  const sent = [...web.matchAll(/doc\["(grind[A-Za-z]+)"\] = s\./g)].map(m => m[1]);
  eq('grind settings found in Web.cpp', sent.length >= 5, true);
  for (const k of new Set(sent)) {
    if (page.includes(`id="${k}"`)) eq(`setting ${k} is loaded and saved`, list.includes(k), true);
  }
  eq('no attribute glued onto an id', /id="[^"]*",/.test(page), false);
}
console.log(fails ? `\n${fails} of ${checks} FAILED` : `page settings: ${checks} checks, 0 failed`);
process.exit(fails ? 1 : 0);
