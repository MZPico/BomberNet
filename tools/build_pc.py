#!/usr/bin/env python3
"""The IBM PC build (OpenWatcom v2, 16-bit DOS, small model): build/pc/BOMBER.EXE.

  tools/build_pc.py [--test recording.bnr [--frames n] | --play] [--out dir]

OpenWatcom from $WATCOM, else ~/src/openwatcom (the Linux snapshot from
github.com/open-watcom/open-watcom-v2, unpacked). --test: PC_TEST, the
recording compiled in and replayed from the title on; each game frame's
work is timed (tools/pc_test.py runs it in DOSBox-X and reads the figures
it writes to C:\\RESULT.TXT). CFLAGS_PC: more flags."""
import os, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
WATCOM = os.environ.get('WATCOM') or os.path.expanduser('~/src/openwatcom')
if not os.path.exists(os.path.join(WATCOM, 'binl64', 'wcc')): sys.exit(f'no OpenWatcom in {WATCOM}')
env = dict(os.environ, WATCOM=WATCOM, INCLUDE=os.path.join(WATCOM, 'h'), PATH=os.path.join(WATCOM, 'binl64') + ':' + os.environ['PATH'])
out = os.path.abspath(sys.argv[sys.argv.index('--out') + 1]) if '--out' in sys.argv else os.path.join(ROOT, 'build', 'pc')
obj = os.path.join(out, 'obj')
shutil.rmtree(obj, ignore_errors=True)
os.makedirs(obj)
flags = ['-zq', '-bt=dos', '-ms', '-0', '-ox', '-w4', '-wcd=202'] + os.environ.get('CFLAGS_PC', '').split()   # -0: 8086 code; W202: unused symbols
if '--test' in sys.argv:
    data = open(sys.argv[sys.argv.index('--test') + 1], 'rb').read()
    if '--frames' in sys.argv: data = data[:16 + 6 * int(sys.argv[sys.argv.index('--frames') + 1])]
    with open(os.path.join(out, 'rec_data.h'), 'w') as f:            # far: the longest recording is 18 KB
        f.write(f'#define REC_LEN {len(data)}u\nstatic const uint8_t __far rec[REC_LEN] = {{\n')
        for i in range(0, len(data), 24): f.write('  ' + ', '.join(str(b) for b in data[i:i + 24]) + ',\n')
        f.write('};\n')
    flags += ['-dPC_TEST', '-dPC_RESULT="C:\\\\RESULT.TXT"']
if '--play' in sys.argv: flags.append('-dPC_PLAYTEST')   # tools/pc_play.py: a 4-player deathmatch preset, the players logged
c = os.path.join(ROOT, 'c')
src = [f'{c}/core/{n}.c' for n in ('bomber', 'netplay', 'data', 'map', 'game', 'input', 'video', 'enemy', 'bomb', 'player')]
src += [f'{c}/platform/pc/plat_pc.c', f'{c}/platform/amiga/tables.c', f'{c}/common/netdev_none.c']   # the Amiga's glyph tables
inc = [f'-i={c}/core', f'-i={c}/common', f'-i={c}/platform/pc', f'-i={c}/platform/amiga', f'-i={out}']
objs = []
for s in src:
    o = os.path.join(obj, os.path.splitext(os.path.basename(s))[0] + '.obj')
    r = subprocess.run(['wcc', *flags, *inc, f'-fo={o}', s], capture_output=True, text=True, env=env)
    if r.returncode: sys.exit(r.stdout[-3000:] + r.stderr[-1000:])
    if r.stdout.strip(): print(r.stdout.strip()[-1500:])
    objs.append(o)
exe = os.path.join(out, 'BOMBER.EXE')
lnk = os.path.join(obj, 'bomber.lnk')
open(lnk, 'w').write('system dos\noption quiet, map=' + os.path.join(out, 'bomber.map') + ', stack=8192\nname ' + exe + '\n'
                     + ''.join(f'file {o}\n' for o in objs))
r = subprocess.run(['wlink', f'@{lnk}'], capture_output=True, text=True, env=env)
if r.returncode: sys.exit(r.stdout[-3000:] + r.stderr[-1000:])
print(f'{exe}: {os.path.getsize(exe)} bytes ({" ".join(flags)})')
