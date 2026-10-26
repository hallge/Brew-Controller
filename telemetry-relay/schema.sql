-- The relay's D1 database (binding AUDIT). Apply once, and again after any change here (every
-- statement is safe to run twice):
--   npx wrangler d1 execute bbw-audit --remote --file=schema.sql

-- Every admin console action (actor: the Access email) and every relay refusal (actor: relay).
-- result: ok, partial (done, but a follow-up step failed), failed, refused or 429.
-- ip_hash: the first 16 hex digits of SHA-256 of the caller's address; never the address itself.
-- detail: JSON.
CREATE TABLE IF NOT EXISTS audit (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  ts TEXT NOT NULL,
  actor TEXT NOT NULL,
  action TEXT NOT NULL,
  target TEXT NOT NULL DEFAULT '',
  result TEXT NOT NULL,
  error TEXT NOT NULL DEFAULT '',
  ip_hash TEXT NOT NULL DEFAULT '',
  detail TEXT NOT NULL DEFAULT '{}'
);
CREATE INDEX IF NOT EXISTS audit_ts ON audit (ts);
CREATE INDEX IF NOT EXISTS audit_action ON audit (action, id);

-- The relay's outcomes per UTC day, for /report and /library/put.
-- outcome: ok, ok:key (a key record stored), 429, refused:<reason>, or error (GitHub failed).
CREATE TABLE IF NOT EXISTS counters (
  day TEXT NOT NULL,
  path TEXT NOT NULL,
  outcome TEXT NOT NULL,
  n INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY (day, path, outcome)
);
