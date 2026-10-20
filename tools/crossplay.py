#!/usr/bin/env python3
"""Two different machines in one network match: a Sharp MZ-800, a ZX
Spectrum, an Amstrad CPC and a Commodore 64, any two of them.

The MZ runs in mz800emu with its MZPico card emulated (JSON-lines TCP to the
relay), the Spectrum in tools/zxemu.py with a Spectranet and the CPC in
tools/cpcemu.py with an M4 board (both WebSocket to the relay). They go
through the same relay and meet in one room; the driver advances them side
by side, one TV frame each in turn, and compares their state hashes. The
C64 runs in VICE with a WiC64 (tools/vice.py), in real time on its own: its
TV frame is 20 ms of waiting, and it stands still while it is read.

  tools/crossplay.py [steps] [host] [joiner]   machines: mz, zx, cpc, esp, c64; default mz zx
                                               (esp: build/zx128, an eLeMeNt with its ESP-01)
                                               (the second argument alone: mz or zx hosts the other)
  PLAYERS=3 LOCAL_HOST=2 LOCAL_JOIN=1   seats: players in total, local players per machine
Needs the local relay (relay/relay.py) on 8765 (WebSocket) and 8766 (TCP),
and the MZ build (build/c) and the Spectrum build (build/zx).
"""
import base64, os, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
os.environ.setdefault('MZ800EMU_CFG', os.path.join(HERE, '..', 'build', 'emucfg_net'))
from emu import Emu, sym_from_map
from zxemu import ZX
from cpcemu import CPC
from vice import Vice
import set_relay

N = int(sys.argv[1]) if len(sys.argv) > 1 else 80
HOST = sys.argv[2] if len(sys.argv) > 2 else 'mz'
JOIN = sys.argv[3] if len(sys.argv) > 3 else ('zx' if HOST == 'mz' else 'mz')
HP = 16
TAP = 24         # TV frames a key is held or released: the Spectrum's title runs at 6 TV frames a frame
NAMES = ('_flush_screen', '_title_mode', '_state_hash', '_frame_no', '_menu_net', '_menu_mode', '_menu_players',
         '_menu_local', '_net_code', '_net_active', '_net_abort', '_net_slot', '_net_delay', '_net_table',
         '_map_layer', '_players')


class MZ:
    """mz800emu, driven one TV frame at a time."""
    kind = 'MZ'
    keys = {'fire': 'SPACE', 'walk': 'LEFT'}

    def __init__(self):
        self.S = {n: sym_from_map('build/c/bomber.map', n) for n in NAMES}
        self.e = Emu(); self.e.load_mzf('build/c/bomber.mzf')
        self.e.run_until(self.S['_flush_screen'])       # the program is running

    def rd(self, n, k=1): return base64.b64decode(self.e.mem(self.S[n], k)['data_b64'])
    def r8(self, n): return self.rd(n)[0]
    def r16(self, n): b = self.rd(n, 2); return b[0] | b[1] << 8
    def wr(self, n, bs): self.e.call('mem_write', {'addr': self.S[n], 'data_hex': bytes(bs).hex()})
    def tv_frame(self): self.e.data('run', {'frames': 1})
    def press(self, k): self.e.press(self.keys.get(k, k))
    def release(self, k): self.e.release(self.keys.get(k, k))
    def close(self): self.e.close()


class Spectrum:
    """tools/zxemu.py with a Spectranet, driven one TV frame at a time."""
    kind = 'ZX'
    keys = {'fire': 'SPACE', 'walk': 'P'}

    build = 'build/zx'

    def __init__(self):
        self.S = {n: sym_from_map(self.build + '/bomber.map', n) for n in NAMES}
        if self.kind == 'ESP':
            self.z = ZX(esp01=True, bank6=self.build + '/bank6', relay=('127.0.0.1', 8765))
        else:
            self.z = ZX(spectranet=True, relay=('127.0.0.1', 8765))
        self.z.load(self.build + '/bomber')
        self.z.set_breakpoint(self.S['_flush_screen'])
        self.flushes = []                                # (frame_no, state_hash) at each flush

    # the game's variables are in page 0 (a 128K may stand in bank 6 at a frame's end)
    def r8(self, n): return self.z.read(self.S[n], 1, page=0)[0]
    def r16(self, n): return int.from_bytes(self.z.read(self.S[n], 2, page=0), 'little')
    def rd(self, n, k=1): return self.z.read(self.S[n], k, page=0)
    def wr(self, n, bs): self.z.poke(self.S[n], bytes(bs), page=0)
    def tv_frame(self):
        start = self.z.frame_count
        while self.z.frame_count == start:
            if self.z.step() == self.S['_flush_screen']:
                self.flushes.append((self.r16('_frame_no'), self.r16('_state_hash')))
    def press(self, k): self.z.press(self.keys.get(k, k))
    def release(self, k): self.z.release(self.keys.get(k, k))
    def close(self): pass


class Element(Spectrum):
    """The 128K build with its ESP-01 driver, on an emulated eLeMeNt ZX."""
    kind = 'ESP'
    build = 'build/zx128'


class Amstrad:
    """tools/cpcemu.py with an M4 board, driven one TV frame at a time."""
    kind = 'CPC'
    keys = {'fire': 'SPACE', 'walk': 'RIGHT'}

    def __init__(self):
        self.S = {n: sym_from_map('build/cpc/bomber.map', n) for n in NAMES}
        self.c = CPC(m4=True, relay=('127.0.0.1', 8765)); self.c.load('build/cpc/bomber.cpc')
        self.c.set_breakpoint(self.S['_flush_screen'])
        self.flushes = []

    def r8(self, n): return self.c.read8(self.S[n])
    def r16(self, n): return self.c.read16(self.S[n])
    def rd(self, n, k=1): return self.c.read(self.S[n], k)
    def wr(self, n, bs): self.c.poke(self.S[n], bytes(bs))
    def tv_frame(self):
        start = self.c.frame_count
        while self.c.frame_count == start:
            if self.c.step() == self.S['_flush_screen']:
                self.flushes.append((self.r16('_frame_no'), self.r16('_state_hash')))
    def press(self, k): self.c.press(self.keys.get(k, k))
    def release(self, k): self.c.release(self.keys.get(k, k))
    def close(self): pass


class Commodore:
    """VICE with a WiC64 (tools/vice.py), running in real time; a copy of
    build/c64/bomber.prg pointed at the local relay (tools/set_relay.py)."""
    kind = 'C64'
    keys = {'fire': 'SPACE', 'walk': 'CRSR_RIGHT'}

    def __init__(self):
        build = os.environ.get('C64_BUILD', 'build/c64')
        out = build + '/net'; os.makedirs(out, exist_ok=True)
        for ext in ('', '.elf'):
            open(f'{out}/bomber.prg{ext}', 'wb').write(open(f'{build}/bomber.prg{ext}', 'rb').read())
        set_relay.main([f'{out}/bomber.prg', '127.0.0.1:8765'])
        self.v = Vice(f'{out}/bomber.prg', wic64=True, log=f'{out}/vice.log')
        self.v.run()

    def rd(self, n, k=1): return self.v.mem(n.lstrip('_'), k)
    def r8(self, n): return self.rd(n)[0]
    def r16(self, n): return int.from_bytes(self.rd(n, 2), 'little')
    def wr(self, n, bs): self.v.poke(n.lstrip('_'), bytes(bs))
    def tv_frame(self): self.v.run(0.02)
    def press(self, k): self.v.key(self.keys.get(k, k), 1)
    def release(self, k): self.v.key(self.keys.get(k, k), 0)
    def close(self): self.v.close()

    def reset_net(self):
        """the counters from the game's start on (a C64_NETTIME build)"""
        if 'c64_frames' in self.v.sym:
            self.v.poke('c64_frames', bytes(12)); self.f0 = self.r16('_frame_no')
            self.c0 = int.from_bytes(self.v.mem('eng_cmds', 4), 'little') if 'eng_cmds' in self.v.sym else 0

    def report(self, frames):
        """the WiC64's commands and the frames' time (a C64_NETTIME build): per game frame"""
        if 'c64_frames' not in self.v.sym: return
        frames -= self.f0
        c = int.from_bytes(self.v.mem('eng_cmds', 4), 'little') if 'eng_cmds' in self.v.sym else 0
        cmds, flags = (c & 0xffff) - (self.c0 & 0xffff), (c >> 16) - (self.c0 >> 16)
        d = self.v.mem('c64_frames', 12)
        busy, idle = int.from_bytes(d[0:4], 'little'), int.from_bytes(d[4:8], 'little')
        n, late = d[8] | d[9] << 8, d[10] | d[11] << 8
        print(f'C64 WiC64 ({frames} frames): {cmds / frames:.2f} commands, {flags / frames:.1f} FLAG2 NMIs '
              f'a frame (background driver); frames: {busy / n / 985.248:.1f} ms of work, {idle / n / 985.248:.1f} ms idle; {late} of {n} late',
              flush=True)


def run(n, *ms):
    for _ in range(n):
        for m in ms: m.tv_frame()


def until(cond, limit, *ms):
    for _ in range(limit):
        if cond(): return True
        run(1, *ms)
    return cond()


t0 = time.time()
MACHINES = {'mz': MZ, 'zx': Spectrum, 'cpc': Amstrad, 'esp': Element, 'c64': Commodore}
host, join = MACHINES[HOST](), MACHINES[JOIN]()
both = (host, join)
run(90, *both)                                                    # both titles up and settled
PLAYERS, LOCAL_H, LOCAL_J = int(os.environ.get('PLAYERS', 2)), int(os.environ.get('LOCAL_HOST', 1)), int(os.environ.get('LOCAL_JOIN', 1))
host.wr('_menu_mode', [1]); host.wr('_menu_players', [PLAYERS]); host.wr('_menu_net', [1]); host.wr('_menu_local', [LOCAL_H])
join.wr('_menu_net', [2]); join.wr('_menu_local', [LOCAL_J])
run(3, *both)
before = host.rd('_net_code', 4)
host.press('fire'); run(TAP, *both); host.release('fire')             # host: create the room
if not until(lambda: host.rd('_net_code', 4) != before, 1500, *both): sys.exit('no room code')
code = host.rd('_net_code', 4)
print(f'{host.kind} hosts room {code.decode()}, {join.kind} joins', flush=True)
join.wr('_net_code', code)
join.press('fire'); run(TAP, *both); join.release('fire'); run(TAP, *both)   # code screen
join.press('fire'); run(TAP, *both); join.release('fire')                    # join
joined = (lambda: join.r8('_s_state') >= 2) if join.kind == 'C64' else (lambda: join.r8('_net_slot') == 1)   # C64: LTO keeps no net_slot
if not until(joined, 1500, *both):
    if hasattr(join, 'z'): join.z.screenshot('build/cross_join.png')
    sys.exit(f'join failed: title_mode {join.r8("_title_mode")} menu_net {join.r8("_menu_net")} code {join.rd("_net_code", 4)}')
run(120, *both)                                                       # lobby: delay measured, seat table
join.press('fire'); run(TAP, *both); join.release('fire')             # ready, joiner first
host.press('fire'); run(TAP, *both); host.release('fire')             # the host goes out last
if not until(lambda: host.r8('_title_mode') == 0 and join.r8('_title_mode') == 0, 1500, *both):
    sys.exit(f'did not start: title_mode {host.kind} {host.r8("_title_mode")} {join.kind} {join.r8("_title_mode")}')
print(f'in game: net_active {host.kind} {host.r8("_net_active")} {join.kind} {join.r8("_net_active")}, delay {host.kind} {host.r8("_net_delay")} '
      f'{join.kind} {join.r8("_net_delay")}, seats {host.kind} {host.rd("_net_table", 4).hex()} {join.kind} {join.rd("_net_table", 4).hex()}', flush=True)
for m in both:
    if m.kind == 'C64': m.reset_net()
host.press('walk'); join.press('walk')                                # inputs cross between the machines
seen = {m.kind: {} for m in both}
start = [m.r16('_frame_no') for m in both]


def collect(m):
    """The hash of each 16-frame bucket: from the flushes the machine recorded,
    or (the MZ, read between TV frames) from its current frame."""
    if hasattr(m, 'flushes'):
        for f, h in m.flushes:
            if f % HP: seen[m.kind][(f // HP) * HP] = h
        m.flushes.clear()
    else:
        f = m.r16('_frame_no')
        if f % HP: seen[m.kind][(f // HP) * HP] = m.r16('_state_hash')


while min(m.r16('_frame_no') - s0 for m, s0 in zip(both, start)) < N:
    run(1, *both)
    for m in both: collect(m)
    if host.r8('_net_abort') or join.r8('_net_abort'): break
H, J = host.kind, join.kind
common = sorted(set(seen[H]) & set(seen[J]) - {0})
mism = [f for f in common if seen[H][f] != seen[J][f]]
for f in common: print(f'frame {f}: {H} {seen[H][f]:04x} {J} {seen[J][f]:04x} {"MISMATCH" if f in mism else "ok"}')
aborts = (host.r8('_net_abort'), join.r8('_net_abort'))
print(f'done: frames {H} {host.r16("_frame_no")} {J} {join.r16("_frame_no")}, {len(common)} hashed frames compared, '
      f'{len(mism)} mismatches; abort flags {H}/{J} {aborts}; {time.time() - t0:.0f} s')
fh, fj = host.rd('_map_layer', 1000), join.rd('_map_layer', 1000)
print(f'fields identical: {fh == fj} (read at frames {H} {host.r16("_frame_no")}, {J} {join.r16("_frame_no")})')
for m in both:
    if m.kind == 'C64': m.report(m.r16('_frame_no'))
for m in both:
    if hasattr(m, 'z'): m.z.screenshot(f'build/cross_{m.kind.lower()}.png')
    elif m.kind == 'C64': m.v.screenshot('build/cross_c64.png')
    elif m.kind == 'CPC': m.c.screenshot('build/cross_cpc.png')
    else: m.e.screenshot('build/cross_mz.png')
for m in both: m.close()
sys.exit(1 if mism or any(aborts) or not common else 0)
