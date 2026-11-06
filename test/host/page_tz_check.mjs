// Checks the web page's time zone rule (tzPosix in src/net/WebPage.h): the browser's zone written as
// the POSIX rule core/TimeZone.h reads, so the power schedule follows daylight saving.
//     node test/host/page_tz_check.mjs
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const here = path.dirname(fileURLToPath(import.meta.url));
const page = fs.readFileSync(path.join(here, '..', '..', 'src', 'net', 'WebPage.h'), 'utf8');

const script = page.match(/<script>([\s\S]*?)<\/script>/)[1];
try { new Function(script); } catch (e) { console.log('FAIL the page script does not parse:', e.message); process.exit(1); }

const a = page.indexOf('function tzPosix(');
if (a < 0) { console.log('FAIL tzPosix not found'); process.exit(1); }
const tzPosix = new Function(page.slice(a, page.indexOf('\n', a)) + '; return tzPosix;')();

let fails = 0, checks = 0;
const eq = (name, got, want) => { checks++; if (got !== want) { fails++; console.log(`FAIL ${name}: got ${JSON.stringify(got)}, want ${JSON.stringify(want)}`); } };

// Node reads TZ again each time it is set.
const CASES = [
  ['America/New_York', 2026, '<-05>5<-04>,M3.2.0,M11.1.0'],
  ['America/New_York', 2027, '<-05>5<-04>,M3.2.0,M11.1.0'],
  ['America/Los_Angeles', 2026, '<-08>8<-07>,M3.2.0,M11.1.0'],
  ['Europe/London', 2026, '<+00>0<+01>,M3.5.0/1,M10.5.0'],
  ['Europe/Berlin', 2026, '<+01>-1<+02>,M3.5.0,M10.5.0/3'],
  ['Australia/Sydney', 2026, '<+10>-10<+11>,M10.1.0,M4.1.0/3'],
  ['Asia/Kolkata', 2026, '<+0530>-5:30'],
  ['Asia/Tokyo', 2026, '<+09>-9'],
  ['UTC', 2026, '<+00>0'],
  ['America/Phoenix', 2026, '<-07>7'],
];
for (const [zone, year, want] of CASES) {
  process.env.TZ = zone;
  eq(`${zone} ${year}`, tzPosix(year), want);
}
console.log(`${checks} checks, ${fails} failed`);
process.exit(fails ? 1 : 0);
