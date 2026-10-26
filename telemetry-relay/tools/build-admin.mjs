// Bundles the admin console's pages (admin/) into src/admin-pages.js, which the Worker serves.
// Wrangler runs it before every deploy ([build] in wrangler.toml); the tests check the bundle is current.
//
//   node tools/build-admin.mjs           write src/admin-pages.js
//   node tools/build-admin.mjs --check   exit 1 if src/admin-pages.js is out of date
import { readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
};

export function bundle() {
  const pages = {};
  for (const name of readdirSync(join(root, 'admin')).sort()) {
    const ext = name.slice(name.lastIndexOf('.'));
    if (!TYPES[ext]) continue;
    pages[name] = { type: TYPES[ext], body: readFileSync(join(root, 'admin', name), 'utf8').replace(/\r\n/g, '\n') };
  }
  return `// Made by tools/build-admin.mjs from admin/: edit those files, not this one.\nexport const ADMIN_PAGES = ${JSON.stringify(pages, null, 1)};\n`;
}

const target = join(root, 'src', 'admin-pages.js');
if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  const text = bundle();
  if (process.argv.includes('--check')) {
    let current = '';
    try {
      current = readFileSync(target, 'utf8');
    } catch {
      // missing counts as out of date
    }
    if (current !== text) {
      console.error('src/admin-pages.js is out of date: run node tools/build-admin.mjs');
      process.exit(1);
    }
  } else {
    writeFileSync(target, text);
    console.log(`wrote ${target}`);
  }
}
