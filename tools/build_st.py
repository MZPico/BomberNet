#!/usr/bin/env python3
"""The Atari ST build (m68k-atari-mint-gcc, MiNTLib; Vincent Riviere's
cross tools): build/st/BOMBER.PRG, a TOS program.

  tools/build_st.py [--test recording.bnr [--frames n]] [-O2|-Os] [--out dir]

--test: ST_TEST, the recording compiled in and replayed from the title on;
each game frame's work is timed (tools/st_test.py runs it in Hatari and
reads the figures it writes to C:\\RESULT.TXT). CFLAGS_ST: more flags
(-DTEST_HASH_OFF: a standalone game)."""
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
out = os.path.abspath(sys.argv[sys.argv.index('--out') + 1]) if '--out' in sys.argv else os.path.join(ROOT, 'build', 'st')
os.makedirs(out, exist_ok=True)
opts = [a for a in sys.argv[1:] if a.startswith('-O')] or ['-O2']
flags = list(opts) + ['-std=gnu99', '-m68000', '-fomit-frame-pointer', '-Wall', '-Wno-main', '-s'] + os.environ.get('CFLAGS_ST', '').split()
if '--test' in sys.argv:
    data = open(sys.argv[sys.argv.index('--test') + 1], 'rb').read()
    if '--frames' in sys.argv: data = data[:16 + 6 * int(sys.argv[sys.argv.index('--frames') + 1])]
    with open(os.path.join(out, 'rec_data.h'), 'w') as f:
        f.write(f'#define REC_LEN {len(data)}u\nstatic const uint8_t rec[REC_LEN] = {{\n')
        for i in range(0, len(data), 24): f.write('  ' + ', '.join(str(b) for b in data[i:i + 24]) + ',\n')
        f.write('};\n')
    flags += ['-DST_TEST', '-DST_RESULT="C:\\\\RESULT.TXT"']
c = os.path.join(ROOT, 'c')
src = [f'{c}/core/{n}.c' for n in ('bomber', 'netplay', 'data', 'map', 'game', 'input', 'video', 'enemy', 'bomb', 'player')]
src += [f'{c}/platform/atarist/plat_st.c', f'{c}/platform/amiga/tables.c', f'{c}/common/netdev_none.c']   # the Amiga's glyph tables
inc = [f'-I{c}/core', f'-I{c}/common', f'-I{c}/platform/atarist', f'-I{c}/platform/amiga', f'-I{out}']
exe = os.path.join(out, 'BOMBER.PRG')
r = subprocess.run(['m68k-atari-mint-gcc', *flags, *inc, *src, '-o', exe], capture_output=True, text=True)
if r.returncode: sys.exit(r.stderr[-4000:])
if r.stderr.strip(): print(r.stderr.strip()[-2000:])
print(f'{exe}: {os.path.getsize(exe)} bytes ({" ".join(flags)})')
