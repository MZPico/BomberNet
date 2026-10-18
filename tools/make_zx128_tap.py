#!/usr/bin/env python3
"""A tape for the 128K Spectrum build: a BASIC loader, the game at 24000
and the bank 6 module at 49152, loaded with bank 6 paged in.

  tools/make_zx128_tap.py game.bin game.map bank6.bin out.tap

The loader: CLEAR 23999, LOAD the game, page bank 6 in, LOAD the module,
page bank 0 back in and start the game. The paging is done by two routines
in the game (zx128_page6, zx128_start in platform/zx/tcp_bank6.c, below
C000h), so the loader stays short: with RAMTOP at 23999 BASIC has little
room, and a longer loader ends in "4 Out of memory" at the first LOAD.
Numbers are written as VAL "..." (no 5-byte forms)."""
import re, sys

T = {'CLEAR': 0xfd, 'LOAD': 0xef, 'CODE': 0xaf, 'POKE': 0xf4, 'OUT': 0xdf,
     'RANDOMIZE': 0xf9, 'USR': 0xc0, 'VAL': 0xb0}


def val(n): return bytes([T['VAL']]) + b'"' + str(n).encode() + b'"'


def basic_line(num, *stmts):
    body = b':'.join(stmts) + b'\r'
    return num.to_bytes(2, 'big') + len(body).to_bytes(2, 'little') + body


def tap_block(flag, data):
    b = bytes([flag]) + data
    x = 0
    for c in b: x ^= c
    b += bytes([x])
    return len(b).to_bytes(2, 'little') + b


def header(kind, name, length, p1, p2):
    return tap_block(0x00, bytes([kind]) + name.ljust(10)[:10].encode() + length.to_bytes(2, 'little') +
                     p1.to_bytes(2, 'little') + p2.to_bytes(2, 'little'))


def load_code(): return bytes([T['LOAD']]) + b'""' + bytes([T['CODE']])


def usr(addr): return bytes([T['RANDOMIZE'], T['USR']]) + val(addr)


def sym(mapf, name):
    return int(re.search(r'^%s\s+= \$([0-9A-F]+)' % name, open(mapf).read(), re.M).group(1), 16)


game, mapf, bank6, out = open(sys.argv[1], 'rb').read(), sys.argv[2], open(sys.argv[3], 'rb').read(), sys.argv[4]
page6, start = sym(mapf, 'zx128_page6'), sym(mapf, 'zx128_start')
assert page6 < 0xc000 and start < 0xc000
prog = basic_line(10, bytes([T['CLEAR']]) + val(23999), load_code(), usr(page6), load_code(), usr(start))
tap = header(0, 'BomberNet', len(prog), 10, len(prog)) + tap_block(0xff, prog)
tap += header(3, 'bomber', len(game), 24000, 32768) + tap_block(0xff, game)
tap += header(3, 'bank6', len(bank6), 49152, 32768) + tap_block(0xff, bank6)
assert 24000 + len(game) <= 0x10000 and len(bank6) <= 0x4000
open(out, 'wb').write(tap)
