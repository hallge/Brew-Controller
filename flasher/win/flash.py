"""Brew Controller first-flash tool for Windows (built into BrewController-Flasher.exe).

Finds the board on USB, downloads the latest release's factory.bin from GitHub, checks its
SHA256 against the release's SHA256SUMS.txt, and writes it with esptool. Without an erase it is
written as two parts that leave the settings partition (nvs, 0x9000-0xdfff) as it is, so a
controller in use keeps its settings, Wi-Fi, pairing and signing key.

    BrewController-Flasher.exe                  latest release, board found by USB ID
    BrewController-Flasher.exe --port COM5      a given port
    BrewController-Flasher.exe --file my.bin    a local factory image (no download, no hash check)
"""

import argparse
import hashlib
import json
import os
import sys
import tempfile
import urllib.request

REPO = "hallge/Brew-Controller"
LATEST_URL = f"https://api.github.com/repos/{REPO}/releases/latest"
IMAGE_NAME = "factory.bin"
SUMS_NAME = "SHA256SUMS.txt"
ESPRESSIF_VID = 0x303A
# partitions.csv: nvs (the settings) at 0x9000, then otadata at 0xe000.
NVS_START = 0x9000
NVS_END = 0xE000
FAST_BAUD = 921600
SAFE_BAUD = 115200


class FlashError(Exception):
    pass


# --- Finding the board ---------------------------------------------------------------------

def espressif_ports(ports):
    """The ports whose USB vendor ID is Espressif's (the ESP32-S3's own USB)."""
    return [p for p in ports if getattr(p, "vid", None) == ESPRESSIF_VID]


def describe(port):
    desc = getattr(port, "description", "") or ""
    return f"{port.device}  {desc}".rstrip()


def choose_port(ports, ask=input, out=print):
    """Picks the board's port: the only Espressif one, or asks the user to choose."""
    if not ports:
        raise FlashError(
            "No serial ports found. Plug the board in with a USB-C data cable. "
            "If it still does not show up, hold BOOT while plugging it in.")
    found = espressif_ports(ports)
    if len(found) == 1:
        out(f"Board found on {describe(found[0])}")
        return found[0].device
    choices = found or list(ports)
    if found:
        out("More than one Espressif board is plugged in:")
    else:
        out("No board was recognised by its USB ID. Ports found:")
    for i, p in enumerate(choices, 1):
        out(f"  {i}. {describe(p)}")
    while True:
        answer = ask(f"Which one is the Brew Controller? [1-{len(choices)}, or a port name]: ").strip()
        if answer.isdigit() and 1 <= int(answer) <= len(choices):
            return choices[int(answer) - 1].device
        for p in choices:
            if answer.lower() == p.device.lower():
                return p.device
        out("Not one of the listed ports.")


def list_ports():
    from serial.tools import list_ports as lp
    return list(lp.comports())


# --- The release ---------------------------------------------------------------------------

def pick_assets(release):
    """From a GitHub release JSON: (tag, factory.bin URL, SHA256SUMS.txt URL)."""
    urls = {a.get("name"): a.get("browser_download_url") for a in release.get("assets", [])}
    tag = release.get("tag_name", "?")
    if not urls.get(IMAGE_NAME):
        raise FlashError(f"Release {tag} has no {IMAGE_NAME}.")
    if not urls.get(SUMS_NAME):
        raise FlashError(f"Release {tag} has no {SUMS_NAME}.")
    return tag, urls[IMAGE_NAME], urls[SUMS_NAME]


def expected_sha256(sums_text, name=IMAGE_NAME):
    """The hash for `name` in sha256sum output ("<64 hex>  name", or "*name" in binary mode)."""
    for line in sums_text.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[1].lstrip("*") == name and len(parts[0]) == 64:
            return parts[0].lower()
    raise FlashError(f"{SUMS_NAME} has no line for {name}.")


def check_sha256(data, sums_text, name=IMAGE_NAME):
    want = expected_sha256(sums_text, name)
    got = hashlib.sha256(data).hexdigest()
    if got != want:
        raise FlashError(f"{name} is damaged: SHA256 {got}, expected {want}. Try again.")
    return got


def fetch(url, accept=None):
    req = urllib.request.Request(url, headers={"User-Agent": "BrewController-Flasher"})
    if accept:
        req.add_header("Accept", accept)
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def download_latest(out=print):
    """Downloads and checks the latest release's factory image; returns (tag, local path)."""
    out("Looking up the latest release...")
    release = json.loads(fetch(LATEST_URL, "application/vnd.github+json"))
    tag, image_url, sums_url = pick_assets(release)
    out(f"Downloading {IMAGE_NAME} from {tag}...")
    data = fetch(image_url)
    sums = fetch(sums_url).decode("utf-8", "replace")
    check_sha256(data, sums)
    out(f"SHA256 checked ({len(data) // 1024} KB).")
    path = os.path.join(tempfile.gettempdir(), f"BrewController-{tag}-{IMAGE_NAME}")
    with open(path, "wb") as f:
        f.write(data)
    return tag, path


# --- Flashing ------------------------------------------------------------------------------

def split_image(data):
    """The factory image without the settings partition: [(offset, bytes), ...]. The image has
    nothing there (0xFF padding); writing it would wipe the settings."""
    if len(data) <= NVS_END or data[NVS_START:NVS_END] != b"\xff" * (NVS_END - NVS_START):
        raise FlashError("This is not a Brew Controller factory image (no gap for the settings).")
    return [(0x0, data[:NVS_START]), (NVS_END, data[NVS_END:])]


def write_parts(path):
    """Writes the two parts beside `path`; returns [(offset, part path), ...]."""
    with open(path, "rb") as f:
        parts = split_image(f.read())
    out = []
    for offset, chunk in parts:
        part = f"{path}.{offset:x}.bin"
        with open(part, "wb") as f:
            f.write(chunk)
        out.append((offset, part))
    return out


def esptool_args(port, parts, baud, erase):
    """`parts`: [(offset, path), ...] or one path written at 0x0."""
    if isinstance(parts, str):
        parts = [(0x0, parts)]
    args = ["--chip", "esp32s3", "--port", port, "--baud", str(baud),
            "--before", "default-reset", "--after", "hard-reset", "write-flash"]
    if erase:
        args.append("--erase-all")
    for offset, path in parts:
        args += [hex(offset), path]
    return args


def run_esptool(args):
    import esptool
    try:
        esptool.main(args)
    except SystemExit as e:  # esptool's command line exits on bad arguments
        if e.code not in (0, None):
            raise FlashError(f"esptool stopped (exit {e.code}).")


def flash(port, path, erase, baud=FAST_BAUD, run=run_esptool, out=print):
    """Writes the image at 0x0, retrying at 115200 if the fast rate fails."""
    try:
        run(esptool_args(port, path, baud, erase))
        return baud
    except Exception as e:  # noqa: BLE001 - any esptool failure gets one slow retry
        if baud == SAFE_BAUD:
            raise
        out(f"\nThat did not work at {baud} baud ({e}). Trying again at {SAFE_BAUD}...\n")
        run(esptool_args(port, path, SAFE_BAUD, erase))
        return SAFE_BAUD


def ask_yes(question, default=True, ask=input):
    hint = "[Y/n]" if default else "[y/N]"
    answer = ask(f"{question} {hint} ").strip().lower()
    return default if not answer else answer.startswith("y")


NEXT_STEPS = """
Done. The controller restarts by itself.

Next:
  1. On your phone or computer, join the Wi-Fi network BrewByWeight-xxxx
     (xxxx is shown on the controller's screen).
  2. Open http://192.168.4.1 in a browser.
  3. Enter your home Wi-Fi there; after that, updates come over Wi-Fi
     (Setup, Firmware) and this tool is not needed again.
"""


def main(argv=None, ask=input, out=print):
    ap = argparse.ArgumentParser(description="Flash the Brew Controller firmware over USB.")
    ap.add_argument("--port", help="serial port, e.g. COM5 (default: find the board)")
    ap.add_argument("--file", help="a local factory image to flash instead of the latest release")
    ap.add_argument("--baud", type=int, default=FAST_BAUD, help=f"baud rate (default {FAST_BAUD})")
    erase = ap.add_mutually_exclusive_group()
    erase.add_argument("--erase", dest="erase", action="store_true", default=None,
                       help="erase the whole flash first, without asking")
    erase.add_argument("--no-erase", dest="erase", action="store_false",
                       help="keep settings and shot history, without asking")
    args = ap.parse_args(argv)

    out("Brew Controller flasher\n")
    if args.file:
        if not os.path.isfile(args.file):
            raise FlashError(f"No such file: {args.file}")
        path = args.file
        out(f"Using {path} (no hash check for a local file).")
    else:
        _, path = download_latest(out)

    port = args.port or choose_port(list_ports(), ask, out)

    erase_first = args.erase
    if erase_first is None:
        erase_first = ask_yes(
            "Erase the board first? Yes for a new board. No keeps a controller's settings, Wi-Fi, "
            "pairing, coffees and shots.", True, ask)

    # Erasing: the whole image. Keeping: everything but the settings partition.
    parts = path if erase_first else write_parts(path)
    out(f"\nFlashing {port}. Do not unplug the board until this finishes.\n")
    flash(port, parts, erase_first, args.baud, out=out)
    out(NEXT_STEPS)
    return 0


def pause_if_double_clicked():
    # A double-clicked .exe gets its own console window that closes at exit; keep it open.
    if getattr(sys, "frozen", False):
        try:
            input("Press Enter to close.")
        except EOFError:
            pass


if __name__ == "__main__":
    code = 1
    try:
        code = main()
    except FlashError as e:
        print(f"\n{e}")
    except KeyboardInterrupt:
        print("\nStopped.")
    except Exception as e:  # noqa: BLE001 - show any failure plainly before the window closes
        print(f"\nSomething went wrong: {e}")
        print("If the board was not found, unplug it, hold BOOT while plugging it back in, and run this again.")
    pause_if_double_clicked()
    sys.exit(code)
