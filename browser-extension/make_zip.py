#!/usr/bin/env python3
"""Packs the Brew Controller helper (this folder) as a zip the controller serves at /extension.zip.

    python browser-extension/make_zip.py      writes .pio/gen/ExtensionZip.h

Run by the firmware build (scripts/gen_extension.py, a PlatformIO pre-script) and by emu/build.py, so
the zip on the controller is always this folder as built. The zip is the same bytes for the same files
(fixed dates, sorted names). Only the extension's own files go in: no tests, no build scripts.
"""
import io
import os
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, ".pio", "gen", "ExtensionZip.h")
FILES = ("manifest.json", "worker.js", "reader.js", "relay.js", "popup.html", "popup.js", "grant.html", "grant.js",
         "icon.png")


def build():
    data = io.BytesIO()
    with zipfile.ZipFile(data, "w", zipfile.ZIP_DEFLATED) as z:
        for name in sorted(FILES):
            info = zipfile.ZipInfo("brew-controller-helper/" + name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            with open(os.path.join(HERE, name), "rb") as f:
                z.writestr(info, f.read())
    return data.getvalue()


def main():
    blob = build()
    rows = ",\n".join("  " + ",".join(str(b) for b in blob[i:i + 32]) for i in range(0, len(blob), 32))
    text = ("// Made by browser-extension/make_zip.py from browser-extension/: do not edit.\n"
            "#pragma once\n#include <stddef.h>\n#include <stdint.h>\n"
            "#if defined(ARDUINO) && !defined(BBW_EMULATOR)\n#include <pgmspace.h>\n#elif !defined(PROGMEM)\n#define PROGMEM\n#endif\n"
            f"static const uint8_t EXTENSION_ZIP[] PROGMEM = {{\n{rows}\n}};\n"
            f"static const size_t EXTENSION_ZIP_LEN = {len(blob)};\n")
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    old = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else ""
    if old != text:   # unchanged: leave it, so nothing rebuilds for nothing
        with open(OUT, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    print(f"extension zip: {len(blob)} bytes -> {OUT}")


if __name__ == "__main__":
    main()
