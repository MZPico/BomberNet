#!/usr/bin/env python3
"""Port step 7 on emulated hardware: two FuseX instances, each running
BomberNet on the real Spectranet firmware and an emulated W5100, play a
network match through a relay; their state hashes are compared.

Both run in real time by themselves; the driver only supplies key presses
(answering the game's keyboard routine at a breakpoint) and reads memory.

  tools/fusex_match.py [frames]        local relay 127.0.0.1:8765; frames exchanged
  RELAY=real tools/fusex_match.py      production: A's Spectranet resolves
                                       api.mzpico.com with its own DNS,
                                       B goes through a local forwarder
Needs Xvfb on :97 (Xvfb :97 &), the local relay, FuseX (FUSEX=path).
"""
import os, socket, sys, threading, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fusex import FuseX, make_fusex_home
from zxemu import sym_from_map, screen_png

N = int(sys.argv[1]) if len(sys.argv) > 1 else 80
REAL = os.environ.get('RELAY') == 'real'
S = lambda n: sym_from_map('build/zx/bomber.map', n)
KEYS_A, TM, NC, NS, FN, SH, NAB, NA = (S(n) for n in ('_plat_keys_a', '_title_mode', '_net_code', '_net_slot',
                                                       '_frame_no', '_state_hash', '_net_abort', '_net_active'))
SPACE, RIGHT = 0x10, 0x04
HP = 16
TRACE = []
NAMES = {S(n): n for n in ('_net_lobby', '_lobby_enter_code', '_room_request', '_net_join', '_lobby_error', '_net_leave', '_title_screen', '_run_game')}
t0 = time.time()
os.environ.setdefault('FUSEX_HOME', make_fusex_home(os.path.abspath('build/fxhome')))


def start():
    fx = FuseX('build/zx/bomber.tap', spectranet=True)
    fx.attach(); fx.cont()
    return fx


def forward(to_host, to_port):
    """A TCP forwarder from a free port on 127.0.0.1 to to_host:to_port; returns its port.

    Both emulated Spectranets choose the same local port and FuseX binds it
    on the host, so two connections to one relay address would repeat the
    same address pair and the second connect fails. Real machines have their
    own IP addresses; here the second machine reaches the relay by another port."""
    srv = socket.create_server(('127.0.0.1', 0))

    def pipe(src, dst, upgrade=False):
        try:
            if upgrade:                                       # the HTTP upgrade names the relay, not 127.0.0.1
                req = b''
                while b'\r\n\r\n' not in req and (d := src.recv(4096)): req += d
                dst.sendall(req.replace(b'Host: 127.0.0.1\r\n', b'Host: %s\r\n' % to_host.encode()))
            while (d := src.recv(4096)): dst.sendall(d)
        except OSError: pass
        for x in (src, dst):
            try: x.shutdown(socket.SHUT_RDWR)
            except OSError: pass

    def serve():
        while True:
            c, _ = srv.accept()
            r = socket.create_connection((to_host, to_port))
            for x in (c, r): x.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            threading.Thread(target=pipe, args=(c, r, True), daemon=True).start()
            threading.Thread(target=pipe, args=(r, c), daemon=True).start()
    threading.Thread(target=serve, daemon=True).start()
    return srv.getsockname()[1]


def boot(fx, port=None):
    fx.boot_program('build/zx/bomber')
    if port:                                                  # the local relay or the forwarder: an IP, no DNS
        fx.write(0x5b70, b'127.0.0.1\x00')             # printer buffer, after the start stub
        fx.write(S('_net_relay_host'), bytes([0x70, 0x5b]))
        fx.write(S('_net_relay_port'), port.to_bytes(2, 'little'))
    fx.cont()


def keys(fx, mask, calls):
    """Answer fx's keyboard routine with mask for its next calls."""
    fx.halt(); fx.bp(KEYS_A); fx.cont()
    done = 0
    while done < calls:
        fx.wait_stop()
        pc = fx.regs()['pc']
        if pc == KEYS_A:                                      # menus only: a press that reaches
            fx.return_from_call(hl=mask if fx.read8(TM) else 0)   # the game would drop a bomb
            done += 1
        else: TRACE.append((round(time.time() - t0, 1), fx.port, NAMES.get(pc, hex(pc))))
        fx.cont()
    fx.halt(); fx.unbp(KEYS_A); fx.cont()


def tap(fx, mask=SPACE): keys(fx, mask, 10); keys(fx, 0, 10)


def peek(fx, fn):
    fx.halt(); v = fn(); fx.cont(); return v


SN_NAMES = {0x3e27: 'gethostbyname', 0x3e00: 'socket', 0x3e0f: 'connect', 0x3e12: 'send',
            0x3e24: 'pollfd', 0x3e15: 'recv', 0x3e03: 'close'}


def trace_calls(fx, n):
    """The next n Spectranet calls of fx with their results."""
    sn, out = S('_sn_call'), []
    fx.halt(); fx.bp(sn); fx.cont()
    while len(out) < n:
        fx.wait_stop(); r = fx.regs()
        fn, ret = r['hl'], fx.read16(r['sp'])
        a_in, bc_in, de_in = fx.read8(S('_sn_a')), fx.read16(S('_sn_bc')), fx.read16(S('_sn_de'))
        fx.unbp(sn); fx.bp(ret); fx.cont(); fx.wait_stop()
        out.append(f"{SN_NAMES.get(fn, hex(fn))}(a={a_in:02x} bc={bc_in:04x} de={de_in:04x}) -> "
                   f"a={fx.read8(S('_sn_a')):02x} bc={fx.read16(S('_sn_bc')):04x} f={fx.regs()['hl'] & 0xff:02x} "
                   f"frame {fx.read16(FN)}")
        fx.unbp(ret); fx.bp(sn); fx.cont()
    fx.halt(); fx.unbp(sn); fx.cont()
    return out


def until(cond, seconds):
    end = time.time() + seconds
    while time.time() < end:
        if cond(): return True
        time.sleep(0.2)
    return False


a, b = start(), start()                                       # both Spectranexts boot to their launcher menu
time.sleep(12)
boot(a, None if REAL else 8765)
boot(b, forward('api.mzpico.com', 80) if REAL else forward('127.0.0.1', 8765))
time.sleep(3)
a.halt(); a.write(S('_menu_mode'), [1]); a.write(S('_menu_players'), [2]); a.write(S('_menu_net'), [1]); a.write(S('_menu_local'), [1])
before = a.read(NC, 4); a.cont()
b.halt(); b.write(S('_menu_net'), [2]); b.write(S('_menu_local'), [1]); b.cont()
tap(a)                                                        # A hosts
if not until(lambda: peek(a, lambda: a.read(NC, 4)) != before, 30): sys.exit('A did not get a room')
code = peek(a, lambda: a.read(NC, 4))
print(f'A hosts room {code.decode()} (FuseX, real Spectranet firmware); {time.time() - t0:.0f} s', flush=True)
b.halt(); b.write(NC, code); b.cont()
if os.environ.get('DEBUG'):
    b.halt()
    for x in NAMES: b.bp(x)
    b.cont()
tap(b); time.sleep(0.5); tap(b)                               # B: code screen, join
if os.environ.get('DEBUG'):
    for _ in range(40):                                        # stops at the traced routines
        try:
            b.wait_stop(1); pc = b.regs()['pc']; TRACE.append((round(time.time() - t0, 1), 'B', NAMES.get(pc, hex(pc)))); b.cont()
        except Exception:
            break
    print('trace:', TRACE, flush=True)
if not until(lambda: peek(b, lambda: b.read8(NS)) == 1, 30):
    b.halt(); screen_png(b.read(0x4000, 6912), 'build/fusex_B.png')
    sys.exit(f'B did not join: title_mode {b.read8(TM)} state {b.read8(S("_s_state"))} code {b.read(NC, 4)} '
             f'last error {b.read8(S("_s_err"))} ws_open {b.read8(S("_ws_open_now"))}\n'
             + b.cmd('qRcmd,' + b'spectranet-info'.hex()))
print(f'B joined; {time.time() - t0:.0f} s', flush=True)
time.sleep(4)                                                 # lobby: delay measured, seat table
tap(b); time.sleep(0.5); tap(a)                               # ready, the host last
if not until(lambda: peek(a, lambda: a.read8(TM)) == 0 and peek(b, lambda: b.read8(TM)) == 0, 30):
    sys.exit(f'did not start: title_mode A {peek(a, lambda: a.read8(TM))} B {peek(b, lambda: b.read8(TM))}')
print(f'in game; delay A {peek(a, lambda: a.read8(S("_net_delay")))} B {peek(b, lambda: b.read8(S("_net_delay")))}', flush=True)
NF = S('_net_frame')                    # frames exchanged; frame_no stops while a player is dying
def clocks(): return [peek(x, lambda: (x.read16(NF), x.read16(0x5c78))) for x in (a, b)] + [time.time()]
c0 = clocks(); time.sleep(10); c1 = clocks()                  # speed, unobserved
dt = c1[2] - c0[2]
for i, name in enumerate('AB'):
    print(f'speed {name}: {(c1[i][0] - c0[i][0]) / dt:.1f} network frames/s, '
          f'{((c1[i][1] - c0[i][1]) & 0xffff) / dt:.1f} TV frames/s (50 = real time)', flush=True)
seen = {'A': {}, 'B': {}}
start_f = peek(a, lambda: a.read16(NF))
while True:
    for name, fx in (('A', a), ('B', b)):
        f, h, ab, nf = peek(fx, lambda: (fx.read16(FN), fx.read16(SH), fx.read8(NAB), fx.read16(NF)))
        if f % HP: seen[name][(f // HP) * HP] = h
        if ab: break
    if ab or nf - start_f >= N or time.time() - t0 > 300: break
    time.sleep(0.5)
common = sorted(set(seen['A']) & set(seen['B']) - {0})
mism = [f for f in common if seen['A'][f] != seen['B'][f]]
for f in common: print(f'frame {f}: A {seen["A"][f]:04x} B {seen["B"][f]:04x} {"MISMATCH" if f in mism else "ok"}')
if os.environ.get('TRACE_NET'):
    for name, fx in (('A', a), ('B', b)): print(name, *trace_calls(fx, 12), sep='\n  ')
a.halt(); b.halt()
aborts = (a.read8(NAB), b.read8(NAB))
screen_png(a.read(0x4000, 6912), 'build/fusex_A.png'); screen_png(b.read(0x4000, 6912), 'build/fusex_B.png')
print(f'done: network frames A {a.read16(NF)} B {b.read16(NF)}, game frames A {a.read16(FN)} B {b.read16(FN)}, {len(common)} hashed frames compared, {len(mism)} mismatches; '
      f'abort flags {aborts}; {time.time() - t0:.0f} s')
a.close(); b.close()
sys.exit(1 if mism or any(aborts) or not common else 0)
