#!/usr/bin/env python3
"""Check synthetic circuits against explicit Boolean truth tables."""

from dataclasses import replace
import itertools
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from devices import BY_NAME, DEVICES
from equations import assignments, definitions, evaluate, jedec, probe_map, put_term
from support import EXE, ROOT


# Expected values are ordered X,Y = 00, 01, 10, 11. These are circuit
# specifications, not captured output from the executable under test.
FUNCTIONS = (
    ("buffer", (("X",),), (0, 0, 1, 1)),
    ("inverter", (("!X",),), (1, 1, 0, 0)),
    ("and_not", (("X", "!Y"),), (0, 0, 1, 0)),
    ("exclusive_or", (("X", "!Y"), ("!X", "Y")), (0, 1, 1, 0)),
    ("constant_high", ((),), (1, 1, 1, 1)),
    ("contradiction", (("X", "!X"),), (0, 0, 0, 0)),
)


class EquationTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="jed2pld-equations-")
        self.addCleanup(self.directory.cleanup)
        self.work = Path(self.directory.name)

    def convert(self, device, bits, alternate=False, labels=None):
        (self.work / "probe.jed").write_bytes(jedec(device, bits, labels))
        result = subprocess.run([str(EXE), "-s", "--format=opal", *(["-a"] if alternate else []),
                                 "-d", device.name, "probe.jed"], cwd=self.work,
                                capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
        self.assertEqual(result.stderr, b"")
        text = (self.work / "probe.eqn").read_text()
        self.assertRegex(text, rf"\bOUT={device.output}\b")
        return text, assignments(text)

    def assert_truth(self, expression, expected, text, extra=None):
        observed = []
        for x, y in itertools.product((False, True), repeat=2):
            values = {"X": x, "Y": y, "pb0": False, "pb1": False, "pb2": False}
            values.update(extra or {})
            observed.append(evaluate(expression, values, definitions(text)))
        self.assertEqual(tuple(observed), tuple(map(bool, expected)), expression)

    def test_boolean_evaluator(self):
        self.assertTrue(evaluate("(X + /Y) * Z :+: X", {"X": 0, "Y": 0, "Z": 1}))
        self.assertFalse(evaluate("X # !Y & Z $ X", {"X": 1, "Y": 1, "Z": 1}))
        for expression in ("unknown", "X +", "X Y", "X ? Y"):
            with self.subTest(expression=expression), self.assertRaises(AssertionError):
                evaluate(expression, {"X": 1, "Y": 0})

    def test_layout_coverage(self):
        self.assertEqual(len(DEVICES), 80)
        self.assertEqual(len(BY_NAME), 80)
        # A new geometry requires a new explicit probe, not a silently skipped
        # test. This reads names only; expected equations do not use C tables.
        keys = set(re.findall(r'\{"([^"]+)",', (ROOT / "device_geometry.h").read_text()))
        keys -= {"MAPL244", "MAPL268"}
        def key(name):
            if name.startswith("GAL22V10"):
                return "G" + name[3:]
            if name == "GAL20RA10":
                return "G20RA10"
            return name[3:] if name.startswith(("PAL", "GAL")) else name
        self.assertEqual({key(device.name) for device in DEVICES}, keys)

    def test_all_layouts_and_operator_sets(self):
        for device in DEVICES:
            for name, products, expected in FUNCTIONS:
                for alternate in (False, True):
                    with self.subTest(device=device.name, function=name, alternate=alternate):
                        text, equations = self.convert(device, probe_map(device, products), alternate)
                        negative = "!" if alternate else "/"
                        lhs = (negative if device.negative else "") + "OUT"
                        if device.kind == "MAPL":
                            lhs = "OUT.d"
                        operator = ":=" if device.kind in ("R", "A", "X") else "="
                        self.assertIn(lhs, equations)
                        self.assertEqual(equations[lhs][0], operator)
                        self.assert_truth(equations[lhs][1], expected, text)
                        # Also check pin assignments independently of labels in
                        # the equation. A package remap must retain both pins.
                        self.assertRegex(text, rf"\bX={device.x}\b")
                        self.assertRegex(text, rf"\bY={device.y}\b")
                        if device.kind in ("T", "A", "MAPL"):
                            self.assertEqual(equations["OUT.oe"], ("=", "gnd"))
                        elif device.kind in ("V10", "6001"):
                            self.assertEqual(equations["OUT.oe"], ("=", "vcc"))
                        elif device.enable_pin:
                            self.assertEqual(equations["OUT.oe"], ("=", "EN"))
                        if name == "exclusive_or":
                            self.assertIn("#" if alternate else "+", equations[lhs][1])
                            self.assertIn("&" if alternate else "*", equations[lhs][1])

    def test_pal_registered_and_xor_cells(self):
        for name, output, address, enable in (
            ("PAL16R4", 17, 512, 11), ("PAL16R6", 18, 256, 11),
            ("PAL16RP4", 17, 512, 11), ("PAL16RP6", 18, 256, 11),
            ("PAL20R4", 20, 640, 13), ("PAL20R6", 21, 320, 13),
            ("PAL20RP4", 20, 640, 13), ("PAL20RP6", 21, 320, 13),
            ("PAL20X4", 20, 480, 13), ("PAL20X8", 22, 160, 13),
            ("PAL20X10", 23, 0, 13),
        ):
            device = replace(BY_NAME[name], output=output, address=address,
                             kind="X" if "X" in name else "R", enable_pin=enable)
            for alternate in (False, True):
                with self.subTest(device=name, alternate=alternate):
                    bits = probe_map(device, (("X",),))
                    if device.kind == "X":
                        put_term(bits, address + 2 * device.width, device.width,
                                 ("Y",), {"Y": device.y_fuse})
                    text, equations = self.convert(device, bits, alternate)
                    lhs = ("!" if alternate else "/") + "OUT"
                    self.assertEqual(equations[lhs][0], ":=")
                    self.assert_truth(equations[lhs][1],
                                      (0, 1, 1, 0) if device.kind == "X" else (0, 0, 1, 1), text)
                    self.assertEqual(equations["OUT.oe"], ("=", "EN"))
                    if device.kind == "X":
                        self.assertIn("$" if alternate else ":+:", equations[lhs][1])

    def test_pal_programmable_polarity(self):
        for name, fuse in (("PAL10P8", 320), ("PAL16P8", 2048),
                           ("PAL16RP8", 2048), ("EC16P8", 2048),
                           ("PAL16RA8", 2048), ("PAL20RA10", 3200), ("GAL20RA10", 3200)):
            device = BY_NAME[name]
            for polarity in (0, 1):
                with self.subTest(device=name, polarity=polarity):
                    bits = probe_map(device, (("X",),))
                    bits[fuse] = polarity
                    text, equations = self.convert(device, bits)
                    negative = device.negative ^ bool(polarity)
                    lhs = ("/" if negative else "") + "OUT"
                    self.assertIn(lhs, equations)
                    self.assert_truth(equations[lhs][1], (0, 0, 1, 1), text)

    def test_pal_async_controls(self):
        for name, enable in (("PAL16RA8", 11), ("PAL20RA10", 13), ("GAL20RA10", 13)):
            device = replace(BY_NAME[name], enable_pin=enable)
            columns = {"X": device.x_fuse, "Y": device.y_fuse}
            for combinational, alternate in itertools.product((False, True), repeat=2):
                with self.subTest(device=name, combinational=combinational, alternate=alternate):
                    bits = probe_map(device, (("X", "!Y"),))
                    for row, term in enumerate((("X",), ("Y",),
                                                () if combinational else ("X", "Y"),
                                                () if combinational else ("!X",))):
                        put_term(bits, row * device.width, device.width, term, columns)
                    text, equations = self.convert(device, bits, alternate)
                    lhs = ("!" if alternate else "/") + "OUT"
                    self.assertEqual(equations[lhs][0], "=" if combinational else ":=")
                    self.assert_truth(equations[lhs][1], (0, 0, 1, 0), text)
                    self.assert_truth(equations["OUT.c"][1], (0, 1, 0, 1), text)
                    self.assert_truth(equations["OUT.re"][1],
                                      (1, 1, 1, 1) if combinational else (0, 0, 0, 1), text)
                    self.assert_truth(equations["OUT.pr"][1],
                                      (1, 1, 1, 1) if combinational else (1, 1, 0, 0), text)
                    self.assert_truth(equations["OUT.oe"][1], (0, 0, 1, 1), text, {"EN": True})
                    self.assert_truth(equations["OUT.oe"][1], (0, 0, 0, 0), text, {"EN": False})

    def test_v8_modes_polarity_and_enable(self):
        for device in (d for d in DEVICES if d.kind == "V8"):
            base = 64 * device.width
            enable = 11 if device.pins == 20 else 16 if device.pins == 28 else 13
            labels = {device.x: "X", device.y: "Y", device.output: "OUT", enable: "EN"}
            for mode, positive, alternate in itertools.product(("SMALL", "REGISTERED", "MEDIUM"),
                                                               (False, True), (False, True)):
                with self.subTest(device=device.name, mode=mode, positive=positive, alternate=alternate):
                    bits = probe_map(device, (("X", "!Y"),))
                    bits[base] = int(positive)
                    bits[base + 144:base + 146] = {
                        "SMALL": [1, 0], "REGISTERED": [0, 1], "MEDIUM": [1, 1]}[mode]
                    if mode == "MEDIUM":
                        bits[base + 72:base + 80] = [1] * 8
                        bits[device.width:2 * device.width] = bits[:device.width]
                        put_term(bits, 0, device.width, ("Y",), {"Y": device.y_fuse})
                    text, equations = self.convert(device, bits, alternate, labels)
                    self.assertIn(f";$GALMODE {mode}\n", text)
                    lhs = ("" if positive else "!" if alternate else "/") + "OUT"
                    self.assertEqual(equations[lhs][0], ":=" if mode == "REGISTERED" else "=")
                    self.assert_truth(equations[lhs][1], (0, 0, 1, 0), text)
                    if mode == "REGISTERED":
                        self.assertEqual(equations["OUT.oe"], ("=", "EN"))
                    elif mode == "MEDIUM":
                        self.assert_truth(equations["OUT.oe"][1], (0, 1, 0, 1), text)
                    else:
                        self.assertNotIn("OUT.oe", equations)

    def test_v10_modes_and_global_controls(self):
        for device in (d for d in DEVICES if d.kind == "V10"):
            columns = {"X": device.x_fuse, "Y": device.y_fuse}
            for positive, combinational, alternate in itertools.product((False, True), repeat=3):
                with self.subTest(device=device.name, positive=positive,
                                  combinational=combinational, alternate=alternate):
                    bits = probe_map(device, (("X", "!Y"),))
                    bits[5808:5810] = [int(positive), int(combinational)]
                    put_term(bits, 0, 44, ("X",), columns)
                    put_term(bits, 44, 44, ("Y",), columns)
                    put_term(bits, 5764, 44, ("!Y",), columns)
                    text, equations = self.convert(device, bits, alternate)
                    lhs = ("" if positive else "!" if alternate else "/") + "OUT"
                    self.assertEqual(equations[lhs][0], "=" if combinational else ":=")
                    self.assert_truth(equations[lhs][1], (0, 0, 1, 0), text)
                    self.assert_truth(equations["global.re"][1], (0, 0, 1, 1), text)
                    self.assert_truth(equations["global.sp"][1], (1, 0, 1, 0), text)
                    self.assert_truth(equations["OUT.oe"][1], (0, 1, 0, 1), text)

    def test_feedback(self):
        devices = [d for d in DEVICES if d.kind in ("V8", "V10", "6001", "MAPL")]
        devices += [replace(BY_NAME["PAL16L8"], output=18, address=288),
                    BY_NAME["PAL16R8"], BY_NAME["PAL20RA10"]]
        for device in devices:
            with self.subTest(device=device.name):
                bits = probe_map(device, ((),))
                lhs, operator, inverted = "OUT", "=", False
                if device.kind == "V8":
                    base = 64 * device.width
                    bits[base + 144:base + 146] = [0, 1]
                    feedback = 2 if device.width == 32 else 6
                    operator = ":="
                elif device.kind == "V10":
                    feedback = 2
                    bits[5809] = 0
                    operator, inverted = ":=", True
                elif device.kind == "6001":
                    feedback = 56
                elif device.kind == "MAPL":
                    feedback = 40 if device.name == "MAPL128" else 42
                    lhs = "OUT.d"
                else:
                    feedback = 6 if device.kind == "T" else 2
                    lhs = "/OUT"
                    operator = "=" if device.kind == "T" else ":="
                bits[device.address + feedback] = 0
                text, equations = self.convert(device, bits)
                self.assertEqual(equations[lhs][0], operator)
                for state in (False, True):
                    self.assert_truth(equations[lhs][1], (state ^ inverted,) * 4, text, {"OUT": state})

    def test_gal6001_clock_reset_and_enable(self):
        for device in (d for d in DEVICES if d.kind == "6001"):
            columns = {"X": device.x_fuse, "Y": device.y_fuse}
            for positive, clock_enable, alternate in itertools.product((False, True), repeat=3):
                with self.subTest(device=device.name, positive=positive,
                                  clock_enable=clock_enable, alternate=alternate):
                    bits = probe_map(device, (("X", "!Y"),))
                    bits[8178:8182] = [int(clock_enable), 0, 0, int(positive)]
                    put_term(bits, 114, 78, ("Y",), columns)
                    bits[114 + 79] = 0
                    put_term(bits, 7998, 78, ("X",), columns)
                    put_term(bits, 8076, 78, ("!Y",), columns)
                    text, equations = self.convert(device, bits, alternate)
                    lhs = ("" if positive else "!" if alternate else "/") + "OUT"
                    self.assertEqual(equations[lhs][0], ":=")
                    self.assert_truth(equations[lhs][1], (0, 0, 1, 0), text)
                    self.assert_truth(equations["OUT.ce" if clock_enable else "OUT.c"][1], (0, 1, 0, 1), text)
                    self.assert_truth(equations["OUT.oe"][1], (0, 0, 1, 1), text)
                    self.assert_truth(equations["global.ar"][1], (1, 0, 1, 0), text)

    def test_mapl_pages_and_controls(self):
        for device in (d for d in DEVICES if d.kind == "MAPL"):
            node = 11 if device.name == "MAPL128" else 3
            columns = {"X": device.x_fuse, "Y": device.y_fuse}
            for page, jk, alternate in itertools.product(range(8), (False, True), (False, True)):
                with self.subTest(device=device.name, page=page, jk=jk, alternate=alternate):
                    bits = probe_map(device, ())
                    address = page * 16 * 112
                    put_term(bits, address, 58, ("X", "!Y"), columns)
                    bits[address + 58 + 2 * node] = 0
                    bits[14622 + node] = int(jk)
                    put_term(bits, 14336, 58, ("Y",), columns)
                    put_term(bits, 14394, 58, ("X",), columns)
                    text, equations = self.convert(device, bits, alternate)
                    lhs = "OUT.j" if jk else "OUT.d"
                    self.assertIn(lhs, equations)
                    self.assertNotIn("OUT.d" if jk else "OUT.j", equations)
                    for selected in range(8):
                        page_bits = {f"pb{bit}": bool(selected & (1 << bit)) for bit in range(3)}
                        self.assert_truth(equations[lhs][1],
                                          (0, 0, 1, 0) if selected == page else (0, 0, 0, 0), text, page_bits)
                    self.assert_truth(equations["OUT.re"][1], (0, 1, 0, 1), text)
                    self.assert_truth(equations["OUT.oe"][1], (0, 0, 1, 1), text)


if __name__ == "__main__":
    unittest.main()
