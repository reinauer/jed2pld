# jed2pld

Convert JEDEC fuse maps for PAL, GAL, and MAPL programmable logic devices
back into source. By default the design is written as a WinCUPL `.pld` file
that can be edited and recompiled; `--format=opal` writes OPAL Boolean
equations instead, which show logic, output polarity, feedback, and register
configuration in the style of the DOS JED2EQN tool.

## Build

A C11 compiler and `make` are required:

```sh
make
```

This produces the `jed2pld` executable. Device tables are built in, so no
additional library files are needed when running it.

To select a compiler or change compiler flags:

```sh
make clean
make CC=clang CFLAGS='-O2 -Wall -Wextra -std=c11'
```

## Quick start

```sh
./jed2pld logic.jed
```

This reads `logic.jed` and writes `logic.pld` in the same directory. An existing
output file is replaced only after conversion and writing succeed. Failed
conversions leave existing output intact. Specify a different output path
with `-o`:

```sh
./jed2pld -o decoded.pld logic.jed
```

For OPAL equations in `logic.eqn` instead:

```sh
./jed2pld --format=opal logic.jed
```

The device is detected from the JEDEC header. If detection fails, or the header
contains the wrong device, select it explicitly:

```sh
./jed2pld -d GAL22V10 -o decoded.pld logic.jed
```

## Command line

```text
jed2pld [options] file.jed
```

| Option | Description |
| --- | --- |
| `-a` | Use the alternate Boolean operators shown below (OPAL output). |
| `-d device` | Select a device instead of using header detection. |
| `-e file` | Write warnings and errors to a file instead of standard error. |
| `-o file` | Write the output to the specified file. |
| `-s` | Suppress progress messages; warnings and errors are still reported. |
| `--pins file` | Take pin names from a file; see [Pin names](#pin-names). |
| `--format=cupl\|opal` | Write a WinCUPL source file (the default) or OPAL equations. |
| `--help`, `--version` | Show the usage text or the version. |

Each short option also has a long form: `--alternate`, `--device`,
`--error-file`, `--output`, and `--silent`. Option arguments may be separate or
attached: `-d GAL22V10`, `-dGAL22V10`, `--device GAL22V10`, and
`--device=GAL22V10` are equivalent. Filenames without an extension receive
`.jed` for input, `.pld` for output (`.eqn` with `--format=opal`), or `.err`
for an error log.

For a quiet conversion with a separate error log:

```sh
./jed2pld -s -d GAL22V10 -e decode.err -o decoded.pld logic.jed
```

Input, output, and error log files must be distinct, including when using
hard links or symbolic links. The output directory must be writable so that a
temporary output file can be created there and renamed on success.

Run `./jed2pld` without arguments to display usage. Exit status is `0` on
success and for `--help` and `--version`, `1` when displaying usage without
arguments, or `2` on an error.

## OPAL equations

`--format=opal` writes the equations in the form of the DOS JED2EQN tool. The
equation file contains the device type, signal-to-pin assignments, and
Boolean equations. Depending on the device, it also includes register controls,
output enables, global controls, and a user electronic signature (`@ues`).

Signal names are taken from `NOTE PINS` and `NOTE NODES` records when available,
or from a file given with `--pins`. Otherwise, names are generated from pin or
node numbers.

| Operation | Default syntax | With `-a` |
| --- | --- | --- |
| NOT | `/` | `!` |
| AND | `*` | `&` |
| OR | `+` | `#` |
| XOR | `$` | `$` |

`=` denotes a combinational assignment; `:=` denotes a registered assignment.
An output's `.oe` equation controls its output enable. `gnd` and `vcc` represent
constant false and true. Output files use LF line endings on Unix and CRLF on Windows.

## Pin names

Most JEDEC files carry no pin names, so the output uses names such as `i2`
and `o19`. Name the pins in a text file and pass it with `--pins`:

```text
# U5 address decoder, GAL16V8
CLK:1  A15:2  A14:3  A13:4
/AS:5                  # active low
/ROM_CS:19  /IO_CS:18
```

```sh
./jed2pld --pins u5.pins u5.jed
./jed2pld --pins u5.pins --format=opal u5.jed
```

The file uses the syntax of `NOTE PINS` records:

- Each entry is `name:pin`, and entries are separated by spaces, tabs or line
  breaks. The pin number follows the last colon.
- A leading `/` marks an active-low signal. The pin is named without the
  slash, and equations are written for the complement, so `/AS` reads as
  asserted when the pin is low.
- Buried nodes are named by their node numbers, as in `NOTE NODES`: 25 to 42
  for the GAL6001 (29 to 46 for the GAL6001V), and above the package pins for
  MAPL devices.
- `#` at the start of an entry begins a comment that runs to the end of the
  line.
- Names of up to 79 characters are accepted. Names for the CUPL output must
  also be valid CUPL names; others are written as `pinN` or `nodeN` there.

Names from the file replace the labels of the JEDEC file pin by pin; pins the
file does not mention keep theirs. An entry that is not `name:pin`, a pin the
device does not have, or a pin named twice is an error that gives the file
name and line, and no output is written. The same checks as for `NOTE PINS`
apply to the names themselves, such as `E1803` for one name on two pins.

## WinCUPL output

By default the design is written as a WinCUPL `.pld` source file. The file declares each pin and buried node in use and gives one assignment per
macrocell input, using CUPL extensions such as `.d`, `.ck`, `.ce`, `.ar`,
`.ap`, `.sp`, and `.oe`. It is meant to compile back to the same fuse map:

- Device names are those of WinCUPL's `cupl.dl` library, for example
  `g16v8ma`, `g22v10`, `p20x8`, or `g6001lcc`. Compile against that library;
  Atmel's library, the default in some installations, lacks the older parts.
- Compile with `-m0` to keep the terms as written. With minimization CUPL may
  merge or drop terms, which gives an equivalent but different fuse map.
- Labels that are not valid CUPL names, such as `x.y`, `1abc`, or the keywords
  `d` and `global`, become `pinN` or `nodeN`, with the original in a comment.
- Product terms that contain a signal and its complement are never true and
  are left out. A sum without terms is written as `'b'0`.
- Wiring the device fixes, such as the `/OE` pin of a PAL registered output, is
  left to CUPL. Shared reset and preset terms are written for every register.
- A user signature of printable text becomes the `PartNo` field, which CUPL
  stores in the signature. Any other signature is shown in a comment.
- In GAL16V8 and GAL20V8 simple mode, CUPL turns every output that the logic
  reads into an input. jed2pld writes such designs with a comment and a
  warning, but they do not compile to the original fuse map.

MAPL devices and the PALs that `cupl.dl` does not include (PAL16C4, PAL16P4,
PAL16PE8, PAL16RD8, PAL16RM4, PAL12H10, PAL14H8, PAL16H6, PAL18H4, PAL20H10,
PAL20H2, PAL20H8, and PAL20P10) cannot be written as CUPL; jed2pld reports
an error for them. Use `--format=opal` for these devices.

## Supported devices

The converter supports 80 device layouts across these families:

| Family | Devices and variants |
| --- | --- |
| PAL | H/L/P, R/RP, RA, RD/RM, C1/C4, PE8, X4/X8/X10, and EC16P8 |
| GAL16V8 | GAL16V8, GAL16V8A, GAL16V8QS |
| GAL20V8 | GAL20V8, GAL20V8A, GAL20V8QS, GAL20V8V, GAL20V8AV, GAL20V8QSV |
| 20RA10 | PAL20RA10, GAL20RA10 |
| 22V10 | PAL22V10, GAL22V10, GAL22V10V, GAL22CV10, GAL22CV10V |
| GAL6001 | GAL6001, GAL6001V |
| MAPL | MAPL128, MAPL144 |

Device names are case-insensitive. `G22V10` is an alias for `GAL22V10`.
Choose the correct package variant, since pin assignments differ. PAL22V10
and GAL22V10 are separate device selections.

MAPL244 and MAPL268 are not supported. Device definitions are compiled into
the program; external device libraries are not loaded.

### A4091 logic

Select `G22V10` explicitly for the A4091 JEDEC files:

```sh
./jed2pld -dG22V10 -o u202.pld \
  ~/git/A4091/a4091-logic/jedec/u202.jed
```

## Tests

Run `make test` for self-contained parser, equation output, and file handling
regression tests. These require Python 3.10 or later and generate their own
input files. File size limit tests run on Unix; they are skipped on platforms
without that facility.

Equation tests cover all 80 supported layouts with fixed fuse-map probes and
Boolean truth tables. Additional cases exercise registers, programmable
polarity, feedback, output enables, reset/preset controls, and MAPL pages.
See [tests/README.md](tests/README.md) for the test structure and scope.

Run `make fuzz-sanitize` for longer randomized checks across the device layouts.
This builds a temporary executable with AddressSanitizer and UBSan, runs the
regression suite against it, then tests random fuse maps. It requires a compiler
that supports those sanitizers; select one with `make fuzz-sanitize CC=clang`.

`tests/cupl_roundtrip.py` checks the WinCUPL output against a CUPL compiler,
such as LinCUPL's. It writes random fuse maps as `.pld` files, compiles them
with `-m0`, and compares the equations of the original and compiled fuse maps:

```sh
CUPL=/path/to/cupl LIBCUPL=/path/to/cupl.dl python3 tests/cupl_roundtrip.py --count 1000
```

GitHub Actions builds and tests Linux (GCC and Clang), macOS (Clang), and Windows
(MinGW GCC). A separate Linux job runs the sanitizer suite. Successful build
jobs provide downloadable binaries; Unix archives preserve executable permissions.

## Troubleshooting

- **Device name not found:** supply `-d` with the appropriate device name.
- **Incorrect QF or QP value:** check that the selected device and package match
  the fuse map. These fields specify fuse and pin counts.
- **Illegal macrocell configuration:** check the device selection and whether
  the JEDEC file is complete.
- **Label warnings or duplicate labels:** inspect the `NOTE PINS` and
  `NOTE NODES` records for invalid pin numbers or conflicting names.

