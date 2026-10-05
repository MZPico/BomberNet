#!/usr/bin/env python3
"""Two emulated Amstrad CPCs with an M4 board play a network match through
the relay (tools/cpcemu.py: the M4 at its port interface, real sockets):
A hosts, B joins by code, both ready up, then the state hashes of both are
compared frame by frame.

  tools/lockstep_cpc.py [steps]          local relay (127.0.0.1:8765)
  RELAY=real tools/lockstep_cpc.py       production, api.mzpico.com
  PLAYERS=3 LOCAL_A=2 LOCAL_B=1 ...      seats as in tools/lockstep_zx.py
"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cpcemu import CPC, sym_from_map, run_together

M = 'build/cpc/bomber.map'
S = lambda n: sym_from_map(M, n)
N = int(sys.argv[1]) if len(sys.argv) > 1 else 60
PLAYERS, LOCAL_A, LOCAL_B = int(os.environ.get('PLAYERS', 2)), int(os.environ.get('LOCAL_A', 1)), int(os.environ.get('LOCAL_B', 1))
relay = None if os.environ.get('RELAY') == 'real' else ('127.0.0.1', int(os.environ.get('RELAY_PORT', 8765)))
F, TM, SH, FN = S('_flush_screen'), S('_title_mode'), S('_state_hash'), S('_frame_no')
MN, MM, MP, ML, NC, NA, NAB, NS, NT, NTOT, NDLY, ND = (S(n) for n in ('_menu_net', '_menu_mode', '_menu_players', '_menu_local',
    '_net_code', '_net_active', '_net_abort', '_net_slot', '_net_table', '_net_total', '_net_delay', '_net_device'))


def inst():
    c = CPC(m4=True, relay=relay)
    c.load('build/cpc/bomber.cpc')
    return c


def frames(n, *ms): run_together(ms, F, n)


def tap(c, key, n=3, *others):
    c.press(key); frames(n, c, *others); c.release(key); frames(1, c, *others)


t0 = time.time()
a, b = inst(), inst()
BSS_END = S('__BSS_END_tail')
STACK_TOP = 0xC000
for c in (a, b): c.poke(BSS_END, bytes([0xa5]) * (STACK_TOP - 0x100 - BSS_END))   # stack watermark (below the stack in use)
frames(4, a, b)
print('network device: A', a.read8(ND), 'B', b.read8(ND), flush=True)
a.poke(MM, [1]); a.poke(MP, [PLAYERS]); a.poke(MN, [1]); a.poke(ML, [LOCAL_A])
b.poke(MN, [2]); b.poke(ML, [LOCAL_B])
before = a.read(NC, 4)
tap(a, 'SPACE', 4, b)                                            # A creates the room
for _ in range(600):
    if a.read(NC, 4) != before: break
    frames(1, a, b)
code = a.read(NC, 4).decode(); print('room code', code, f'{time.time() - t0:.1f} s, CPC time A {a.now() / 4e6:.1f} s B {b.now() / 4e6:.1f} s', flush=True)
b.poke(NC, code.encode())
tap(b, 'SPACE', 4, a); frames(4, a, b); tap(b, 'SPACE', 4, a)  # B: code screen, join
for _ in range(600):
    if b.read8(NS) == 1 and a.read8(S('_s_members')) >= 2: break
    frames(1, a, b)
print('slots: A', a.read8(NS), 'B', b.read8(NS), f'{time.time() - t0:.1f} s, CPC time A {a.now() / 4e6:.1f} s B {b.now() / 4e6:.1f} s', flush=True)
frames(40, a, b)                                                 # lobby: delay measured, seats taken
tap(b, 'SPACE', 4, a); tap(a, 'SPACE', 4, b)                     # both ready, the host last
for i in range(300):
    if a.read8(TM) == 0 and b.read8(TM) == 0: break
    frames(1, a, b)
print('in game: title_mode', a.read8(TM), b.read8(TM), 'net_active', a.read8(NA), b.read8(NA),
      '| seats', a.read8(NTOT), a.read(NT, 4).hex(), '| delay', a.read8(NDLY), b.read8(NDLY), flush=True)
a.press('RIGHT'); b.press('LEFT')                                # both walk: inputs cross the network
HP = 16
seen = {'A': {}, 'B': {}}
def on_flush(i, c):
    f = c.read16(FN)
    if f >= HP: seen['AB'[i]][(f // HP) * HP] = c.read16(SH).to_bytes(2, 'little').hex()
run_together((a, b), F, N, on_hit=on_flush, max_seconds=600)
print(f'frames: A {a.read16(FN)} B {b.read16(FN)} abort {a.read8(NAB)}/{b.read8(NAB)}', flush=True)
common = sorted(set(seen['A']) & set(seen['B']))
mism = [f for f in common if seen['A'][f] != seen['B'][f]]
for f in common: print(f'frame {f}: A {seen["A"][f]} B {seen["B"][f]} {"MISMATCH" if f in mism else "ok"}')
aborts = (a.read8(NAB), b.read8(NAB))
print(f'done: {N} steps, {len(common)} hashed frames compared, {len(mism)} mismatches; abort flags {aborts}; {time.time() - t0:.0f} s')
a.screenshot('build/cpc_net_A.png'); b.screenshot('build/cpc_net_B.png')
for name, c in (('A', a), ('B', b)):
    m = c.read(BSS_END, STACK_TOP - 0x100 - BSS_END)
    used_from = next((BSS_END + i for i, v in enumerate(m) if v != 0xa5), STACK_TOP - 0x100)
    print(f'{name}: lowest stack use {used_from:04x}, {used_from - BSS_END} bytes above the program left unused')
sys.exit(1 if mism or any(aborts) or not common else 0)
