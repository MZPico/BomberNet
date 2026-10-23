#!/usr/bin/env python3
"""The Atari ST build (m68k-atari-mint-gcc, MiNTLib; Vincent Riviere's
cross tools): build/st/BOMBER.PRG, a TOS program.

  tools/build_st.py [--test recording.bnr [--frames n] [--result-drive A] | --play | --net host|join] [-O2|-Os] [--out dir] [--st]

--st: also bombernet.st, a 360 KB floppy image with the program in its AUTO
folder (tools/make_st_disk.py): it starts when the ST boots from it.

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
    drive = sys.argv[sys.argv.index('--result-drive') + 1] if '--result-drive' in sys.argv else 'C'   # A: for a floppy-booted test
    flags += ['-DST_TEST', f'-DST_RESULT="{drive}:\\\\RESULT.TXT"']
if '--play' in sys.argv: flags.append('-DST_PLAYTEST')   # tools/st_play.py: a 4-player deathmatch preset, the players logged
if '--net' in sys.argv:                                   # tools/st_match.py, tools/crossplay.py: host (or join) a room on SPACE, the room's state logged
    i = sys.argv.index('--net')
    flags += [f'-DST_NETTEST={1 if sys.argv[i + 1] == "host" else 2}', f'-DST_NETTEST_PLAYERS={os.environ.get("PLAYERS", 2)}',
              f'-DST_NETTEST_LOCAL={os.environ.get("LOCAL", 1)}']
c = os.path.join(ROOT, 'c')
src = [f'{c}/core/{n}.c' for n in ('bomber', 'netplay', 'data', 'map', 'game', 'input', 'video', 'enemy', 'bomb', 'player')]
src += [f'{c}/platform/atarist/plat_st.c', f'{c}/platform/amiga/tables.c']   # the Amiga's glyph tables
if '--test' in sys.argv: src += [f'{c}/common/netdev_none.c']      # a replay needs no network
else: src += [f'{c}/platform/atarist/tcp_modem.c', f'{c}/common/netdev_soft.c', f'{c}/common/ws.c']
inc = [f'-I{c}/core', f'-I{c}/common', f'-I{c}/platform/atarist', f'-I{c}/platform/amiga', f'-I{out}']
exe = os.path.join(out, 'BOMBER.PRG')
flags.append('-Dmain=bomber_main')                 # the core's main; plat_st.c's main enters supervisor mode around it
r = subprocess.run(['m68k-atari-mint-gcc', *flags, *inc, *src, '-o', exe], capture_output=True, text=True)
if r.returncode: sys.exit(r.stderr[-4000:])
if r.stderr.strip(): print(r.stderr.strip()[-2000:])
print(f'{exe}: {os.path.getsize(exe)} bytes ({" ".join(flags)})')
if '--st' in sys.argv:
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import make_st_disk
    disk = os.path.join(out, 'bombernet.st')
    open(disk, 'wb').write(make_st_disk.make({'AUTO\\BOMBER.PRG': open(exe, 'rb').read()}))
    print(f'{disk}: {os.path.getsize(disk)} bytes, AUTO\\BOMBER.PRG')
