#!/usr/bin/env python3
"""Round trip of the CUPL writer through a CUPL compiler.

For random fuse maps of every device the writer supports: write a .pld with
--format=cupl, compile it with cupl -m0 (no minimization) against the cupl.dl
device library, decode both the original and the compiled fuse map as OPAL
equations, and compare the equations pin by pin. Terms are compared as sets,
without terms that contain a signal and its complement, since those are
never true and CUPL may drop them. Signatures are compared too when the
writer could express them as PartNo text.

Needs a CUPL compiler, such as lincupl's: set CUPL to the compiler and
LIBCUPL to cupl.dl.

Usage: cupl_roundtrip.py [--count N] [--devices REGEX] [seed]
"""
import argparse
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from devices import DEVICES
from support import EXE

LABELS = ['a', 'b', 'CLK', 'OE', '/x', 'q', 'r', 'dup', 'dup', 'x.y', 'a-b', '1abc', 'd', 'global',
          'pin', 'Name', 'L' + 'x' * 40, 'i2', 'o19', 'pb0']
DENSITIES = [0.0, 0.01, 0.05, 0.2, 0.5, 0.9, 0.98, 1.0]
UNSUPPORTED = 'has no WinCUPL device'


def make_jedec(rng, name, fuses, pins, labels):
    if rng.random() < 0.5:
        density = rng.choice(DENSITIES)
        bits = [1 if rng.random() < density else 0 for _ in range(fuses)]
    else:
        # Mostly unused terms with a few programmed fuses, and random
        # configuration bits, so outputs get small, readable equations.
        bits = [0] * fuses
        for _ in range(rng.randint(0, 60)):
            start = rng.randrange(fuses)
            for k in range(rng.randint(1, 80)):
                if start + k < fuses:
                    bits[start + k] = 1 if rng.random() < 0.8 else 0
        for a in range(max(0, fuses - rng.randint(50, 400)), fuses):
            bits[a] = rng.randint(0, 1)
    text = ''.join(map(str, bits))
    rows = ''.join('L%d %s*\r\n' % (a, text[a:a + 32]) for a in range(0, fuses, 32))
    note = ''
    if labels and rng.random() < 0.6:
        chosen = rng.sample(range(1, pins + 1), rng.randint(1, max(1, pins // 2)))
        note = 'NOTE PINS ' + ' '.join(f'{rng.choice(LABELS)}:{p}' for p in chosen) + '*\r\n'
    return ('\x02%s*\r\nQF%d*\r\nQP%d*\r\nF0*\r\n%s%s\x030000' % (name, fuses, pins, rows, note)).encode()


# GAL6001 output macrocells without an output enable term are nodes 33-42
# (37-46 in the PLCC package); one that is never enabled is the same thing.
DIP_TO_PLCC = [0, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14, 16, 17, 18, 19, 20, 21, 23, 24, 25, 26, 27, 28]
G6001_NODES = {'GAL6001': {p: p + 19 for p in range(14, 24)},
               'GAL6001V': {DIP_TO_PLCC[p]: p + 23 for p in range(14, 24)}}


def equations(path, device):
    """The equations of an OPAL file as {(pin, target): (negated, terms, xor terms)}."""
    lines = path.read_text().splitlines()
    pins, inverted, body, start = {}, {}, [], None
    for i, line in enumerate(lines):
        if line.startswith('chip '):
            start = i + 1
        elif line == 'equations':
            body = lines[i + 1:]
            break
        elif start is not None:
            for name, pin in re.findall(r'(\S+)=(\d+)', line):
                inverted[int(pin)] = name.startswith('/')
                pins[name.lstrip('/')] = int(pin)

    def literal(text):
        neg = text.startswith('/')
        name = text.lstrip('/')
        return (pins[name], neg ^ inverted[pins[name]])

    def term(text):
        if text == 'vcc':
            return frozenset()
        lits = frozenset(literal(t.strip()) for t in text.split('*'))
        return None if any((p, not n) in lits for p, n in lits) else lits

    # Join wrapped terms: a continuation line starts with spaces and an AND.
    joined = []
    for line in body:
        if re.match(r'\s+[*&] ', line) and joined:
            joined[-1] += ' ' + line.strip()
        elif line.strip():
            joined.append(line)
    result, current, xor = {}, None, False
    for line in joined:
        m = re.match(r'(/?)(\S+?)(\.\w+)? (:?=) (.*)$', line)
        if m and not line.startswith(' '):
            neg, name, ext, op, rhs = m.groups()
            pin = pins.get(name, name)
            target = (ext or '') + (':=' if op == ':=' else '')
            negated = bool(neg) ^ (inverted.get(pin, False) if not ext else False)
            current = result[(pin, target)] = [negated, set(), set()]
            xor = False
        elif line.startswith('  :+: '):
            xor, rhs = True, line[6:]
        else:
            rhs = line.strip()[2:]
        if rhs.strip() == 'gnd':
            continue
        t = term(rhs.strip().replace('&', '*'))
        if t is not None:
            current[2 if xor else 1].add(t)
    # A reset, preset or clock term that is never true is the same as none.
    # An output that is never enabled is left out: the listing does not show
    # its logic consistently, and CUPL may drop it.
    for k in list(result):
        if k[1] not in ('', ':=', '.oe') and not result[k][1]:
            del result[k]
    nodes = G6001_NODES.get(device, {})
    for pin in {k[0] for k in result if k[1] == '.oe' and not result[k][1]}:
        for k in [k for k in result if k[0] == pin]:
            v = result.pop(k)
            if pin in nodes and k[1] != '.oe':
                result[(nodes[pin], k[1])] = v
    # A sum with a constant true term is true. Shared reset and preset terms
    # matter only when there are registers.
    for v in result.values():
        for i in (1, 2):
            if frozenset() in v[i]:
                v[i] = {frozenset()}
    # A node or disabled output with only empty sums does nothing, and the
    # listing may leave out an output whose terms are never true.
    for pin in {k[0] for k in result}:
        keys = [k for k in result if k[0] == pin]
        if all(k[1] == '.oe' or not result[k][1] and not result[k][2] for k in keys):
            for k in keys:
                del result[k]
    if not any(k[1] == ':=' for k in result):
        for k in [k for k in result if k[0] == 'global']:
            del result[k]
    ues = re.search(r'^@ues (\w+)', path.read_text(), re.M)
    return {k: (v[0], frozenset(v[1]), frozenset(v[2])) for k, v in result.items()}, ues and ues.group(1)


def run(cmd, cwd, env=None):
    return subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True, timeout=60)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--count', type=int, default=500)
    parser.add_argument('--devices', default='')
    parser.add_argument('--keep', action='store_true', help='keep the work directory')
    parser.add_argument('seed', type=int, nargs='?', default=1)
    args = parser.parse_args()
    cupl = os.environ.get('CUPL')
    if not cupl or not os.environ.get('LIBCUPL'):
        print('set CUPL to a CUPL compiler and LIBCUPL to cupl.dl')
        return 2
    rng = random.Random(args.seed)
    known = [d for d in DEVICES if re.search(args.devices, d.name)]
    work = Path(tempfile.mkdtemp(prefix='cupl-roundtrip-'))
    counts = {'match': 0, 'unsupported': 0, 'rejected': 0, 'unrepresentable': 0, 'mismatch': 0, 'failed': 0}
    for i in range(args.count):
        device = rng.choice(known)
        case = work / f'c{i}'
        case.mkdir()
        (case / 'a.jed').write_bytes(make_jedec(rng, device.name, device.fuses, device.pins, True))
        p = run([str(EXE), '-s', '--format=cupl', 'a.jed'], case)
        if p.returncode:
            if UNSUPPORTED in p.stderr:
                counts['unsupported'] += 1
            elif re.search(r'^E\d{4}: ', p.stderr, re.M) and p.returncode in (1, 2):
                counts['rejected'] += 1
            else:
                counts['failed'] += 1
                print(f'FAILED {device.name} case {case}: exit {p.returncode}\n{p.stderr[-2000:]}')
                continue
            shutil.rmtree(case)
            continue
        pld = (case / 'a.pld').read_text()
        if 'cannot read back an output' in pld:
            counts['unrepresentable'] += 1
            shutil.rmtree(case)
            continue
        built = case / 'cupl'
        built.mkdir()
        shutil.copy(case / 'a.pld', built / 'a.pld')
        p = run([cupl, '-j', '-m0', 'a.pld'], built)
        if p.returncode or not (built / 'a.jed').exists():
            counts['failed'] += 1
            print(f'COMPILE {device.name} case {case}:\n' + '\n'.join(
                line for line in p.stdout.splitlines() if line.startswith('[') or ':' in line[:6]))
            continue
        # Decode both without labels, so default names stand for the same pins.
        text = (case / 'a.jed').read_bytes()
        (case / 'plain.jed').write_bytes(re.sub(rb'NOTE PINS[^*]*\*\r\n', b'', text))
        run([str(EXE), '-s', '--format=opal', '-d', device.name, 'plain.jed'], case)
        # CUPL writes no L records when every fuse keeps its default.
        compiled = (built / 'a.jed').read_bytes()
        if not re.search(rb'\*\s*L\d', compiled):
            compiled = compiled.replace(b'*F0', b'*F0\r\n*L0 0', 1)
        (built / 'a.jed').write_bytes(compiled)
        p = run([str(EXE), '-s', '--format=opal', '-d', device.name, '-o', 'back.eqn', 'cupl/a.jed'], case)
        if p.returncode:
            counts['failed'] += 1
            print(f'UNDECODABLE {device.name} case {case}: {p.stderr.strip()}')
            continue
        original, ues = equations(case / 'plain.eqn', device.name)
        back, back_ues = equations(case / 'back.eqn', device.name)
        signature_comment = 'cannot reproduce it' in pld
        problems = [k for k in sorted(set(original) | set(back), key=str) if original.get(k) != back.get(k)]
        if ues != back_ues and not signature_comment:
            problems.append(('ues', ues, back_ues))
        if problems:
            counts['mismatch'] += 1
            if counts['mismatch'] <= 5:
                print(f'MISMATCH {device.name} case {case}:')
                for k in problems[:6]:
                    if k and k[0] == 'ues':
                        print(f'  signature {k[1]} -> {k[2]}')
                    else:
                        print(f'  {k}: {original.get(k)}\n  {" " * len(str(k))}  {back.get(k)}')
        else:
            counts['match'] += 1
            shutil.rmtree(case)
    if not args.keep and not counts['mismatch'] and not counts['failed']:
        shutil.rmtree(work)
    print(f'{args.count} cases: {counts}')
    return 1 if counts['mismatch'] or counts['failed'] else 0


if __name__ == '__main__':
    sys.path.insert(0, str(Path(__file__).parent))
    raise SystemExit(main())
