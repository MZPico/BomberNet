#!/usr/bin/env python3
"""Run a C64_TEST build (tools/build_c64.py --test) in VICE and print its
figures: the recording replayed with the hashes checked, each game frame's
work (flush .. next frame sync) in cycles, as wall time on a PAL C64.

  tools/c64_test.py [build/c64/bomber.prg] [--ntsc] [--png out.png]

VICE (tools/vice.py) runs in warp: the figures are emulated cycles; its
monitor reads c64_test once done is 1."""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vice import Vice

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
args = [a for i, a in enumerate(sys.argv[1:], 1) if not a.startswith('--') and sys.argv[i - 1] != '--png']
prg = args[0] if args else os.path.join(ROOT, 'build', 'c64', 'bomber.prg')
png = sys.argv[sys.argv.index('--png') + 1] if '--png' in sys.argv else os.path.join(ROOT, 'build', 'c64', 'test.png')

t0 = time.time()
v = Vice(prg, warp=True, ntsc='--ntsc' in sys.argv)
try:
    while True:
        v.run(2)
        head = v.mem('c64_test', 12)
        if head[0] or time.time() - t0 > 600: break
    n = head[6] | head[7] << 8
    work = [w[0] | w[1] << 8 for w in zip(*[iter(v.mem(v.sym['c64_test'] + 12, 2 * n))] * 2)]
    v.screenshot(png)
finally:
    v.close()
hz = 1022727 if head[1] else 985248
bad, late, flush = head[2] | head[3] << 8, head[4] | head[5] << 8, int.from_bytes(head[8:12], 'little')
if not head[0]: sys.exit(f'not done after {time.time() - t0:.0f} s; {n} frames timed')
ws = sorted(work)
ms = lambda c: c / hz * 1000
q = lambda f: ws[min(len(ws) - 1, int(len(ws) * f))]
print(f'{os.path.basename(prg)} ({"NTSC" if head[1] else "PAL"}): {n} game frames, hashes {"MISMATCH at offset " + str(bad) if bad else "all equal"}, '
      f'late {late}')
print(f'  work a frame: median {ms(q(.5)):.1f} ms, p90 {ms(q(.9)):.1f}, p99 {ms(q(.99)):.1f}, max {ms(ws[-1]):.1f}'
      f'{" (or more: capped at 65535 cycles)" if ws[-1] == 0xffff else ""}; over 60 ms {sum(ms(w) > 60 for w in work)}; '
      f'the flush {ms(flush / n):.1f} ms on average')
print(f'  screenshot {png}; {time.time() - t0:.0f} s')
sys.exit(1 if bad else 0)
