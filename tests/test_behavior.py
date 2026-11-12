#!/usr/bin/env python3
"""Standalone checks for supported input formats and equation output."""
from pathlib import Path
import datetime
import re
import subprocess
import tempfile
from support import EXE


def run(work, name, body, *args, device='PAL16L8'):
    (work / (name + '.jed')).write_bytes(('\2' + device + '*' + body + '\3').encode())
    result = subprocess.run([str(EXE), '-s', '--format=opal', *args, name + '.jed'],
                            cwd=work, capture_output=True, text=True)
    eqn = work / (name + '.eqn')
    return result, eqn.read_text() if eqn.exists() else None


def main():
    failures = []

    def check(label, condition):
        print(('ok   ' if condition else 'FAIL ') + label)
        if not condition:
            failures.append(label)

    with tempfile.TemporaryDirectory(prefix='behavior-') as directory:
        work = Path(directory)

        _, eqn = run(work, 'year', 'QF2048*F0*L0 0*')
        check('header date has a four-digit year',
              f'-{datetime.date.today().year}\n' in eqn)

        result, _ = run(work, 'label', 'QF2048*F0*NOTE PINS bad *L0 0*')
        check('label without a pin number warns once',
              result.stderr.count('W0401') == 1)

        # All-ones fuses with one OR fuse cleared give a constant-high reset term.
        result, eqn = run(work, 'mapl', 'QF14833*F1*L58 0*', device='MAPL128')
        check('constant-high MAPL reset term is written as vcc',
              '.re = vcc\n' in eqn and '.re = \n' not in eqn)

        # Empty labels and negative pin numbers are invalid.
        for name, label in (('empty', ':2'), ('negative', 'a:-2')):
            result, eqn = run(work, name, 'QF2048*F0*NOTE PINS ' + label + ' *L0 0*')
            check(f'NOTE PINS {label} is a W0401 warning',
                  'W0401' in result.stderr and 'W0402' in result.stderr)

        # CUPL names the part on a "Device" line.
        result, eqn = run(work, 'cupl', 'QF2048*F0*L0 0*',
                          device='CUPL(WM) 5.0a\r\nDevice          p16l8  Library DLIB')
        check('CUPL "Device p16l8" header selects the device',
              result.returncode == 0 and 'PAL16L8' in (eqn or ''))
        result, _ = run(work, 'cuplbad', 'QF2048*F0*L0 0*', device='CUPL\r\nDevice zzz9')
        check('unknown CUPL device is E1804', 'E1804' in result.stderr)

        # PALCE16V8 uses the GAL16V8 layout.
        result, eqn = run(work, 'palce', 'QF2194*QP20*F1*L0 1*', device='PALCE16V8')
        check('PALCE16V8 is decoded as GAL16V8',
              result.returncode == 0 and 'GAL16V8' in (eqn or ''))

        result, eqn = run(work, 'input', 'QF2048*F0*L0 0*', '-o', 'input.jed')
        check('refuses to overwrite the JEDEC input',
              result.returncode == 2 and eqn is None and
              (work / 'input.jed').read_text().startswith('\2PAL16L8'))

        # WinCUPL ends the GAL20RA10 map after the signature; the table has
        # 16 more fuses. Both counts are accepted, the signature read alike.
        sig = 'L3210 0100000101000010*'
        for count in (3274, 3290):
            result, eqn = run(work, f'ra{count}', f'QF{count}*F0*{sig}', device='GAL20RA10')
            check(f'GAL20RA10 accepts QF{count}',
                  result.returncode == 0 and '@ues 4142000000000000' in (eqn or ''))
        result, _ = run(work, 'ra3273', 'QF3273*F0*L0 0*', device='GAL20RA10')
        check('GAL20RA10 rejects other QF values', 'E0404' in result.stderr)

        # GAL6001 macrocells without terms: DOS reports E0100 for an enabled
        # output and for an unread buried macrocell with only a clock term.
        result, eqn = run(work, 'g6ones', 'QF8294*F1*L0 1*', device='GAL6001')
        check('GAL6001 enabled output without terms is written as gnd',
              result.returncode == 0 and 'o14 = gnd\no14.oe = vcc\n' in (eqn or ''))
        result, eqn = run(work, 'g6clk', 'QF8294*F1*L99 0*L7296 ' + '0' * 780 + '*', device='GAL6001')
        check('GAL6001 buried macrocell with only a clock term is written',
              result.returncode == 0 and 'b25 = gnd\n' in (eqn or ''))

        # Long options, with the value attached or as the next word.
        result, _ = run(work, 'long', 'QF2048*F0*L0 0*', '--device=PAL16L8', '--output', 'long_out')
        out = work / 'long_out.eqn'
        check('--device= and --output take values both ways',
              result.returncode == 0 and out.exists() and 'PAL16L8' in out.read_text())
        result, _ = run(work, 'longbad', 'QF2048*F0*L0 0*', '--bogus')
        check('unknown long option is an error', result.returncode != 0 and '--bogus' in result.stderr)
        for option in ('--help', '--version'):
            result = subprocess.run([str(EXE), option], capture_output=True, text=True)
            check(f'{option} exits 0', result.returncode == 0 and 'JED2PLD' in result.stdout and (option != '--help' or '--format=cupl|opal' in result.stdout))

        # --pins names pins from a file, over the labels of the JEDEC file.
        (work / 'names.txt').write_text('# U5 address decoder\nclk:2  /rst:3 # reset\n\nsel:19\n')
        jed = 'QF2048*F1*L32 01011111111111111111111111111111*NOTE PINS a:2 b:4*'
        result, eqn = run(work, 'pinfile', jed, '--pins', 'names.txt')
        names = set(re.findall(r'(\S+=\d+)', eqn or ''))
        check('--pins names pins and replaces NOTE PINS labels',
              result.returncode == 0 and {'clk=2', '/rst=3', 'b=4', 'sel=19'} <= names and 'a=2' not in names
              and '/sel = clk * i1' in eqn)
        result, _ = run(work, 'pinfile', jed, '--pins=names.txt', '--format=cupl')
        pld = (work / 'pinfile.pld').read_text() if (work / 'pinfile.pld').exists() else ''
        check('--pins names pins in CUPL output',
              result.returncode == 0 and 'PIN 2 = clk;' in pld and 'PIN 19 = sel;' in pld)
        for name, text, message in (('noname', 'a:2\n:3\n', 'names.txt:2: Expected name:pin'),
                                    ('nopin', 'a:2 b\n', "Expected name:pin, not 'b'"),
                                    ('range', 'a:21\n', 'has no pin 21'),
                                    ('twice', 'a:2\nb:2\n', 'names.txt:2: Pin 2 is named twice')):
            (work / 'names.txt').write_text(text)
            result, eqn = run(work, 'pinbad_' + name, 'QF2048*F0*L0 0*', '--pins', 'names.txt')
            check(f'--pins rejects {text!r}',
                  result.returncode == 2 and message in result.stderr and eqn is None)
        result, _ = run(work, 'pinmissing', 'QF2048*F0*L0 0*', '--pins', 'missing.txt')
        check('--pins with a missing file is E0001', result.returncode == 2 and 'E0001' in result.stderr)
        (work / 'names.txt').write_text('clk:2\n')
        result, _ = run(work, 'pinover', 'QF2048*F0*L0 0*', '--pins', 'names.txt', '-o', 'names.txt')
        check('--pins file is never overwritten',
              result.returncode == 2 and (work / 'names.txt').read_text() == 'clk:2\n')

        # WinCUPL source output, the default.
        (work / 'default.jed').write_bytes(b'\2PAL16L8*QF2048*F0*L0 0*\3')
        result = subprocess.run([str(EXE), '-s', 'default.jed'], cwd=work, capture_output=True, text=True)
        pld = work / 'default.pld'
        check('without --format a .pld is written',
              result.returncode == 0 and not (work / 'default.eqn').exists() and pld.exists()
              and 'Device   p16l8;' in pld.read_text())
        result, eqn = run(work, 'cuplout', 'QF2048*F0*L0 0*', '--format=cupl')
        pld = work / 'cuplout.pld'
        text = pld.read_text() if pld.exists() else ''
        check('--format=cupl writes a .pld with the CUPL device',
              result.returncode == 0 and eqn is None and 'Device   p16l8;' in text and 'Name     cuplout;' in text)
        result, eqn = run(work, 'badformat', 'QF2048*F0*L0 0*', '--format=abel')
        check('unknown output format is an error', result.returncode == 2 and 'abel' in result.stderr)
        result, _ = run(work, 'maplcupl', 'QF14833*F1*L58 0*', '--format=cupl', device='MAPL128')
        check('device without a WinCUPL equivalent is an error',
              result.returncode == 2 and 'no WinCUPL device' in result.stderr and
              not (work / 'maplcupl.pld').exists())

    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
