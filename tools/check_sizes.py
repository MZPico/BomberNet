#!/usr/bin/env python3
"""Memory budgets: each build's program end against its machine's limit, from
the link maps, so that a change that eats a machine's stack fails here and not
on a player's machine. (A z88dk build over 64 KB fails by itself; one that
grows into the stack does not: the 128K Spectrum once kept 32 bytes, see
docs/net-timing.md.)

  tools/check_sizes.py [build dir ...]     default: whichever of the builds below exist

The Spectrum limits: the stack starts at FFFFh. Since the lobby's buffers
are static it does not reach below FF00h in a 4-player match on either model
(tools/lockstep_zx.py's watermark, October 2026: a peak of at most 256
bytes); the program must end by FEA0h, 352 bytes below the top: that peak
and about 100 bytes to spare. The test workflow measures the stack itself
(lockstep_zx.py with STACK_MIN).
"""
import os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))


def z88dk_end(mapf):
    for l in open(mapf):
        m = re.match(r'__BSS_END_tail\s+= \$([0-9A-F]+)', l)
        if m: return int(m.group(1), 16)
    return None


def check_z80(name, d, limit, what):
    mapf = os.path.join(d, 'bomber.map')
    if not os.path.exists(mapf): return None
    end = z88dk_end(mapf)
    if end is None: return (name, False, 'no __BSS_END_tail in the map')
    return (name, end <= limit, f'program ends at {end:04X}h, {what} {limit:04X}h ({limit - end:+d} bytes)')


def check_c64(d):
    """the end of the variables too (__heap_start in the ELF: the zeroed data is not in the file)"""
    elf = os.path.join(d, 'bomber.prg.elf')
    if not os.path.exists(elf): return None
    import subprocess
    sdk = os.environ.get('LLVM_MOS', os.path.expanduser('~/src/llvm-mos-dl/llvm-mos'))
    nm = subprocess.run([os.path.join(sdk, 'bin', 'llvm-nm'), elf], capture_output=True, text=True).stdout
    m = re.search(r'^([0-9a-f]+) \w __heap_start$', nm, re.M)
    if not m: return ('C64', False, 'no __heap_start in the ELF (LLVM_MOS: the SDK)')
    end = int(m.group(1), 16)
    return ('C64', end <= 0xCFFF, f'program and variables end at {end:04X}h, the I/O area at D000h ({0xCFFF - end:+d} bytes)')


def check_pc(d):
    mapf = os.path.join(d, 'bomber.map')
    if not os.path.exists(mapf): return None
    for l in open(mapf):
        m = re.match(r'DGROUP\s+\S+\s+([0-9a-f]+)', l)
        if m:
            size = int(m.group(1), 16)
            return ('PC', size <= 0xF000, f'DGROUP {size} bytes (the data, the stack among them), at most 61440 of 64 KB ({0xF000 - size:+d})')
    return ('PC', False, 'no DGROUP in the map')


CHECKS = [
    lambda: check_z80('MZ-800', os.path.join(ROOT, 'build', 'c'), 0xC000, 'the stack (from D000h) wants it under'),
    lambda: check_z80('ZX 48K', os.path.join(ROOT, 'build', 'zx'), 0xFEA0, 'the stack (down from FFFFh) wants it under'),
    lambda: check_z80('ZX 128K', os.path.join(ROOT, 'build', 'zx128'), 0xFEA0, 'the stack (down from FFFFh) wants it under'),
    lambda: check_z80('CPC', os.path.join(ROOT, 'build', 'cpc'), 0xBC00, 'the firmware stubs (BCCBh) and the stack (BE00h) want it under'),
    lambda: check_c64(os.path.join(ROOT, 'build', 'c64')),
    lambda: check_pc(os.path.join(ROOT, 'build', 'pc')),
]

if __name__ == '__main__':
    results = [r for r in (c() for c in CHECKS) if r]
    if not results:                       # (the Atari's linker checks its own memory map; Amiga, ST, web: no tight limit)
        print('no build with a budget here'); sys.exit(0)
    fails = 0
    for name, ok, text in results:
        print(f'{"ok  " if ok else "FAIL"} {name:8} {text}')
        fails += not ok
    sys.exit(1 if fails else 0)
