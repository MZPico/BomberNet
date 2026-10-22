#!/usr/bin/env python3
"""The C64 build played in VICE (tools/vice.py), in real time: the title,
the menu by the cursor keys, then a 4-player deathmatch with key set A
(cursor keys, SHIFT for left and up, SPACE), key set B (W A S D, E) and
both joysticks; each input must move its player. Screenshots of the title
and the game go to build/c64/.

  tools/c64_play.py [build/c64/bomber.prg]"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vice import Vice

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
OUT = os.path.join(ROOT, 'build', 'c64')
v = Vice(sys.argv[1] if len(sys.argv) > 1 else os.path.join(OUT, 'bomber.prg'))
P = v.sym['players']                     # player_t: 16 bytes, x at 3, y at 4
fails = []


def check(what, ok):
    print(f'{"ok  " if ok else "FAIL"} {what}')
    if not ok: fails.append(what)


def pos(i):
    d = v.mem(P + 16 * i, 5)
    return d[3], d[4]


def hold(keys=(), joy=None, t=0.8):
    """hold keys (and a joystick: (port, directions)) for t seconds; the players' positions after"""
    for k in keys: v.key(k, 1)
    if joy: v.joy(joy[0], *joy[1])
    v.run(t)
    for k in keys: v.key(k, 0)
    if joy: v.joy(joy[0])
    v.run(0.3)
    return [pos(i) for i in range(4)]


try:
    v.run(4)
    v.screenshot(os.path.join(OUT, 'title.png'))
    check('title screen', v.u8('title_mode') == 1)
    item = v.u8('menu_item')
    hold(['CRSR_DOWN'], t=0.2)
    down = v.u8('menu_item')
    hold(['RSHIFT', 'CRSR_DOWN'], t=0.2)
    check(f'menu: CRSR down {item} -> {down}, SHIFT+CRSR back to {v.u8("menu_item")}', down > item and v.u8('menu_item') == item)
    for k in ('RSHIFT', 'LSHIFT', 'CRSR_DOWN'): v.key(k, 0)    # once more: a SHIFT left down (rarely seen) walks player 1 into the walls
    v.poke('menu_mode', [1]); v.poke('menu_players', [4]); v.poke('menu_inputs', [1, 2, 3, 4])   # deathmatch: A, B, joy 1, joy 2
    hold(['SPACE'], t=0.3)
    for _ in range(60):                      # the round under way (game frames, not seconds: VICE may lag)
        v.run(0.25)
        if v.u8('title_mode') == 0 and v.u16('frame_no') >= 40: break
    check('SPACE starts the game', v.u8('title_mode') == 0)
    v.screenshot(os.path.join(OUT, 'game.png'))
    start = [pos(i) for i in range(4)]       # the corners: (1,1), (37,21), (37,1), (1,21)
    moves = [(0, ['CRSR_RIGHT'], None, 0, +1), (0, ['RSHIFT', 'CRSR_RIGHT'], None, 0, -1),
             (0, ['CRSR_DOWN'], None, 1, +1), (0, ['LSHIFT', 'CRSR_DOWN'], None, 1, -1),
             (1, ['A'], None, 0, -1), (1, ['D'], None, 0, +1),
             (2, [], (1, ['down']), 1, +1), (2, [], (1, ['up']), 1, -1),
             (3, [], (2, ['right']), 0, +1), (3, [], (2, ['left']), 0, -1)]
    for p, keys, joy, axis, sign in moves:
        before = pos(p)
        after = hold(keys, joy)[p]
        what = '+'.join(keys) if keys else f'joystick {joy[0]} {joy[1][0]}'
        good = (after[axis] - before[axis]) * sign > 0
        check(f'player {p + 1} {what}: {before} -> {after}' + ('' if good else
              f' (record {v.mem(P + 16 * p, 10).hex(" ")}, inputs {v.mem("menu_inputs", 4).hex()}, frame {v.u16("frame_no")})'), good)
    hold(['SPACE', 'E'], (1, ['fire']), t=0.2)
    hold([], (2, ['fire']), t=0.2)
    v.screenshot(os.path.join(OUT, 'bombs.png'))
finally:
    v.close()
print('all inputs work' if not fails else f'{len(fails)} failed')
sys.exit(1 if fails else 0)
