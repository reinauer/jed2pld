"""Fixed fuse-map probes for all supported layouts.

Each probe describes one macrocell and two independent input pins. Addresses
are recorded here rather than read from the decoder at test time. The tests
program simple functions at those addresses and check their truth tables.
"""

from dataclasses import dataclass


@dataclass(frozen=True)
class Device:
    name: str
    fuses: int
    pins: int
    width: int
    address: int
    output: int
    x: int
    x_fuse: int
    y: int
    y_fuse: int
    negative: bool = False
    kind: str = "C"
    enable_pin: int = 0


# PAL macrocell kinds: C = combinational, T = product-term enable,
# R = registered, A = asynchronous controls, X = registered XOR.
# name         QF   QP  width data out  X Xbit  Y Ybit polarity kind OE
PAL_PROBES = """
GAL20RA10     3290 24 40 160 23  2  0  3  4 - A  0
EC16P8        2056 24 32   0 21  1  0 23  2 + C  0
PAL16C4       2056 28 32 512 25  1  0 27  2 - C  0
PAL16P4       2056 24 32 512 20  1  0 23  2 + C  0
PAL16PE8      2056 28 32   0 25  1  0 27  2 + C  0
PAL16RD8      2056 24 32   0 21 23  2  2  4 + R 13
PAL16RM4      2056 24 32 512 20 23  2  2  4 + R  0
PAL10H8        320 20 20   0 19  2  0  1  2 + C  0
PAL10L8        320 20 20   0 19  2  0  1  2 - C  0
PAL10P8        328 20 20   0 19  2  0  1  2 - C  0
PAL12H10       480 24 24   0 23  2  0  1  2 + C  0
PAL12H6        384 20 24   0 18  2  0  1  2 + C  0
PAL12L10       480 24 24   0 23  2  0  1  2 - C  0
PAL12L6        384 20 24   0 18  2  0  1  2 - C  0
PAL12P10       490 24 24   0 23  2  0  1  2 - C  0
PAL12P6        390 20 24   0 18  2  0  1  2 - C  0
PAL14H4        448 20 28   0 17  2  0  1  2 + C  0
PAL14H8        560 24 28   0 22  2  0  1  2 + C  0
PAL14L4        448 20 28   0 17  2  0  1  2 - C  0
PAL14L8        560 24 28   0 22  2  0  1  2 - C  0
PAL14P4        452 20 28   0 17  2  0  1  2 - C  0
PAL14P8        568 24 28   0 22  2  0  1  2 - C  0
PAL16C1        512 20 32   0 16  2  0  1  2 + C  0
PAL16H2        512 20 32   0 16  2  0  1  2 + C  0
PAL16H6        640 24 32   0 21  2  0  1  2 + C  0
PAL16H8       2048 20 32  32 19  2  0  1  2 + T  0
PAL16L2        512 20 32   0 16  2  0  1  2 - C  0
PAL16L6        640 24 32   0 21  2  0  1  2 - C  0
PAL16L8       2048 20 32  32 19  2  0  1  2 - T  0
PAL16P2        514 20 32   0 16  2  0  1  2 - C  0
PAL16P6        646 24 32   0 21  2  0  1  2 - C  0
PAL16P8       2056 20 32  32 19  2  0  1  2 - T  0
PAL16R4       2048 20 32  32 19  2  0  3  4 - T  0
PAL16R6       2048 20 32  32 19  2  0  3  4 - T  0
PAL16R8       2048 20 32   0 19  2  0  3  4 - R 11
PAL16RA8      2056 20 32 128 19  2  0  3  4 - A  0
PAL16RP4      2056 20 32  32 19  2  0  3  4 - T  0
PAL16RP6      2056 20 32  32 19  2  0  3  4 - T  0
PAL16RP8      2056 20 32   0 19  2  0  3  4 - R 11
PAL18H4        720 24 36   0 20  2  0  1  2 + C  0
PAL18L4        720 24 36   0 20  2  0  1  2 - C  0
PAL18P4        724 24 36   0 20  2  0  1  2 - C  0
PAL20C1        640 24 40   0 19  2  0  1  2 + C  0
PAL20H10      1600 24 40  40 23  2  0  1  2 + T  0
PAL20H2        640 24 40   0 19  2  0  1  2 + C  0
PAL20H8       2560 24 40  40 22  2  0  1  2 + T  0
PAL20L10      1600 24 40  40 23  2  0  1  2 - T  0
PAL20L2        640 24 40   0 19  2  0  1  2 - C  0
PAL20L8       2560 24 40  40 22  2  0  1  2 - T  0
PAL20P10      1610 24 40  40 23  2  0  1  2 - T  0
PAL20P2        642 24 40   0 19  2  0  1  2 - C  0
PAL20P8       2568 24 40  40 22  2  0  1  2 - T  0
PAL20R4       2560 24 40  40 22  2  0 23  2 - T  0
PAL20R6       2560 24 40  40 22  2  0 23  2 - T  0
PAL20R8       2560 24 40   0 22  2  0 23  2 - R 13
PAL20RA10     3210 24 40 160 23  2  0  3  4 - A  0
PAL20RP4      2568 24 40  40 22  2  0 23  2 - T  0
PAL20RP6      2568 24 40  40 22  2  0 23  2 - T  0
PAL20RP8      2568 24 40   0 22  2  0 23  2 - R 13
PAL20X10      1600 24 40   0 23  2  0  3  4 - X 13
PAL20X4       1600 24 40  40 23  2  0  3  4 - T  0
PAL20X8       1600 24 40  40 23  2  0  3  4 - T  0
"""

DEVICES = []
for row in PAL_PROBES.splitlines():
    if row.strip():
        name, *values, polarity, kind, enable = row.split()
        DEVICES.append(Device(name, *map(int, values), polarity == "-", kind, int(enable)))

# V8 probes use small combinational mode, with XOR=1 and AC1=0.
for name in ("GAL16V8", "GAL16V8A", "GAL16V8QS"):
    DEVICES.append(Device(name, 2194, 20, 32, 0, 19, 2, 0, 3, 4, kind="V8"))
for name in ("GAL20V8", "GAL20V8A", "GAL20V8QS"):
    DEVICES.append(Device(name, 2706, 24, 40, 0, 22, 2, 0, 3, 4, kind="V8"))
for name in ("GAL20V8V", "GAL20V8AV", "GAL20V8QSV"):
    DEVICES.append(Device(name, 2706, 28, 40, 0, 26, 3, 0, 4, 4, kind="V8"))

# 22V10: row 1 enables the output; rows 2..9 carry its data terms.
for name, fuses, pins, out, x, y in (
    ("PAL22V10", 5828, 24, 23, 2, 3),
    ("GAL22V10", 5892, 24, 23, 2, 3),
    ("GAL22V10V", 5892, 28, 27, 3, 4),
    ("GAL22CV10", 5828, 24, 23, 2, 3),
    ("GAL22CV10V", 5828, 28, 27, 3, 4),
):
    DEVICES.append(Device(name, fuses, pins, 44, 88, out, x, 4, y, 8, kind="V10"))

# Shared AND/OR arrays: select the first physical output and page zero.
DEVICES.extend((
    Device("GAL6001", 8294, 24, 78, 0, 14, 2, 2, 3, 4, kind="6001"),
    Device("GAL6001V", 8294, 28, 78, 0, 17, 3, 2, 4, 4, kind="6001"),
    Device("MAPL128", 14833, 28, 58, 0, 23, 6, 0, 5, 2, kind="MAPL"),
    Device("MAPL144", 14721, 44, 58, 0, 7, 6, 0, 5, 2, kind="MAPL"),
))

BY_NAME = {device.name: device for device in DEVICES}
