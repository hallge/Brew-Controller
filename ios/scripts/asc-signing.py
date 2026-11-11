#!/usr/bin/env python3
"""Creates (and afterwards removes) the App Store signing assets for one CI release run.

The team has no registered devices, so Xcode's automatic signing can't make the development
profile it signs archives with. Instead this script uses the App Store Connect API key to
create an Apple Distribution certificate and an App Store profile (neither needs devices),
installs both, and revokes them again in `cleanup` so the account never hits the
certificate limit. Builds already uploaded stay valid after the revoke.

  asc-signing.py setup    -> writes <key>_profile names and keychain to $GITHUB_OUTPUT
  asc-signing.py cleanup  -> revokes the certificate and deletes the profiles
  asc-signing.py next-build -> prints one more than the highest build number uploaded for
                               the app (APP_BUNDLE_ID, default com.hallge.brewcontroller), so
                               builds keep counting up

Adapted from the Sideline project's script of the same name (one app, no capabilities needed:
Bluetooth is a usage string and a background mode in Info.plist, not an entitlement).

Env: ASC_KEY_ID, ASC_ISSUER_ID, ASC_KEY_PATH, KEYCHAIN_PASSWORD, RUNNER_TEMP, and
BUNDLES = "key=bundle.id,..." e.g.
  "app=com.hallge.brewcontroller"
"""
import base64
import json
import os
import pathlib
import subprocess
import sys
import time
import urllib.error
import urllib.request

import jwt  # pyjwt[crypto]

API = "https://api.appstoreconnect.apple.com/v1"
WORK = pathlib.Path(os.environ.get("RUNNER_TEMP", "/tmp")) / "asc-signing"
STATE = WORK / "state.json"
WWDR = "https://www.apple.com/certificateauthority/AppleWWDRCAG3.cer"


def token():
    key = pathlib.Path(os.environ["ASC_KEY_PATH"]).read_text()
    now = int(time.time())
    return jwt.encode(
        {"iss": os.environ["ASC_ISSUER_ID"], "iat": now, "exp": now + 900, "aud": "appstoreconnect-v1"},
        key, algorithm="ES256", headers={"kid": os.environ["ASC_KEY_ID"], "typ": "JWT"})


def call(method, path, body=None, ok=(200, 201, 204)):
    req = urllib.request.Request(API + path, method=method,
                                 data=json.dumps(body).encode() if body is not None else None)
    req.add_header("Authorization", "Bearer " + token())
    req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req) as r:
            text = r.read().decode()
            return r.status, (json.loads(text) if text else {})
    except urllib.error.HTTPError as e:
        text = e.read().decode()
        if e.code in ok:
            return e.code, {}
        try:
            errs = json.loads(text).get("errors", [])
            detail = "; ".join(f"{x.get('title')}: {x.get('detail')}" for x in errs) or text
        except ValueError:
            detail = text
        raise SystemExit(f"{method} {path} -> HTTP {e.code}: {detail}")


def run(*cmd, **kw):
    subprocess.run(cmd, check=True, **kw)


def bundle_id(identifier, name):
    _, res = call("GET", f"/bundleIds?filter[identifier]={identifier}&limit=50")
    for item in res.get("data", []):
        if item["attributes"]["identifier"] == identifier:
            print(f"App ID {identifier}: found ({item['attributes'].get('platform')})")
            return item["id"]
    _, res = call("POST", "/bundleIds", {"data": {"type": "bundleIds", "attributes": {
        "identifier": identifier, "name": name, "platform": "IOS"}}})
    print(f"App ID {identifier}: created")
    return res["data"]["id"]


def setup():
    WORK.mkdir(parents=True, exist_ok=True)
    state = {"profile_ids": []}
    bundles = []
    for part in os.environ["BUNDLES"].split(","):
        key, spec = part.strip().split("=")
        identifier = spec.strip()
        bid = bundle_id(identifier, f"Brew Controller {key}")
        bundles.append((key, identifier, bid))
    # The upload needs an App Store Connect app record for the container's bundle ID.
    _, res = call("GET", f"/apps?filter[bundleId]={bundles[0][1]}")
    apps = res.get("data", [])
    if apps:
        for a in apps:
            print(f"App record: {a['attributes'].get('name')} ({a['attributes'].get('bundleId')}, id {a['id']})")
    else:
        print(f"WARNING: no App Store Connect app uses {bundles[0][1]}")
        _, res = call("GET", "/apps?limit=20")
        for a in res.get("data", []):
            print(f"  existing app: {a['attributes'].get('name')} -> {a['attributes'].get('bundleId')}")

    # Certificate: a fresh key and CSR on the runner, signed by Apple.
    key, csr = WORK / "dist.key", WORK / "dist.csr"
    run("/usr/bin/openssl", "req", "-new", "-newkey", "rsa:2048", "-nodes", "-keyout", str(key),
        "-out", str(csr), "-subj", "/CN=Brew Controller CI/O=Brew Controller/C=US")
    _, res = call("POST", "/certificates", {"data": {"type": "certificates", "attributes": {
        "certificateType": "DISTRIBUTION", "csrContent": csr.read_text()}}})
    cert = res["data"]
    state["certificate_id"] = cert["id"]
    STATE.write_text(json.dumps(state))
    print(f"Certificate: created {cert['attributes'].get('name')} (id {cert['id']})")
    der, pem, p12 = WORK / "dist.cer", WORK / "dist.pem", WORK / "dist.p12"
    der.write_bytes(base64.b64decode(cert["attributes"]["certificateContent"]))
    run("/usr/bin/openssl", "x509", "-inform", "DER", "-in", str(der), "-out", str(pem))
    pw = os.environ["KEYCHAIN_PASSWORD"]
    # /usr/bin/openssl is LibreSSL; its PKCS#12 defaults are ones `security import` accepts.
    run("/usr/bin/openssl", "pkcs12", "-export", "-inkey", str(key), "-in", str(pem),
        "-out", str(p12), "-passout", f"pass:{pw}")

    keychain = str(WORK / "signing.keychain-db")
    run("security", "create-keychain", "-p", pw, keychain)
    run("security", "set-keychain-settings", "-lut", "21600", keychain)
    run("security", "unlock-keychain", "-p", pw, keychain)
    existing = subprocess.run(["security", "list-keychains", "-d", "user"], check=True,
                              capture_output=True, text=True).stdout.split()
    run("security", "list-keychains", "-d", "user", "-s", keychain, *[k.strip('"') for k in existing])
    wwdr = WORK / "wwdr.cer"
    urllib.request.urlretrieve(WWDR, wwdr)
    run("security", "import", str(wwdr), "-k", keychain, "-T", "/usr/bin/codesign")
    run("security", "import", str(p12), "-k", keychain, "-P", pw, "-T", "/usr/bin/codesign",
        "-T", "/usr/bin/security")
    run("security", "set-key-partition-list", "-S", "apple-tool:,apple:,codesign:", "-s", "-k", pw,
        keychain, stdout=subprocess.DEVNULL)
    state["keychain"] = keychain
    STATE.write_text(json.dumps(state))
    run("security", "find-identity", "-v", "-p", "codesigning", keychain)

    # App Store profiles (no devices needed).
    out_lines = [f"keychain={keychain}"]
    home = pathlib.Path.home()
    run_id = os.environ.get("GITHUB_RUN_ID", str(int(time.time())))
    for key, identifier, bid in bundles:
        name = f"Brew Controller CI {run_id} {key}"
        _, res = call("POST", "/profiles", {"data": {"type": "profiles",
            "attributes": {"name": name, "profileType": "IOS_APP_STORE"},
            "relationships": {
                "bundleId": {"data": {"type": "bundleIds", "id": bid}},
                "certificates": {"data": [{"type": "certificates", "id": cert["id"]}]}}}})
        prof = res["data"]
        state["profile_ids"].append(prof["id"])
        STATE.write_text(json.dumps(state))
        uuid = prof["attributes"]["uuid"]
        content = base64.b64decode(prof["attributes"]["profileContent"])
        for d in (home / "Library/MobileDevice/Provisioning Profiles",
                  home / "Library/Developer/Xcode/UserData/Provisioning Profiles"):
            d.mkdir(parents=True, exist_ok=True)
            (d / f"{uuid}.mobileprovision").write_bytes(content)
        print(f"Profile: created {name} for {identifier} ({uuid})")
        out_lines.append(f"{key}_profile={name}")

    out_lines.append(f"certificate_id={cert['id']}")
    out_lines.append(f"profile_ids={','.join(state['profile_ids'])}")
    out_lines.append(f"app_id={apps[0]['id'] if apps else ''}")
    out = os.environ.get("GITHUB_OUTPUT")
    if out:
        with open(out, "a") as f:
            f.write("\n".join(out_lines) + "\n")


def retire():
    """Revoke after Apple has processed the upload: processing checks the signing
    certificate, so revoking earlier turns the build into an Invalid Binary."""
    app_id, build = os.environ.get("APP_ID", ""), os.environ["BUILD_NUMBER"]
    deadline = time.time() + float(os.environ.get("WAIT_MINUTES", "75")) * 60
    while app_id:
        _, res = call("GET", f"/builds?filter[app]={app_id}&filter[version]={build}&limit=5")
        states = [b["attributes"].get("processingState") for b in res.get("data", [])]
        print(f"{time.strftime('%H:%M:%S')} build {build}: {states or 'not listed yet'}", flush=True)
        if states and all(s != "PROCESSING" for s in states):
            break
        if time.time() > deadline:
            print("WARNING: still processing at the deadline; revoking anyway")
            break
        time.sleep(60)
    for pid in filter(None, os.environ.get("PROFILE_IDS", "").split(",")):
        call("DELETE", f"/profiles/{pid}", ok=(204, 404))
        print("Profile: deleted")
    if os.environ.get("CERTIFICATE_ID"):
        call("DELETE", f"/certificates/{os.environ['CERTIFICATE_ID']}", ok=(204, 404))
        print("Certificate: revoked")


def cleanup():
    if not STATE.exists():
        print("Nothing to clean up")
        return
    state = json.loads(STATE.read_text())
    for pid in state.get("profile_ids", []):
        call("DELETE", f"/profiles/{pid}", ok=(204, 404))
        print("Profile: deleted")
    if "certificate_id" in state:
        call("DELETE", f"/certificates/{state['certificate_id']}", ok=(204, 404))
        print("Certificate: revoked")
    if "keychain" in state:
        subprocess.run(["security", "delete-keychain", state["keychain"]])
    STATE.unlink()


def next_build():
    bundle = os.environ.get("APP_BUNDLE_ID", "com.hallge.brewcontroller")
    _, res = call("GET", f"/apps?filter[bundleId]={bundle}")
    apps = res.get("data", [])
    if not apps:
        raise SystemExit(f"no App Store Connect app uses {bundle}")
    _, res = call("GET", f"/builds?filter[app]={apps[0]['id']}&sort=-uploadedDate&limit=50")
    numbers = [int(b["attributes"]["version"]) for b in res.get("data", [])
               if str(b["attributes"].get("version", "")).isdigit()]
    floor = int(os.environ.get("MIN_BUILD", "0") or 0)
    print(max(numbers + [floor - 1]) + 1)


if __name__ == "__main__":
    {"setup": setup, "cleanup": cleanup, "retire": retire, "next-build": next_build}[sys.argv[1]]()
