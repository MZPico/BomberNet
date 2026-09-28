#!/usr/bin/env python3
"""ZX Spectrum tables from the MZ ones: logical code -> glyph number and attribute.

A cell on the Spectrum is 6 x 8 pixels (40 cells in 240 pixels). The MZ glyphs
are 8 x 8; they are narrowed by OR-ing columns 1+2 and 5+6, which keeps every
stroke and turns the MZ dot graphics (3 x 3 dots on a 4 pixel pitch) into
2 x 3 dots on a 3 pixel pitch. Letters and digits come from a 5 x 7 font.
Glyph bytes are left aligned (bits 7..2).

Attribute byte of the tables: ink, paper and BRIGHT as on the Spectrum; bit 7
is not FLASH but "figure": where two cells share an attribute square, a figure
wins over scenery (see flush_screen in plat_zx.c). The coloured player digits
of the status line are figures too: on the status bar they keep their colour
while the rest of the text is black.
"""
import os, re, sys

here = os.path.dirname(os.path.abspath(__file__))
CGROM = os.environ.get('MZ_CGROM', os.path.expanduser('~/src/mz-catalog/tools/mzfont/cgrom.bin'))
cg = open(CGROM, 'rb').read()
src = open(os.path.join(here, '..', 'c', 'platform', 'mz', 'tables.c')).read()


def table(name):
    i = src.index(name + '[512] = {'); j = src.index('};', i)
    return [int(x, 0) for x in re.findall(r'0x[0-9a-fA-F]+', src[i:j])]


FONT = {
    'A': ['.###.', '#...#', '#...#', '#####', '#...#', '#...#', '#...#'],
    'B': ['####.', '#...#', '#...#', '####.', '#...#', '#...#', '####.'],
    'C': ['.###.', '#...#', '#....', '#....', '#....', '#...#', '.###.'],
    'D': ['####.', '#...#', '#...#', '#...#', '#...#', '#...#', '####.'],
    'E': ['#####', '#....', '#....', '####.', '#....', '#....', '#####'],
    'F': ['#####', '#....', '#....', '####.', '#....', '#....', '#....'],
    'G': ['.###.', '#...#', '#....', '#.###', '#...#', '#...#', '.###.'],
    'H': ['#...#', '#...#', '#...#', '#####', '#...#', '#...#', '#...#'],
    'I': ['.###.', '..#..', '..#..', '..#..', '..#..', '..#..', '.###.'],
    'J': ['..###', '...#.', '...#.', '...#.', '...#.', '#..#.', '.##..'],
    'K': ['#...#', '#..#.', '#.#..', '##...', '#.#..', '#..#.', '#...#'],
    'L': ['#....', '#....', '#....', '#....', '#....', '#....', '#####'],
    'M': ['#...#', '##.##', '#.#.#', '#.#.#', '#...#', '#...#', '#...#'],
    'N': ['#...#', '##..#', '#.#.#', '#..##', '#...#', '#...#', '#...#'],
    'O': ['.###.', '#...#', '#...#', '#...#', '#...#', '#...#', '.###.'],
    'P': ['####.', '#...#', '#...#', '####.', '#....', '#....', '#....'],
    'Q': ['.###.', '#...#', '#...#', '#...#', '#.#.#', '#..#.', '.##.#'],
    'R': ['####.', '#...#', '#...#', '####.', '#.#..', '#..#.', '#...#'],
    'S': ['.####', '#....', '#....', '.###.', '....#', '....#', '####.'],
    'T': ['#####', '..#..', '..#..', '..#..', '..#..', '..#..', '..#..'],
    'U': ['#...#', '#...#', '#...#', '#...#', '#...#', '#...#', '.###.'],
    'V': ['#...#', '#...#', '#...#', '#...#', '#...#', '.#.#.', '..#..'],
    'W': ['#...#', '#...#', '#...#', '#.#.#', '#.#.#', '##.##', '#...#'],
    'X': ['#...#', '#...#', '.#.#.', '..#..', '.#.#.', '#...#', '#...#'],
    'Y': ['#...#', '#...#', '.#.#.', '..#..', '..#..', '..#..', '..#..'],
    'Z': ['#####', '....#', '...#.', '..#..', '.#...', '#....', '#####'],
    '0': ['.###.', '#...#', '#..##', '#.#.#', '##..#', '#...#', '.###.'],
    '1': ['..#..', '.##..', '..#..', '..#..', '..#..', '..#..', '.###.'],
    '2': ['.###.', '#...#', '....#', '...#.', '..#..', '.#...', '#####'],
    '3': ['####.', '....#', '....#', '.###.', '....#', '....#', '####.'],
    '4': ['...#.', '..##.', '.#.#.', '#..#.', '#####', '...#.', '...#.'],
    '5': ['#####', '#....', '####.', '....#', '....#', '#...#', '.###.'],
    '6': ['.###.', '#....', '#....', '####.', '#...#', '#...#', '.###.'],
    '7': ['#####', '....#', '...#.', '..#..', '.#...', '.#...', '.#...'],
    '8': ['.###.', '#...#', '#...#', '.###.', '#...#', '#...#', '.###.'],
    '9': ['.###.', '#...#', '#...#', '.####', '....#', '....#', '.###.'],
}


# the MZ arrows are inverse-video boxes, unreadable at 6 pixels: plain arrows instead
ARROWS = {
    0x21: ['..#..', '..#..', '..#..', '..#..', '#.#.#', '.###.', '..#..'],     # down
    0x22: ['..#..', '.###.', '#.#.#', '..#..', '..#..', '..#..', '..#..'],     # up
    0x23: ['.....', '..#..', '...#.', '#####', '...#.', '..#..', '.....'],     # right
    0x24: ['.....', '..#..', '.#...', '#####', '.#...', '..#..', '.....'],     # left
}


def pattern_glyph(rows):
    return bytes([int(r.replace('#', '1').replace('.', '0') + '0', 2) << 2 for r in rows] + [0])


def font_glyph(ch):
    return bytes([int(r.replace('#', '1').replace('.', '0') + '0', 2) << 2 for r in FONT[ch]] + [0])


def mz_rows(code):
    g = cg[code * 8:code * 8 + 8]
    return [[(b >> k) & 1 for k in range(8)] for b in g]      # bit 0 is the leftmost pixel


def narrow(code):
    rows = mz_rows(code)
    raw = cg[code * 8:code * 8 + 8]
    if set(raw) <= {0x55, 0xaa} and len(set(raw)) == 2:      # checker: keep it a checker
        first = rows[0][0]
        return bytes([(0xa8 if (r % 2 == 0) == bool(first) else 0x54) for r in range(8)])
    out = []
    for r in rows:
        six = [r[0], r[1] | r[2], r[3], r[4], r[5] | r[6], r[7]]
        out.append(sum(bit << (7 - i) for i, bit in enumerate(six)))
    return bytes(out)


def glyph_for(code):
    if 0x01 <= code <= 0x1a: return font_glyph(chr(ord('A') + code - 1))
    if 0x20 <= code <= 0x29: return font_glyph(chr(ord('0') + code - 0x20))
    return narrow(code)


# logical codes whose cell is a figure (players, enemies, bombs, fire, items, death frames)
def is_figure(logical, title):
    if title: return False
    return (0x8a <= logical <= 0x8d or 0x9a <= logical <= 0x9d or 0xa0 <= logical <= 0xbd or
            logical >= 0xc0 or 0x40 <= logical <= 0x7f or 0x0a <= logical <= 0x0f or 0x1a <= logical <= 0x1f and logical not in (0x1c, 0x1d))


def zx_attr(mz):
    ink, paper = (mz >> 4) & 7, mz & 7
    return 0x40 | (paper << 3) | ink                          # BRIGHT: the MZ colours are vivid


glyphs = [bytes(8)]                                           # glyph 0 is the blank
index = {bytes(8): 0}


def gi(code, glyph=None):
    g = glyph if glyph is not None else glyph_for(code)
    if g not in index:
        index[g] = len(glyphs); glyphs.append(g)
    return index[g]


def convert(name, title):
    t = table(name); out = bytearray(512)
    for c in range(256):
        out[2 * c] = gi(t[2 * c], pattern_glyph(ARROWS[c]) if title and c in ARROWS else None)
        out[2 * c + 1] = zx_attr(t[2 * c + 1]) | (0x80 if is_figure(c, title) else 0)
    return bytes(out)


game, title = convert('game_table', False), convert('title_table', True)
assert len(glyphs) <= 256


def carray(name, data, per=16, comment=None):
    lines = [f'/* {comment} */'] if comment else []
    lines.append(f'const uint8_t {name}[{len(data)}] = {{')
    for i in range(0, len(data), per):
        lines.append('  ' + ', '.join(f'0x{b:02x}' for b in data[i:i + per]) + ',')
    lines.append('};'); lines.append('')
    return '\n'.join(lines)


out = os.path.join(here, '..', 'c', 'platform', 'zx')
open(os.path.join(out, 'tables.c'), 'w').write('\n'.join([
    '/* Generated by tools/make_zx_tables.py - do not edit. */', '#include <stdint.h>', '#include "tables.h"', '',
    carray('zx_glyphs', b''.join(glyphs), 8, f'{len(glyphs)} glyphs, 6 x 8, left aligned'),
    carray('zx_game_tab', game, 16, 'logical code -> glyph, attribute (bit 7: figure)'),
    carray('zx_title_tab', title, 16, 'title mode')]))
open(os.path.join(out, 'tables.h'), 'w').write('\n'.join([
    '/* Generated by tools/make_zx_tables.py - do not edit. */', '#ifndef TABLES_H', '#define TABLES_H', '#include <stdint.h>',
    f'extern const uint8_t zx_glyphs[{len(glyphs) * 8}];', 'extern const uint8_t zx_game_tab[512];',
    'extern const uint8_t zx_title_tab[512];', '#endif', '']))
print(f'wrote c/platform/zx/tables.[ch]: {len(glyphs)} glyphs, {len(glyphs) * 8 + 1024} bytes')

if '--show' in sys.argv:
    for n, g in enumerate(glyphs):
        print(n)
        for b in g: print('  ' + ''.join('#' if (b >> (7 - i)) & 1 else '.' for i in range(6)))
