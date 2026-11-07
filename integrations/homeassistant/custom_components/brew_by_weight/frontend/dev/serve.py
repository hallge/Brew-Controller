"""Dev harness for the Brew panel: serves the panel with a fake Home Assistant and proxies to a
controller (normally an emulator copy on its own port). Enforces the integration's proxy.py rules.

    python serve.py --controller http://127.0.0.1:8181 --port 8381 [--no-admin]

Never point it at the real board for write tests, and never at the user's own emulator (8080/8090).
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import sys
import urllib.error
import urllib.parse
import urllib.request
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

FRONTEND = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location("bbw_proxy", FRONTEND.parent / "proxy.py")
proxy = importlib.util.module_from_spec(_spec)
sys.modules["bbw_proxy"] = proxy  # dataclasses look their module up while the class is built
_spec.loader.exec_module(proxy)

DOWNLOADS = {
    "shots_csv": ("/shots.csv", False),
    "shot_csv": ("/shot.csv", False),
    "log": ("/log.txt", False),
    "backup": ("/api/backup", True),
}

ARGS = None


def forward(method: str, path: str, query: dict, body):
    """Ask the controller; return (status, content_type, bytes, headers)."""
    url = ARGS.controller.rstrip("/") + path
    if query:
        url += "?" + urllib.parse.urlencode(query)
    data = None
    headers = {}
    if method == "POST":
        data = json.dumps(body).encode() if body is not None else b""
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=15) as r:
            return r.status, r.headers.get("Content-Type", ""), r.read(), dict(r.headers)
    except urllib.error.HTTPError as e:
        return e.code, e.headers.get("Content-Type", ""), e.read(), dict(e.headers)
    except Exception:  # noqa: BLE001 - unreachable controller
        return 502, "application/json", json.dumps({"ok": False, "error": "The controller could not be reached"}).encode(), {}


def ws_request(msg: dict) -> dict:
    method = str(msg.get("method", "GET")).upper()
    path = msg.get("path", "")
    query = msg.get("query") or {}
    body = msg.get("body")
    d = proxy.check(method, path, query, body)
    if not d.allowed:
        return {"status": 403, "json": {"ok": False, "error": d.reason}}
    if d.admin_only or method == "POST":
        if ARGS.no_admin:
            return {"status": 403, "json": {"ok": False, "error": "Only an administrator can change this"}}
    status, ctype, raw, headers = forward(method, path, query, d.body if method == "POST" else None)
    text = raw.decode("utf-8", "replace")
    if "json" in ctype or text[:1] in "{[":
        try:
            return {"status": status, "json": json.loads(text) if text else None}
        except ValueError:
            pass
    return {"status": status, "text": text, "headers": {k.lower(): v for k, v in headers.items()}}


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=str(FRONTEND), **kw)

    def log_message(self, fmt, *args):  # quiet
        pass

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def do_GET(self):  # noqa: N802
        u = urllib.parse.urlparse(self.path)
        if u.path in ("/", "/index.html"):
            self.path = "/dev/index.html"
            return super().do_GET()
        if u.path.startswith("/api/brew_by_weight/download/"):
            q = dict(urllib.parse.parse_qsl(u.query))
            kind = q.pop("kind", "")
            if kind not in DOWNLOADS:
                return self._send(404, "text/plain", b"unknown kind")
            path, admin = DOWNLOADS[kind]
            if (admin or q.get("secrets") == "1") and ARGS.no_admin:
                return self._send(403, "text/plain", b"admin only")
            status, ctype, raw, _ = forward("GET", path, q, None)
            return self._send(status, ctype or "application/octet-stream", raw)
        return super().do_GET()

    def do_POST(self):  # noqa: N802
        if self.path != "/__ws":
            return self._send(404, "text/plain", b"")
        n = int(self.headers.get("Content-Length", "0") or 0)
        msg = json.loads(self.rfile.read(n) or b"{}")
        t = msg.get("type")
        if t == "brew_by_weight/entries":
            out = [{"entry_id": "dev", "title": "Brew by Weight (dev)", "host": ARGS.controller}]
        elif t == "brew_by_weight/request":
            out = ws_request(msg)
        else:
            return self._send(400, "application/json", json.dumps({"error": f"unknown type {t}"}).encode())
        return self._send(200, "application/json", json.dumps(out).encode())

    def _send(self, status, ctype, raw: bytes):
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


def main():
    global ARGS
    p = argparse.ArgumentParser()
    p.add_argument("--controller", required=True, help="e.g. http://127.0.0.1:8181 (an emulator copy)")
    p.add_argument("--port", type=int, default=8381)
    p.add_argument("--no-admin", action="store_true", help="act as a non-admin user (writes refused)")
    ARGS = p.parse_args()
    if any(x in ARGS.controller for x in (":8080", ":8090")):
        sys.exit("Refusing the user's own emulator ports (8080/8090): run your own copy.")
    print(f"Brew panel harness on http://127.0.0.1:{ARGS.port}/ -> {ARGS.controller}")
    ThreadingHTTPServer(("127.0.0.1", ARGS.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
