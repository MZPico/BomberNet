#!/usr/bin/env python3
"""The Amiga build (bebbo's gcc in Docker, tools/amiga-cc): build/amiga/bomber,
an AmigaOS executable.

  tools/build_amiga.py [--test recording.bnr [--frames n] | --play | --net host|join] [-O2|-Os] [--out dir] [--adf]

--test: AMIGA_TEST, the recording compiled in and replayed from the title on;
each game frame's work is timed (tools/amiga_test.py runs it in FS-UAE and
reads the figures it writes to DH0:result.txt). CFLAGS_AMIGA: more flags
(-DTEST_HASH_OFF: a standalone game)."""
import os, subprocess, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
out = sys.argv[sys.argv.index('--out') + 1] if '--out' in sys.argv else os.path.join(ROOT, 'build', 'amiga')
os.makedirs(out, exist_ok=True)
opts = [a for a in sys.argv[1:] if a.startswith('-O')] or ['-O2']
flags = list(opts) + ['-noixemul', '-fomit-frame-pointer', '-m68000', '-Wall', '-Wno-main'] + os.environ.get('CFLAGS_AMIGA', '').split()
if '--test' in sys.argv:
    data = open(sys.argv[sys.argv.index('--test') + 1], 'rb').read()
    if '--frames' in sys.argv: data = data[:16 + 6 * int(sys.argv[sys.argv.index('--frames') + 1])]
    with open(os.path.join(out, 'rec_data.h'), 'w') as f:
        f.write(f'#define REC_LEN {len(data)}u\nstatic const uint8_t rec[REC_LEN] = {{\n')
        for i in range(0, len(data), 24): f.write('  ' + ', '.join(str(b) for b in data[i:i + 24]) + ',\n')
        f.write('};\n')
    flags += ['-DAMIGA_TEST', '-DAMIGA_RESULT="DH0:result.txt"']
if '--adf' in sys.argv: adf = os.path.join(out, 'bombernet.adf')    # a bootable OFS floppy: the executable and a Startup-Sequence that runs it
if '--play' in sys.argv: flags.append('-DAMIGA_PLAYTEST')   # tools/amiga_play.py: a 4-player deathmatch preset, the players logged
if '--net' in sys.argv:                                     # tools/crossplay.py: host (or join) a room on SPACE, the room's state logged
    i = sys.argv.index('--net')
    flags += [f'-DAMIGA_NETTEST={1 if sys.argv[i + 1] == "host" else 2}', f'-DAMIGA_NETTEST_PLAYERS={os.environ.get("PLAYERS", 2)}',
              f'-DAMIGA_NETTEST_LOCAL={os.environ.get("LOCAL", 1)}']
rel = lambda p: os.path.relpath(p, ROOT)             # the compiler runs in Docker with the repository as /work
c = os.path.join(ROOT, 'c')
src = [f'{c}/core/{n}.c' for n in ('bomber', 'netplay', 'data', 'map', 'game', 'input', 'video', 'enemy', 'bomb', 'player')]
src += [f'{c}/platform/amiga/plat_amiga.c', f'{c}/platform/amiga/tables.c', f'{c}/platform/amiga/mem.c']
if '--test' in sys.argv: src += [f'{c}/common/netdev_none.c']      # a replay needs no network
else: src += [f'{c}/platform/amiga/tcp_bsdsocket.c', f'{c}/common/netdev_soft.c', f'{c}/common/ws.c']
inc = [f'-I{rel(c)}/core', f'-I{rel(c)}/common', f'-I{rel(c)}/platform/amiga', f'-I{rel(out)}']
exe = os.path.join(out, 'bomber')
flags.append('-Dmain=bomber_main')                 # the core's main; plat_amiga.c's main swaps to a stack of its own around it
r = subprocess.run([os.path.join(ROOT, 'tools', 'amiga-cc'), *flags, *inc, *[rel(s) for s in src], '-o', rel(exe), f'-Wl,-Map,{rel(exe)}.map'],
                   capture_output=True, text=True)
if r.returncode: sys.exit(r.stderr[-4000:])
if r.stderr.strip(): print(r.stderr.strip()[-2000:])
print(f'{exe}: {os.path.getsize(exe)} bytes ({" ".join(flags)})')
if '--adf' in sys.argv:
    xdftool = os.path.join(ROOT, 'build', 'venv', 'bin', 'xdftool')   # amitools, in the project's venv
    seq = os.path.join(out, 'startup-sequence')
    open(seq, 'w').write('bomber\n')
    if os.path.exists(adf): os.remove(adf)
    r = subprocess.run([xdftool, adf, 'create', '+', 'format', 'BomberNet', '+', 'boot', 'install', '+', 'write', exe, 'bomber',
                        '+', 'makedir', 's', '+', 'write', seq, 's/startup-sequence'], capture_output=True, text=True)
    if r.returncode: sys.exit('xdftool: ' + r.stderr[-2000:] + r.stdout[-2000:])
    print(f'{adf}: {os.path.getsize(adf)} bytes, OFS, bootable, bomber and s/startup-sequence')
