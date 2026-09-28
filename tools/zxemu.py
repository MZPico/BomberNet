#!/usr/bin/env python3
"""Headless ZX Spectrum 48K for tests: the 'zx' package (pip install zx) with
a few helpers: load the program into memory, run to an address, hold keys,
read and write memory, save the screen as a PNG."""
import os, re, struct, zlib
import zx
from zx._device import KeyStroke
from zx._keyboard import KEYS

ORG = 24000


def sym_from_map(path, name):
    m = re.search(r'^%s\s+= \$([0-9A-F]+)' % re.escape(name), open(path).read(), re.M)
    if not m: raise KeyError(name)
    return int(m.group(1), 16)


class ZX(zx.Spectrum):
    def __init__(self):
        super().__init__(headless=True)
        self.hit = False
        self.reset_and_wait()                      # ROM start-up: system variables, IM 1, IY

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
            if self.hit and self.pc == addr: return
        raise TimeoutError(f'{addr:04x} not reached, pc={self.pc:04x}')

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
