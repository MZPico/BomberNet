#!/usr/bin/env python3
"""Amstrad CPC tables from the MZ ones: logical code -> glyph and colour scheme.

The CPC runs in Mode 1: 320 x 200 pixels in four inks, so a cell is 8 x 8
pixels as on the MZ and the MZ character ROM is used as it is (no
redrawing). Palette (pens 0..3): black, red, yellow, cyan.

A colour scheme is one ink, or a checkerboard of two (pixel x + line y even:
the first ink, odd: the second); at normal size a checker reads as a mixed
colour. The four players and the four enemy types get their own schemes,
everything else takes the scheme of its MZ ink.

Tables written to c/platform/cpc/tables.[ch]:
  cpc_glyphs      8 bytes per glyph, bit 7 = leftmost pixel
  cpc_game_tab    logical code -> glyph number, scheme number
  cpc_title_tab   the same in title mode
  cpc_lut         per scheme: 2 line parities x 16 nibbles -> Mode 1 byte
                  (a nibble is 4 pixels of a glyph line; the byte has pen bit 0
                  in bits 7..4 and pen bit 1 in bits 3..0)
"""
import os, re, sys

here = os.path.dirname(os.path.abspath(__file__))
CGROM = os.environ.get('MZ_CGROM', os.path.expanduser('~/src/mz-catalog/tools/mzfont/cgrom.bin'))
cg = open(CGROM, 'rb').read()
src = open(os.path.join(here, '..', 'c', 'platform', 'mz', 'tables.c')).read()

BLACK, RED, YELLOW, CYAN = 0, 1, 2, 3
PALETTE_HW = [0x14, 0x0C, 0x0A, 0x13]      # gate array colour numbers: black, bright red, bright yellow, bright cyan

# schemes: (ink on even pixels, ink on odd pixels)
SCHEMES = []
def scheme(a, b=None):
    s = (a, a if b is None else b)
    if s not in SCHEMES: SCHEMES.append(s)
    return SCHEMES.index(s)

# MZ ink (attribute bits 4..6) -> scheme
MZ_INK = {
    0: scheme(BLACK),
    1: scheme(CYAN, BLACK),          # blue: dim cyan
    2: scheme(RED),                  # red
    3: scheme(CYAN),                 # magenta: the outer wall
    4: scheme(YELLOW, CYAN),         # green: pale green
    5: scheme(CYAN),                 # cyan
    6: scheme(YELLOW),               # yellow
    7: scheme(YELLOW),               # white: text
}
PLAYER = [scheme(YELLOW), scheme(CYAN), scheme(YELLOW, CYAN), scheme(YELLOW, RED)]
ENEMY = [scheme(RED), scheme(CYAN, RED), scheme(YELLOW, BLACK), scheme(CYAN, BLACK)]


def table(name):
    i = src.index(name + '[512] = {'); j = src.index('};', i)
    return [int(x, 0) for x in re.findall(r'0x[0-9a-fA-F]+', src[i:j])]


def glyph(display):
    """MZ character (bit 0 = leftmost pixel) -> bit 7 = leftmost."""
    return bytes(int(f'{b:08b}'[::-1], 2) for b in cg[display * 8:display * 8 + 8])


def player_of(code):
    """Logical codes of each player's standing frames and status-line digit."""
    if 0x8a <= code <= 0x8d or 0x9a <= code <= 0x9d: return 0
    for p, base in ((1, 0xa0), (2, 0xa4), (3, 0xa8)):
        if base <= code <= base + 3 or base + 0x10 <= code <= base + 0x13: return p
    return {0xae: 0, 0xaf: 1, 0xbc: 2, 0xbd: 3}.get(code)


glyphs = [bytes(8)]
index = {bytes(8): 0}


def gi(g):
    if g not in index:
        index[g] = len(glyphs); glyphs.append(g)
    return index[g]


def convert(name, title):
    t = table(name); out = bytearray(512)
    for c in range(256):
        out[2 * c] = gi(glyph(t[2 * c]))
        ink = (t[2 * c + 1] >> 4) & 7
        s = MZ_INK[ink]
        if title and ink == 1: s = scheme(RED)        # blue text (a greyed menu row): a checker does not read on letters
        if not title:
            p = player_of(c)
            if p is not None: s = PLAYER[p]
            elif c >= 0xc0 and c < 0xe0: s = ENEMY[(c & 0x0f) // 4]
        out[2 * c + 1] = s
    return bytes(out)


game, title = convert('game_table', False), convert('title_table', True)
assert len(glyphs) <= 256 and len(SCHEMES) <= 16

lut = bytearray()
for a, b in SCHEMES:
    for parity in (0, 1):
        for n in range(16):
            byte = 0
            for k in range(4):                       # pixel k of the nibble, k = 0 leftmost
                if n & (8 >> k):
                    pen = a if (k + parity) % 2 == 0 else b
                    byte |= (pen & 1) << (7 - k) | ((pen >> 1) & 1) << (3 - k)
            lut.append(byte)

# player schemes for plat_player_colour (the shared death frames)
player_schemes = bytes(PLAYER)

# Cells rendered in advance: every (glyph, scheme) pair the two tables use, 16
# bytes each (line 0 bytes 0 and 1, line 1 ...); a logical code is then an
# index, and drawing a cell is a copy. The live renderer (glyph + lookup)
# remains for the players' death frames, recoloured per player.
def render(g, s):
    out = bytearray()
    for y in range(8):
        row = lut[s * 32 + (y & 1) * 16:s * 32 + (y & 1) * 16 + 16]
        out += bytes([row[glyphs[g][y] >> 4], row[glyphs[g][y] & 15]])
    return bytes(out)

cells, cell_index = [], {}
def ci(g, s):
    key = render(g, s)
    if key not in cell_index:
        cell_index[key] = len(cells); cells.append(key)
    return cell_index[key]

game_idx = bytes(ci(game[2 * c], game[2 * c + 1]) for c in range(256))
title_idx = bytes(ci(title[2 * c], title[2 * c + 1]) for c in range(256))
assert len(cells) <= 256, len(cells)


def carray(name, data, per=16, comment=None):
    lines = [f'/* {comment} */'] if comment else []
    lines.append(f'const uint8_t {name}[{len(data)}] = {{')
    for i in range(0, len(data), per):
        lines.append('  ' + ', '.join(f'0x{b:02x}' for b in data[i:i + per]) + ',')
    lines.append('};'); lines.append('')
    return '\n'.join(lines)


out = os.path.join(here, '..', 'c', 'platform', 'cpc')
os.makedirs(out, exist_ok=True)
open(os.path.join(out, 'tables.c'), 'w').write('\n'.join([
    '/* Generated by tools/make_cpc_tables.py - do not edit. */', '#include <stdint.h>', '#include "tables.h"', '',
    carray('cpc_glyphs', b''.join(glyphs), 8, f'{len(glyphs)} glyphs from the MZ character ROM, 8 x 8, bit 7 leftmost'),
    carray('cpc_game_tab', game, 16, 'logical code -> glyph, scheme (the live renderer)'),
    carray('cpc_cells', b''.join(cells), 16, f'{len(cells)} cells rendered in advance, 16 bytes each'),
    carray('cpc_game_idx', game_idx, 16, 'logical code -> cell'),
    carray('cpc_title_idx', title_idx, 16, 'title mode: logical code -> cell'),
    carray('cpc_lut', lut, 16, f'{len(SCHEMES)} schemes x 2 line parities x 16 nibbles -> Mode 1 byte'),
    carray('cpc_player_schemes', player_schemes, 4, 'scheme of each player'),
    carray('cpc_palette', bytes(PALETTE_HW), 4, 'gate array colours of pens 0..3')]))
open(os.path.join(out, 'tables.h'), 'w').write('\n'.join([
    '/* Generated by tools/make_cpc_tables.py - do not edit. */', '#ifndef TABLES_H', '#define TABLES_H', '#include <stdint.h>',
    f'extern const uint8_t cpc_glyphs[{len(glyphs) * 8}];', 'extern const uint8_t cpc_game_tab[512];',
    f'extern const uint8_t cpc_cells[{len(cells) * 16}];', 'extern const uint8_t cpc_game_idx[256];',
    'extern const uint8_t cpc_title_idx[256];', f'extern const uint8_t cpc_lut[{len(lut)}];',
    'extern const uint8_t cpc_player_schemes[4];', 'extern const uint8_t cpc_palette[4];', '#endif', '']))
print(f'wrote c/platform/cpc/tables.[ch]: {len(glyphs)} glyphs, {len(SCHEMES)} schemes, {len(cells)} cells, '
      f'{len(glyphs) * 8 + 512 + len(cells) * 16 + 512 + len(lut) + 8} bytes')
