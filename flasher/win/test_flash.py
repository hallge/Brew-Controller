"""Unit tests for flash.py: python -m unittest discover -s flasher/win"""

import hashlib
import os
import sys
import unittest
from types import SimpleNamespace

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import flash  # noqa: E402


def port(device, vid=None, description=""):
    return SimpleNamespace(device=device, vid=vid, description=description)


def answers(*replies):
    it = iter(replies)
    return lambda _prompt: next(it)


def quiet(*_args, **_kwargs):
    pass


class PortTests(unittest.TestCase):
    def test_single_espressif_port_is_picked_without_asking(self):
        ports = [port("COM3", 0x0403, "FTDI"), port("COM7", 0x303A, "USB JTAG/serial debug unit")]
        self.assertEqual(flash.choose_port(ports, ask=answers(), out=quiet), "COM7")

    def test_no_espressif_port_lists_all_and_asks(self):
        ports = [port("COM3", 0x0403), port("COM4", None)]
        self.assertEqual(flash.choose_port(ports, ask=answers("2"), out=quiet), "COM4")

    def test_two_boards_ask_between_them_only(self):
        ports = [port("COM3", 0x0403), port("COM7", 0x303A), port("COM9", 0x303A)]
        self.assertEqual(flash.choose_port(ports, ask=answers("2"), out=quiet), "COM9")

    def test_port_name_answer_and_retry_after_bad_answer(self):
        ports = [port("COM3"), port("COM4")]
        self.assertEqual(flash.choose_port(ports, ask=answers("9", "x", "com3"), out=quiet), "COM3")

    def test_no_ports_explains_boot_button(self):
        with self.assertRaises(flash.FlashError) as ctx:
            flash.choose_port([], ask=answers(), out=quiet)
        self.assertIn("BOOT", str(ctx.exception))



class ShaTests(unittest.TestCase):
    data = b"\xe9" + b"factory image" * 100

    def sums(self, digest=None):
        digest = digest or hashlib.sha256(self.data).hexdigest()
        other = "0" * 64
        # Same layout as the release: the existing lines first, factory.bin after them.
        return (f"{other}  firmware.bin\n{other}  firmware.elf\n{other}  bootloader.bin\n"
                f"{other}  partitions.bin\n{digest}  factory.bin\n")

    def test_matching_hash_passes(self):
        self.assertEqual(flash.check_sha256(self.data, self.sums()),
                         hashlib.sha256(self.data).hexdigest())

    def test_wrong_hash_fails(self):
        with self.assertRaises(flash.FlashError):
            flash.check_sha256(self.data, self.sums("a" * 64))

    def test_missing_line_fails(self):
        with self.assertRaises(flash.FlashError):
            flash.check_sha256(self.data, "0" * 64 + "  firmware.bin\n")

    def test_binary_mode_and_uppercase(self):
        digest = hashlib.sha256(self.data).hexdigest().upper()
        self.assertTrue(flash.check_sha256(self.data, f"{digest} *factory.bin\r\n"))

    def test_firmware_bin_line_is_not_taken_for_factory(self):
        digest = hashlib.sha256(self.data).hexdigest()
        with self.assertRaises(flash.FlashError):
            flash.check_sha256(self.data, f"{digest}  firmware.bin\n")


def release(*names, tag="v0.1.21"):
    base = f"https://github.com/hallge/Brew-Controller/releases/download/{tag}/"
    return {"tag_name": tag,
            "assets": [{"name": n, "browser_download_url": base + n} for n in names]}


class ReleaseTests(unittest.TestCase):
    def test_picks_factory_and_sums(self):
        rel = release("firmware.bin", "firmware.elf", "bootloader.bin", "partitions.bin",
                      "factory.bin", "SHA256SUMS.txt", "BrewController-Flasher.exe")
        tag, image, sums = flash.pick_assets(rel)
        self.assertEqual(tag, "v0.1.21")
        self.assertTrue(image.endswith("/v0.1.21/factory.bin"))
        self.assertTrue(sums.endswith("/v0.1.21/SHA256SUMS.txt"))

    def test_older_release_without_factory_image_fails(self):
        with self.assertRaises(flash.FlashError) as ctx:
            flash.pick_assets(release("firmware.bin", "SHA256SUMS.txt", tag="v0.1.20"))
        self.assertIn("factory.bin", str(ctx.exception))

    def test_missing_sums_fails(self):
        with self.assertRaises(flash.FlashError):
            flash.pick_assets(release("factory.bin"))


class FlashTests(unittest.TestCase):
    def test_args_write_at_zero_with_optional_erase(self):
        a = flash.esptool_args("COM7", "f.bin", 921600, True)
        self.assertEqual(a[-2:], ["0x0", "f.bin"])
        self.assertIn("--erase-all", a)
        self.assertEqual(a[a.index("--chip") + 1], "esp32s3")
        self.assertNotIn("--erase-all", flash.esptool_args("COM7", "f.bin", 921600, False))

    def test_falls_back_to_115200(self):
        calls = []

        def run(args):
            calls.append(args[args.index("--baud") + 1])
            if len(calls) == 1:
                raise RuntimeError("timed out")

        self.assertEqual(flash.flash("COM7", "f.bin", True, run=run, out=quiet), 115200)
        self.assertEqual(calls, ["921600", "115200"])

    def test_failure_at_115200_is_raised(self):
        def run(_args):
            raise RuntimeError("no board")

        with self.assertRaises(RuntimeError):
            flash.flash("COM7", "f.bin", False, run=run, out=quiet)

    def test_erase_question_defaults_to_yes(self):
        self.assertTrue(flash.ask_yes("Erase?", True, ask=answers("")))
        self.assertFalse(flash.ask_yes("Erase?", True, ask=answers("n")))


if __name__ == "__main__":
    unittest.main()
