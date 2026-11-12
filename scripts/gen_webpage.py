#!/usr/bin/env python3
"""Gzips the web page (src/net/WebPage.h) into .pio/gen/WebPageGz.h, with a tag naming its content.

    python scripts/gen_webpage.py

Run by the firmware build (a PlatformIO pre-script) and by emu/build.py, so the
gzipped page is always the page as it is in WebPage.h: never check the output in. The controller sends
it with Content-Encoding: gzip (113 KB instead of 361 KB) and the tag as its ETag, so a browser that
already has this page gets a 304 (src/net/Web.cpp, the "/" handler). Same input, same bytes (mtime 0).
"""
import gzip
import hashlib
import os
import sys

try:
    Import("env")  # noqa: F821  (run by PlatformIO as a pre-script, without __file__)
    ROOT = env["PROJECT_DIR"]  # noqa: F821
    UNDER_PIO = True
except NameError:
    ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    UNDER_PIO = False
SRC = os.path.join(ROOT, "src", "net", "WebPage.h")
OUT = os.path.join(ROOT, ".pio", "gen", "WebPageGz.h")
START, END = b'R"HTML(', b')HTML"'


def page_bytes():
    with open(SRC, "rb") as f:
        text = f.read()
    a = text.find(START)
    b = text.find(END, a + len(START))
    if a < 0 or b < 0 or text.find(START, a + 1) >= 0:
        sys.exit("gen_webpage: WebPage.h must hold exactly one R\"HTML(...)HTML\" string")
    return text[a + len(START):b]


def build():
    page = page_bytes()
    packed = gzip.compress(page, compresslevel=9, mtime=0)
    tag = hashlib.sha256(page).hexdigest()[:16]
    lines = [
        "// Made by scripts/gen_webpage.py from src/net/WebPage.h: do not edit, do not check in.",
        "#pragma once",
        "#include <stddef.h>",
        "#include <stdint.h>",
        '#define WEB_PAGE_TAG "\\"p-%s\\""' % tag,
        "static const size_t WEB_PAGE_GZ_LEN = %d;  // the page is %d bytes" % (len(packed), len(page)),
        "static const uint8_t WEB_PAGE_GZ[] PROGMEM = {",
    ]
    for i in range(0, len(packed), 24):
        lines.append("  " + ",".join(str(x) for x in packed[i:i + 24]) + ",")
    lines.append("};")
    data = ("\n".join(lines) + "\n").encode()
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    try:
        with open(OUT, "rb") as f:
            if f.read() == data:
                return len(page), len(packed), False
    except OSError:
        pass
    with open(OUT, "wb") as f:
        f.write(data)
    return len(page), len(packed), True


def main():
    raw, packed, written = build()
    print("web page: %d bytes -> %d gzipped%s" % (raw, packed, "" if written else " (unchanged)"))


if UNDER_PIO or __name__ == "__main__":
    main()
