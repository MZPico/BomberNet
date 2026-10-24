#!/usr/bin/env python3
"""The browser build (Emscripten, WebAssembly): build/web/bombernet.html, .js, .wasm.

  tools/build_web.py [--test recording.bnr [--frames n]] [--out dir]

Emscripten from $EMSDK, else ~/src/emsdk. The core's main is compiled as
bomber_main (c/platform/web/plat_web.c has the real main); Asyncify lets the
core's waits yield to the browser. --test: WEB_TEST, the recording compiled
in and replayed from the title on, every hash checked, for Node.js
(build/web-test/bombernet.js; tools/web_test.py runs it). CFLAGS_WEB: more
flags."""
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
EMSDK = os.environ.get('EMSDK') or os.path.expanduser('~/src/emsdk')
EMCC = os.path.join(EMSDK, 'upstream', 'emscripten', 'emcc')
if not os.path.exists(EMCC): sys.exit(f'no Emscripten in {EMSDK}')
test = '--test' in sys.argv
out = os.path.abspath(sys.argv[sys.argv.index('--out') + 1]) if '--out' in sys.argv else os.path.join(ROOT, 'build', 'web-test' if test else 'web')
os.makedirs(out, exist_ok=True)
c = os.path.join(ROOT, 'c')
flags = ['-O2', '-Wall', '-Wno-main', '-Wno-unused-function'] + os.environ.get('CFLAGS_WEB', '').split()
if test:
    data = open(sys.argv[sys.argv.index('--test') + 1], 'rb').read()
    if '--frames' in sys.argv: data = data[:16 + 6 * int(sys.argv[sys.argv.index('--frames') + 1])]
    with open(os.path.join(out, 'rec_data.h'), 'w') as f:
        f.write(f'#define REC_LEN {len(data)}u\nstatic const uint8_t rec[REC_LEN] = {{\n')
        for i in range(0, len(data), 24): f.write('  ' + ', '.join(str(b) for b in data[i:i + 24]) + ',\n')
        f.write('};\n')
    flags.append('-DWEB_TEST')
core = [f'{c}/core/{n}.c' for n in ('bomber', 'netplay', 'data', 'map', 'game', 'input', 'video', 'enemy', 'bomb', 'player')]
src = core + [f'{c}/platform/web/plat_web.c', f'{c}/platform/amiga/tables.c']   # the Amiga's glyph tables
src += [f'{c}/common/netdev_none.c'] if test else [f'{c}/common/netdev_soft.c', f'{c}/platform/web/ws_web.c']
inc = [f'-I{c}/core', f'-I{c}/common', f'-I{c}/platform/web', f'-I{c}/platform/amiga', f'-I{out}']
objs = []
for s in src:
    o = os.path.join(out, os.path.splitext(os.path.basename(s))[0] + '.o')
    extra = ['-Dmain=bomber_main'] if s.endswith('core/bomber.c') else []
    r = subprocess.run([EMCC, *flags, *extra, *inc, '-c', s, '-o', o], capture_output=True, text=True)
    if r.returncode: sys.exit(r.stdout[-3000:] + r.stderr[-3000:])
    if r.stderr.strip(): print(r.stderr.strip()[-1500:])
    objs.append(o)
link = ['-O2', '-sASYNCIFY', '-sASYNCIFY_STACK_SIZE=65536', '-sSTACK_SIZE=1048576',
        '-sEXPORTED_FUNCTIONS=_main,_web_key,_web_pad,_web_state,_web_poke', '-sEXPORTED_RUNTIME_METHODS=HEAPU8,UTF8ToString']
if test: target = [os.path.join(out, 'bombernet.js'), '-sENVIRONMENT=node', '-sEXIT_RUNTIME=1']
else: target = [os.path.join(out, 'bombernet.html'), '--shell-file', f'{c}/platform/web/shell.html', '-sENVIRONMENT=web']
r = subprocess.run([EMCC, *link, *objs, '-o', *target], capture_output=True, text=True)
if r.returncode: sys.exit(r.stdout[-3000:] + r.stderr[-3000:])
for f in sorted(os.listdir(out)):
    if f.startswith('bombernet.'): print(f'{out}/{f}: {os.path.getsize(os.path.join(out, f))} bytes')
