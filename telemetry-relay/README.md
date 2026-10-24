# Diagnostics relay

A small Cloudflare Worker that takes the signed diagnostic reports controllers send
(`POST /report`), checks them, and commits them to a private GitHub repository through a GitHub
App. Controllers need no GitHub account or token; the relay holds the only credential, and that
credential can write to the repositories the App is installed on and nothing else.

It also carries the shared coffee library (`POST /library/put`, `GET /library/get`), so an owner
can share without a GitHub token of their own. A controller can still be set to use its owner's
token instead.

The report format and the relay's rules are in [docs/TELEMETRY.md](../docs/TELEMETRY.md).

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
   "No access" (Metadata: read-only is added by GitHub and cannot be removed).
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
