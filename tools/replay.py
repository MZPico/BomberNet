#!/usr/bin/env python3
"""Replay a .bnr recording (from the host simulator) on the Z80 build in
mz800emu and compare the state hash frame by frame.

  tools/replay.py build/replay/dm2s.bnr [build/c/bomber.mzf] [build/c/bomber.map]
"""
import sys, os, base64
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu import Emu, sym_from_map

rec_path = sys.argv[1]
mzf = sys.argv[2] if len(sys.argv) > 2 else 'build/c/bomber.mzf'
mapf = sys.argv[3] if len(sys.argv) > 3 else 'build/c/bomber.map'
data = open(rec_path, 'rb').read()
assert data[:4] == b'BNR1', 'not a BNR1 file'
seed = data[4] | (data[5] << 8); mode = data[6]; nplayers = data[7]
inputs = data[8:12]; hash_period = data[12]
records = [data[16 + i * 6:22 + i * 6] for i in range((len(data) - 16) // 6)]
print(f'{rec_path}: seed {seed:04x} mode {mode} players {nplayers} inputs {list(inputs)} hash every {hash_period}, {len(records)} frames')

S = lambda n: sym_from_map(mapf, n)
F = S('_flush_screen')
e = Emu()
e.load_mzf(mzf)
def rd(a, n): return base64.b64decode(e.mem(a, n)['data_b64'])
def wr(a, bs): e.call('mem_write', {'addr': a, 'data_hex': bytes(bs).hex()})
try:
    for _ in range(4): e.run_until(F)
    wr(S('_menu_mode'), [mode]); wr(S('_menu_players'), [nplayers]); wr(S('_menu_inputs'), inputs)
    wr(S('_match_seed'), [seed & 0xff, seed >> 8]); wr(S('_hash_period'), [hash_period])
    wr(S('_joy_type'), [1 if nplayers > 2 else 0]); wr(S('_replay_active'), [1])
    TM, SH, RK, FN = S('_title_mode'), S('_state_hash'), S('_replay_keys'), S('_frame_no')
    e.press('SPACE')
    k = 0; mismatches = 0; started = False
    while k < len(records):
        e.run_until(F)
        if rd(TM, 1)[0]: continue
        if not started: started = True; e.release('SPACE')   # first game flush = record 0
        h = rd(SH, 2); h = h[0] | (h[1] << 8)
        rec = records[k]
        fh = rec[4] | (rec[5] << 8)
        if h != fh:
            mismatches += 1
            if mismatches <= 10:
                fn = rd(FN, 2); fn = fn[0] | (fn[1] << 8)
                print(f'HASH MISMATCH at game frame {k} (frame_no {fn}): file {fh:04x}, z80 {h:04x}')
        wr(RK, rec[:4])
        k += 1
        if k % 50 == 0: print(f'  {k} frames, {mismatches} mismatches', flush=True)
    print(f'done: {k} frames compared, {mismatches} mismatches')
    sys.exit(1 if mismatches else 0)
finally:
    e.close()
