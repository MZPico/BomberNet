#!/usr/bin/env python3
"""Two C64s with WiC64s (VICE, tools/vice.py) play a network match through
the local relay, both in real time, untouched while they play: the frames'
time with the network, for a C64_NETTIME build (tools/build_c64.py).

  tools/c64_match.py [seconds] [build]     default 30 s, build/c64nt
  WIC64_FW_TIMING=1 ...                    the WiC64 answers after the real
                                           firmware's pauses (the VICE patch,
                                           tools/c64/vice-3.9-bombernet.patch)
  PLAYERS=4 LOCAL_HOST=2 LOCAL_JOIN=2 ...  seats, as in tools/crossplay.py
  C64_NET=uci ...                          an Ultimate's network (else a WiC64)
  RELAY=real ...                           the production relay (else the local one)

The figures are each machine's: game frames a second, the frames' work
(from one frame sync to the next, the waits for the network included), the
WiC64 commands and FLAG2 NMIs a frame (the background driver)."""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vice import Vice
import set_relay

SECS = int(sys.argv[1]) if len(sys.argv) > 1 else 30
BUILD = sys.argv[2] if len(sys.argv) > 2 else 'build/c64nt'
PLAYERS, LOCAL_H, LOCAL_J = int(os.environ.get('PLAYERS', 2)), int(os.environ.get('LOCAL_HOST', 1)), int(os.environ.get('LOCAL_JOIN', 1))
out = BUILD + '/net'; os.makedirs(out, exist_ok=True)
for ext in ('', '.elf'):
    open(f'{out}/bomber.prg{ext}', 'wb').write(open(f'{BUILD}/bomber.prg{ext}', 'rb').read())
if os.environ.get('RELAY') != 'real': set_relay.main([f'{out}/bomber.prg', '127.0.0.1:8765'])   # RELAY=real: production
UCI = os.environ.get('C64_NET') == 'uci'          # C64_NET=uci: the Ultimate's network, else a WiC64
host = Vice(f'{out}/bomber.prg', wic64=not UCI, uci=UCI, port=6510)
join = Vice(f'{out}/bomber.prg', wic64=not UCI, uci=UCI, port=6511)
both = (host, join)
try:
    for v in both: v.run()
    time.sleep(4)
    host.poke('menu_mode', [1]); host.poke('menu_players', [PLAYERS]); host.poke('menu_net', [1]); host.poke('menu_local', [LOCAL_H])
    join.poke('menu_net', [2]); join.poke('menu_local', [LOCAL_J])
    before = host.mem('net_code', 4)

    def tap(v):
        v.key('SPACE', 1); v.run(0.5); v.key('SPACE', 0); v.run(0.5)

    join.run(); tap(host)
    for _ in range(40):
        if host.mem('net_code', 4) != before: break
        host.run(0.5)
    code = host.mem('net_code', 4); host.run()
    print(f'host room {code.decode()}', flush=True)
    join.poke('net_code', code); tap(join); join.run(0.5); tap(join)
    for _ in range(40):
        if join.u8('s_state') >= 2: break
        join.run(0.5)
    join.run(); time.sleep(4)
    tap(join); tap(host)                     # ready: the joiner, then the host
    for _ in range(60):
        if host.u8('title_mode') == 0 and join.u8('title_mode') == 0: break
        host.run(); join.run(0.5)
    if host.u8('title_mode') or join.u8('title_mode'): sys.exit('the match did not start')
    print(f'input delay: host {host.u8("net_delay")} frames ({host.u8("net_delay") * 60} ms), join {join.u8("net_delay")}', flush=True)
    host.key('CRSR_RIGHT', 1); join.key('CRSR_RIGHT', 1)
    f0 = []
    for v in both:
        v.poke('c64_frames', bytes(12))
        f0.append((v.u16('frame_no'), int.from_bytes(v.mem('eng_cmds', 4), 'little') if 'eng_cmds' in v.sym else 0))
    for v in both: v.run()
    t0 = time.time(); time.sleep(SECS); secs = time.time() - t0
    for name, v, (fr, c) in zip(('host', 'join'), both, f0):
        frames = v.u16('frame_no') - fr
        c1 = int.from_bytes(v.mem('eng_cmds', 4), 'little') if 'eng_cmds' in v.sym else 0
        cmds, flags = (c1 & 0xffff) - (c & 0xffff), (c1 >> 16) - (c >> 16)
        d = v.mem('c64_frames', 12)
        busy, idle = int.from_bytes(d[0:4], 'little'), int.from_bytes(d[4:8], 'little')
        n, late = d[8] | d[9] << 8, d[10] | d[11] << 8
        print(f'{name}: {frames} game frames in about {secs:.0f} s ({frames / secs:.1f} a second, 16.7 at full speed); '
              f'work {busy / n / 985.248:.1f} ms, idle {idle / n / 985.248:.1f} ms a frame, {late} of {n} late; '
              f'{cmds / max(frames, 1):.2f} WiC64 commands, {flags / max(frames, 1):.0f} FLAG2 NMIs a frame; '
              f'abort {v.u8("net_abort")}', flush=True)
    print('state hashes', host.mem('state_hash', 2).hex(), join.mem('state_hash', 2).hex(),
          'at frames', host.u16('frame_no'), join.u16('frame_no'))
finally:
    for v in both: v.close()
