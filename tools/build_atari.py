#!/usr/bin/env python3
"""The Atari XL/XE build (llvm-mos, atari8-dos target): build/atari/bomber.xex.

  tools/build_atari.py [--net] [--test recording.bnr [--frames n]] [-O2|-Os]

--net: the network over FujiNet (netdev_soft.c, ws.c, tcp_fujinet.c). The
network device and WebSocket code live in the RAM under the OS ROM
(c/platform/atari/ld/link.ld): this script appends them to the .xex as
segments of at most 4 KB that load into a buffer and are copied up by
atari_hicopy (atari.s) through INITAD.

--test: ATARI_TEST, the recording compiled in and replayed from the title
on, with its figures shown at the end (docs/port-atari-xl.md, step 3);
--frames: only its first n frames (a long recording does not fit).
CFLAGS_ATARI: more compiler flags (-DTEST_HASH_OFF: a standalone game).
LLVM_MOS: the SDK (default ~/src/llvm-mos-dl/llvm-mos)."""
import os, re, subprocess, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
SDK = os.environ.get('LLVM_MOS', os.path.expanduser('~/src/llvm-mos-dl/llvm-mos'))
out = os.path.join(ROOT, 'build', 'atari'); os.makedirs(out, exist_ok=True)
opts = [a for a in sys.argv[1:] if a.startswith('-O')] or ['-Os']
flags = list(opts) + os.environ.get('CFLAGS_ATARI', '').split()
if '--test' in sys.argv:
    data = open(sys.argv[sys.argv.index('--test') + 1], 'rb').read()
    if '--frames' in sys.argv: data = data[:16 + 6 * int(sys.argv[sys.argv.index('--frames') + 1])]
    with open(os.path.join(out, 'rec_data.h'), 'w') as f:
        f.write(f'#define REC_LEN {len(data)}u\nstatic const uint8_t rec[REC_LEN] = {{\n')
        for i in range(0, len(data), 24): f.write('  ' + ', '.join(str(b) for b in data[i:i + 24]) + ',\n')
        f.write('};\n')
    flags.append('-DATARI_TEST')
c = os.path.join(ROOT, 'c')
src = [f'{c}/core/{n}.c' for n in ('bomber', 'netplay', 'data', 'map', 'game', 'input', 'video', 'enemy', 'bomb', 'player')]
src += [f'{c}/platform/atari/{n}' for n in ('plat_atari.c', 'tables.c', 'atari.s', 'sio.s')]
src += [f'{c}/cpu/6502/loops.s']
inc = [f'-I{c}/core', f'-I{c}/common', f'-I{c}/platform/atari', f'-I{out}']
CC = f'{SDK}/bin/mos-atari8-dos-clang'
if '--net' in sys.argv:                  # FujiNet (step 4): the network device in software over it
    src += [f'{c}/platform/atari/tcp_fujinet.c']
    for n in ('netdev_soft', 'ws'):      # under the ROM: plain objects, so the link script sees their names
        o = os.path.join(out, f'hi_{n}.o')
        r = subprocess.run([CC, *flags, '-fno-lto', *inc, '-c', f'{c}/common/{n}.c', '-o', o], capture_output=True, text=True)
        if r.returncode: sys.exit(r.stderr[-4000:])
        src.append(o)
else: src += [f'{c}/common/netdev_none.c']
xex = os.path.join(out, 'bomber.xex')
SCREEN, PMG = 0xb400, 0xb800     # the top of RAM: see plat_atari.c, hiram.ld
LIMIT = SCREEN - 512             # the C stack below the screen (atari.s)
r = subprocess.run([CC, *flags, *inc, *src, '-o', xex, '-Wl,--gc-sections', '-Wl,--undefined=atari_hicopy', f'-Wl,-T,{c}/platform/atari/hiram.ld',
                    f'-Wl,--defsym=atari_screen={SCREEN}', f'-Wl,--defsym=atari_pmg={PMG}'],
                   capture_output=True, text=True)
if r.returncode: sys.exit(r.stderr[-4000:])
nm = subprocess.run([f'{SDK}/bin/llvm-nm', xex + '.elf'], capture_output=True, text=True).stdout
end = int(re.search(r'^([0-9a-f]+) \w __heap_start$', nm, re.M).group(1), 16)
if end > LIMIT: sys.exit(f'the program ends at ${end:04X}, over its limit ${LIMIT:04X} (the C stack, the screen)')
HIBUF, HICHUNK = 0xb400, 2048 - 4      # the screen's place, free while loading (atari_hicopy)
hi = 0                                 # code and fonts under the ROM (hiram.ld): segments copied up
dump = subprocess.run([f'{SDK}/bin/llvm-objdump', '-h', xex + '.elf'], capture_output=True, text=True).stdout
copier = int(re.search(r'^([0-9a-f]+) \w atari_hicopy$', nm, re.M).group(1), 16)
segs = b''
for sec in ('.hifont', '.hitext'):
    m = re.search(r'^\s*\d+\s+' + re.escape(sec) + r'\s+([0-9a-f]+)\s+([0-9a-f]+)', dump, re.M)
    if not m or not int(m.group(1), 16): continue
    vma = int(m.group(2), 16)
    hb = f'{xex}{sec}'
    subprocess.run([f'{SDK}/bin/llvm-objcopy', '-O', 'binary', '-j', sec, xex + '.elf', hb], check=True)
    data = open(hb, 'rb').read(); hi += len(data)
    for i in range(0, len(data), HICHUNK):
        part = data[i:i + HICHUNK]
        body = (vma + i).to_bytes(2, 'little') + len(part).to_bytes(2, 'little') + part
        segs += HIBUF.to_bytes(2, 'little') + (HIBUF + len(body) - 1).to_bytes(2, 'little') + body
        segs += (0x2e2).to_bytes(2, 'little') + (0x2e3).to_bytes(2, 'little') + copier.to_bytes(2, 'little')
dend = int(re.search(r'^([0-9a-f]+) \w __data_end$', nm, re.M).group(1), 16)
if dend > HIBUF: sys.exit(f'the loaded program ends at ${dend:04X}, over the load buffer ${HIBUF:04X}')
open(xex, 'ab').write(segs)
print(f'{xex}: {os.path.getsize(xex)} bytes, $2000-${end:04X}, under the ROM {hi} bytes'
      + f' ({" ".join(flags)})')
