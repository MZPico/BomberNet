#!/usr/bin/env python3
"""Atari XL/XE tables from the MZ ones (screen design D, docs/port-atari-xl.md).

The field is ANTIC mode 4: 40 x 25 cells of 4 x 8 pixels, two bits a pixel
(00 background, 01 PF0, 10 PF1, 11 PF2, or PF3 when the cell's code has bit
7 set). Palette: background black, PF0 red, PF1 magenta, PF2 yellow, PF3
cyan, the MZ's inks folded onto them: red -> PF0, magenta -> PF1, yellow and
white -> PF2, cyan, blue and green -> PF3. The four players are hardware
sprites (player/missile graphics) in their own colours, so their cells and
their death frames stay blank in the field. Text rows (the status line, a
message's text) are ANTIC mode 2, the MZ characters as they are.

Converting a character:
- MZ dot graphics (dots of 3 x 3 pixels at x 1-3 / 5-7 and rows 1-3 / 5-7):
  each dot becomes one pixel at the 2nd or 4th position, rows kept: the dot
  grid stays;
- the two checkers (outer wall, pillars) stay checkers;
- the brick '#': its own pattern;
- anything else: two pixels become one (set if either is).

Tables written to c/platform/atari/tables.[ch]:
  atari_field_font  128 x 8 bytes, ANTIC mode 4 (pixel 0 in bits 7-6)
  atari_field_code  logical code -> screen code (bit 7: PF3), 0 = blank
  atari_text_font   128 x 8 bytes, ANTIC mode 2, from the MZ characters
  atari_text_code   logical code -> screen code in atari_text_font
  atari_sprite      logical code -> 8 rows of a 4-bit sprite half
                    (the players' and their death frames' shapes)
  atari_title_font  128 x 8 bytes, ANTIC mode 2: the title and lobby screens
  atari_title_code  logical code (title mode) -> screen code in it
  atari_logo_font   128 x 8 bytes, ANTIC mode 4: the title's logo rows (0-5),
                    in the MZ's colours (PF0 red, PF1 magenta, PF2 white)
  atari_logo_code   logical code (title mode) -> screen code in it
  atari_title_ink   logical code (title mode) -> its MZ ink (0-7), for the
                    sprites that colour the legend's icons
The fonts are 1 KB aligned (ATARI_ALIGN, tables.h), as ANTIC wants them.

  tools/make_atari_tables.py [--mock screen.bin out.png]   (a logical screen rendered as design D)
"""
import os, re, struct, sys, zlib

here = os.path.dirname(os.path.abspath(__file__))
CGROM = os.environ.get('MZ_CGROM', os.path.expanduser('~/src/mz-catalog/tools/mzfont/cgrom.bin'))
cg = open(CGROM, 'rb').read()
src = open(os.path.join(here, '..', 'c', 'platform', 'mz', 'tables.c')).read()
data = open(os.path.join(here, '..', 'c', 'core', 'data.c')).read()


def table(name):
    i = src.index(name + '[512] = {'); j = src.index('};', i)
    return [int(x, 0) for x in re.findall(r'0x[0-9a-fA-F]+', src[i:j])]


gt = table('game_table')
tt = table('title_table')
BAK, PF0, PF1, PF2, PF3 = 'bak', 'pf0', 'pf1', 'pf2', 'pf3'
# PF2 is a pale yellow, apart from P2's strong yellow (fire is white on the MZ)
INK = {2: PF0, 3: PF1, 6: PF2, 7: PF2, 5: PF3, 1: PF3, 4: PF3, 0: PF2}
# as atari800 shows the colours plat_atari.c sets (PAL): $36, $4A, $EE, $9A; players $BA, $EC, $0E, $78
RGB = {BAK: (0, 0, 0), PF0: (132, 55, 63), PF1: (185, 106, 169), PF2: (232, 217, 122), PF3: (89, 155, 169)}   # PF2 pale yellow
PLAYER_RGB = [(97, 170, 70), (193, 178, 82), (211, 211, 211), (84, 98, 188)]   # P1 green, P2 strong yellow, P3 white, P4 blue
BRICK = ['.#.#', '.#.#', '####', '.#.#', '####', '.#.#', '.#.#', '....']


def mz_glyph(code):
    return cg[gt[2 * code] * 8: gt[2 * code] * 8 + 8]


def bits(b): return [(b >> k) & 1 for k in range(8)]          # MZ: bit 0 is the leftmost pixel


def is_dots(g):
    return g[0] == 0 and g[4] == 0 and g[1] == g[2] == g[3] and g[5] == g[6] == g[7] and \
        all(b in (0, 0x0e, 0xe0, 0xee) for b in g) and any(g)


def player_of(code):
    """the players' own codes and the shared death frames (40h-5Fh): sprites, not field cells"""
    if 0x8a <= code <= 0x8d or 0x9a <= code <= 0x9d or 0x40 <= code <= 0x5f: return 0
    for p, base in ((1, 0xa0), (2, 0xa4), (3, 0xa8)):
        if base <= code <= base + 3 or base + 0x10 <= code <= base + 0x13: return p
    return None


def four(code):
    """the cell as 8 rows of 4 on/off pixels"""
    if code == 0x80: return [[c == '#' for c in r] for r in BRICK]
    return four_glyph(mz_glyph(code))


def four_glyph(g):
    if set(g) <= {0x55, 0xaa}: return [[(x + y) % 2 == 0 for x in range(4)] for y in range(8)]
    if is_dots(g): return [[0, (b & 0x0e) != 0, 0, (b & 0xe0) != 0] for b in g]
    return [[bool(bits(b)[2 * i] | bits(b)[2 * i + 1]) for i in range(4)] for b in g]


def colour(code):
    return INK.get((gt[2 * code + 1] >> 4) & 7, PF2)


# ---- the field font: one 4 x 8 shape per distinct (shape, colour pair), PF3 = PF2's bits + inverse
field_font, field_index, field_code = [bytes(8)], {bytes(8): 0}, [0] * 256
for code in range(256):
    if player_of(code) is not None or not any(mz_glyph(code)): continue
    rows, col = four(code), colour(code)
    v = {PF0: 1, PF1: 2, PF2: 3, PF3: 3}[col]
    shape = bytes(sum((v if px else 0) << (6 - 2 * i) for i, px in enumerate(r)) for r in rows)
    if shape not in field_index:
        field_index[shape] = len(field_font); field_font.append(shape)
    field_code[code] = field_index[shape] | (0x80 if col == PF3 else 0)
assert len(field_font) <= 128, len(field_font)

# ---- the text font (mode 2): the MZ characters, bit 7 leftmost, for the status line and messages
text_font, text_index, text_code = [bytes(8)], {bytes(8): 0}, [0] * 256
for code in range(256):
    g = bytes(int(f'{b:08b}'[::-1], 2) for b in mz_glyph(code))
    if player_of(code) is not None and code not in range(0x40, 0x60): pass
    if g not in text_index:
        if len(text_font) == 128: continue
        text_index[g] = len(text_font); text_font.append(g)
    text_code[code] = text_index[g]

# ---- the title font (mode 2): the title table's characters (letters, logo blocks, the legend)
title_font, title_index, title_code = [bytes(8)], {bytes(8): 0}, [0] * 256
for code in range(256):
    g = bytes(int(f'{b:08b}'[::-1], 2) for b in cg[tt[2 * code] * 8: tt[2 * code] * 8 + 8])
    if g not in title_index:
        title_index[g] = len(title_font); title_font.append(g)
    title_code[code] = title_index[g]
assert len(title_font) <= 128, len(title_font)

# ---- the logo font (mode 4): the logo's cells in their colours, anything else halved in PF2 (white)
i = data.index('title_logo[240] = {'); logo = set(int(x, 0) for x in re.findall(r'0x[0-9a-fA-F]+', data[i:data.index('};', i)]))
logo_font, logo_index, logo_code, title_ink = [bytes(8)], {bytes(8): 0}, [0] * 256, [0] * 256
for code in range(256):
    g = cg[tt[2 * code] * 8: tt[2 * code] * 8 + 8]
    title_ink[code] = (tt[2 * code + 1] >> 4) & 7
    if not any(g): continue
    v = {2: 1, 3: 2}.get(title_ink[code], 3) if code in logo else 3
    shape = bytes(sum((v if px else 0) << (6 - 2 * i) for i, px in enumerate(r)) for r in four_glyph(g))
    if shape not in logo_index:
        logo_index[shape] = len(logo_font); logo_font.append(shape)
    logo_code[code] = logo_index[shape]
assert len(logo_font) <= 128, len(logo_font)

# ---- sprite halves: a player cell as 8 rows of 4 bits (one sprite byte is two cells)
sprite = [bytes(8)] * 256
for code in range(256):
    if player_of(code) is None: continue
    rows = four(code)
    sprite[code] = bytes(sum(1 << (3 - i) for i, px in enumerate(r) if px) for r in rows)


def carray(name, data, per=16, align=''):
    lines = [f'const uint8_t {name}[{len(data)}]{align} = {{']
    for i in range(0, len(data), per): lines.append('  ' + ', '.join(f'0x{b:02x}' for b in data[i:i + per]) + ',')
    return '\n'.join(lines + ['};', ''])


def write_tables():
    out = os.path.join(here, '..', 'c', 'platform', 'atari')
    os.makedirs(out, exist_ok=True)
    c = ['/* Generated by tools/make_atari_tables.py from the MZ tables - do not edit. */',
         '#include <stdint.h>', '#include "tables.h"', '',
         carray('atari_field_font', b''.join(field_font) + bytes(8 * (128 - len(field_font))), align=' ATARI_ALIGN(1024)'),
         carray('atari_text_font', b''.join(text_font) + bytes(8 * (128 - len(text_font))), align=' ATARI_ALIGN(1024)'),
         carray('atari_title_font', b''.join(title_font) + bytes(8 * (128 - len(title_font))), align=' ATARI_ALIGN(1024)'),
         carray('atari_field_code', bytes(field_code)),
         carray('atari_text_code', bytes(text_code)),
         carray('atari_title_code', bytes(title_code)),
         carray('atari_logo_font', b''.join(logo_font) + bytes(8 * (128 - len(logo_font))), align=' ATARI_ALIGN(1024)'),
         carray('atari_logo_code', bytes(logo_code)),
         carray('atari_title_ink', bytes(title_ink)),
         carray('atari_sprite', b''.join(sprite))]
    open(os.path.join(out, 'tables.c'), 'w').write('\n'.join(c))
    open(os.path.join(out, 'tables.h'), 'w').write(
        '/* Generated by tools/make_atari_tables.py - do not edit. */\n#ifndef TABLES_H\n#define TABLES_H\n#include <stdint.h>\n'
        '#ifndef ATARI_ALIGN\n#define ATARI_ALIGN(n) __attribute__((aligned(n)))\n#endif\n'
        'extern const uint8_t atari_field_font[1024], atari_text_font[1024], atari_title_font[1024];\n'
        'extern const uint8_t atari_field_code[256], atari_text_code[256], atari_title_code[256], atari_sprite[2048];\n'
        'extern const uint8_t atari_logo_font[1024], atari_logo_code[256], atari_title_ink[256];\n#endif\n')
    print(f'field font {len(field_font)} of 128 shapes, text font {len(text_font)}, title font {len(title_font)}, logo font {len(logo_font)}')


def mock(screen, path, scale=2):
    """a logical screen as design D shows it: the field from the tables, the status line as text, players as sprites"""
    img = [[RGB[BAK]] * 320 for _ in range(200)]
    owner, cur = {}, None                                   # status line: whose number, icon and count a cell is
    for x in range(40):
        c = screen[960 + x]
        if c in (0xae, 0xaf, 0xbc, 0xbd): cur = (0xae, 0xaf, 0xbc, 0xbd).index(c); owner[x] = cur
        elif c == 0x90 and cur is not None: owner[x] = owner[x + 1] = cur
    for cell in range(1000):
        cy, cx = divmod(cell, 40)
        code = screen[cell]
        p = player_of(code)
        for y in range(8):
            if cy == 24 and cx in owner:                        # a player's number / lives: that player's sprite, in its colour
                b = mz_glyph(code)[y]
                half = [bits(b)[2 * i] | bits(b)[2 * i + 1] for i in range(4)]
                pix = [PLAYER_RGB[owner[cx]] if half[x // 2] else None for x in range(8)]
            elif cy == 24:                                      # the status line: mode 2, one colour
                b = text_font[text_code[code]][y]
                pix = [RGB[PF2] if (b >> (7 - x)) & 1 else None for x in range(8)]
            elif p is not None:                                 # a sprite
                b = sprite[code][y]
                pix = [PLAYER_RGB[p] if (b >> (3 - x // 2)) & 1 else None for x in range(8)]
            else:
                sc = field_code[code]
                b = field_font[sc & 0x7f][y]
                pix = []
                for i in range(4):
                    v = (b >> (6 - 2 * i)) & 3
                    col = [None, RGB[PF0], RGB[PF1], RGB[PF3] if sc & 0x80 else RGB[PF2]][v]
                    pix += [col, col]
            for x in range(8):
                if pix[x] is not None: img[cy * 8 + y][cx * 8 + x] = pix[x]
    rows = [b''.join(bytes(px) * scale for px in r) for r in img for _ in range(scale)]
    raw = b''.join(b'\x00' + r for r in rows)
    ch = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', 320 * scale, 200 * scale, 8, 2, 0, 0, 0)) +
                           ch(b'IDAT', zlib.compress(raw, 9)) + ch(b'IEND', b''))


if '--mock' in sys.argv:
    i = sys.argv.index('--mock')
    mock(open(sys.argv[i + 1], 'rb').read(), sys.argv[i + 2])
else:
    write_tables()
