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

  tools/crossplay.py [steps] [host] [joiner]   machines: mz, zx, cpc, esp, c64, amiga, st; default mz zx
                                               (esp: build/zx128, an eLeMeNt with its ESP-01)
                                               (the second argument alone: mz or zx hosts the other)
  PLAYERS=3 LOCAL_HOST=2 LOCAL_JOIN=1   seats: players in total, local players per machine
  MODE=0                                co-op (default 1, deathmatch)
  GAP=100                               the C64 stops polling the network for 5 s at game frame 100
  C64_NET=uci                           the C64 on an Ultimate's network (else a WiC64)
Needs the local relay (relay/relay.py) on 8765 (WebSocket) and 8766 (TCP),
and the MZ build (build/c) and the Spectrum build (build/zx).
"""
import base64, os, subprocess, sys, time
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
        uci = os.environ.get('C64_NET') == 'uci'                    # the Ultimate's network, else a WiC64
        self.v = Vice(f'{out}/bomber.prg', wic64=not uci, uci=uci, log=f'{out}/vice.log')
        self.v.run()

    def rd(self, n, k=1): return self.v.mem(n.lstrip('_'), k)
    def r8(self, n): return self.rd(n)[0]
    def r16(self, n): return int.from_bytes(self.rd(n, 2), 'little')
    def wr(self, n, bs): self.v.poke(n.lstrip('_'), bytes(bs))
    def tv_frame(self): self.v.run(0.02)
    def press(self, k): self.v.key(self.keys.get(k, k), 1)
    def release(self, k): self.v.key(self.keys.get(k, k), 0)
    def close(self): self.v.close()

    def gap(self, secs, *others):
        """the game stops polling the network for secs (as between stages).
        A C64_NETTIME build waits in its next frame sync (c64_gap, poked:
        no PC tricks through the monitor, which can stop the CPU inside an
        interrupt handler); the others run on, their input lines arrive
        meanwhile."""
        if 'c64_gap' not in self.v.sym: sys.exit('GAP needs a C64_NETTIME build (C64_BUILD=build/c64nt)')
        self.v.poke('c64_gap', [min(250, secs * 50)]); self.v.run()
        t = time.time()
        while time.time() - t < secs + 1: run(1, *others)

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


for _n, _f in (('AMIGA_KICKSTART', 'aros-rom.bin'), ('AMIGA_KICKSTART_EXT', 'aros-ext.bin')):   # the AROS nightly ROM in build/amiga/rom
    if os.path.exists(f'{HERE}/../build/amiga/rom/{_f}'): os.environ.setdefault(_n, f'{HERE}/../build/amiga/rom/{_f}')   # (FS-UAE's bundled one crashes on a 68000)
class Amiga:
    """FS-UAE with its bsdsocket.library (the host's network), running in real
    time: a tools/build_amiga.py --net host|join build (the menu preset; a
    joiner takes the room code from DH0:code.txt), pointed at the local
    relay (tools/set_relay.py), its keys by xdotool, its state read from the
    log it writes (DH0:net.log) instead of its memory."""
    kind = 'AMIGA'
    keys = {'fire': 'space', 'walk': 'Right'}

    def __init__(self):
        import shutil
        role = 'host' if HOST == 'amiga' else 'join'
        out = os.path.abspath(f'build/amiga/net_{role}')
        env = dict(os.environ, PLAYERS=os.environ.get('PLAYERS', '2'),
                   LOCAL=os.environ.get('LOCAL_HOST' if role == 'host' else 'LOCAL_JOIN', '1'))
        env['CFLAGS_AMIGA'] = os.environ.get('CFLAGS_AMIGA', '')
        r = subprocess.run([sys.executable, 'tools/build_amiga.py', '--net', role, '--out', out], capture_output=True, text=True, env=env)
        if r.returncode: sys.exit('Amiga build failed: ' + r.stderr[-2000:])
        set_relay.main([f'{out}/bomber', '127.0.0.1:8765'])
        self.dh0 = f'{out}/dh0'
        for d in ('C', 'S'): os.makedirs(f'{self.dh0}/{d}', exist_ok=True)
        shutil.copy(f'{out}/bomber', f'{self.dh0}/C/bomber')
        open(f'{self.dh0}/S/Startup-Sequence', 'w').write('C:bomber\n')
        for f in ('net.log', 'code.txt'):
            if os.path.exists(f'{self.dh0}/{f}'): os.remove(f'{self.dh0}/{f}')
        self.env = dict(os.environ, DISPLAY=os.environ.get('AMIGA_DISPLAY', ':97'))   # the invisible display, never the desktop's
        self.p = subprocess.Popen(['fs-uae', f'--base-dir={os.path.abspath("build/amiga/fsuae-base")}', f'--amiga-model={os.environ.get("AMIGA_MODEL", "A500")}', '--chip-memory=1024',
                                   '--fast-memory=8192', f'--hard-drive-0={self.dh0}', '--bsdsocket-library=1', '--fullscreen=0',
                                   '--video-sync=0', '--automatic-input-grab=0', '--joystick-port-1=none', '--window-width=640',
                                   '--window-height=400']
                                  + ([f'--kickstart-file={os.environ["AMIGA_KICKSTART"]}'] if os.environ.get('AMIGA_KICKSTART') else [])
                                  + ([f'--kickstart-ext-file={os.environ["AMIGA_KICKSTART_EXT"]}'] if os.environ.get('AMIGA_KICKSTART_EXT') else []),
                                  stdout=open(f'{out}/fsuae.log', 'w'), stderr=subprocess.STDOUT, env=self.env)
        t = time.time()
        while time.time() - t < 90:                              # until the program logs (the title screen)
            time.sleep(1)
            if os.path.exists(f'{self.dh0}/net.log') and os.path.getsize(f'{self.dh0}/net.log') > 0: break
        else: sys.exit(f'the Amiga did not start its program in 90 s (see {out}/fsuae.log)')
        time.sleep(1)
        wid = subprocess.run(['xdotool', 'search', '--name', 'FS-UAE'], capture_output=True, text=True, env=self.env).stdout.split()
        if wid: subprocess.run(['xdotool', 'windowfocus', '--sync', wid[-1]], env=self.env, capture_output=True)
        self.last, self.cached = {}, None

    def _state(self):
        """the last complete line of the log: its tail only (the log grows by 2 KB a frame), once a TV frame"""
        if self.cached is not None: return self.cached
        try:
            with open(f'{self.dh0}/net.log', 'rb') as f:
                f.seek(0, 2); n = f.tell(); f.seek(max(0, n - 8192)); lines = f.read().decode('latin-1').split('\n')
        except FileNotFoundError: lines = []
        for l in reversed(lines):
            p = l.split()
            if len(p) >= 20 and p[0] == 'frame' and (len(p[19]) == 2000 or p[19] == 'none'):
                self.last = dict(zip(p[0:20:2], p[1:20:2])); break
        self.cached = self.last
        return self.last

    def rd(self, n, k=1):
        st = self._state()
        if n == '_net_code': return st.get('code', '    ').encode()
        if n == '_net_table': return bytes.fromhex(st.get('table', 'ffffffff'))
        if n == '_map_layer': return bytes.fromhex(st['map']) if st.get('map', 'none') != 'none' else None
        return self.r16(n).to_bytes(2, 'little')[:k]
    def r8(self, n): return self.r16(n) & 0xff
    def r16(self, n):
        st = self._state()
        if n == '_state_hash': return int(st.get('hash', '0'), 16)
        return int(st.get(n.lstrip('_').replace('net_', '').replace('title_mode', 'title').replace('frame_no', 'frame'), '0'))
    def wr(self, n, bs):
        if n == '_net_code': open(f'{self.dh0}/code.txt', 'wb').write(bytes(bs))    # the rest is the build's preset
    def tv_frame(self): time.sleep(0.02); self.cached = None
    def press(self, k): subprocess.run(['xdotool', 'keydown', self.keys.get(k, k)], env=self.env)
    def release(self, k): subprocess.run(['xdotool', 'keyup', self.keys.get(k, k)], env=self.env)
    def close(self): self.p.kill()


class AtariST:
    """Hatari with a WiFi modem emulated on its serial port (tools/st_modem.py),
    running in real time: a tools/build_st.py --net host|join build (the menu
    preset; a joiner takes the room code from C:\\CODE.TXT), its keys as scan
    codes through Hatari's command FIFO, its state read from the log it
    writes (C:\\NET.LOG, written out every 32 frames: the state read may be
    a second or two old, which only makes the harness wait)."""
    kind = 'ST'
    keys = {'fire': 0x39, 'walk': 0x4d}

    def __init__(self):
        import st_match
        self.m = st_match.ST('host' if HOST == 'st' else 'join', nomap=False)
        self.last, self.cached = {}, None
        self.flushes, self.read_to = [], 0       # (frame, hash) of every game frame logged: collect() takes them

    def _new_lines(self):
        try:
            with open(f'{self.m.hd}/NET.LOG', 'rb') as f:
                f.seek(self.read_to); data = f.read()
        except FileNotFoundError: return
        end = data.rfind(b'\n') + 1
        self.read_to += end
        for l in data[:end].decode('latin-1').split('\n'):
            p = l.split()
            if len(p) >= 20 and p[0] == 'frame' and p[3] == '0': self.flushes.append((int(p[1]), int(p[17], 16)))

    def _state(self):
        if self.cached is None: self.cached = self.m.state() or self.last
        self.last = self.cached
        return self.cached

    def rd(self, n, k=1):
        st = self._state()
        if n == '_net_code': return st.get('code', '    ').encode()
        if n == '_net_table': return bytes.fromhex(st.get('table', 'ffffffff'))
        if n == '_map_layer':                      # the newest line with a map (the hashed frames' lines carry one)
            for l in reversed(self.m.lines()):
                p = l.split()
                if len(p) >= 20 and p[0] == 'frame' and len(p[19]) == 2000: return bytes.fromhex(p[19])
            return None
        return self.r16(n).to_bytes(2, 'little')[:k]
    def r8(self, n): return self.r16(n) & 0xff
    def r16(self, n):
        st = self._state()
        if n == '_state_hash': return int(st.get('hash', '0'), 16)
        return int(st.get(n.lstrip('_').replace('net_', '').replace('title_mode', 'title').replace('frame_no', 'frame'), '0'))
    def wr(self, n, bs):
        if n == '_net_code': open(f'{self.m.hd}/CODE.TXT', 'wb').write(bytes(bs))   # the rest is the build's preset
    def tv_frame(self): time.sleep(0.02); self.cached = None; self._new_lines()
    def press(self, k): self.m.key(self.keys.get(k, k), True)
    def release(self, k): self.m.key(self.keys.get(k, k), False)
    def close(self): self.m.close()


def run(n, *ms):
    for _ in range(n):
        for m in ms: m.tv_frame()


def until(cond, limit, *ms):
    for _ in range(limit):
        if cond(): return True
        run(1, *ms)
    return cond()


t0 = time.time()
MACHINES = {'mz': MZ, 'zx': Spectrum, 'cpc': Amstrad, 'esp': Element, 'c64': Commodore, 'amiga': Amiga, 'st': AtariST}
host, join = MACHINES[HOST](), MACHINES[JOIN]()
both = (host, join)
run(90, *both)                                                    # both titles up and settled
PLAYERS, LOCAL_H, LOCAL_J = int(os.environ.get('PLAYERS', 2)), int(os.environ.get('LOCAL_HOST', 1)), int(os.environ.get('LOCAL_JOIN', 1))
host.wr('_menu_mode', [int(os.environ.get('MODE', 1))]); host.wr('_menu_players', [PLAYERS]); host.wr('_menu_net', [1]); host.wr('_menu_local', [LOCAL_H])
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
started = lambda: host.r8('_title_mode') == 0 and join.r8('_title_mode') == 0
for _ in range(12):                                                   # ready, the joiner first, the host last:
    join.press('fire'); run(TAP, *both); join.release('fire')         # tapped again until both are in the game (a
    host.press('fire'); run(TAP, *both); host.release('fire')         # host's tap counts only once it saw the joiner ready)
    if until(started, 120, *both): break
if not started():
    sys.exit(f'did not start: title_mode {host.kind} {host.r8("_title_mode")} {join.kind} {join.r8("_title_mode")}')
print(f'in game: net_active {host.kind} {host.r8("_net_active")} {join.kind} {join.r8("_net_active")}, delay {host.kind} {host.r8("_net_delay")} '
      f'{join.kind} {join.r8("_net_delay")}, seats {host.kind} {host.rd("_net_table", 4).hex()} {join.kind} {join.rd("_net_table", 4).hex()}', flush=True)
for m in both:
    if m.kind == 'C64': m.reset_net()
if not os.environ.get('NOWALK'): host.press('walk'); join.press('walk')   # inputs cross between the machines
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


GAP = int(os.environ.get('GAP', 0))       # GAP=n: at game frame n the C64 stops polling for 5 s
while min(m.r16('_frame_no') - s0 for m, s0 in zip(both, start)) < N:
    run(1, *both)
    for m in both:
        if GAP and m.kind == 'C64' and m.r16('_frame_no') >= GAP:
            print(f'C64 at frame {m.r16("_frame_no")}: 5 s without polling', flush=True)
            m.gap(5, *[o for o in both if o is not m]); GAP = 0
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
    if m.kind == 'C64' and os.environ.get('C64_DUMP'):
        print('C64 at the end:', m.v.cmd('r').split('.;')[1].split('\n')[0], '| engine', m.v.mem('eng_state', 20).hex(' '),
              '| frame_no', m.r16('_frame_no'), '| title_mode', m.r8('_title_mode'), flush=True)
for m in both:
    if hasattr(m, 'z'): m.z.screenshot(f'build/cross_{m.kind.lower()}.png')
    elif m.kind == 'C64': m.v.screenshot('build/cross_c64.png')
    elif m.kind == 'AMIGA': subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'x11grab', '-video_size', '1024x768', '-i',
                                            m.env['DISPLAY'], '-frames:v', '1', 'build/cross_amiga.png'], env=m.env)
    elif m.kind == 'ST':
        import st_match
        subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'x11grab', '-video_size', '1024x768', '-i', st_match.DISPLAY,
                        '-frames:v', '1', 'build/cross_st.png'], env=dict(os.environ, DISPLAY=st_match.DISPLAY))
    elif m.kind == 'CPC': m.c.screenshot('build/cross_cpc.png')
    else: m.e.screenshot('build/cross_mz.png')
for m in both: m.close()
sys.exit(1 if mism or any(aborts) or not common else 0)
