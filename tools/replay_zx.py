#!/usr/bin/env python3
"""Replay a .bnr recording (from the host simulator) on the ZX Spectrum build
and compare the state hash frame by frame. Same check as tools/replay.py does
for the MZ build: a recording that replays on both proves that the two
machines simulate identically.

  tools/replay_zx.py build/ref/dm2.bnr [build/zx/bomber] [build/zx/bomber.map]
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from zxemu import ZX, sym_from_map

rec_path = sys.argv[1]
binary = sys.argv[2] if len(sys.argv) > 2 else 'build/zx/bomber'
mapf = sys.argv[3] if len(sys.argv) > 3 else 'build/zx/bomber.map'
data = open(rec_path, 'rb').read()
assert data[:4] == b'BNR1', 'not a BNR1 file'
seed = data[4] | (data[5] << 8); mode = data[6]; nplayers = data[7]
inputs = data[8:12]; hash_period = data[12]
records = [data[16 + i * 6:22 + i * 6] for i in range((len(data) - 16) // 6)]
print(f'{rec_path}: seed {seed:04x} mode {mode} players {nplayers} hash every {hash_period}, {len(records)} frames')

S = lambda n: sym_from_map(mapf, n)
F = S('_flush_screen')
z = ZX(); z.load(binary)
for _ in range(4): z.run_until(F)
z.poke(S('_menu_mode'), [mode]); z.poke(S('_menu_players'), [nplayers]); z.poke(S('_menu_inputs'), inputs)
z.poke(S('_match_seed'), [seed & 0xff, seed >> 8]); z.poke(S('_hash_period'), [hash_period])
z.poke(S('_joy_type'), [1 if nplayers > 2 else 0]); z.poke(S('_replay_active'), [1])
TM, SH, RK, FN = S('_title_mode'), S('_state_hash'), S('_replay_keys'), S('_frame_no')
z.press('SPACE')
k = 0; mismatches = 0; started = False
while k < len(records):
    z.run_until(F)
    if z.read8(TM): continue
    if not started: started = True; z.release('SPACE')      # first game flush = record 0
    h = z.read16(SH)
    rec = records[k]
    fh = rec[4] | (rec[5] << 8)
    if h != fh:
        mismatches += 1
        if mismatches <= 10: print(f'HASH MISMATCH at game frame {k} (frame_no {z.read16(FN)}): file {fh:04x}, zx {h:04x}')
    z.poke(RK, rec[:4])
    k += 1
print(f'done: {k} frames compared, {mismatches} mismatches')
sys.exit(1 if mismatches else 0)
