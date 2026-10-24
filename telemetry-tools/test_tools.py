"""Tests for fleet.py and crashes.py. Run from this folder: python -m unittest"""
import io
import json
import os
import subprocess
import sys
import tempfile
import textwrap
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import crashes  # noqa: E402
import fleet  # noqa: E402

FIXTURES = os.path.join(HERE, "fixtures")
A = "a1a1a1a1a1b2b2b2b2b2"
B = "c3c3c3c3c3d4d4d4d4d4"
NOW = fleet.parse_date("2026-10-23")

# Stands in for xtensa-esp32s3-elf-addr2line: insists on -pfiaC -e <elf>, then
# prints one line per address in the -pfiaC shape.
STUB = textwrap.dedent("""\
    import sys
    args = sys.argv[1:]
    if args[:2] != ["-pfiaC", "-e"] or len(args) < 4:
        sys.exit("bad arguments: %r" % args)
    for a in args[3:]:
        print("%s: fake_fn_%s at src/fake.cpp:%d" % (a, a[-4:], int(a, 16) % 1000))
""")


def analysed(**kw):
    records, problems = fleet.load(FIXTURES, **kw)
    return fleet.analyse(records, now=NOW), problems


class FleetTest(unittest.TestCase):
    def setUp(self):
        self.a, self.problems = analysed()

    def test_units_versions_and_silence(self):
        self.assertEqual(self.problems, [])
        self.assertEqual(self.a["reports"], {"diag_daily": 4, "diag_crash": 3})
        units = {u["kid"]: u for u in self.a["units"]}
        self.assertEqual(set(units), {A, B})
        self.assertEqual(units[A]["fw"], "v0.1.20")  # newest report wins over the older crash
        self.assertEqual(units[A]["last"], "2026-10-22")
        self.assertEqual((units[B]["daily"], units[B]["crash"]), (1, 2))
        self.assertEqual(self.a["versions"], {"v0.1.19": 1, "v0.1.20": 1})
        self.assertEqual(self.a["silent"], [B])

    def test_emulator_left_out_unless_asked(self):
        a, _ = analysed(emulator=True)
        self.assertEqual(len(a["units"]), 3)
        self.assertNotIn("e0e0e0e0e0e0e0e0e0e0", [u["kid"] for u in self.a["units"]])

    def test_hardware(self):
        hw = self.a["hardware"]
        self.assertEqual(hw["scales"], {"acaia / LUNAR": 1, "bookoo / Themis Mini": 1})
        bots = {(b["role"], b["kind"]): b["units"] for b in hw["bots"]}
        self.assertEqual(bots, {("brew", "SwitchBot Bot"): 1, ("power", "SwitchBot Bot"): 1,
                                ("brew", "Fingerbot"): 1, ("stop", "Fingerbot"): 1})
        self.assertEqual(hw["machines"], {"breville-dual-boiler (toggle_button)": 1,
                                          "lever-generic (momentary_button)": 1})

    def test_links(self):
        links = {(l["role"], l["device"]): l for l in self.a["links"]}
        power = links[("power", "SwitchBot Bot")]
        self.assertEqual((power["failures"], power["attempts"], power["longest_wait_ms"]), (2, 7, 7000))
        self.assertAlmostEqual(power["failure_rate"], 0.286)
        bookoo = links[("scale", "bookoo")]
        self.assertEqual((bookoo["failure_rate"], bookoo["longest_wait_ms"]), (0.6, 12000))
        self.assertEqual(self.a["scale_frames"]["bookoo"], {"frames": 900, "bad": 4, "dropped": 7, "units": 1})

    def test_shots(self):
        s = self.a["shots"]["breville-dual-boiler"]
        self.assertEqual(s["count"], 6)
        self.assertEqual(s["end"], {"weight": 5, "manual": 1})
        self.assertEqual(s["faults"], {"scale_lost": 1})
        self.assertEqual(s["stop_latency_ms"], {"reports": 3, "median": 3400, "lowest_median": 3300,
                                                "highest_median": 3500, "max": 4200})
        self.assertEqual(s["overshoot_g"]["max"], 2.6)
        lever = self.a["shots"]["lever-generic"]
        self.assertEqual(lever["faults"], {"stop_not_confirmed": 2})
        self.assertEqual(lever["stop_retried"], 2)

    def test_power(self):
        self.assertEqual(self.a["power"]["total"],
                         {"done": 6, "bot_unreachable": 1, "machine_busy": 1, "failed": 0})

    def test_warnings_ranked_with_unit_counts(self):
        w = [(x["n"], x["units"], x["p"][:14]) for x in self.a["warnings"]]
        self.assertEqual(w, [(6, 2, "<scale>: notif"), (3, 1, "<power bot>: c"), (2, 1, "<stop bot>: pr")])

    def test_crash_groups(self):
        c = self.a["crashes"]
        self.assertEqual(len(c), 2)
        self.assertEqual(c[0]["key"], ["0x42012345", "0x42012345", "0x4200abcd", "0x42009876"])
        self.assertEqual(c[0]["count"], 2)  # different 4th backtrace address, same group
        self.assertEqual(c[0]["units"], [A, B])
        self.assertEqual(c[0]["versions"], ["v0.1.19"])
        self.assertEqual(c[1]["key"], ["no dump", "TASK WATCHDOG"])

    def test_since(self):
        a, _ = analysed(since=fleet.parse_date("2026-10-20"))
        self.assertEqual([u["kid"] for u in a["units"]], [A])
        self.assertEqual(a["reports"], {"diag_daily": 3})
        self.assertEqual(a["crashes"], [])

    def test_unreadable_files_are_reported_not_fatal(self):
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(os.path.join(d, "reports"))
            with open(os.path.join(d, "reports", "bad.json"), "w") as f:
                f.write("{not json")
            with open(os.path.join(d, "reports", "odd.json"), "w") as f:
                json.dump({"body": json.dumps({"t": "something_else"})}, f)
            records, problems = fleet.load(d)
            self.assertEqual(records, [])
            self.assertEqual(len(problems), 2)

    def test_cli_json_and_text(self):
        py = [sys.executable, os.path.join(HERE, "fleet.py"), FIXTURES, "--now", "2026-10-23"]
        out = subprocess.run(py + ["--json"], capture_output=True, text=True, check=True).stdout
        a = json.loads(out)
        self.assertEqual(a["silent"], [B])
        self.assertEqual(a["crashes"][0]["count"], 2)
        text = subprocess.run(py, capture_output=True, text=True, check=True).stdout
        self.assertIn("Silent for more than 3 days: 1", text)
        self.assertIn("2 x 0x42012345", text)


class CrashesTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.stub = os.path.join(self.tmp.name, "addr2line_stub.py")
        with open(self.stub, "w") as f:
            f.write(STUB)
        self.cache = os.path.join(self.tmp.name, "elf")
        self.elf = os.path.join(self.cache, "v0.1.19", "firmware.elf")
        os.makedirs(os.path.dirname(self.elf))
        with open(self.elf, "wb") as f:
            f.write(b"\x7fELF fake")

    def tearDown(self):
        self.tmp.cleanup()

    def test_decode_one_address(self):
        cmd = crashes.find_addr2line(self.stub)
        self.assertEqual(cmd, [sys.executable, self.stub])
        self.assertEqual(crashes.decode(cmd, self.elf, ["0x42012345"]),
                         ["0x42012345: fake_fn_2345 at src/fake.cpp:821"])

    def test_cached_elf_is_used_without_download(self):
        path, why = crashes.fetch_elf("v0.1.19", self.cache)
        self.assertEqual((path, why), (self.elf, None))

    def test_report_decodes_groups_and_prints_log_tail(self):
        records, _ = fleet.load(FIXTURES)
        out = io.StringIO()
        crashes.report(records, crashes.find_addr2line(self.stub), self.cache, tail=2, out=out)
        text = out.getvalue()
        self.assertIn("Crash group 1: 2 x 0x42012345", text)
        self.assertIn("0x4200abcd: fake_fn_abcd at src/fake.cpp", text)
        self.assertIn("0x40379999: fake_fn_9999", text)  # newest crash's full backtrace
        self.assertIn("E shot: weight frame after scale freed", text)
        self.assertNotIn("I wifi: connected", text)  # tail=2 keeps only the last two lines
        self.assertIn("Crash group 2: 1 x no dump TASK WATCHDOG", text)


if __name__ == "__main__":
    unittest.main()
