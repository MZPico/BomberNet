#!/usr/bin/env python3
"""Drive FuseX (the Spectranext fork of the Fuse emulator) for tests, through
its GDB server: load the real tape, read and write memory, breakpoints, and
key presses supplied by answering the game's keyboard routine at a
breakpoint. With --spectranet, FuseX runs the real Spectranet ROM on an
emulated W5100: the game's socket calls go through the interface's own
firmware, which is the point of testing here.

FuseX builds on Linux with its SDL interface (see docs/port-zx-spectrum.md);
it runs on a virtual X display (Xvfb :97, FUSEX_DISPLAY to change).
FUSEX=<path to fuse> selects the binary, FUSEX_HOME a private home."""
import os, re, socket, subprocess, threading, time

FUSEX = os.environ.get('FUSEX', os.path.expanduser('~/src/fusex/fuse'))
REG = {'af': 0, 'bc': 1, 'de': 2, 'hl': 3, 'sp': 4, 'pc': 5, 'ix': 6, 'iy': 7}


class FuseX:
    def __init__(self, tape, port=None, spectranet=True, video='xvfb', log=None):
        # stdout line-buffered: the port FuseX actually uses is read from it
        if port is None:                            # a free port
            with socket.socket() as t:
                t.bind(('127.0.0.1', 0)); port = t.getsockname()[1]
        env = dict(os.environ)
        if video == 'xvfb':                         # a virtual X display (start Xvfb :97 first)
            env['DISPLAY'] = os.environ.get('FUSEX_DISPLAY', ':97'); env.pop('WAYLAND_DISPLAY', None)
            env['SDL_VIDEODRIVER'] = 'x11'
        elif video: env['SDL_VIDEODRIVER'] = video
        env['SDL_AUDIODRIVER'] = 'dummy'
        if os.environ.get('FUSEX_HOME'):            # a private home: its own persisted Spectranet flash
            env['HOME'] = os.environ['FUSEX_HOME']
        libs = os.path.join(os.path.dirname(FUSEX), '3rdparty', 'dist', 'lib')   # its own libspectrum
        env['LD_LIBRARY_PATH'] = libs + (':' + env['LD_LIBRARY_PATH'] if env.get('LD_LIBRARY_PATH') else '')
        roms = os.path.join(os.path.dirname(FUSEX), 'roms')
        cmd = ['stdbuf', '-oL', FUSEX, '--machine', '48', '--no-sound', '--no-banner', '--rom-dir', roms,
               '--gdbserver-enable', '--gdbserver-port', str(port),
               '--tape', os.path.abspath(tape), '--auto-load', '--traps']
        if spectranet: cmd.insert(3, '--spectranet')
        self.log = open(log or f'/tmp/fusex_{port}.log', 'w')
        self.p = subprocess.Popen(cmd, env=env, stdout=self.log, stderr=subprocess.STDOUT,
                                  cwd=os.path.dirname(FUSEX))
        # FuseX moves to another port when the one asked for is busy and says so
        # in its log; it accepts connections only once the emulation runs
        for _ in range(100):
            time.sleep(0.1)
            text = open(self.log.name).read()
            m = re.search(r'using port (\d+) instead', text)
            if m: port = int(m.group(1))
            if 'FuseX version' in text or m: break
        self.port = port
        for _ in range(100):
            try:
                self.s = socket.create_connection(('127.0.0.1', port), timeout=5); break
            except OSError:
                time.sleep(0.1)
        else:
            raise RuntimeError('FuseX GDB server did not come up')
        self.s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.buf = b''
        self.running = False
        self.bps = set()

    # ---- GDB remote protocol ----
    def _send(self, data):
        pkt = b'$' + data.encode() + b'#' + b'%02x' % (sum(data.encode()) & 0xff)
        self.s.sendall(pkt)

    def _recv_packet(self, timeout=10):
        self.s.settimeout(timeout)
        while True:
            i = self.buf.find(b'$')
            j = self.buf.find(b'#', i + 1) if i >= 0 else -1
            if i >= 0 and j >= 0 and len(self.buf) >= j + 3:
                data = self.buf[i + 1:j].decode()
                self.buf = self.buf[j + 3:]
                self.s.sendall(b'+')
                return data
            chunk = self.s.recv(4096)
            if not chunk: raise RuntimeError('FuseX closed the connection')
            self.buf += chunk.replace(b'+', b'') if b'$' not in chunk else chunk

    @staticmethod
    def _is_stop(p): return p[:1] in ('T', 'S')

    def cmd(self, data, timeout=10):
        """A command and its reply; stop replies still in the stream (an
        interrupt that crossed a breakpoint stop answers twice) are skipped."""
        self._send(data)
        while True:
            p = self._recv_packet(timeout)
            if not self._is_stop(p): return p

    def _drain(self, t=0.02):
        while True:
            try:
                p = self._recv_packet(t)
            except (TimeoutError, OSError):
                self.s.settimeout(10); return

    # ---- control ----
    def attach(self):
        """First contact: '?' stops the emulator and answers with a stop reply."""
        self._send('?')
        r = self.wait_stop()
        self._drain()
        return r

    def halt(self):
        """Stop the emulator. An interrupt that arrives before FuseX has
        processed the preceding continue is lost, so it is repeated."""
        if not self.running: return
        for _ in range(20):
            self.s.sendall(b'\x03')
            try:
                while not self._is_stop(self._recv_packet(0.5)): pass
                break
            except (TimeoutError, OSError):
                continue
        else:
            raise RuntimeError('FuseX does not stop')
        self._drain()
        self.running = False

    def cont(self):
        self._send('c')
        self.running = True

    def wait_stop(self, timeout=30):
        while True:
            r = self._recv_packet(timeout)
            if self._is_stop(r): break
        self.running = False
        return r

    def regs(self):
        h = self.cmd('g')
        return {n: int(h[i * 4 + 2:i * 4 + 4] + h[i * 4:i * 4 + 2], 16) for n, i in REG.items()}

    def set_reg(self, name, v):
        """Through G (all registers): FuseX's P packet checks the wrong
        character for the '=' and always fails."""
        h = self.cmd('g')
        i = REG[name] * 4
        h = h[:i] + '%02x%02x' % (v & 0xff, (v >> 8) & 0xff) + h[i + 4:]
        r = self.cmd('G' + h)
        if r != 'OK': raise RuntimeError('set register: ' + r)

    def monitor(self, text):
        """A FuseX monitor command (GDB 'monitor'); returns its console output."""
        self._send('qRcmd,' + text.encode().hex())
        out = ''
        while True:
            p = self._recv_packet()
            if p.startswith('O') and p != 'OK': out += bytes.fromhex(p[1:]).decode(errors='replace')
            elif p == 'OK' or p.startswith('E') or p == '': return out

    def read(self, addr, n):
        return bytes.fromhex(self.cmd('m%x,%x' % (addr, n)))

    def read8(self, addr): return self.read(addr, 1)[0]
    def read16(self, addr): b = self.read(addr, 2); return b[0] | b[1] << 8

    def write(self, addr, data):
        r = self.cmd('M%x,%x:%s' % (addr, len(data), bytes(data).hex()))
        if r != 'OK': raise RuntimeError('write: ' + r)

    def bp(self, addr):
        if addr in self.bps: return
        r = self.cmd('Z0,%x,1' % addr)
        if r != 'OK': raise RuntimeError('breakpoint: ' + r)
        self.bps.add(addr)

    def unbp(self, addr):
        if addr not in self.bps: return
        self.cmd('z0,%x,1' % addr); self.bps.discard(addr)

    def return_from_call(self, hl=None):
        """At the first instruction of a routine: return from it at once,
        with HL as its result (the routine is not run). Three round trips:
        the machine stands still meanwhile, so fewer is better."""
        h = self.cmd('g')
        word = lambda i: int(h[i * 4 + 2:i * 4 + 4] + h[i * 4:i * 4 + 2], 16)
        put = lambda h, i, v: h[:i * 4] + '%02x%02x' % (v & 0xff, (v >> 8) & 0xff) + h[i * 4 + 4:]
        sp = word(REG['sp'])
        if hl is not None: h = put(h, REG['hl'], hl)
        h = put(h, REG['pc'], self.read16(sp))
        h = put(h, REG['sp'], (sp + 2) & 0xffff)
        r = self.cmd('G' + h)
        if r != 'OK': raise RuntimeError('set registers: ' + r)

    def boot_program(self, binary, org=24000, stub=0x5b00):
        """Replace what runs (the Spectranext launcher menu, which holds no
        sockets; see make_fusex_home) by our program, written at org and
        started through a stub in the printer buffer: page the Spectranet out
        (a call to 007Ch, its page-out trap, where the Spectrum ROM has a
        RET), interrupt mode 1 and IY = 5C3Ah for the ROM interrupt, stack at
        the top, jump. The Spectranet stays booted, as after a power-on."""
        data = open(binary, 'rb').read()
        self.halt()
        for i in range(0, len(data), 2048):
            self.write(org + i, data[i:i + 2048])
        self.write(stub, bytes([0xf3,                       # di
                                0x31, 0xff, 0x5b,           # ld sp,5BFFh (a stack of its own for the call)
                                0xcd, 0x7c, 0x00,           # call 007Ch: Spectranet paged out
                                0xed, 0x56,                 # im 1
                                0xfd, 0x21, 0x3a, 0x5c,     # ld iy,5C3Ah
                                0x31, 0xff, 0xff,           # ld sp,FFFFh
                                0xfb,                       # ei
                                0xc3, org & 0xff, org >> 8]))   # jp org
        self.set_reg('pc', stub)
        return len(data)

    def close(self):
        try:
            self.s.close()
        finally:
            self.p.terminate()
            try: self.p.wait(5)
            except subprocess.TimeoutExpired: self.p.kill()


def make_fusex_home(home):
    """A private home for FuseX whose persisted Spectranet flash has no boot
    mount: the Spectranext then stops in its launcher menu instead of loading
    its Resource Index, a network program whose open sockets would remain
    when our program takes over. Made from the flash FuseX itself persisted
    (~/.config/fuse-emulator/spectranet.rom, created on its first run)."""
    src = os.path.expanduser('~/.config/fuse-emulator/spectranet.rom')
    dst = os.path.join(home, '.config', 'fuse-emulator', 'spectranet.rom')
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    d = bytearray(open(src, 'rb').read())
    d[16 + 0x1f000:16 + 0x1f002] = b'\x00\x00'         # configuration size 0 (after the 16-byte file header)
    open(dst, 'wb').write(d)
    return home


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
