#!/usr/bin/env python3
"""Compose the BOMBERNET title logo from the original logo's letter glyphs.

The logo is 6 rows x 40 chars of quarter-block graphics (each char = 2x2
dots), i.e. an 80x12 dot bitmap with a checker border (code 2Eh).
Letters are 8 dots wide x 7 dots tall in a "disconnected stroke" style.
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def original_dots(body, tt):
    logo = body[0x1551 - 0x1200:0x1551 - 0x1200 + 240]
    dots = [[0] * 80 for _ in range(12)]
    for r in range(6):
        for c in range(40):
            code = logo[r * 40 + c]
            n = 15 if code == 0x2e else (0 if code == 0x20 else tt[2 * code] - 0xF0)
            dots[2 * r][2 * c] = (n >> 1) & 1
            dots[2 * r][2 * c + 1] = n & 1
            dots[2 * r + 1][2 * c] = (n >> 3) & 1
            dots[2 * r + 1][2 * c + 1] = (n >> 2) & 1
    return dots


def glyph(dots, c0):
    return [[dots[r][c] for c in range(c0, c0 + 8)] for r in range(2, 9)]


T_GLYPH = [
    [1, 0, 1, 1, 1, 1, 0, 1],
    [0, 0, 0, 1, 1, 0, 0, 0],
    [0, 0, 0, 1, 1, 0, 0, 0],
    [0, 0, 0, 1, 1, 0, 0, 0],
    [0, 0, 0, 1, 1, 0, 0, 0],
    [0, 0, 0, 1, 1, 0, 0, 0],
    [0, 0, 0, 1, 1, 0, 0, 0],
]


def compose(body, tt, word='BOMBERNET'):
    od = original_dots(body, tt)
    glyphs = {'B': glyph(od, 2), 'O': glyph(od, 10), 'M': glyph(od, 18),
              'E': glyph(od, 34), 'R': glyph(od, 42), 'N': glyph(od, 70), 'T': T_GLYPH}
    dots = [[0] * 80 for _ in range(12)]
    x = 4
    for ch in word:
        g = glyphs[ch]
        for r in range(7):
            for c in range(8):
                dots[2 + r][x + c] = g[r][c]
        x += 8
    codes = bytearray(240)
    for r in range(6):
        for c in range(40):
            if r in (0, 5) or c in (0, 39):
                codes[r * 40 + c] = 0x2e
                continue
            n = dots[2 * r][2 * c] * 2 + dots[2 * r][2 * c + 1] + dots[2 * r + 1][2 * c] * 8 + dots[2 * r + 1][2 * c + 1] * 4
            codes[r * 40 + c] = 0x20 if n == 0 else (0x2f if n == 1 else 0x30 + n)
    return bytes(codes), dots


if __name__ == '__main__':
    mzf = open(os.path.join(HERE, '..', 'bomber.mzf'), 'rb').read()[128:]
    tt = mzf[0x314F - 0x1200:0x314F - 0x1200 + 180]
    codes, dots = compose(mzf, tt)
    for row in dots:
        print(''.join('#' if d else '.' for d in row))
