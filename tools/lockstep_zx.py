#!/usr/bin/env python3
"""Two emulated ZX Spectrums with a Spectranet play a network match through
the relay: A hosts, B joins by code, both ready up, then the state hashes of
both are compared frame by frame.

  tools/lockstep_zx.py [steps]           local relay (127.0.0.1:8765)
  RELAY=real tools/lockstep_zx.py        production, api.mzpico.com
  PLAYERS=3 LOCAL_A=2 LOCAL_B=1 ...      seats as in tools/lockstep_test.py
  B_BUILD=build/v020/build/zx ...        B runs another build (its bomber and bomber.map)
  BREAK_AT=40 ...                        A leaves with BREAK after 40 frames: A ends with
                                         MATCH LEFT, B with the other player gone
"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from zxemu import ZX, sym_from_map, run_together

M = 'build/zx/bomber.map'
S = lambda n: sym_from_map(M, n)
B_BUILD = os.environ.get('B_BUILD', 'build/zx')
SB = lambda n: sym_from_map(os.path.join(B_BUILD, 'bomber.map'), n)
N = int(sys.argv[1]) if len(sys.argv) > 1 else 60
PLAYERS, LOCAL_A, LOCAL_B = int(os.environ.get('PLAYERS', 2)), int(os.environ.get('LOCAL_A', 1)), int(os.environ.get('LOCAL_B', 1))
relay = None if os.environ.get('RELAY') == 'real' else ('127.0.0.1', 8765)
F, SYNC, TM, SH, FN = S('_flush_screen'), S('_plat_frame_sync'), S('_title_mode'), S('_state_hash'), S('_frame_no')
MN, MM, MP, ML, NC, NA, NAB, NS, NT, NTOT, NDLY = (S(n) for n in ('_menu_net', '_menu_mode', '_menu_players', '_menu_local',
    '_net_code', '_net_active', '_net_abort', '_net_slot', '_net_table', '_net_total', '_net_delay'))


def inst(path):
    z = ZX(spectranet=True, relay=relay)
    z.load(path)
    return z


def frames(n, *ms):                        # n frames on each, the machines running side by side
    run_together(ms, [F if z is a else FB for z in ms], n)


def tap(z, key, n=3, *others):
    z.press(key); frames(n, z, *others); z.release(key); frames(1, z, *others)


t0 = time.time()
a, b = inst('build/zx/bomber'), inst(os.path.join(B_BUILD, 'bomber'))
FB, TMB, SHB, FNB, NSB, NAB_B, NDLY_B = (SB(n) for n in ('_flush_screen', '_title_mode', '_state_hash', '_frame_no',
    '_net_slot', '_net_abort', '_net_delay'))
BSS = {a: S('__BSS_END_tail'), b: SB('__BSS_END_tail')}
for z in (a, b): z.poke(BSS[z], bytes([0xa5]) * (0xff00 - BSS[z]))   # stack watermark
frames(4, a, b)
a.poke(MM, [1]); a.poke(MP, [PLAYERS]); a.poke(MN, [1]); a.poke(ML, [LOCAL_A])
b.poke(SB('_menu_net'), [2]); b.poke(SB('_menu_local'), [LOCAL_B])
before = a.read(NC, 4)
tap(a, 'SPACE', 4, b)                                            # A creates the room
for _ in range(600):                                             # the reply takes real time
    if a.read(NC, 4) != before: break
    frames(1, a, b)
code = a.read(NC, 4).decode(); print('room code', code, flush=True)
b.poke(SB('_net_code'), code.encode())
tap(b, 'SPACE', 4, a); frames(4, a, b); tap(b, 'SPACE', 4, a)  # B: code screen, join
for _ in range(600):
    if b.read8(NSB) == 1 and a.read8(S('_s_members')) >= 2: break
    frames(1, a, b)
print('slots: A', a.read8(NS), 'B', b.read8(NSB), flush=True)
frames(40, a, b)                                                 # lobby: delay measured, seats taken
tap(a, 'SPACE', 4, b); tap(b, 'SPACE', 4, a)                     # both ready (the host goes out last)
for _ in range(200):
    if a.read8(TM) == 0 and b.read8(TMB) == 0: break
    frames(1, a, b)
print('in game: title_mode', a.read8(TM), b.read8(TMB), 'net_active', a.read8(NA), b.read8(SB('_net_active')),
      '| seats', a.read8(NTOT), a.read(NT, 4).hex(), '| delay', a.read8(NDLY), b.read8(NDLY_B), flush=True)
a.press('P'); b.press('O')                                       # both walk: inputs cross the network
HP = 16
seen = {'A': {}, 'B': {}}
work = []
COMP = S('_composite_map')
a.set_breakpoint(SYNC); a.set_breakpoint(COMP)                  # A only: its frame time
last = {}
def on_flush(i, z):                                # every frame of each machine
    name = 'AB'[i]
    f = z.read16(FN if z is a else FNB)
    if f >= HP: seen[name][(f // HP) * HP] = z.read16(SH if z is a else SHB).to_bytes(2, 'little').hex()
def step_a():                                      # A's own quanta, to time its frames
    pc = a.step()
    if pc == SYNC: last['sync'] = a.now()
    elif pc == COMP:
        if 'comp' in last and 'sync' in last: work.append((last['sync'] - last['comp']) / 3500.0)
        last['comp'] = a.now()
    elif pc == F: on_flush(0, a); return 1
    return 0
done_a = done_b = 0
BREAK_AT = int(os.environ.get('BREAK_AT', 0))
end = a.now() + 120 * 3500000
while (done_a < N or done_b < N) and a.now() < end:
    done_a += step_a()
    if BREAK_AT and done_a == BREAK_AT: a.press('CAPS SHIFT', 'SPACE')
    if b.step() == FB: on_flush(1, b); done_b += 1
print(f'frames: A {a.read16(FN)} B {b.read16(FNB)} abort {a.read8(NAB)}/{b.read8(NAB_B)}', flush=True)
common = sorted(set(seen['A']) & set(seen['B']))
mism = [f for f in common if seen['A'][f] != seen['B'][f]]
for f in common: print(f'frame {f}: A {seen["A"][f]} B {seen["B"][f]} {"MISMATCH" if f in mism else "ok"}')
aborts = (a.read8(NAB), b.read8(NAB_B))
if work: print('A: frame time incl. waiting for the network ms avg %.1f (not a CPU figure: the emulator runs faster than real time)' % (sum(work) / len(work)))
print(f'done: {N} steps, {len(common)} hashed frames compared, {len(mism)} mismatches; abort flags {aborts}; '
      f'{a.sn_calls + b.sn_calls} Spectranet calls; {time.time() - t0:.0f} s')
a.screenshot('build/zx_net_A.png'); b.screenshot('build/zx_net_B.png')
for name, z in (('A', a), ('B', b)):
    m = z.read(BSS[z], 0xff00 - BSS[z])
    used_from = next((BSS[z] + i for i, v in enumerate(m) if v != 0xa5), 0xff00)
    print(f'{name}: stack reached {used_from:04x}, {used_from - BSS[z]} bytes above the program left unused')
if BREAK_AT:
    print(f'BREAK at frame {BREAK_AT}: A abort {aborts[0]} (7 = left), B abort {aborts[1]}, A title_mode {a.read8(TM)}')
    sys.exit(0 if aborts[0] == 7 and aborts[1] and not mism else 1)
sys.exit(1 if mism or any(aborts) or not common else 0)
