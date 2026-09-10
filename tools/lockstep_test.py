#!/usr/bin/env python3
"""Two headless emulators play a network deathmatch through the local relay:
A hosts, B joins by code, both ready up, then the state hashes of both
instances are compared at every flush for N frames while both players move.

  build/venv/bin/uvicorn --app-dir relay relay:app --port 8765 &
  MZ800EMU_CFG=build/emucfg_net tools/lockstep_test.py [frames]
"""
import sys, os, base64, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu import Emu, sym_from_map
M = 'build/c/bomber.map'
S = lambda n: sym_from_map(M, n)
N = int(sys.argv[1]) if len(sys.argv) > 1 else 40
F, TM, SH, FN = S('_flush_screen'), S('_title_mode'), S('_state_hash'), S('_frame_no')
MN, MM, MP, NC, NA, NAB, NS, ND = S('_menu_net'), S('_menu_mode'), S('_menu_players'), S('_net_code'), S('_net_active'), S('_net_abort'), S('_net_slot'), S('_net_device')

class Inst:
    def __init__(self, name):
        self.name = name; self.e = Emu(); self.e.load_mzf('build/c/bomber.mzf')
    def rd(self, a, n): return base64.b64decode(self.e.mem(a, n)['data_b64'])
    def wr(self, a, bs): self.e.call('mem_write', {'addr': a, 'data_hex': bytes(bs).hex()})
    def frames(self, n):
        for _ in range(n): self.e.run_until(F)
    def tap(self, key, n=3):
        self.e.press(key); self.frames(n); self.e.release(key); self.frames(1)

a, b = Inst('A'), Inst('B')
try:
    a.frames(4); b.frames(4)
    print('net_device', a.rd(ND, 1)[0], b.rd(ND, 1)[0], '(3 = MZPico with NET)')
    a.wr(MM, [1]); a.wr(MP, [2]); a.wr(MN, [1])          # A: deathmatch, 2 players, HOST
    b.wr(MN, [2])                                        # B: JOIN
    a.tap('SPACE', 4)                                    # A enters the lobby (creates the room)
    a.frames(6)
    code = a.rd(NC, 4).decode(); print('room code', code)
    b.wr(NC, code.encode())                              # B: code pre-filled, SPACE joins from the entry box
    b.tap('SPACE', 4); b.frames(4); b.tap('SPACE', 4); b.frames(4)
    print('B slot', b.rd(NS, 1)[0], 'A slot', a.rd(NS, 1)[0])
    a.tap('SPACE', 4); b.tap('SPACE', 4)                 # both ready
    for _ in range(30):                                  # wait until both left the title
        if a.rd(TM, 1)[0] == 0 and b.rd(TM, 1)[0] == 0: break
        a.frames(1); b.frames(1)
    print('in game: title_mode', a.rd(TM, 1)[0], b.rd(TM, 1)[0], 'net_active', a.rd(NA, 1)[0], b.rd(NA, 1)[0])
    a.e.press('LEFT'); b.e.press('D')                    # A walks left, B (WASD as local input? no: local input row 0 = cursor) -> use RIGHT
    b.e.release('D'); b.e.press('RIGHT')
    mism = 0; last = None
    for i in range(N):
        a.e.run_until(F); b.e.run_until(F)
        fa, fb = a.rd(FN, 2), b.rd(FN, 2)
        ha, hb = a.rd(SH, 2), b.rd(SH, 2)
        if fa == fb and ha != hb: mism += 1
        if i % 10 == 0 or ha != hb:
            print(f'step {i}: A frame {fa[0] | fa[1] << 8} hash {ha.hex()} | B frame {fb[0] | fb[1] << 8} hash {hb.hex()} abort {a.rd(NAB, 1)[0]}/{b.rd(NAB, 1)[0]}')
    print(f'done: {N} steps, {mism} hash mismatches on equal frames; abort flags {a.rd(NAB, 1)[0]} {b.rd(NAB, 1)[0]}')
    sys.exit(1 if mism else 0)
finally:
    a.e.close(); b.e.close()
