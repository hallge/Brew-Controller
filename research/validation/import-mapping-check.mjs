// Draft mapping check: one Beanconqueror single-brew export -> this controller's Coffee + dial-in fields.
// Usage: node import-mapping-check.mjs <path to a Beanconqueror brew/export JSON>
// The roast mapping below is a DESIGN judgement (Beanconqueror has 14 values, the controller 5), not a standard.
import fs from 'node:fs';
const j = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const ROAST = { CINNAMON_ROAST: 'light', NEW_ENGLAND_ROAST: 'light', HALF_CITY_ROAST: 'light', MODERATE_LIGHT_ROAST: 'medium-light',
  AMERICAN_ROAST: 'medium-light', CITY_ROAST: 'medium', CITY_PLUS_ROAST: 'medium', FULL_CITY_ROAST: 'medium-dark', FULL_CITY_PLUS_ROAST: 'medium-dark',
  VIEANNA_ROAST: 'dark', VIENNA_ROAST: 'dark', ITALIAN_ROAST: 'dark', FRENCH_ROAST: 'dark' };
const LIM = { name: 23, roaster: 23, origin: 23, region: 31, producer: 31, varietal: 31, process: 15, notes: 47 };
const info = (j.bean.bean_information || [])[0] || {};
const notes = [], loss = [];
const clip = (k, v) => { v = (v ?? '').toString(); if (v.length > LIM[k]) { loss.push(`${k} truncated ${v.length}->${LIM[k]}`); return v.slice(0, LIM[k]); } return v; };
const coffee = { name: clip('name', j.bean.name), roaster: clip('roaster', j.bean.roaster), origin: clip('origin', info.country), region: clip('region', info.region),
  producer: clip('producer', info.farmer), varietal: clip('varietal', info.variety), process: clip('process', info.processing),
  notes: clip('notes', j.bean.aromatics), roast_level: ROAST[j.bean.roast] ?? '', roast_date: (j.bean.roastingDate || '').slice(0, 10) };
if (!ROAST[j.bean.roast]) loss.push(`roast "${j.bean.roast}" has no controller level`);
const b = j.brew, grind = Number(b.grind_size);
const dialin = { dose_g: b.grind_weight, yield_g: b.brew_beverage_quantity || null, time_s: b.brew_time, grind: Number.isInteger(grind) ? grind : null,
  at: b.config?.unix_timestamp ? new Date(b.config.unix_timestamp * 1000).toISOString() : null, rating: b.rating || null, grinder: j.mill?.name };
if (!dialin.yield_g) loss.push('beverage quantity is 0: yield unknown (water in is not yield)');
if (dialin.grind === null) loss.push(`grind_size "${b.grind_size}" is not an integer`);
if (dialin.dose_g > 30 || dialin.dose_g < 5) loss.push(`dose ${dialin.dose_g} g outside controller range 5-30`);
if (dialin.time_s > 90 || dialin.time_s < 5) loss.push(`time ${dialin.time_s} s outside controller range 5-90`);
const style = j.preparation?.style_type || j.preparation?.type; if (!/ESPRESSO|PORTAFILTER|ESPRESSO/i.test(String(style))) loss.push(`preparation "${j.preparation?.name}" (${style}) is not espresso: would be skipped`);
if ('tds' in b && b.tds) loss.push(`tds ${b.tds} has no controller field`);
console.log(JSON.stringify({ coffee, dialin }, null, 1)); console.log('LOSS/REJECT:', loss);
