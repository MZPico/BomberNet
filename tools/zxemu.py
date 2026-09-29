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
there instead (the local reference relay); None uses the real network."""
import os, re, select, socket, struct, zlib
import zx
from zx._device import KeyStroke
from zx._keyboard import KEYS, Keyboard

ORG = 24000


def sym_from_map(path, name):
    m = re.search(r'^%s\s+= \$([0-9A-F]+)' % re.escape(name), open(path).read(), re.M)
    if not m: raise KeyError(name)
    return int(m.group(1), 16)


class ZX(zx.Spectrum):
    IXCALL = 0x3ffd

    def __init__(self, spectranet=False, relay=('127.0.0.1', 8765)):
        kb = Keyboard()
        kb._state = [0xff] * 8                     # the package keeps it in the class: one per machine here
        self.kb = kb
        super().__init__(headless=True, keyboard=kb)
        self.hit = False
        self.ports = {}                            # low port byte -> value: joystick interfaces
        self.spectranet = spectranet
        self.relay = relay
        self.socks = {}
        self.sn_calls = 0
        self.set_on_input_callback(self._input)
        self.reset_and_wait()                      # ROM start-up: system variables, IM 1, IY
        if spectranet: self.set_breakpoint(self.IXCALL)

    def _input(self, addr):
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
        """T-states since start (69888 per TV frame)."""
        return self.frame_count * 69888 + self.ticks_since_int

    def load(self, binary, org=ORG):
        data = open(binary, 'rb').read()
        self.poke(org, data)
        self.pc = org
        return len(data)

    def poke(self, addr, data):
        """Write bytes into RAM (the package's own write() handles 48K pages awkwardly)."""
        mem = self._SpectrumState__memory
        mem[addr:addr + len(data)] = bytes(data)

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
        mem = self.read(0x4000, 6912)
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
    is called at every pass (the machine stands at addr then)."""
    for z in machines: z.set_breakpoint(addr)
    left = [count] * len(machines)
    end = [z.now() + int(max_seconds * 3500000) for z in machines]
    while any(n > 0 for n in left):
        for i, z in enumerate(machines):
            if z.step() == addr:
                if on_hit: on_hit(i, z)
                left[i] -= 1
            if left[i] > 0 and z.now() > end[i]:
                raise TimeoutError(f'{addr:04x} not reached, pc={z.pc:04x}')
