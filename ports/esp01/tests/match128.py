#!/usr/bin/env python3
"""Two emulated 128K Spectrums with ESP-AT UART play a network match through
the relay: A hosts, B joins by code, both ready up, then the state hashes of
both are compared frame by frame.

  Invoked by test_relay128.py against a local upstream relay.
  Adapted from upstream tools/lockstep_zx.py.
"""
import os, sys, time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[3]/"tools"))
from zxemu import sym_from_map, run_together
from test_bank128 import BankZX

M = 'build/esp01-128/bomber.map'
S = lambda n: sym_from_map(M, n)
N = int(sys.argv[1]) if len(sys.argv) > 1 else 60
PLAYERS, LOCAL_A, LOCAL_B = int(os.environ.get('PLAYERS', 2)), int(os.environ.get('LOCAL_A', 1)), int(os.environ.get('LOCAL_B', 1))
relay = None if os.environ.get('RELAY') == 'real' else ('127.0.0.1', 8765)
F, SYNC, TM, SH, FN = S('_flush_screen'), S('_plat_frame_sync'), S('_title_mode'), S('_state_hash'), S('_frame_no')
MN, MM, MP, ML, NC, NA, NAB, NS, NT, NTOT, NDLY = (S(n) for n in ('_menu_net', '_menu_mode', '_menu_players', '_menu_local',
    '_net_code', '_net_active', '_net_abort', '_net_slot', '_net_table', '_net_total', '_net_delay'))


def inst(name):
    z = BankZX(real=True)
    z.pc = 24000
    ref=os.environ.get('ESP_REFERENCE_'+name)
    z.reference=bool(ref)
    if ref:
        raw=Path(ref,'bomber').read_bytes();z.write(24000,raw,ram_page=0)
        z.allocation_end=24000+len(raw)
        if os.environ.get('ESP_LEGACY_MIN2')=='1':
            z.poke(S('_lobby_apply_rtt'),b'\x3e\x02\x32'+NDLY.to_bytes(2,'little')+b'\xc9')
    else:z.allocation_end=S('__BSS_END_tail')
    if os.environ.get('ESP_WALK')=='1':
        assert PLAYERS==2
        stage_reset=S('_players_stage_reset');z.set_breakpoint(stage_reset)
        prior=z.on_breakpoint
        def walk_start():
            prior()
            if z.pc==stage_reset and z.read8(NA):
                arena=bytearray([32]*1000)
                for y in range(24):
                    for x in range(40):
                        if y in (0,23) or x in (0,39):arena[y*40+x]=0x88
                z.poke(S('_map_layer'),arena)
                z.poke(S('_players')+3,bytes([1,1]))
                z.poke(S('_players')+16+3,bytes([37,20]))
        z.on_breakpoint=walk_start
    if os.environ.get('ESP_KEYS_ON_START')=='1':
        z.set_breakpoint(S('_net_match_start'))
        prior_start=z.on_breakpoint
        def keys_on_start():
            prior_start()
            if z.pc==S('_net_match_start'):z.press('P' if name=='A' else 'O')
        z.on_breakpoint=keys_on_start
    return z


def frames(n, *ms):                        # n frames on each, the machines running side by side
    if 'link' not in globals():
        run_together(ms,F,n);return
    counts=[0]*len(ms)
    while min(counts)<n:
        i=min(range(len(ms)),key=lambda i:link.now(i))
        if ms[i].step()==F:counts[i]+=1


def tap(z, key, n=3, *others):
    z.press(key); frames(n, z, *others); z.release(key); frames(1, z, *others)


t0 = time.time()
a, b = inst('A'), inst('B')
BSS_END = S('__BSS_END_tail')
for z in (a, b): z.poke(z.allocation_end, bytes([0xa5]) * (0xff00 - z.allocation_end))   # stack watermark
frames(4, a, b)
if os.environ.get('ESP_HISTORY')=='1':
    a.poke(S('_hit_x'),[7,12]);b.poke(S('_hit_x'),[22,3])
a.poke(MM, [int(os.environ.get('GAME_MODE',1))]); a.poke(MP, [PLAYERS]); a.poke(MN, [1]); a.poke(ML, [LOCAL_A])
b.poke(MN, [2]); b.poke(ML, [LOCAL_B])
before = a.read(NC, 4)
tap(a, 'SPACE', 4, b)                                            # A creates the room
for _ in range(600):                                             # the reply takes real time
    if a.read(NC, 4) != before: break
    frames(1, a, b)
code = a.read(NC, 4).decode(); print('room code', code, flush=True)
b.poke(NC, code.encode())
tap(b, 'SPACE', 4, a); frames(4, a, b); tap(b, 'SPACE', 4, a)  # B: code screen, join
for _ in range(600):
    if b.read8(NS) == 1 and a.read8(S('_s_members')) >= 2: break
    frames(1, a, b)
print('slots: A', a.read8(NS), 'B', b.read8(NS), flush=True)
frames(40, a, b)                                                 # lobby: delay measured, seats taken
tap(a, 'SPACE', 4, b); tap(b, 'SPACE', 4, a)                     # both ready (the host goes out last)
for _ in range(200):
    if a.read8(TM) == 0 and b.read8(TM) == 0: break
    frames(1, a, b)
print('in game: title_mode', a.read8(TM), b.read8(TM), 'net_active', a.read8(NA), b.read8(NA),
      '| seats', a.read8(NTOT), a.read(NT, 4).hex(), '| delay', a.read8(NDLY), b.read8(NDLY), flush=True)
if os.environ.get('ESP_WALK')=='1':print('WALK: clear corridor, continuous bidirectional movement',flush=True)
if os.environ.get('ESP_TIMED_LINK'):
    from timed_link128 import TimedLink
    frames(8,a,b)
    link=TimedLink(a,b,int(os.environ['ESP_TIMED_LINK']))
    frames(20,a,b)
print('effective input delay:',*[z.read8(NDLY if z.reference else S('_esp_input_delay')) for z in (a,b)],flush=True)
started=[z.now() for z in (a,b)];origin=[z.read8(S('_players')+i*16+3) for i,z in enumerate((a,b))]
first_move=[None,None];crossing=[None,None]
a.press('P'); b.press('O')                                       # both walk: inputs cross the network
HP = 16
seen = {'A': {}, 'B': {}}
work = []
try:COMP=S('_composite_frame')
except KeyError:COMP=S('_composite_map')
for z in (a, b): z.set_breakpoint(SYNC); z.set_breakpoint(COMP)
last = {}
def on_flush(i, z):                                # every frame of each machine
    if os.environ.get('ESP_WALK')=='1':
        x=z.read8(S('_players')+i*16+3)
        if x>=36:z.release('P');z.press('O')
        elif x<=2:z.release('O');z.press('P')
    x=z.read8(S('_players')+i*16+3)
    if x!=origin[i] and first_move[i] is None:first_move[i]=(z.now()-started[i])/3500
    if ((i==0 and x>=36) or (i==1 and x<=2)) and crossing[i] is None:crossing[i]=(z.now()-started[i])/3500000
    name = 'AB'[i]
    f = z.read16(FN)
    if f >= HP: seen[name][(f // HP) * HP] = z.read16(SH).to_bytes(2, 'little').hex()
def step_a():                                      # A's own quanta, to time its frames
    pc = a.step()
    if pc == SYNC: last['sync'] = a.now()
    elif pc == COMP:
        if 'comp' in last and 'sync' in last: work.append((last['sync'] - last['comp']) / 3500.0)
        last['comp'] = a.now()
    elif pc == F: on_flush(0, a); return 1
    return 0
done_a = done_b = 0
end = a.now() + 120 * 3500000
while (done_a < N or done_b < N) and a.now() < end:
    if 'link' not in globals() or link.now(0)<=link.now(1):done_a += step_a()
    else:
        if b.step()==F:on_flush(1,b);done_b+=1
        continue
    if 'link' not in globals():
        if b.step()==F:on_flush(1,b);done_b+=1
print('first movement ms',first_move,'crossing seconds',crossing,flush=True)
print(f'frames: A {a.read16(FN)} B {b.read16(FN)} abort {a.read8(NAB)}/{b.read8(NAB)}', flush=True)
common = sorted(set(seen['A']) & set(seen['B']))
mism = [f for f in common if seen['A'][f] != seen['B'][f]]
for f in common: print(f'frame {f}: A {seen["A"][f]} B {seen["B"][f]} {"MISMATCH" if f in mism else "ok"}')
aborts = (a.read8(NAB), b.read8(NAB))
if work: print('A: frame time incl. waiting for the network ms avg %.1f (not a CPU figure: the emulator runs faster than real time)' % (sum(work) / len(work)))
print(f'done: {N} steps, {len(common)} hashed frames compared, {len(mism)} mismatches; abort flags {aborts}; '
      f'{len(a.uart.commands) + len(b.uart.commands)} ESP commands; {time.time() - t0:.0f} s')
a.screenshot('build/esp01-128/net_A.png'); b.screenshot('build/esp01-128/net_B.png')
for name, z in (('A', a), ('B', b)):
    m = z.read(z.allocation_end, 0xff00 - z.allocation_end)
    used_from = next((z.allocation_end + i for i, v in enumerate(m) if v != 0xa5), 0xff00)
    print(f'{name}: stack reached {used_from:04x}, {used_from - z.allocation_end} bytes above the program left unused')
if os.environ.get('ESP_TEST_RESTART')=='1' and not mism and not any(aborts):
    for z in (a,b):
        z.release('P');z.release('O');z.release('SPACE');z.poke(NAB,[4])
    frames(45,a,b)  # message deliberately ignores FIRE for its first 40 frames
    for z in (a,b):z.press('SPACE')
    frames(4,a,b)
    for z in (a,b):z.release('SPACE')
    frames(8,a,b)
    assert all(z.read8(TM)==1 and z.read8(NA)==0 for z in (a,b)), 'network abort did not return to title'
    for z in (a,b):z.poke(MN,[0]);z.press('SPACE')
    frames(4,a,b)
    for z in (a,b):z.release('SPACE')
    frames(8,a,b)
    for z in (a,b):
        assert z.read8(TM)==0 and z.read8(NA)==0 and z.read8(NAB)==0
        assert z.read8(S('_hash_period'))==0
        z.poke(S('_map_layer'),bytes([32])*1000);z.poke(S('_players')+3,[5,5]);z.press('P')
    frames(12,a,b)
    assert all(z.read8(S('_players')+3)>5 and z.read8(NAB)==0 for z in (a,b))
    print('PASS: real online match -> injected DESYNC -> PRESS FIRE -> title -> OFF -> offline movement on both clients',flush=True)
sys.exit(1 if mism or any(aborts) or not common else 0)
