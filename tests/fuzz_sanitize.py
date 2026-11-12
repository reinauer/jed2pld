#!/usr/bin/env python3
"""Crash fuzz: build with AddressSanitizer/UBSan and feed every device random maps.

Any sanitizer report or exit status other than
0 (equations written) or 2 (diagnostic) is a failure.

Usage: fuzz_sanitize.py [seed]
"""
from pathlib import Path
import os
import random
import shlex
import subprocess
import sys
import tempfile
from devices import DEVICES
from support import ROOT

DENSITIES = (0.0, 0.02, 0.5, 0.98, 1.0)


def main():
    seed = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    rng = random.Random(seed)
    sources = [str(path) for path in sorted(ROOT.glob('*.c'))]
    runs = bad = 0
    with tempfile.TemporaryDirectory(prefix='fuzz-sanitize-') as directory:
        work = Path(directory)
        binary = work / 'jed2pld-asan'
        subprocess.run([*shlex.split(os.environ.get('CC', 'cc')), '-std=c11', '-g', '-O1',
                        '-DJED2PLD_VERSION="fuzz"', '-fsanitize=address,undefined',
                        '-fno-sanitize-recover=undefined', '-o', str(binary), *sources],
                       check=True)
        # Exercise valid circuits and I/O failures under the same instrumented
        # binary before fuzzing, where invalid configurations are also allowed.
        environment = dict(os.environ, JED2PLD=str(binary))
        for script in ('test_regressions.py', 'test_behavior.py', 'test_equations.py'):
            subprocess.run([sys.executable, str(ROOT / 'tests' / script)],
                           env=environment, check=True)
        for device in DEVICES:
            name, fuses, pins = device.name, device.fuses, device.pins
            for density in DENSITIES:
                for trial in range(6):
                    bits = ''.join('1' if rng.random() < density else '0' for _ in range(fuses))
                    lines = ['\x02%s*' % name, 'QF%d*' % fuses, 'QP%s*' % pins, 'F0*']
                    lines += ['L%05d %s*' % (i, bits[i:i + 32]) for i in range(0, fuses, 32)]
                    if trial % 2:
                        lines.append('NOTE PINS ' + ' '.join(f'a{k}:{k}' for k in range(1, int(pins) + 1)) + '*')
                    (work / 't.jed').write_text('\n'.join(lines) + '\n\x03')
                    for flags in ([], ['--format=opal'], ['--format=opal', '-a']):
                        result = subprocess.run([str(binary), '-s', '-d', name, *flags, 't.jed'],
                                                cwd=work, capture_output=True, text=True,
                                                timeout=60)
                        runs += 1
                        if (result.returncode not in (0, 2) or 'Sanitizer' in result.stderr or
                                'runtime error' in result.stderr):
                            bad += 1
                            print('FAIL', name, density, flags, result.returncode)
                            print(result.stderr[:600])
    print(f'{runs} runs; {bad} failures')
    return int(bool(bad))


if __name__ == '__main__':
    raise SystemExit(main())
