// `node --test telemetry-relay/test` loads this file (package.json "main"): every test file, in one run.
await import('./relay.test.mjs');
await import('./library.test.mjs');
await import('./admin.test.mjs');
