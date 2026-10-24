# Diagnostics relay

A small Cloudflare Worker that takes the signed diagnostic reports controllers send
(`POST /report`), checks them, and commits them to a private GitHub repository through a GitHub
App. Controllers need no GitHub account or token; the relay holds the only credential, and that
credential can write to the reports repository and nothing else.

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

Emulator reports (`alg` `EMULATOR-UNSIGNED`) are not signature-checked and go to `emulator/...`
only, with their own daily count.

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

## Tests

No packages to install:

```bash
node --test telemetry-relay/test
```

They make a P-256 key with WebCrypto, sign reports with it, and run the Worker against a stand-in
for GitHub and an in-memory KV.

## Notes

- The installation token GitHub issues lasts an hour; the relay keeps it in memory and renews it
  five minutes before it runs out, or at once if GitHub refuses it.
- The daily counts are kept in KV for two days and then expire on their own. KV is eventually
  consistent, so a burst from one key at the same moment could slip one or two over the limit; the
  limit is there to stop a stuck controller filling the repository, not as a hard quota.
- To stop all reports at once, `npx wrangler delete`, or suspend the App's installation on GitHub.
