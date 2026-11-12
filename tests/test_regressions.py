#!/usr/bin/env python3
"""Self-contained parser and file handling regression tests."""

import os
from pathlib import Path
import signal
import stat
import subprocess
import tempfile
import unittest
from support import EXE

try:
    import resource
except ImportError:
    resource = None


VALID = b"\x02PAL16L8*QF2048*QP20*F0*L0 0*\x030000"
INVALID_CONFIG = b"\x02GAL16V8*QF2194*F0*L0 0*\x030000"
PREVIOUS = b"previous successful equations\n"


class ConversionTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="jed2pld-test-")
        self.addCleanup(self.directory.cleanup)
        self.work = Path(self.directory.name)
        self.input = self.work / "input.jed"
        self.output = self.work / "input.eqn"
        self.input.write_bytes(VALID)

    def run_converter(self, *args, preexec_fn=None):
        result = subprocess.run(
            [str(EXE), "-s", "--format=opal", *args, "input.jed"],
            cwd=self.work,
            capture_output=True,
            preexec_fn=preexec_fn,
            timeout=30,
        )
        self.assertFalse(list(self.work.glob("*.tmp")), "temporary file left behind")
        return result

    def test_success_replaces_existing_equations(self):
        self.output.write_bytes(PREVIOUS)
        result = self.run_converter()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, b"")
        equations = self.output.read_bytes()
        self.assertNotEqual(equations, PREVIOUS)
        self.assertIn(b"PAL16L8", equations)
        self.assertEqual(self.input.read_bytes(), VALID)
        if os.name != "nt":
            self.assertNotIn(b"\r\n", equations)
        else:
            self.assertIn(b"\r\n", equations)
            self.assertNotIn(b"\n", equations.replace(b"\r\n", b""))

    def test_invalid_configuration_preserves_destination(self):
        self.input.write_bytes(INVALID_CONFIG)
        for existing in (False, True):
            with self.subTest(existing=existing):
                if existing:
                    self.output.write_bytes(PREVIOUS)
                result = self.run_converter()
                self.assertEqual(result.returncode, 2)
                self.assertIn(b"E0408", result.stderr)
                if existing:
                    self.assertEqual(self.output.read_bytes(), PREVIOUS)
                else:
                    self.assertFalse(self.output.exists())

    @unittest.skipIf(os.name == "nt", "requires POSIX permissions")
    def test_existing_output_permissions_are_preserved(self):
        self.output.write_bytes(PREVIOUS)
        self.output.chmod(0o600)
        result = self.run_converter()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(stat.S_IMODE(self.output.stat().st_mode), 0o600)

    @unittest.skipUnless(resource is not None and hasattr(signal, "SIGXFSZ"),
                         "requires POSIX file size limits")
    def test_write_failures_preserve_destination(self):
        # Small output fails on fclose; dense output also exercises an earlier
        # stdio buffer flush. Neither failure may publish partial equations.
        dense = b"\x02PAL22V10*QF5828*F1*L0 " + b"01" * 2900 + b"*\x030000"

        def limit_output():
            signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
            resource.setrlimit(resource.RLIMIT_FSIZE, (64, 64))

        for body in (VALID, dense):
            self.input.write_bytes(body)
            baseline = self.run_converter()
            self.assertEqual(baseline.returncode, 0, baseline.stderr)
            if body == dense:
                self.assertGreater(self.output.stat().st_size, 8192)
            for existing in (False, True):
                with self.subTest(dense=body == dense, existing=existing):
                    self.output.unlink(missing_ok=True)
                    if existing:
                        self.output.write_bytes(PREVIOUS)
                    result = self.run_converter(preexec_fn=limit_output)
                    self.assertEqual(result.returncode, 2)
                    self.assertIn(b"Could not write output file", result.stderr)
                    if existing:
                        self.assertEqual(self.output.read_bytes(), PREVIOUS)
                    else:
                        self.assertFalse(self.output.exists())

    def test_publish_failure_preserves_destination(self):
        self.output.mkdir()
        sentinel = self.output / "keep"
        sentinel.write_bytes(PREVIOUS)
        result = self.run_converter()
        self.assertEqual(result.returncode, 2)
        self.assertIn(b"Could not replace output file", result.stderr)
        self.assertEqual(sentinel.read_bytes(), PREVIOUS)

    def test_log_cannot_alias_input_or_equations(self):
        self.output.write_bytes(PREVIOUS)
        for destination in (self.input, self.output):
            for name in (destination.name, "./" + destination.name):
                with self.subTest(name=name):
                    result = self.run_converter("-e", name)
                    self.assertEqual(result.returncode, 2)
                    self.assertIn(b"Error log must be different", result.stderr)
                    self.assertEqual(self.input.read_bytes(), VALID)
                    self.assertEqual(self.output.read_bytes(), PREVIOUS)

    def test_equations_cannot_alias_input(self):
        for name in ("input.jed", "./input.jed"):
            with self.subTest(name=name):
                result = self.run_converter("-o", name)
                self.assertEqual(result.returncode, 2)
                self.assertEqual(self.input.read_bytes(), VALID)

    def check_link_aliases(self, symbolic):
        self.output.write_bytes(PREVIOUS)
        for option, target in (("-e", self.input), ("-e", self.output), ("-o", self.input)):
            with self.subTest(option=option, target=target.name):
                alias = self.work / "alias.err"
                try:
                    if symbolic:
                        alias.symlink_to(target.name)
                    else:
                        os.link(target, alias)
                except OSError as error:
                    self.skipTest(f"links unavailable: {error}")
                result = self.run_converter(option, alias.name)
                self.assertEqual(result.returncode, 2)
                self.assertEqual(self.input.read_bytes(), VALID)
                self.assertEqual(self.output.read_bytes(), PREVIOUS)
                self.assertEqual(alias.read_bytes(), target.read_bytes())
                alias.unlink()

    def test_hard_link_aliases(self):
        self.check_link_aliases(symbolic=False)

    def test_symbolic_link_aliases(self):
        self.check_link_aliases(symbolic=True)

    def test_error_log_is_truncated_and_receives_errors(self):
        log = self.work / "conversion.err"
        log.write_bytes(b"stale diagnostic\n" * 50)
        self.input.write_bytes(VALID.replace(b"QF2048", b"QF1"))
        result = self.run_converter("-e", log.name)
        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stderr, b"")
        self.assertIn(b"E0404", log.read_bytes())
        self.assertNotIn(b"stale diagnostic", log.read_bytes())

    def test_unwritable_log_is_an_error(self):
        result = self.run_converter("-e", "missing/log.err")
        self.assertEqual(result.returncode, 2)
        self.assertIn(b"Cannot write error file", result.stderr)
        self.assertFalse(self.output.exists())

    @unittest.skipUnless(resource is not None and hasattr(signal, "SIGXFSZ"),
                         "requires POSIX file size limits")
    def test_error_log_write_failure_is_reported(self):
        # Warnings fill the log while the valid equation file fits the limit.
        self.input.write_bytes(VALID.replace(b"L0 0", b"L0 0" + b"X" * 100))

        def limit_output():
            signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
            resource.setrlimit(resource.RLIMIT_FSIZE, (1024, 1024))

        result = self.run_converter("-e", "conversion.err", preexec_fn=limit_output)
        self.assertEqual(result.returncode, 2)
        self.assertIn(b"Could not write error log", result.stderr)

    def test_oversized_counts_and_addresses_are_rejected(self):
        cases = (
            (b"QF2048", b"QF4294969344", b"E0404"),
            (b"QP20", b"QP4294967316", b"E0406"),
            (b"L0 0", b"L4294967296 1", b"E0405"),
            (b"QF2048", b"QF18446744073709553664", b"E0404"),
            (b"QP20", b"QP18446744073709551636", b"E0406"),
            (b"L0 0", b"L18446744073709551616 1", b"E0405"),
            (b"QF2048", b"QF-18446744073709549568", b"E0404"),
            (b"QP20", b"QP-18446744073709551596", b"E0406"),
        )
        self.output.write_bytes(PREVIOUS)
        for old, new, diagnostic in cases:
            with self.subTest(field=new):
                self.input.write_bytes(VALID.replace(old, new))
                result = self.run_converter()
                self.assertEqual(result.returncode, 2)
                self.assertIn(diagnostic, result.stderr)
                self.assertEqual(self.output.read_bytes(), PREVIOUS)

    def test_unterminated_final_fuse_field_is_rejected(self):
        prefix = VALID.split(b"\x03")[0]
        self.output.write_bytes(PREVIOUS)
        for final in (b"L32 1111", b"L32 ", b"L32"):
            with self.subTest(field=final):
                self.input.write_bytes(prefix + final)
                result = self.run_converter()
                self.assertEqual(result.returncode, 2)
                self.assertIn(b"E0010", result.stderr)
                self.assertEqual(self.output.read_bytes(), PREVIOUS)
        self.input.write_bytes(prefix + b"L32 1111*\x030000")
        result = self.run_converter()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotEqual(self.output.read_bytes(), PREVIOUS)


if __name__ == "__main__":
    unittest.main()
