#!/usr/bin/env python3
"""The Atari XL/XE build (llvm-mos, atari8-dos target): build/atari/bomber.xex.

  tools/build_atari.py [--test recording.bnr] [-O2|-Os]

--test: ATARI_TEST, the recording compiled in and replayed from the title
on, with its figures shown at the end (docs/port-atari-xl.md, step 3).
CFLAGS_ATARI: more compiler flags (-DTEST_HASH_OFF: a standalone game).
LLVM_MOS: the SDK (default ~/src/llvm-mos-dl/llvm-mos)."""
import os, subprocess, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
SDK = os.environ.get('LLVM_MOS', os.path.expanduser('~/src/llvm-mos-dl/llvm-mos'))
out = os.path.join(ROOT, 'build', 'atari'); os.makedirs(out, exist_ok=True)
opts = [a for a in sys.argv[1:] if a.startswith('-O')] or ['-Os']
flags = list(opts) + os.environ.get('CFLAGS_ATARI', '').split()
if '--test' in sys.argv:
    data = open(sys.argv[sys.argv.index('--test') + 1], 'rb').read()
    with open(os.path.join(out, 'rec_data.h'), 'w') as f:
        f.write(f'#define REC_LEN {len(data)}u\nstatic const uint8_t rec[REC_LEN] = {{\n')
        for i in range(0, len(data), 24): f.write('  ' + ', '.join(str(b) for b in data[i:i + 24]) + ',\n')
        f.write('};\n')
    flags.append('-DATARI_TEST')
c = os.path.join(ROOT, 'c')
src = [f'{c}/core/{n}.c' for n in ('bomber', 'netplay', 'data', 'map', 'game', 'input', 'video', 'enemy', 'bomb', 'player')]
src += [f'{c}/platform/atari/{n}' for n in ('plat_atari.c', 'tables.c', 'atari.s')]
src += [f'{c}/cpu/6502/loops.s', f'{c}/common/netdev_none.c']
inc = [f'-I{c}/core', f'-I{c}/common', f'-I{c}/platform/atari', f'-I{out}']
xex = os.path.join(out, 'bomber.xex')
r = subprocess.run([f'{SDK}/bin/mos-atari8-dos-clang', *flags, *inc, *src, '-o', xex, '-Wl,--gc-sections'],
                   capture_output=True, text=True)
if r.returncode: sys.exit(r.stderr[-4000:])
print(f'{xex}: {os.path.getsize(xex)} bytes ({" ".join(flags)})')
