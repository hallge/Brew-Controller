# Diagnostics relay

A small Cloudflare Worker that takes the signed diagnostic reports controllers send
(`POST /report`), checks them, and commits them to a private GitHub repository through a GitHub
App. Controllers need no GitHub account or token; the relay holds the only credential, and that
credential can write to the repositories the App is installed on and nothing else.

It also carries the shared coffee library (`POST /library/put`, `GET /library/get`), so an owner
can share without a GitHub token of their own. A controller can still be set to use its owner's
token instead.

The report format and the relay's rules are in [docs/TELEMETRY.md](../docs/TELEMETRY.md).

It has an admin console at `/admin`, behind Cloudflare Access: the library's review queue, approve
and reject, hiding published entries, blocking contributors, health, devices and an audit log. See
[The admin console](#the-admin-console).

## What it does with a report

| Check | Refused with |
| --- | --- |
| More than 32 KB | `413` |
| Not JSON, envelope fields not exactly `v kid alg pub sig body`, unknown `v` or `alg`, `t` not `diag_daily` / `diag_crash` | `400` |
| `kid` is not the first 10 bytes of SHA-256(`pub`), or `sig` does not verify | `401` |
| The body still holds a MAC address or a dotted IPv4 address | `422` |
| More than 4 `diag_daily` or 10 `diag_crash` from one key in one UTC day | `429` (try tomorrow) |
| GitHub would not store it | `502` (try later; it is not counted) |

A good report is answered `201 {"ok":true}` and committed to
`reports/<yyyy>-<mm>/<kid>/<yyyymmddThhmmssZ>-<t>.json`. The time is when the relay received it,
in UTC; a second report in the same second takes the next free second. The stored file keeps the
signed `body` string, `pub` and `sig` as they came, so it can be checked again later, plus the
parsed `report` for reading.

Emulator reports go to `emulator/...` only, with their own daily count: those whose body says
`"emu":1` (an emulator with a real key; the signature is still checked), and those with `alg`
`EMULATOR-UNSIGNED` (no key: not signature-checked).

## The shared coffee library

### `POST /library/put`

Files one signed library record in the library's inbox. The request body is
`{"path": "<inbox path>", "record": "<the envelope, as a JSON string>"}`; the envelope is the
firmware's (`src/core/Share.h` `sealRecord`): exactly `v kid alg sig body`.

| Check | Refused with |
| --- | --- |
| Request over 16 KB, or the record over 8 KB | `413` |
| Not JSON, not `{path, record}`, a path that is not one of the patterns below, envelope fields not exactly `v kid alg sig body`, an `alg` other than `ES256`, or a body whose `t` is not the type its folder holds | `400` |
| `alg` is `EMULATOR-UNSIGNED` (the emulator has no key) | `403` |
| The kid in the path is not the envelope's `kid` | `403` |
| The kid is blocked (a key `blocked:<kid>` in `LIMITS`) | `403` |
| A key record whose `kid` is not the first 10 bytes of SHA-256(key), or a signature that does not verify | `401` |
| Any other record from a kid whose key record has not arrived | `409 {"ok":false,"error":"send the key first"}` |
| Over 60 records from one kid in a UTC day, or over 20 key records from one network address in a UTC day | `429` (try tomorrow) |
| GitHub would not store it | `502` (try later; it is not counted) |

The paths, as the firmware writes them (`src/main.cpp`); `<n>` is a plain number and `<month>` is
`yyyy/mm`, or `undated` for a controller with no clock:

```
keys/<kid>.json
coffees/<kid>/c<n>-<n>.json      grinders/<kid>/g<n>-<n>.json      services/<kid>/s<n>-<n>.json
machines/<kid>/m1-<n>.json
tastes/<kid>-r<n>.json           svcreviews/<kid>-v<n>.json
shots/<month>/<kid>-<n>.json     dialins/<month>/<kid>-<n>.json
```

A key record (`keys/<kid>.json`) vouches for itself, checked as `library-template/tools/verify_record.py`
checks it; once stored, its key is kept in the `KEYS` namespace and every later record from that kid
is checked against it. The record is committed exactly as received to `inbox/<path>` on the
library's `inbox` branch; the library's own intake checks it again before anything is published.
If the file is already there (GitHub's `422`, a controller resending after a lost answer) it counts
as sent. Success is `201 {"ok":true}`.

The network address is kept only as a SHA-256 hash, for the day's key count, and expires with it.
To stop a contributor at once: `npx wrangler kv key put blocked:<kid> 1 --binding LIMITS --remote`.

### `GET /library/get?path=...`

Reads one published file from the library's `main` branch: only `status/<kid>.json` and
`index/<path>.json` (letters, digits, `.`, `_`, `-`; no `..`, no leading slash). Answers the file
itself with `200`, `404` if there is no such file, or `400` for any other path. Answers are cached
at the edge: index files for 10 minutes, status files for 2.

## The admin console

`/admin` (pages) and `/admin/api/*` (JSON). Plain HTML, CSS and JS in `admin/`, bundled into
`src/admin-pages.js` by `tools/build-admin.mjs` (Wrangler runs it before every deploy); the API is
`src/admin.js`. It works at phone width, light or dark.

### Who can open it

Every request must carry a Cloudflare Access token (`Cf-Access-Jwt-Assertion`, which Access adds
once you have signed in). The relay checks it itself: RS256 against the team's keys from
`https://<ACCESS_TEAM>.cloudflareaccess.com/cdn-cgi/access/certs` (kept for an hour), the audience
must be `ACCESS_AUD`, it must not have expired, and it must carry an email. That email is who the
audit log says did each action. Anything else gets `403`, so the console stays shut on any address
Access does not cover (the `workers.dev` one included) and until both variables are set.

Actions are `POST` with a header only the console's own page sends (`x-bbw-admin: 1`), so another
site cannot make your signed-in browser act.

### Views

| View | What it shows and does |
| --- | --- |
| Queue | Open pull requests in `LIBRARY_REPO` whose branch is `review/*`: kind, title, kid, opened, age |
| Review | The signed record behind the review (`pending/<section>/<kid>/...` on main) and the entry it would publish; the checks redone (below); the field-by-field diff against the published entry with the same id; the kid's status file and its other review pull requests. **Approve** squash-merges; **Reject** comments `Declined: <note>` and closes, so `review-outcome.yml` records the decision as before |
| Health | The last runs of Intake, Review outcome and Something arrived (the inbox branch's), with failures; the relay's outcomes per day for 7 days; KV use today, estimated, against the free limits |
| Devices | The library keys in `KEYS` with when each was first seen (kept from this version on; older keys show none), blocked or not; diagnostics units under `reports/<this month>/` and the month before, with their latest report, its firmware and a link |
| Hide & block | Hide a published coffee, grinder, machine or service (or restore it); block or unblock a kid |
| Audit | Every admin action and every relay refusal, newest first, filtered by action, result, actor, text or date |

**Checks redone on a review**, ported from `library-template/tools/verify_record.py`: the
envelope; ES256 only; the body; the key (the relay's `KEYS` first, then the library's
`keys/<kid>.json`, itself checked as a key record); the signature; every value's range and the link
rules; that the record is the one the branch names; and the entry to publish (the pull request's
file, which a reviewer may have edited) as the library lists it. Left to the library's own tools,
and listed on every review: similar existing entries, whether a newer version has arrived since,
the contributor's measurement count, and the index builder's checks on publishing.

**Hide** makes one commit on the library's `main` that moves `catalog/...` (or `grinders/`,
`machines/`, `services/`) to `hidden/...`, then starts the Intake workflow
(`workflow_dispatch`), which rebuilds the index without it. There is no index-only workflow:
Intake and Review outcome are the two that rebuild it, and Intake is the one that can be started
by hand. The commit message names the file, not who hid it (the library is public); the reason
goes to the audit log only. Known gaps: the contributor's status file still says `published`, and
if starting Intake fails the move stands and the action is logged as `partial` (start Intake by
hand from the Actions tab).

**Block** writes `blocked:<kid>` in `LIMITS` (with when, who and the note); the relay then refuses
that kid's library records with `403`. **Unblock** deletes it.

### D1: the audit log and the counters

`AUDIT` is a D1 database with two tables (`schema.sql`):

- `audit` (ts, actor, action, target, result, error, ip_hash, detail JSON): every admin action
  (`approve`, `reject`, `hide`, `unhide`, `block`, `unblock`; result `ok`, `partial` or `failed`)
  and every relay refusal (actor `relay`, action `refuse`, the reason in `error`, the kid as
  claimed in `target`, the path in `detail`). `ip_hash` is the first 16 hex digits of SHA-256 of
  the caller's address, never the address.
- `counters` (day, path, outcome, n): `/report` and `/library/put` outcomes per UTC day: `ok`,
  `ok:key`, `429`, `refused:<reason>`, `error`.

These writes happen after the answer is sent and never change it: if D1 is down or unbound, the
relay works as before. D1's free plan allows 100,000 writes a day; each refusal is two (the audit
row and its counter), each stored record one.

### Setting it up

You do these once, then deploy.

1. **The database.** In `telemetry-relay`:

   ```bash
   npx wrangler d1 create bbw-audit
   ```

   Copy the `database_id` it prints into `wrangler.toml` in place of
   `REPLACE_WITH_D1_DATABASE_ID`. Then create the tables:

   ```bash
   npx wrangler d1 execute bbw-audit --remote --file=schema.sql
   ```

2. **A Zero Trust team** (free): Cloudflare dashboard › Zero Trust. The first time, it asks for a
   team name: your team domain is `<team>.cloudflareaccess.com`. Choose the Free plan. Under
   Settings › Authentication, the one-time PIN (a code by email) is enough.
3. **The Access application.** Zero Trust › Access › Applications › Add an application ›
   Self-hosted:
   - Domain: the relay's address, path `admin*` (this covers `/admin`, `/admin/` and
     `/admin/api/...`). If the relay runs on `workers.dev`, turn on Access for it under Workers ›
     the relay › Settings › Domains & Routes (`workers.dev` › Cloudflare Access), or give it a
     route on your own domain and use that.
   - A policy: Action **Allow**, Include › **Emails** › your email.
   - Save, then open the application's Overview and copy the **Application Audience (AUD) Tag**.
4. **The variables.** In `wrangler.toml`, set `ACCESS_TEAM` to the team name (the part before
   `.cloudflareaccess.com`) and `ACCESS_AUD` to the AUD tag. Neither is a secret, but this
   repository is public: keep that edit local if you prefer.
5. **The App's permissions.** GitHub › Settings › Developer settings › GitHub Apps › the relay's
   App › Permissions & events › Repository permissions:
   - **Pull requests: Read and write** (the queue, approve, reject);
   - **Actions: Read and write** (the workflow runs on Health, and starting Intake after a hide).

   Save, then accept the new permissions on the installation (GitHub shows a banner on the
   installation page and emails the owner). If the library's `main` is protected, allow the App to
   push to it, or hiding fails (the error says so).
6. **Deploy:** `npx wrangler deploy`. Open `https://<the relay>/admin/`: Access asks you to sign
   in, then the queue shows. Without signing in, `curl -i https://<the relay>/admin/` must give
   `403`.

## One-time setup

You create the accounts and enter the secrets yourself. Nothing here, and no one else, needs to
see the App's private key.

You need Node.js (for `npx`).

### 1. The private reports repository

On GitHub, create a new **private** repository, for example `bbw-diagnostics`. Tick "Add a README"
so it is not empty.

### 2. The GitHub App

1. GitHub › Settings › Developer settings › GitHub Apps › **New GitHub App**.
2. Name: anything, e.g. `bbw-diagnostics-relay`. Homepage URL: this repository's URL.
3. Webhook: untick **Active**.
4. Repository permissions: **Contents: Read and write**. Leave every other permission at
   "No access" (Metadata: read-only is added by GitHub and cannot be removed). The admin console
   needs two more, added when you set it up: Pull requests and Actions, both read and write
   (see [The admin console](#the-admin-console)).
5. "Where can this GitHub App be installed?": **Only on this account**.
6. Create it. Note the **App ID** shown at the top of its page.
7. Under "Private keys", **Generate a private key**. A `.pem` file downloads. Keep it out of this
   repository and delete your copy once step 5 below is done.
8. In the App's left menu, **Install App** › Install › **Only select repositories** › pick the
   reports repository only.
9. After installing, the page's address ends in `/installations/<number>`: that number is the
   **installation id**.

### 3. Cloudflare

1. Create a free Cloudflare account at <https://dash.cloudflare.com/sign-up>.
2. In this folder, sign in:

   ```bash
   cd telemetry-relay
   npx wrangler login
   ```

### 4. The KV namespace

```bash
npx wrangler kv namespace create LIMITS
```

Copy the `id` it prints into `wrangler.toml` in place of `REPLACE_WITH_KV_NAMESPACE_ID`, and set
`REPORTS_REPO` there to `<your GitHub name>/<repository>`.

Neither of these is a secret, but this repository is public: keep that edit local (or commit it,
if you are happy for the namespace id and repository name to be seen).

### 5. The secrets

Each command asks for the value; paste it and press Enter.

```bash
npx wrangler secret put GITHUB_APP_ID
```

```bash
npx wrangler secret put GITHUB_INSTALLATION_ID
```

For the key, pipe the file in so the whole PEM goes through intact:

```bash
npx wrangler secret put GITHUB_APP_KEY < path/to/your-app.private-key.pem
```

The key GitHub hands out (`BEGIN RSA PRIVATE KEY`, PKCS#1) works as it is; a PKCS#8 key
(`BEGIN PRIVATE KEY`) works too.

### 6. Deploy and check

```bash
npx wrangler deploy
```

It prints the Worker's address, e.g. `https://bbw-telemetry-relay.<you>.workers.dev`. Check it is
up: a report with a made-up key must be refused.

```bash
curl -i -X POST https://bbw-telemetry-relay.<you>.workers.dev/report -H "content-type: application/json" -d "{\"v\":1}"
```

The answer should be `400` with `{"ok":false,"error":"envelope fields must be exactly v, kid, alg, pub, sig, body"}`.
`GET` on `/report` gives `405`, and any other path `404`.

For an end-to-end check, point the emulator at the relay and press Setup › Diagnostics › Send now:
a file should appear under `emulator/` in the reports repository. A real controller's report goes
under `reports/`.

To watch requests as they arrive: `npx wrangler tail`.

### 7. The shared coffee library

1. On GitHub, the App's **Install App** › the gear by your installation › **Repository access**:
   add the library repository (`Community-Coffee`) next to the reports repository. The
   installation id stays the same. The library needs an `inbox` branch.
2. Create the keys namespace:

   ```bash
   npx wrangler kv namespace create KEYS
   ```

   Copy the `id` it prints into `wrangler.toml` in place of `REPLACE_WITH_KEYS_NAMESPACE_ID`.
3. In `wrangler.toml`, set `LIBRARY_REPO` to `<your GitHub name>/Community-Coffee`.
4. Deploy again: `npx wrangler deploy`.
5. Check it: a status read for a made-up kid should be `404`, and any other path `400`.

   ```bash
   curl -i "https://bbw-telemetry-relay.<you>.workers.dev/library/get?path=status/00000000000000000000.json"
   ```

## Tests

No packages to install:

```bash
node --test telemetry-relay/test
```

They make P-256 keys with WebCrypto, sign reports and library records with them (key records
built as `buildKeyRecord` builds them), and run the Worker against a stand-in for GitHub, an
in-memory KV and an in-memory cache. `test/index.mjs` runs every test file.

The admin console's tests (`test/admin.test.mjs`) also make an Access signing key and tokens, and
use SQLite (`node:sqlite`, Node 22.13 or later; it prints an "experimental" warning) with
`schema.sql` applied as the D1 database. After changing anything in `admin/`, run
`node telemetry-relay/tools/build-admin.mjs`: a test checks the bundle is current.

## Notes

- The installation token GitHub issues lasts an hour; the relay keeps it in memory and renews it
  five minutes before it runs out, or at once if GitHub refuses it.
- The daily counts are kept in KV for two days and then expire on their own. KV is eventually
  consistent, so a burst from one key at the same moment could slip one or two over the limit; the
  limit is there to stop a stuck controller filling the repository, not as a hard quota.
- Cloudflare's free KV allows about 1,000 writes a day. Each report and each library record is one
  counter write, and a key record two more (its address count and the key). That is plenty for
  tens of controllers; past that, the counters move to D1 or a paid plan.
- To stop all reports at once, `npx wrangler delete`, or suspend the App's installation on GitHub.
