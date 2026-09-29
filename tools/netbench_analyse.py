#!/usr/bin/env python3
"""Timing of network matches from the relay's log (RELAY_TIMING=<file>).

For each room: the joiners' turnaround in the lobby (the host's PING in ->
the joiner's PONG in, at the relay: the joiner's link both ways and its
polling) and, in the game, the interval between each device's input lines.

  tools/netbench_analyse.py build/relay_timing.jsonl [--from T --to T]
"""
import json, sys


def pct(v, p): return v[min(len(v) - 1, int(len(v) * p))]


def stats(v):
    v = sorted(v)
    return f'n={len(v)} median {pct(v, .5):.0f} p90 {pct(v, .9):.0f} max {v[-1]:.0f} ms' if v else 'n=0'


def analyse(ev, t_from=None, t_to=None):
    rooms = {}
    for e in ev:
        if e['room']: rooms.setdefault(e['room'], []).append(e)
    out = {}
    for code, es in rooms.items():
        if t_from is not None: es = [e for e in es if t_from <= e['t'] <= t_to]
        pings, turn = {}, {}
        for e in es:
            d = e['data'] or ''
            if e['op'] != 'msg': continue
            if d.startswith('01') and e['slot'] == 0: pings[d[2:4]] = e['t']
            elif d.startswith('02') and d[2:4] in pings:
                turn.setdefault(e['slot'], []).append((e['t'] - pings[d[2:4]]) * 1000)
        pace = {}
        for s in sorted({e['slot'] for e in es if e['op'] == 'input'}):
            t = [e['t'] for e in es if e['op'] == 'input' and e['slot'] == s]
            pace[s] = [(b - a) * 1000 for a, b in zip(t, t[1:])]
        out[code] = (turn, pace)
    return out


if __name__ == '__main__':
    ev = [json.loads(l) for l in open(sys.argv[1])]
    t_from = t_to = None
    if '--from' in sys.argv:
        t_from, t_to = float(sys.argv[sys.argv.index('--from') + 1]), float(sys.argv[sys.argv.index('--to') + 1])
    for code, (turn, pace) in analyse(ev, t_from, t_to).items():
        print(f'room {code}')
        for s, v in turn.items(): print(f'  lobby turnaround of slot {s}: {stats(v)}')
        for s, v in pace.items(): print(f'  game input interval of slot {s}: {stats(v)}')
