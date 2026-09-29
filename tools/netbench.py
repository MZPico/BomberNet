#!/usr/bin/env python3
"""Network timing of a two-machine match, every machine in real time.

  tools/netbench.py HOST JOIN [runs]      HOST, JOIN: mz, mz011 or zx
      mz     MZ-800, current build (build/c), mz800emu with its MZPico card
      mz011  MZ-800, release 0.1.1 (git worktree build/v011, build/v011/build/c)
      zx     ZX Spectrum 48K, current build (build/zx), FuseX with a Spectranet
  tools/netbench.py HOST JOIN [runs] --prod   both local, through the production relay,
      each through its own logging proxy (tools/netproxy.py)
  tools/netbench.py HOST web [runs]       HOST plays the MZ-800 on the mzpico.com
      play page (Chromium on Xvfb :97, tools/web_join.mjs) through the
      production relay; a logging proxy next to HOST (tools/netproxy.py) times
      the messages. The host's round trip: its PING leaves -> the web
      player's PONG comes back (internet both ways, the relay, the browser).

Every emulator runs freely at real speed: mz800emu with MZ800EMU_REALTIME=1
(a local patch: headless it has no audio clock to pace it), FuseX by itself.
The driver waits by watching the relay's log, never by stopping a machine
while something is measured; a FuseX machine is stopped only for its key
presses (at its keyboard routine), which the windows below leave out.

Measured at the local relay (relay/relay.py started with RELAY_TIMING=<file>,
RELAY_TIMING_FILE here, default build/relay_timing.jsonl), so every machine
and version is measured the same way:
  lobby  the joiner's turnaround: the host's PING arrives -> the joiner's
         PONG arrives. That is the joiner's link both ways plus the time until
         its lobby loop polls, handles and answers. And the input delay the
         host chose from its own round trips (read from both after the game).
  game   the interval between a machine's input lines (a frame is 60 ms; a
         longer interval means it waited for the other one or was busy).
"""
import base64, json, os, re, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
os.environ.setdefault('MZ800EMU_CFG', os.path.join(HERE, '..', 'build', 'emucfg_net'))
os.environ['MZ800EMU_REALTIME'] = '1'
from emu import Emu, sym_from_map, CPU_HZ
from fusex import FuseX, make_fusex_home, forward
from netbench_analyse import stats

ROOT = os.path.join(HERE, '..')
LOG = os.environ.get('RELAY_TIMING_FILE', os.path.join(ROOT, 'build', 'relay_timing.jsonl'))
LOBBY_S, GAME_S = 12, 20
BUILDS = {'mz': ('build/c/bomber.mzf', 'build/c/bomber.map'),
          'mz011': ('build/v011/build/c/bomber.mzf', 'build/v011/build/c/bomber.map'),
          'zx': ('build/zx/bomber', 'build/zx/bomber.map')}
NAMES = ('_menu_net', '_menu_mode', '_menu_players', '_menu_local', '_net_code', '_net_delay', '_title_mode', '_plat_keys_a',
         '_net_frame')


class MZ:
    """mz800emu at real speed; keys and memory while it runs."""
    def __init__(self, kind, cfg=None):
        self.kind = kind
        mzf, m = (os.path.join(ROOT, p) for p in BUILDS[kind])
        self.S = {n: sym_from_map(m, n) for n in NAMES if n != '_plat_keys_a'}
        old = os.environ['MZ800EMU_CFG']
        if cfg: os.environ['MZ800EMU_CFG'] = cfg
        self.e = Emu(); self.e.load_mzf(mzf)
        os.environ['MZ800EMU_CFG'] = old
        self.e.call('set_speed', {'mode': 'normal'})
        self.e.call('run', {})

    def rd(self, n, k=1): return base64.b64decode(self.e.mem(self.S[n], k)['data_b64'])
    def wr(self, n, bs): self.e.call('mem_write', {'addr': self.S[n], 'data_hex': bytes(bs).hex()})
    def tap(self):
        self.e.press('SPACE'); time.sleep(0.4); self.e.release('SPACE'); time.sleep(0.4)
    def clock(self):                     # emulated seconds
        for _ in range(100):
            r = self.e.call('get_raster_pos', check=False)
            if r.get('success'): return r['data']['total_cycles'] / CPU_HZ
            time.sleep(0.01)
    def close(self): self.e.close()


class ZX:
    """FuseX with the real Spectranet firmware, in real time by itself."""
    fusex_count = 0
    tap_seconds = []

    def __init__(self, kind, port=None):
        self.kind = kind
        self.S = {n: sym_from_map(os.path.join(ROOT, BUILDS[kind][1]), n) for n in NAMES + ('_net_relay_host', '_net_relay_port')}
        os.environ.setdefault('FUSEX_HOME', make_fusex_home(os.path.join(ROOT, 'build', 'fxhome')))
        self.f = FuseX(os.path.join(ROOT, 'build', 'zx', 'bomber.tap'))
        self.f.attach(); self.f.cont()
        # two FuseX on one PC pick the same local port: the second reaches the
        # relay through a forwarder (see tools/fusex_match.py)
        self.port = port or (8765 if ZX.fusex_count == 0 else forward('127.0.0.1', 8765))
        ZX.fusex_count += 1

    def boot(self):
        self.f.boot_program(os.path.join(ROOT, BUILDS[self.kind][0]))
        self.f.write(0x5b70, b'127.0.0.1\x00')                 # the local relay: an IP, no DNS
        self.f.write(self.S['_net_relay_host'], bytes([0x70, 0x5b]))
        self.f.write(self.S['_net_relay_port'], self.port.to_bytes(2, 'little'))
        self.f.cont()

    def _peek(self, fn):
        self.f.halt(); v = fn(); self.f.cont(); return v
    def rd(self, n, k=1): return self._peek(lambda: self.f.read(self.S[n], k))
    def wr(self, n, bs): self._peek(lambda: self.f.write(self.S[n], bytes(bs)))
    def tap(self):
        """SPACE for 10 keyboard calls, then nothing for 10: answered at a
        breakpoint on the keyboard routine (the machine stops at each call)."""
        t = time.monotonic()
        for mask, calls in ((0x10, 10), (0, 10)):
            self.f.halt(); self.f.bp(self.S['_plat_keys_a']); self.f.cont()
            for _ in range(calls):
                self.f.wait_stop(); self.f.return_from_call(hl=mask); self.f.cont()
            self.f.halt(); self.f.unbp(self.S['_plat_keys_a']); self.f.cont()
        ZX.tap_seconds.append(time.monotonic() - t)
    def clock(self):                     # emulated seconds from the ROM's frame counter (wraps: short spans only)
        return self._peek(lambda: self.f.read16(0x5c78)) / 50
    def close(self): self.f.close()


def machine(kind): return ZX(kind) if kind == 'zx' else MZ(kind)


class Relay:
    """The relay's timing log, from where it stood when the run began."""
    def __init__(self):
        if not os.path.exists(LOG): sys.exit(f'{LOG} missing: start the relay with RELAY_TIMING={LOG}')
        self.pos = os.path.getsize(LOG); self.ev = []

    def poll(self):
        with open(LOG) as f:
            f.seek(self.pos); data = f.read()
        lines = data.split('\n'); rest = lines.pop()
        self.pos += len(data) - len(rest)
        self.ev += [json.loads(l) for l in lines if l]
        return self.ev

    def wait(self, cond, seconds, what):
        end = time.time() + seconds
        while time.time() < end:
            r = cond(self.poll())
            if r: return r
            time.sleep(0.1)
        sys.exit(f'timeout: {what}')


def run_once(hk, jk):
    t0 = time.time()
    relay = Relay()
    host, join = machine(hk), machine(jk)
    if any(m.kind == 'zx' for m in (host, join)): time.sleep(12)   # the Spectranext boots to its menu
    for m in (host, join):
        if m.kind == 'zx': m.boot()
    time.sleep(3)                                                  # titles up
    host.wr('_menu_mode', [1]); host.wr('_menu_players', [2]); host.wr('_menu_net', [1]); host.wr('_menu_local', [1])
    join.wr('_menu_net', [2]); join.wr('_menu_local', [1])
    host.tap()
    relay.wait(lambda ev: [e for e in ev if e['op'] == 'create'], 30, 'room created')
    code = None
    for _ in range(50):
        c = host.rd('_net_code', 4)
        if c.isalpha() and c.isupper() and c != b'AAAA': code = c; break
        time.sleep(0.2)
    if not code: sys.exit('no room code')
    room = code.decode()
    join.wr('_net_code', code)
    join.tap(); time.sleep(0.5); join.tap()                        # code screen, join
    t_join = relay.wait(lambda ev: next((e['t'] for e in ev if e['room'] == room and e['slot'] == 1), None), 30, 'join')
    lobby_from = max(t_join, time.monotonic()) + 1                 # time.monotonic: the relay's clock too (Linux)
    time.sleep(LOBBY_S)                                            # lobby: nobody touched
    lobby_to = time.monotonic()
    join.tap(); time.sleep(4); host.tap()                          # ready, the host last
    t_start = relay.wait(lambda ev: next((e['t'] for e in ev if e['room'] == room and e['op'] == 'input'), None), 30, 'start')
    c0 = (host.clock(), join.clock(), time.time())
    time.sleep(GAME_S)                                             # game: nobody touched
    c1 = (host.clock(), join.clock(), time.time())
    ev = relay.poll()
    delays = (host.rd('_net_delay')[0], join.rd('_net_delay')[0])
    host.close(); join.close()
    lobby = [e for e in ev if e['room'] == room and lobby_from <= e['t'] <= lobby_to]
    game = [e for e in ev if e['room'] == room and t_start + 1 <= e['t'] <= t_start + GAME_S - 1]
    pings, turn = {}, []
    for e in lobby:
        d = e['data'] or ''
        if e['op'] != 'msg': continue
        if d.startswith('01') and e['slot'] == 0: pings[d[2:4]] = e['t']
        elif d.startswith('02') and e['slot'] == 1 and d[2:4] in pings: turn.append((e['t'] - pings[d[2:4]]) * 1000)
    pace = {}
    for s in (0, 1):
        t = [e['t'] for e in game if e['op'] == 'input' and e['slot'] == s]
        pace[s] = [(b - a) * 1000 for a, b in zip(t, t[1:])]
    dt = c1[2] - c0[2]
    speed = tuple(((c1[i] - c0[i]) % (65536 / 50 if m.kind == 'zx' else 1e9)) / dt * 100 for i, m in enumerate((host, join)))
    return {'host': hk, 'join': jk, 'room': room, 'delay': delays, 'turn': turn, 'pace': pace, 'speed': speed,
            'seconds': time.time() - t0}


def report(r):
    if ZX.tap_seconds: print(f"  (FuseX key press: {sum(ZX.tap_seconds) / len(ZX.tap_seconds):.1f} s each, outside the windows)")
    late = lambda v: sum(1 for x in v if x > 90) / max(1, len(v)) * 100
    print(f"{r['host']} hosts, {r['join']} joins (room {r['room']}): input delay {r['delay'][0]} / {r['delay'][1]} frames "
          f"({r['delay'][0] * 60} ms); emulators at {r['speed'][0]:.0f} % / {r['speed'][1]:.0f} % of real time", flush=True)
    print(f"  lobby turnaround of the joiner ({r['join']}): {stats(r['turn'])}", flush=True)
    for s, k in ((0, r['host']), (1, r['join'])):
        print(f"  game input interval, {k} (slot {s}): {stats(r['pace'][s])}; over 90 ms: {late(r['pace'][s]):.1f} %", flush=True)


# ---------------- against the play page, through the production relay ----------------
import shutil, socket, subprocess, statistics
NODE = os.environ.get('NODE', os.path.expanduser('~/.nvm/versions/node/v22.23.2/bin/node'))
CHROMIUM = os.environ.get('CHROMIUM', '/snap/bin/chromium')


def free_port():
    with socket.socket() as t: t.bind(('127.0.0.1', 0)); return t.getsockname()[1]


def chromium():
    """Chromium with its debugging port on the virtual display (started once, left running)."""
    try: socket.create_connection(('127.0.0.1', 9222), timeout=1).close(); return
    except OSError: pass
    env = dict(os.environ, DISPLAY=os.environ.get('FUSEX_DISPLAY', ':97'))
    # its own profile: Chromium ignores the debugging port on the default one
    profile = os.path.expanduser('~/snap/chromium/common/netbench-profile')
    subprocess.Popen([CHROMIUM, '--remote-debugging-port=9222', f'--user-data-dir={profile}',
                      '--no-first-run', '--autoplay-policy=no-user-gesture-required',
                      '--ignore-gpu-blocklist', '--enable-unsafe-swiftshader', 'about:blank'], env=env,
                     stdout=open(os.path.join(ROOT, 'build', 'chromium.log'), 'w'), stderr=subprocess.STDOUT)
    for _ in range(60):
        try: socket.create_connection(('127.0.0.1', 9222), timeout=1).close(); return
        except OSError: time.sleep(0.5)
    sys.exit('Chromium did not come up')


def connect_ms(host='api.mzpico.com', n=7):
    """TCP connect times: the bare round trip to the relay's edge."""
    v = []
    for _ in range(n):
        t = time.time(); socket.create_connection((host, 80), timeout=5).close(); v.append((time.time() - t) * 1000)
    return statistics.median(v)


def proxy_events(log):
    with open(log) as f: return [json.loads(l) for l in f.read().split('\n') if l]


def prod_machine(kind, role):
    """A local machine that reaches the production relay through its own logging proxy."""
    port, log = free_port(), os.path.join(ROOT, 'build', f'proxy_{role}.jsonl')
    open(log, 'w').close()
    proxy = subprocess.Popen([sys.executable, os.path.join(HERE, 'netproxy.py'), 'ws' if kind == 'zx' else 'lines',
                              str(port), log])
    time.sleep(1)
    if kind == 'zx':
        m = ZX(kind, port=port)
    else:
        cfg = os.path.join(ROOT, 'build', f'emucfg_{role}')
        shutil.rmtree(cfg, ignore_errors=True)
        shutil.copytree(os.environ['MZ800EMU_CFG'], cfg)
        ini = [f for f in os.listdir(cfg) if f.endswith('.ini') and 'imgui' not in f][0]
        text = open(os.path.join(cfg, ini)).read()
        open(os.path.join(cfg, ini), 'w').write(re.sub(r'net_relay = .*', f'net_relay = 127.0.0.1:{port}', text))
        m = MZ(kind, cfg=cfg)
    m.proxy, m.log = proxy, log
    return m


def margins(ev, delay, t0, t1):
    """At one machine's proxy: for each frame F, how long before the machine needed the
    other's input for F it arrived. The machine sends its own input for F + delay at the
    start of frame F and then waits for the other's F; negative = it had to wait."""
    out = {e['frame']: e['t'] for e in ev if e['op'] == 'input' and e['dir'] == 'out' and t0 <= e['t'] <= t1}
    inn = {e['frame']: e['t'] for e in ev if e['op'] == 'input' and e['dir'] == 'in'}
    return [(out[f + delay] - inn[f]) * 1000 for f in inn if f + delay in out]


def intervals(ev, direction, t0, t1):
    t = sorted(e['t'] for e in ev if e['op'] == 'input' and e['dir'] == direction and t0 <= e['t'] <= t1)
    return [(b - a) * 1000 for a, b in zip(t, t[1:])]


def run_prod(hk, jk):
    """HOST (local) against JOIN (local, or the play page = web), through the production relay."""
    t0 = time.time()
    if jk == 'web': chromium()
    base_rtt = connect_ms()
    machines = []
    try:
        host = prod_machine(hk, 'host'); machines.append(host)
        join = prod_machine(jk, 'join') if jk != 'web' else None
        if join: machines.append(join)
        if any(m.kind == 'zx' for m in machines): time.sleep(12)
        for m in machines:
            if m.kind == 'zx': m.boot()
        time.sleep(3)
        host.wr('_menu_mode', [1]); host.wr('_menu_players', [2]); host.wr('_menu_net', [1]); host.wr('_menu_local', [1])
        if join: join.wr('_menu_net', [2]); join.wr('_menu_local', [1])
        host.tap()
        code = None
        for _ in range(100):
            c = host.rd('_net_code', 4)
            if c.isalpha() and c.isupper() and c != b'AAAA' and any(e['op'] == 'room' for e in proxy_events(host.log)): code = c; break
            time.sleep(0.3)
        if not code: sys.exit('no room code')
        joined = lambda: any(e['dir'] == 'in' and e['op'] == 'msg' and e['from'] == 1 for e in proxy_events(host.log))
        started = lambda: any(e['op'] == 'input' for e in proxy_events(host.log))

        def until(cond, seconds, what):
            end = time.time() + seconds
            while time.time() < end:
                if cond(): return
                time.sleep(0.2)
            sys.exit(f'timeout: {what}')
        if join:
            join.wr('_net_code', code)
            join.tap(); time.sleep(0.5); join.tap()
            until(joined, 40, 'join')
            lobby_from = time.monotonic() + 1
            time.sleep(LOBBY_S)
            lobby_to = time.monotonic()
            join.tap()
        else:
            web = subprocess.Popen([NODE, os.path.join(HERE, 'web_join.mjs'), code.decode(), str(LOBBY_S), str(GAME_S + 8)],
                                   stdout=subprocess.PIPE, text=True)
            lines = []

            def wait_for(word, seconds):
                end = time.time() + seconds
                while time.time() < end:
                    l = web.stdout.readline()
                    if not l: break
                    lines.append(l.strip())
                    if l.startswith(word): return
                sys.exit(f'web player: no {word}; ' + ' | '.join(lines[-4:]))
            wait_for('JOINED', 240)
            lobby_from = time.monotonic() + 1
            wait_for('READY', LOBBY_S + 30)
            lobby_to = time.monotonic() - 1
        time.sleep(4); host.tap()                                      # the host readies last
        until(started, 40, 'start')
        t_start = time.monotonic()
        time.sleep(1)
        f0 = [(m.clock(), int.from_bytes(m.rd('_net_frame', 2), 'little'), time.monotonic()) for m in machines]
        time.sleep(GAME_S - 2)
        f1 = [(m.clock(), int.from_bytes(m.rd('_net_frame', 2), 'little'), time.monotonic()) for m in machines]
        delays = [m.rd('_net_delay')[0] for m in machines]
    finally:
        for m in machines:
            try: m.close()
            except Exception: pass
            m.proxy.terminate()
        if jk == 'web':
            try: web.wait(60)
            except Exception: pass
    keep = os.path.join(ROOT, 'build', 'proxylogs'); os.makedirs(keep, exist_ok=True)
    for m, role in zip(machines, ('host', 'join')):                  # kept per run, for a closer look
        shutil.copy(m.log, os.path.join(keep, f"{code.decode()}_{role}_{m.kind}.jsonl"))
    g0, g1 = t_start + 1, t_start + GAME_S - 1
    r = {'host': hk, 'join': jk, 'room': code.decode(), 'delay': delays[0], 'base': base_rtt, 'seconds': time.time() - t0}
    ev = proxy_events(host.log)
    pings, r['rtt'] = {}, []
    for e in ev:
        d = e['data'] or ''
        if e['op'] != 'msg' or not lobby_from <= e['t'] <= lobby_to: continue
        if e['dir'] == 'out' and d.startswith('01'): pings[d[2:4]] = e['t']
        elif e['dir'] == 'in' and d.startswith('02') and d[2:4] in pings: r['rtt'].append((e['t'] - pings[d[2:4]]) * 1000)
    r['h_out'], r['h_in'] = intervals(ev, 'out', g0, g1), intervals(ev, 'in', g0, g1)
    r['h_margin'] = margins(ev, delays[0], g0, g1)
    # the game's own pace: frames exchanged per emulated second (16.7 = every frame on time)
    emu = [((b[0] - a[0]) % (65536 / 50) if m.kind == 'zx' else b[0] - a[0]) for a, b, m in zip(f0, f1, machines)]
    r['fps'] = [((b[1] - a[1]) & 0xffff) / e for a, b, e in zip(f0, f1, emu)]
    r['speed'] = [e / (b[2] - a[2]) * 100 for a, b, e in zip(f0, f1, emu)]      # emulated time / wall time
    if join:
        jev = proxy_events(join.log)
        r['j_margin'] = margins(jev, delays[1], g0, g1)
        r['j_out'] = intervals(jev, 'out', g0, g1)
    return r


def report_prod(r):
    late = lambda v: sum(1 for x in v if x > 90) / max(1, len(v)) * 100
    waited = lambda v: sum(1 for x in v if x < 0) / max(1, len(v)) * 100
    print(f"{r['host']} hosts, {r['join']} joins (room {r['room']}, production): input delay {r['delay']} frames "
          f"({r['delay'] * 60} ms); TCP connect to the relay {r['base']:.0f} ms", flush=True)
    print(f"  lobby round trip at the host (PING out -> PONG in): {stats(r['rtt'])}", flush=True)
    print(f"  game pace: " + ', '.join(f'{x:.2f}' for x in r['fps']) + " frames per emulated second (16.67 = no frame late); "
          f"emulators at " + ', '.join(f'{x:.0f} %' for x in r['speed']) + " of real time", flush=True)
    print(f"  game input interval, {r['host']} out: {stats(r['h_out'])}; over 90 ms: {late(r['h_out']):.1f} %", flush=True)
    if 'j_out' in r:
        print(f"  game input interval, {r['join']} out: {stats(r['j_out'])}; over 90 ms: {late(r['j_out']):.1f} %", flush=True)
    else:
        print(f"  game input interval, web in: {stats(r['h_in'])}; over 90 ms: {late(r['h_in']):.1f} %", flush=True)
    print(f"  margin at the host: {stats(r['h_margin'])}; waited in {waited(r['h_margin']):.1f} % of frames", flush=True)
    if 'j_margin' in r:
        print(f"  margin at the joiner: {stats(r['j_margin'])}; waited in {waited(r['j_margin']):.1f} % of frames", flush=True)


if __name__ == '__main__':
    hk, jk = sys.argv[1], sys.argv[2]
    runs = next((int(a) for a in sys.argv[3:] if a.isdigit()), 1)
    if jk == 'web' or '--prod' in sys.argv:
        rs = []
        for i in range(runs):
            r = run_prod(hk, jk); report_prod(r); rs.append(r)
        if runs > 1:
            allm = [x for r in rs for x in r['h_margin'] + r.get('j_margin', [])]
            print(f"ALL {hk}-{jk} production: delays {[r['delay'] for r in rs]}; round trip {stats([x for r in rs for x in r['rtt']])}; "
                  f"margin {stats(allm)}, waited in {sum(1 for x in allm if x < 0) / max(1, len(allm)) * 100:.1f} % of frames; "
                  f"TCP connect {[round(r['base']) for r in rs]} ms", flush=True)
        sys.exit(0)
    results = []
    for i in range(runs):
        r = run_once(hk, jk); report(r); results.append(r)
    if runs > 1:
        allturn = [x for r in results for x in r['turn']]
        print(f"ALL {hk}-{jk}: delays {[r['delay'][0] for r in results]}; joiner turnaround {stats(allturn)}; "
              + '; '.join(f"game interval {k} {stats([x for r in results for x in r['pace'][s]])}"
                          for s, k in ((0, hk), (1, jk))), flush=True)
