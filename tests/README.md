# Standalone tests

Run `make test` with Python 3.10 or later. Every test creates its inputs in a
temporary directory and uses the executable built by the Makefile. The
equation and regression tests read OPAL output, so they pass `--format=opal`. To test a
different build, set `JED2PLD` to its absolute path.

- `test_regressions.py` covers parser errors, integer overflow, file aliases,
  failed writes, output preservation, and platform line endings.
- `test_behavior.py` checks supported header formats, labels, dates, and
  constant reset expressions.
- `test_equations.py` programs Boolean functions into a selected macrocell in
  each of the 80 supported layouts. The six functions cover positive and
  negative literals, AND, sums of products, constant high, and contradictory
  literals. Each function is checked with both operator sets against an
  explicit four-entry truth table. Pin assignments, output polarity, and
  combinational versus registered assignment are checked separately.

The equation suite also exercises PAL registered and XOR cells, asynchronous
clock/reset/preset controls, V8 operating modes, V10 global controls, GAL6001
clock enables, feedback paths, and all eight MAPL pages in D and JK modes.
These are representative circuits, not an exhaustive enumeration of every
macrocell and configuration bit.

`devices.py` records fixed probe addresses and expected package pins.
`equations.py` programs those probes and evaluates the emitted Boolean
expressions. Expected truth tables are specified in the tests, never obtained
by running the converter. A coverage check compares device names with the
geometry header so a new layout also requires an explicit test probe. Banner
version, copyright text, and formatting whitespace are not equation expectations.

Run `make fuzz-sanitize CC=clang` to build with AddressSanitizer and UBSan,
run all three test scripts, and then exercise 7,200 random conversions across
the supported devices, as WinCUPL source and as OPAL equations with both
operator sets. An optional seed can be passed directly:

```sh
CC=clang python3 tests/fuzz_sanitize.py 42
```

Random inputs may legitimately produce diagnostics. A crash, sanitizer report,
timeout, or exit status other than 0 or 2 fails the run. Deterministic equation
tests require successful conversion and the specified logic.

`cupl_roundtrip.py` is not part of `make test`, since it needs a CUPL
compiler: set `CUPL` to the compiler and `LIBCUPL` to its `cupl.dl` library.
For random fuse maps it writes a `.pld` with `--format=cupl`, compiles it with
`cupl -m0`, decodes both fuse maps, and compares their equations pin by pin.
The comparison ignores differences that do not change the logic, such as
never-true terms that CUPL drops, and counts designs that CUPL cannot
represent or that jed2pld rejects separately from mismatches.

GitHub Actions runs the regular suite on Linux, macOS, and Windows, and runs
sanitizers on Linux. Unix file size limit and permission tests are skipped on
Windows; link tests skip when the host does not permit creating links.
