#!/usr/bin/env python3
"""The Commodore 64 build (llvm-mos, c64 target): build/c64/bomber.prg.

  tools/build_c64.py [--test recording.bnr [--frames n]] [-O2|-Os]

--test: C64_TEST, the recording compiled in and replayed from the title on;
each game frame's work is timed in cycles (tools/c64_test.py reads the
figures through VICE's monitor); --frames: only its first n frames.
CFLAGS_C64: more compiler flags (-DTEST_HASH_OFF: a standalone game).
LLVM_MOS: the SDK (default ~/src/llvm-mos-dl/llvm-mos)."""
import os, re, subprocess, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
SDK = os.environ.get('LLVM_MOS', os.path.expanduser('~/src/llvm-mos-dl/llvm-mos'))
out = os.path.join(ROOT, 'build', 'c64'); os.makedirs(out, exist_ok=True)
opts = [a for a in sys.argv[1:] if a.startswith('-O')] or ['-Os']
flags = list(opts) + os.environ.get('CFLAGS_C64', '').split()
if '--test' in sys.argv:
    data = open(sys.argv[sys.argv.index('--test') + 1], 'rb').read()
    if '--frames' in sys.argv: data = data[:16 + 6 * int(sys.argv[sys.argv.index('--frames') + 1])]
    with open(os.path.join(out, 'rec_data.h'), 'w') as f:
        f.write(f'#define REC_LEN {len(data)}u\nstatic const uint8_t rec[REC_LEN] = {{\n')
        for i in range(0, len(data), 24): f.write('  ' + ', '.join(str(b) for b in data[i:i + 24]) + ',\n')
        f.write('};\n')
    flags.append('-DC64_TEST')
c = os.path.join(ROOT, 'c')
src = [f'{c}/core/{n}.c' for n in ('bomber', 'netplay', 'data', 'map', 'game', 'input', 'video', 'enemy', 'bomb', 'player')]
src += [f'{c}/platform/c64/{n}' for n in ('plat_c64.c', 'tables.c', 'c64.s')]
src += [f'{c}/cpu/6502/loops.s', f'{c}/common/netdev_none.c']
inc = [f'-I{c}/core', f'-I{c}/common', f'-I{c}/platform/c64', f'-I{out}']
prg = os.path.join(out, 'bomber.prg')
r = subprocess.run([f'{SDK}/bin/mos-c64-clang', *flags, *inc, *src, '-o', prg, '-Wl,--gc-sections'], capture_output=True, text=True)
if r.returncode: sys.exit(r.stderr[-4000:])
nm = subprocess.run([f'{SDK}/bin/llvm-nm', prg + '.elf'], capture_output=True, text=True).stdout
end = int(re.search(r'^([0-9a-f]+) \w __heap_start$', nm, re.M).group(1), 16)
print(f'{prg}: {os.path.getsize(prg)} bytes, $0801-${end:04X} ({" ".join(flags)})')
