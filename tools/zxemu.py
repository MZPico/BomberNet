#!/usr/bin/env python3
"""Headless ZX Spectrum 48K for tests: the 'zx' package (pip install zx) with
a few helpers: load the program into memory, run to an address, hold keys,
read and write memory, save the screen as a PNG.

With spectranet=True the machine also has a Spectranet, emulated at the level
of its programming interface: calls through IXCALL (3FFDh) are answered here
with real sockets (socket, connect, send, recv, pollfd, close,
gethostbyname), with the registers and flags of the Spectranet ROM, and the
control register (033Bh) mirrors the border as the real one does. The
program under test is unchanged. relay=(host, port) sends every connection
there instead (the local reference relay); None uses the real network.

With bank6=<file> the machine is a 128K Spectrum: the 128 ROM starts up,
the file goes into RAM page 6 (the build/zx128 network driver), and the
program starts with ROM 1 (48K BASIC) and page 0 selected, as after the
build's tape loader. read/poke at C000h and above reach the page selected
now (BANKM, 5B5Ch, which the program keeps with the port).

With esp01=True the machine is an eLeMeNt ZX with its ESP-01 (class Esp01):
the ZXi ID register, the UART and an ESP8266 AT firmware behind it, enough
for the build/zx128 driver (platform/zx/tcp_esp01*.c), which runs unchanged."""
import os, re, select, socket, struct, time, zlib
import zx
from zx._device import KeyStroke
from zx._keyboard import KEYS, Keyboard
from zx._data import Spectrum128

ORG = 24000


def sym_from_map(path, name):
    m = re.search(r'^%s\s+= \$([0-9A-F]+)' % re.escape(name), open(path).read(), re.M)
    if not m: raise KeyError(name)
    return int(m.group(1), 16)


class Esp01:
    """The eLeMeNt ZX's UART with an ESP-01 behind it, as the eLeMeNt ZX
    Programmer's Reference 1.28 and the Wi-Fi BIOS 2.0 (Busy and Hood)
    describe them:
    - ZXi register FFh of the eLeMeNt (select 783Bh, read 793Bh) reads E1h,
      the MB03+'s (select 703Bh, read 773Bh) is absent (FFh); register 5 at
      703Bh/713Bh bit 0 is the module's power, register 30h at 7C3Bh/7D3Bh,
      written 1, erases the receive FIFO;
    - 133Bh: read the status (bit 0 a byte waiting, 1 sending, 2 the FIFO
      full, 3 256 bytes or more, 4 1024 or more), write a byte to send;
      143Bh: read the next byte received. 115200 Bd: a byte every 304
      T-states both ways; the receive FIFO holds 2048 bytes, more are lost.
    - The ESP: AT, ATE0 (echo until then), AT+CIPMUX/CIPDINFO/CIPRECVMODE/
      CIPMODE, AT+CIPSTART="TCP",... (a real connection, to relay=(host,
      port) when given), AT+CIPCLOSE, AT+CIPSEND in CIPMODE=1: transparent,
      every byte to the socket and back, until "+++" alone (20 ms of silence
      before and after). Anything else answers ERROR.
    When it has nothing to deliver it waits up to 2 ms of real time per
    emulated millisecond, so that the relay's timeouts mean something."""
    BYTE_T = 3500000 * 10 // 115200
    GUARD_T = 70000                                # 20 ms

    def __init__(self, z, relay):
        self.z, self.relay = z, relay
        self.sel = {}                              # ZXi select register per pair (high byte of the select port)
        self.reg5 = 0x01                           # the module powered
        self.fifo = bytearray()                    # what the Spectrum can read
        self.out = bytearray()                     # what the ESP has said and is still on the wire
        self.out_t = 0                             # when the next byte of out lands in the FIFO
        self.tx_free = 0
        self.sleep_t = 0
        self.lost = 0
        self.reset()

    def reset(self):
        self.echo, self.cipmode, self.transparent = True, 0, False
        self.line = bytearray()
        self.plus, self.last_tx = 0, 0
        if getattr(self, 'sock', None): self.sock.close()
        self.sock = None
        self.commands = []

    def say(self, text):
        if not self.out: self.out_t = max(self.out_t, self.z.now())
        self.out += text.encode() if isinstance(text, str) else text

    def _deliver(self):
        now = self.z.now()
        if self.transparent and self.sock and len(self.out) < 512:
            try:
                r, _, _ = select.select([self.sock], [], [], 0)
                if r:
                    d = self.sock.recv(4096)
                    if d: self.say(d)
                    else: self.sock.close(); self.sock = None
            except OSError:
                self.sock = None
        while self.out and self.out_t <= now:
            if len(self.fifo) < 2048: self.fifo.append(self.out[0])
            else: self.lost += 1
            del self.out[0]
            self.out_t += self.BYTE_T
        if self.plus == 3 and now - self.last_tx >= self.GUARD_T:
            self.plus = 0; self.transparent = False    # +++ alone: back to commands, the connection stays
        if not self.fifo and not self.out and self.transparent and now - self.sleep_t > 3500:
            self.sleep_t = now
            if self.sock: select.select([self.sock], [], [], 0.002)

    def status(self):
        self._deliver()
        n = len(self.fifo)
        return ((1 if n else 0) | (2 if self.z.now() < self.tx_free else 0) | (4 if n >= 2048 else 0) |
                (8 if n >= 256 else 0) | (16 if n >= 1024 else 0))

    def read(self):
        self._deliver()
        return self.fifo.pop(0) if self.fifo else 0xff

    def write(self, b):
        now = self.z.now()
        self.tx_free = max(now, self.tx_free) + self.BYTE_T
        if not self.reg5 & 1: return
        if self.transparent:
            if b == 0x2b and (self.plus or now - self.last_tx >= self.GUARD_T) and self.plus < 3:
                self.plus += 1                     # perhaps the escape: held back
            else:
                data = b'+' * self.plus + bytes([b]); self.plus = 0
                if self.sock:
                    try: self.sock.sendall(data)
                    except OSError: self.sock = None
            self.last_tx = now
            return
        self.last_tx = now
        if self.echo: self.say(bytes([b]))
        if b == 10:
            line = self.line.decode('latin-1').strip(); self.line = bytearray()
            if line: self.command(line)
        else: self.line.append(b)

    def command(self, c):
        self.commands.append(c)
        ok = '\r\nOK\r\n'
        if c == 'AT': self.say(ok)
        elif c == 'ATE0': self.echo = False; self.say(ok)
        elif c in ('AT+CIPMUX=0', 'AT+CIPDINFO=0', 'AT+CIPRECVMODE=0', 'AT+CIPRECVMODE=1'): self.say(ok)
        elif c in ('AT+CIPMODE=0', 'AT+CIPMODE=1'): self.cipmode = int(c[-1]); self.say(ok)
        elif c == 'AT+CIPCLOSE':
            if self.sock: self.sock.close(); self.sock = None; self.say('CLOSED\r\n' + ok)
            else: self.say('\r\nERROR\r\n')
        elif c.startswith('AT+CIPSTART="TCP","'):
            m = re.match(r'AT\+CIPSTART="TCP","([^"]+)",(\d+)$', c)
            try:
                s = socket.create_connection(self.relay if self.relay else (m.group(1), int(m.group(2))), timeout=10)
                s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                self.sock = s; self.say('CONNECT\r\n' + ok)
            except (OSError, AttributeError):
                self.say('\r\nERROR\r\nCLOSED\r\n')
        elif c == 'AT+CIPSEND' and self.cipmode == 1 and self.sock:
            self.say(ok + '\r\n>'); self.transparent = True; self.plus = 0
        else: self.say('\r\nERROR\r\n')

    def port_in(self, addr):
        hi = addr >> 8
        if hi == 0x13: return self.status()
        if hi == 0x14: return self.read()
        if hi == 0x79: return 0xe1 if self.sel.get(0x78) == 0xff else 0x00
        if hi == 0x71: return self.reg5 if self.sel.get(0x70) == 5 else 0xff
        return 0xff                                # no MB03+ (773Bh), the rest unused here

    def port_out(self, addr, v):
        hi = addr >> 8
        if hi in (0x78, 0x70, 0x7c): self.sel[hi] = v
        elif hi == 0x13: self.write(v)
        elif hi == 0x71 and self.sel.get(0x70) == 5:
            if v & 1 and not self.reg5 & 1:        # powered up: a fresh module after its start-up text
                self.reset(); self.out_t = self.z.now() + 3500000 // 2; self.out = bytearray(b'\r\nready\r\n')
            self.reg5 = v
        elif hi == 0x7d and self.sel.get(0x7c) == 0x30 and v == 1:
            self.fifo = bytearray()


class ZX(zx.Spectrum):
    IXCALL = 0x3ffd

    def __init__(self, spectranet=False, relay=('127.0.0.1', 8765), bank6=None, esp01=False):
        kb = Keyboard()
        kb._state = [0xff] * 8                     # the package keeps it in the class: one per machine here
        self.kb = kb
        if bank6: super().__init__(headless=True, keyboard=kb, model=Spectrum128)
        else: super().__init__(headless=True, keyboard=kb)
        self.bank6 = bank6
        self.hit = False
        self.ports = {}                            # low port byte -> value: joystick interfaces
        self.spectranet = spectranet
        self.relay = relay
        self.socks = {}
        self.sn_calls = 0
        self.esp = Esp01(self, relay) if esp01 else None
        self.set_on_input_callback(self._input)
        if esp01: self.set_on_output_callback(self._output)
        self.reset_and_wait()                      # ROM start-up: system variables, IM 1, IY
        if spectranet:
            self.set_breakpoint(self.IXCALL)
            # the API jump table the game checks with the interface paged in
            # (no paging here: it is written over the ROM's copy of the font)
            for i in range(14): self.poke(0x3e00 + 3 * i, b'\xc3')
            if bank6:                              # the 48K BASIC ROM, paged in to run the game: its image is
                mem = self._SpectrumState__memory  # at 4 x 16K (the package's write() misplaces ROM bytes)
                for i in range(14): mem[0x10000 + 0x3e00 + 3 * i] = 0xc3
        if bank6: self.write(0xc000, open(bank6, 'rb').read(), ram_page=6)

    def _output(self, addr, value):
        if (addr & 0xff) == 0x3b: self.esp.port_out(addr & 0xffff, value)

    def _input(self, addr):
        if self.esp and (addr & 0xff) == 0x3b: return self.esp.port_in(addr & 0xffff)
        if self.spectranet and (addr & 0xffff) == 0x033b:
            return 0xf8 | (self.border_colour & 7)     # the control register mirrors the border
        low = addr & 0xff
        if low in self.ports: return self.ports[low]
        if low & 1: return 0xff                    # no device on this port
        return self._Spectrum__on_input(addr)      # keyboard and tape (port FEh)

    # ---- Spectranet programming interface ----
    def _cstr(self, addr):
        b = bytearray()
        while True:
            c = self.read8(addr + len(b))
            if not c: return b.decode('latin-1')
            b.append(c)

    def _sn_return(self, a=None, bc=None, carry=False, zero=False):
        if a is None: a = self.af >> 8
        f = (self.af & 0xbe) | (0x01 if carry else 0) | (0x40 if zero else 0)
        self.af = (a << 8) | f
        if bc is not None: self.bc = bc & 0xffff
        self.pc = self.read16(self.sp)             # the RET of the call
        self.sp = (self.sp + 2) & 0xffff

    def _sn_call(self):
        self.sn_calls += 1
        fn, a, bc, de, hl = self.ix, self.af >> 8, self.bc, self.de, self.hl
        try:
            if fn == 0x3e27:                       # GETHOSTBYNAME HL = name, DE = 4 bytes
                name = self._cstr(hl)
                ip = b'\x0a\x00\x00\x01' if self.relay else socket.inet_aton(socket.gethostbyname(name))
                self.poke(de, ip); self._sn_return(a=0)
            elif fn == 0x3e00:                     # SOCKET C = type -> A = fd
                fd = next(i for i in range(4) if i not in self.socks)
                self.socks[fd] = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self._sn_return(a=fd)
            elif fn == 0x3e0f:                     # CONNECT A = fd, DE = IP, BC = port
                ip = socket.inet_ntoa(self.read(de, 4))
                self.socks[a].connect(self.relay if self.relay else (ip, bc))
                self.socks[a].setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                self._sn_return(a=0)
            elif fn == 0x3e12:                     # SEND A = fd, DE = data, BC = length
                self.socks[a].sendall(self.read(de, bc)); self._sn_return(bc=bc)
            elif fn == 0x3e24:                     # POLLFD A = fd -> Z = not ready, C = reason
                s = self.socks[a]
                # an emulated Spectrum runs far faster than real time; waiting
                # a moment when nothing is there keeps its timeouts (counted in
                # TV frames) meaningful against a real server
                r, _, _ = select.select([s], [], [], 0.003)
                if not r: self._sn_return(zero=True, bc=bc & 0xff00)
                elif not s.recv(1, socket.MSG_PEEK): self._sn_return(bc=(bc & 0xff00) | 0x02)
                else: self._sn_return(bc=(bc & 0xff00) | 0x04)
            elif fn == 0x3e15:                     # RECV A = fd, DE = buffer, BC = size -> BC = got
                d = self.socks[a].recv(bc); self.poke(de, d); self._sn_return(bc=len(d))
                self.rxlog = (getattr(self, 'rxlog', b'') + d)[-3000:]
            elif fn == 0x3e03:                     # CLOSE A = fd
                s = self.socks.pop(a, None)
                if s: s.close()
                self._sn_return()
            else:
                raise RuntimeError(f'Spectranet call {fn:04x} not emulated')
        except (OSError, KeyError, StopIteration) as e:
            self._sn_return(a=0xfa, carry=True)

    def on_breakpoint(self):
        self.hit = True                            # the quantum ends here, pc is at the address

    def now(self):
        """T-states since start (69888 per TV frame, 70908 on the 128K)."""
        return self.frame_count * (70908 if self.bank6 else 69888) + self.ticks_since_int

    def load(self, binary, org=ORG):
        data = open(binary, 'rb').read()
        self.poke(org, data, page=0)
        self.pc = org
        if self.bank6:                             # ROM 1 and page 0 (BANKM and port 7FFDh), then the program
            stub = bytes([0x3e, 0x10, 0x01, 0xfd, 0x7f, 0xed, 0x79, 0x32, 0x5c, 0x5b, 0xc3]) + org.to_bytes(2, 'little')
            self.poke(0x5d00, stub)                # below C000h (the 128 menu has page 7 there), in the
            self.pc = 0x5d00                       # loader's area, which the game uses later
        return len(data)

    def _offsets(self, addr, n, page=None):
        """Memory image offsets of addr .. addr + n - 1 (128K: the page at C000h)."""
        if not self.bank6 or addr + n <= 0xc000: return [(addr, n)]
        if page is None: page = self._SpectrumState__memory[0x5b5c] & 7
        base = self._SpectrumState__RAM_PAGE_IMAGE_OFFSETS[page] - 0xc000
        if addr >= 0xc000: return [(base + addr, n)]
        return [(addr, 0xc000 - addr), (base + 0xc000, addr + n - 0xc000)]

    def read(self, addr, size, page=None):
        mem = self._SpectrumState__memory
        return b''.join(bytes(mem[o:o + k]) for o, k in self._offsets(addr, size, page))

    def poke(self, addr, data, page=None):
        """Write bytes into RAM (the package's own write() handles 48K pages awkwardly)."""
        mem = self._SpectrumState__memory
        data = bytes(data)
        for o, k in self._offsets(addr, len(data), page):
            mem[o:o + k] = data[:k]; data = data[k:]

    def run_until(self, addr, max_seconds=10.0):
        """Run until the instruction at addr is about to execute. Breakpoints
        stay set (the package cannot clear them); hits elsewhere are passed."""
        self.set_breakpoint(addr)
        end = self.now() + int(max_seconds * 3500000)
        while self.now() < end:
            self.hit = False
            self._Spectrum__run_quantum(fast_forward=True)     # returns at a breakpoint or a frame end
            if self.hit and self.spectranet and self.pc == self.IXCALL:
                self._sn_call()
                continue
            if self.hit and self.pc == addr: return
        raise TimeoutError(f'{addr:04x} not reached, pc={self.pc:04x}')

    def step(self):
        """One quantum (to the next breakpoint or the end of a TV frame);
        returns the breakpoint address, or None."""
        self.hit = False
        self._Spectrum__run_quantum(fast_forward=True)
        if self.hit and self.spectranet and self.pc == self.IXCALL:
            self._sn_call()
            return None
        return self.pc if self.hit else None

    def press(self, *keys):
        for k in keys: self.devices.notify(KeyStroke(KEYS[k].ID, pressed=True))

    def release(self, *keys):
        for k in keys: self.devices.notify(KeyStroke(KEYS[k].ID, pressed=False))

    def screenshot(self, path, scale=2):
        """PNG of the 256 x 192 screen from display memory (not the emulator's frame)."""
        screen_png(self.read(0x4000, 6912), path, scale)


def screen_png(mem, path, scale=2):
    """PNG from the 6912 bytes of a Spectrum's display memory."""
    pal = [(0, 0, 0), (0, 0, 205), (205, 0, 0), (205, 0, 205), (0, 205, 0), (0, 205, 205), (205, 205, 0), (205, 205, 205)]
    bri = [(0, 0, 0), (0, 0, 255), (255, 0, 0), (255, 0, 255), (0, 255, 0), (0, 255, 255), (255, 255, 0), (255, 255, 255)]
    rows = []
    for y in range(192):
        base = ((y & 0xc0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2)
        line = bytearray()
        for cx in range(32):
            b = mem[base + cx]; a = mem[6144 + (y >> 3) * 32 + cx]
            p = bri if a & 0x40 else pal
            ink, paper = p[a & 7], p[(a >> 3) & 7]
            for bit in range(8):
                line += bytes(ink if (b << bit) & 0x80 else paper) * scale
        rows += [bytes(line)] * scale
    w, h = 256 * scale, 192 * scale
    raw = b''.join(b'\x00' + r for r in rows)
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def run_together(machines, addr, count=1, on_hit=None, max_seconds=60.0):
    """Run several machines side by side, one quantum each in turn, until each
    has passed addr count more times. None of them is ever held: machines that
    wait for each other over the network must all keep running. on_hit(i, z)
    is called at every pass (the machine stands at addr then). addr can be a
    list, one address per machine (different builds)."""
    addrs = list(addr) if isinstance(addr, (list, tuple)) else [addr] * len(machines)
    for z, ad in zip(machines, addrs): z.set_breakpoint(ad)
    left = [count] * len(machines)
    end = [z.now() + int(max_seconds * 3500000) for z in machines]
    while any(n > 0 for n in left):
        for i, z in enumerate(machines):
            if z.step() == addrs[i]:
                if on_hit: on_hit(i, z)
                left[i] -= 1
            if left[i] > 0 and z.now() > end[i]:
                raise TimeoutError(f'{addrs[i]:04x} not reached, pc={z.pc:04x}')
