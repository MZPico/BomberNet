#!/usr/bin/env python3
"""Headless Amstrad CPC 464 for tests, on the z80 package's Z80 core (pip
install z80): 64 KB of RAM, the gate array (screen mode, palette, ROM
enables, the 300 Hz interrupt), upper ROM selection, the CRTC's screen
address, the 8255 PPI with the keyboard and joysticks behind the AY's port
A, VSYNC; screenshots of Mode 0 and Mode 1.

There is no firmware: a program is loaded into RAM and started at its entry,
as after the game has taken over the machine (it runs with both ROMs paged
out and its own interrupt handler at 38h).

With m4=True the machine has an M4 board, emulated at its port interface as
the M4's own ROM and examples use it (github.com/M4Duke, m4rom/M4ROM.s):
a command packet is written to port FE00h a byte at a time (length, command
word, parameters) and started by an OUT to FC00h; the answer is in the M4's
ROM, which the program pages in at C000h: the pointer table at FF00h (FF02h
-> response buffer E800h, FF06h -> socket table FE00h), the ROM name
"M4 BOARD" in its RSX table. Socket entries are 16 bytes: status (0 idle,
1 connect in progress, 2 send in progress, 3 remote closed, 5 lookup in
progress on entry 0, F0h.. error), last command, bytes received (word), IP
(4, least significant byte first, i.e. reversed dotted order), port. Entry 0
belongs to C_NETHOSTIP. Real sockets do the work; relay=(host, port) sends
every connection there instead (the local reference relay).

Timing: T-states of a plain Z80 at 4 MHz. A real CPC stretches every
instruction to whole microseconds, about 20 % slower: measured frame costs
are optimistic by that much."""
import errno, os, re, select, socket, struct, zlib
import z80

ORG = 0x0100                    # default load address, overridden by load()
FRAME_T = 79872                 # 312 lines x 64 us x 4 MHz
LINE_T = 256
INT_T = 52 * LINE_T             # gate array interrupt: every 52 lines
VSYNC_LINES = 8

# hardware colour number (0..31) -> RGB
HW_RGB = [
    (128, 128, 128), (128, 128, 128), (0, 255, 128), (255, 255, 128), (0, 0, 128), (255, 0, 128), (0, 128, 128), (255, 128, 128),
    (255, 0, 128), (255, 255, 128), (255, 255, 0), (255, 255, 255), (255, 0, 0), (255, 0, 255), (255, 128, 0), (255, 128, 255),
    (0, 0, 128), (0, 255, 128), (0, 255, 0), (0, 255, 255), (0, 0, 0), (0, 0, 255), (0, 128, 0), (0, 128, 255),
    (128, 0, 128), (128, 255, 128), (128, 255, 0), (128, 255, 255), (128, 0, 0), (128, 0, 255), (128, 128, 0), (128, 128, 255),
]

# keyboard matrix: name -> (line, bit); lines 6 and 9 also carry the joysticks
KEYS = {}
for line, names in enumerate([
        ['UP', 'RIGHT', 'DOWN', 'F9', 'F6', 'F3', 'ENTER', 'F.'],
        ['LEFT', 'COPY', 'F7', 'F8', 'F5', 'F1', 'F2', 'F0'],
        ['CLR', '[', 'RETURN', ']', 'F4', 'SHIFT', '\\', 'CONTROL'],
        ['^', '-', '@', 'P', ';', ':', '/', '.'],
        ['0', '9', 'O', 'I', 'L', 'K', 'M', ','],
        ['8', '7', 'U', 'Y', 'H', 'J', 'N', 'SPACE'],
        ['6', '5', 'R', 'T', 'G', 'F', 'B', 'V'],
        ['4', '3', 'E', 'W', 'S', 'D', 'C', 'X'],
        ['1', '2', 'ESC', 'Q', 'TAB', 'A', 'CAPSLOCK', 'Z'],
        ['J0_UP', 'J0_DOWN', 'J0_LEFT', 'J0_RIGHT', 'J0_FIRE2', 'J0_FIRE1', 'J0_FIRE3', 'DEL']]):
    for bit, name in enumerate(names): KEYS[name] = (line, bit)
for name, key in (('J1_UP', '6'), ('J1_DOWN', '5'), ('J1_LEFT', 'R'), ('J1_RIGHT', 'T'), ('J1_FIRE2', 'G'), ('J1_FIRE1', 'F')):
    KEYS[name] = KEYS[key]

C_NETSOCKET, C_NETCONNECT, C_NETCLOSE, C_NETSEND, C_NETRECV, C_NETHOSTIP = 0x4331, 0x4332, 0x4333, 0x4334, 0x4335, 0x4336
M4_RESPONSE, M4_SOCKS = 0xE800, 0xFE00


def sym_from_map(path, name):
    m = re.search(r'^%s\s+= \$([0-9A-F]+)' % re.escape(name), open(path).read(), re.M)
    if not m: raise KeyError(name)
    return int(m.group(1), 16)


def m4_rom():
    """A 16 KB image with what a program looks at in the M4's ROM."""
    rom = bytearray(0x4000)
    rom[0:4] = bytes([0x01, 0x02, 0x00, 0x00])                  # foreground ROM, version
    rom[4:6] = (0xC100).to_bytes(2, 'little')                   # RSX name table
    name = b'M4 BOARD'
    rom[0x100:0x100 + len(name)] = name[:-1] + bytes([name[-1] | 0x80])
    for i, v in enumerate((0x206, M4_RESPONSE, 0, M4_SOCKS)):  # pointer table at FF00h
        rom[0x3F00 + 2 * i:0x3F02 + 2 * i] = v.to_bytes(2, 'little')
    return rom


class CPC:
    def __init__(self, m4=False, relay=('127.0.0.1', 8765), m4_rom_number=6):
        self.m = z80.Z80Machine()
        self.mem = self.m.memory
        self.m.set_input_callback(self._in)
        self.m.set_output_callback(self._out)
        self.m.set_read_callback(self._read)
        self.roms = {}
        self.rom_sel = 0
        self.rmr = 0x0C                            # mode 0, both ROMs disabled
        self.upper_on = False
        self.pen, self.palette = 0, [20] * 17      # all black
        self.crtc_sel, self.crtc = 0, [0] * 18
        self.crtc[12], self.crtc[13] = 0x30, 0x00  # screen at C000h
        self.ppi_a, self.ppi_c, self.ppi_ctl = 0xff, 0, 0x82
        self.psg_sel, self.psg = 0, [0] * 16
        self.matrix = [0xff] * 10
        self.t = 0                                 # T-states since start
        self.int_pending = False
        self.next_int = 2 * LINE_T
        self.frame_count = 0
        self.bps = set()
        self._stopped_at = None
        self.tones = []                            # (t, register, value) writes to the AY's tone registers
        self.m4 = m4
        self.relay = relay
        if m4:
            self.roms[m4_rom_number] = m4_rom()
            self.m4_cmd = bytearray()
            self.socks = {}                        # number -> dict(sock, state, rx)
            self.dns = None
        self._page()

    # ---- memory: writes always go to RAM; reads of C000h.. see the upper ROM when it is on ----
    def _page(self):
        on = not (self.rmr & 0x08) and self.rom_sel in self.roms
        if on != self.upper_on:
            (self.m.mark_addrs if on else self.m.unmark_addrs)(0xC000, 0x4000, self.m.READ_MARK)
            self.upper_on = on

    def _read(self, addr):
        if self.upper_on and addr >= 0xC000: return self.roms[self.rom_sel][addr - 0xC000]
        return self.mem[addr]

    # ---- ports ----
    def _in(self, port):
        hi = port >> 8
        if self.m4 and hi in (0xFE, 0xFC): return 0xff
        if not port & 0x0800:                      # PPI
            f = (port >> 8) & 3
            if f == 0:
                if (self.ppi_c >> 6) == 1:         # PSG read
                    if self.psg_sel == 14: return self.matrix[self.ppi_c & 0x0f] if (self.ppi_c & 0x0f) < 10 else 0xff
                    return self.psg[self.psg_sel]
                return 0xff
            if f == 1:                             # port B: VSYNC, 50 Hz, Amstrad
                line = (self.t % FRAME_T) // LINE_T
                return 0x1E | (1 if line < VSYNC_LINES else 0)
            if f == 2: return self.ppi_c
        return 0xff

    def _out(self, port, v):
        hi = port >> 8
        if self.m4 and hi == 0xFE: self.m4_cmd.append(v); return
        if self.m4 and hi == 0xFC: self._m4_exec(); return
        if (port & 0xC000) == 0x4000:              # gate array
            f = v >> 6
            if f == 0: self.pen = 16 if v & 0x10 else v & 0x0f
            elif f == 1: self.palette[self.pen] = v & 0x1f
            elif f == 2:
                self.rmr = v & 0x1f
                if v & 0x10: self.int_pending = False
                self._page()
        if not port & 0x4000:                      # CRTC
            if (port >> 8) & 3 == 0: self.crtc_sel = v & 0x1f
            elif (port >> 8) & 3 == 1 and self.crtc_sel < 18: self.crtc[self.crtc_sel] = v
        if not port & 0x2000:                      # upper ROM number
            self.rom_sel = v
            self._page()
        if not port & 0x0800:                      # PPI
            f = (port >> 8) & 3
            if f == 0: self.ppi_a = v
            elif f == 2:
                self.ppi_c = v
                op = v >> 6
                if op == 3: self.psg_sel = self.ppi_a & 0x0f
                elif op == 2:
                    self.psg[self.psg_sel] = self.ppi_a
                    if self.psg_sel <= 1 or self.psg_sel == 8: self.tones.append((self.t, self.psg_sel, self.ppi_a))
            elif f == 3: self.ppi_ctl = v

    # ---- running ----
    def _slice(self, ticks):
        self.m.ticks_to_stop = ticks
        ev = self.m.run()
        done = ticks - self.m.ticks_to_stop if self.m.ticks_to_stop else ticks
        self.t += done
        return ev

    def _service(self):
        """Interrupt and network bookkeeping at the current time."""
        while self.t >= self.next_int:
            self.int_pending = True
            self.next_int += INT_T
            if (self.next_int - 2 * LINE_T) % FRAME_T == 0: self.frame_count += 1
        if self.int_pending and self.m.iff1 and self.m.on_handle_active_int():
            self.int_pending = False
        if self.m4: self._m4_poll()

    def step(self):
        """Run to the next breakpoint or interrupt time; returns the
        breakpoint address it stopped at, or None. A breakpoint the machine
        stands on is passed first."""
        if self._stopped_at is not None and self.m.pc == self._stopped_at: self._pass_breakpoint()
        self._stopped_at = None
        budget = max(4, self.next_int - self.t) if not self.int_pending else 64
        ev = self._slice(budget)
        self._service()
        if ev & 1 and self.m.pc in self.bps:
            self._stopped_at = self.m.pc
            return self.m.pc
        return None

    def now(self): return self.t

    def run_until(self, addr, max_seconds=10.0):
        """Run until the instruction at addr is about to execute."""
        self.set_breakpoint(addr)
        end = self.t + int(max_seconds * 4_000_000)
        while self.t < end:
            if self.step() == addr: return
        raise TimeoutError(f'{addr:04x} not reached, pc={self.m.pc:04x}')

    def _pass_breakpoint(self):
        """Execute the instruction under a breakpoint (the core stops before it)."""
        a = self.m.pc
        self.m.clear_breakpoint(a)
        self._slice(1)
        if a in self.bps: self.m.set_breakpoint(a)

    def run_ticks(self, n):
        """Run n T-states, passing breakpoints."""
        end = self.t + n
        while self.t < end:
            self.step()

    def set_breakpoint(self, addr):
        if addr not in self.bps:
            self.bps.add(addr)
            self.m.set_breakpoint(addr)

    def clear_breakpoint(self, addr):
        if addr in self.bps:
            self.bps.discard(addr)
            self.m.clear_breakpoint(addr)

    @property
    def pc(self): return self.m.pc

    @pc.setter
    def pc(self, v): self.m.pc = v

    # ---- program, memory, keys ----
    def load(self, binary, org=None, entry=None):
        """Load a raw binary, or an AMSDOS file (its header gives the load and
        execution addresses), and set PC to the entry."""
        data = open(binary, 'rb').read()
        if len(data) > 128 and sum(data[:67]) & 0xffff == int.from_bytes(data[67:69], 'little'):
            org = org if org is not None else int.from_bytes(data[21:23], 'little')
            entry = entry if entry is not None else int.from_bytes(data[26:28], 'little')
            data = data[128:]
        org = ORG if org is None else org
        self.poke(org, data)
        # The two firmware routines z88dk's start-up calls before it takes
        # over: MC START PROGRAM (BD16h: jump to HL) and KL ROM WALK (BCCBh).
        self.poke(0xBD16, bytes([0xE9]))
        self.poke(0xBCCB, bytes([0xC9]))
        self.m.pc = entry if entry is not None else org
        self.m.sp = 0xC000
        return len(data)

    def poke(self, addr, data): self.m.set_memory_block(addr, bytes(data))
    def read(self, addr, n): return bytes(self.mem[addr:addr + n])
    def read8(self, addr): return self.mem[addr]
    def read16(self, addr): return self.mem[addr] | self.mem[(addr + 1) & 0xffff] << 8

    def press(self, *keys):
        for k in keys:
            line, bit = KEYS[k]; self.matrix[line] &= ~(1 << bit) & 0xff

    def release(self, *keys):
        for k in keys:
            line, bit = KEYS[k]; self.matrix[line] |= 1 << bit

    # ---- screen ----
    def screen_rgb(self):
        """320 x 200 RGB rows of the current screen (Mode 0 pixels doubled)."""
        ma = ((self.crtc[12] & 0x03) << 8) | self.crtc[13]        # CRTC start address (words)
        page = (self.crtc[12] & 0x30) << 10                       # 16 KB page: C000h for 30h
        mode = self.rmr & 3
        rows = []
        for y in range(200):
            line = []
            for bx in range(80):
                b = self.mem[page | ((y % 8) << 11) | ((ma * 2 + (y // 8) * 80 + bx) & 0x7ff)]
                if mode == 1:
                    for k in range(4):
                        pen = ((b >> (7 - k)) & 1) | (((b >> (3 - k)) & 1) << 1)
                        line.append(HW_RGB[self.palette[pen]])
                elif mode == 0:
                    for p in (((b >> 7) & 1) | (((b >> 3) & 1) << 1) | (((b >> 5) & 1) << 2) | (((b >> 1) & 1) << 3),
                              ((b >> 6) & 1) | (((b >> 2) & 1) << 1) | (((b >> 4) & 1) << 2) | ((b & 1) << 3)):
                        line += [HW_RGB[self.palette[p]]] * 2
                else:
                    for k in range(8):
                        line.append(HW_RGB[self.palette[(b >> (7 - k)) & 1]])
            rows.append(line[:320] if mode != 2 else line[::2])
        return rows

    def screenshot(self, path, scale=2):
        rows = self.screen_rgb()
        raw = b''.join(b'\x00' + b''.join(bytes(c) * scale for c in r) for r in rows for _ in range(scale))
        def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
        w, h = 320 * scale, 200 * scale
        open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                               chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))

    # ---- M4 board ----
    def _rom(self): return self.roms[next(n for n, r in self.roms.items() if r[0x100:0x107] == b'M4 BOAR')]

    def _resp(self, status, data=b''):
        r = self._rom(); o = M4_RESPONSE - 0xC000
        r[o:o + 3] = bytes([len(data) + 6 & 0xff, 0, 0])
        r[o + 3] = status & 0xff
        r[o + 4:o + 6] = len(data).to_bytes(2, 'little')
        r[o + 6:o + 6 + len(data)] = data

    def _sock_entry(self, n, status=None, lastcmd=None, received=None, ip=None, port=None):
        r = self._rom(); o = M4_SOCKS - 0xC000 + 16 * n
        if status is not None: r[o] = status & 0xff
        if lastcmd is not None: r[o + 1] = lastcmd
        if received is not None: r[o + 2:o + 4] = min(received, 0xffff).to_bytes(2, 'little')
        if ip is not None: r[o + 4:o + 8] = bytes(reversed(ip))
        if port is not None: r[o + 8:o + 10] = port.to_bytes(2, 'little')

    def _m4_exec(self):
        p, self.m4_cmd = bytes(self.m4_cmd), bytearray()
        if len(p) < 3: return
        cmd = int.from_bytes(p[1:3], 'little')
        a = p[3:]
        if cmd == C_NETSOCKET:
            n = next((i for i in range(1, 5) if i not in self.socks), 255)
            if n != 255:
                self.socks[n] = {'sock': None, 'rx': bytearray(), 'state': 0}
                self._sock_entry(n, status=0, lastcmd=0, received=0)
            self._resp(n)
        elif cmd == C_NETCONNECT:
            n, ip, port = a[0], bytes(reversed(a[1:5])), int.from_bytes(a[5:7], 'little')
            s = self.socks.get(n)
            if not s: self._resp(255); return
            host, port = (self.relay if self.relay else (socket.inet_ntoa(ip), port))
            so = socket.socket(); so.setblocking(False)
            so.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            try: so.connect((host, port))
            except BlockingIOError: pass
            except OSError: self._sock_entry(n, status=0xF0, lastcmd=3); self._resp(0); return
            s['sock'], s['state'] = so, 1
            self._sock_entry(n, status=1, lastcmd=3, ip=ip, port=port)
            self._resp(0)
        elif cmd == C_NETSEND:
            n, size = a[0], int.from_bytes(a[1:3], 'little')
            s = self.socks.get(n)
            if not s or not s['sock'] or s['state'] != 0: self._resp(255); return
            try:
                s['sock'].sendall(a[3:3 + size])
                self._sock_entry(n, status=0, lastcmd=1); self._resp(0)
            except OSError:
                self._sock_entry(n, status=3, lastcmd=1); self._resp(255)
        elif cmd == C_NETRECV:
            n, size = a[0], int.from_bytes(a[1:3], 'little')
            s = self.socks.get(n)
            if not s: self._resp(255); return
            self._m4_poll()
            got = bytes(s['rx'][:min(size, 0x800)])
            del s['rx'][:len(got)]
            self._sock_entry(n, lastcmd=5, received=len(s['rx']))
            self._resp(0, got)
        elif cmd == C_NETCLOSE:
            s = self.socks.pop(a[0], None)
            if s and s['sock']: s['sock'].close()
            self._sock_entry(a[0], status=0, lastcmd=0, received=0)
            self._resp(0)
        elif cmd == C_NETHOSTIP:
            name = a.split(b'\0')[0].decode(errors='replace')
            try:
                ip = socket.inet_aton(self.relay[0] if self.relay else socket.gethostbyname(name))
                self._sock_entry(0, status=0, lastcmd=2, ip=ip)
            except OSError:
                self._sock_entry(0, status=0xF1, lastcmd=2)
            self._resp(1)                          # "lookup in progress": done by the next poll of entry 0
        else:
            self._resp(255)

    def _m4_poll(self):
        for n, s in self.socks.items():
            so = s['sock']
            if not so: continue
            if s['state'] == 1:                    # connect in progress
                _, w, _ = select.select([], [so], [], 0)
                if not w: continue
                err = so.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR)
                s['state'] = 0 if err == 0 else 3
                self._sock_entry(n, status=0 if err == 0 else 0xF0)
                continue
            if s['state'] == 3: continue
            r, _, _ = select.select([so], [], [], 0)
            if r:
                try: d = so.recv(4096)
                except OSError: d = b''
                if d: s['rx'] += d
                else:
                    s['state'] = 3
                    if not s['rx']: self._sock_entry(n, status=3)
            self._sock_entry(n, received=len(s['rx']))
            if s['state'] == 3 and not s['rx']: self._sock_entry(n, status=3)


def run_together(machines, addr, count=1, on_hit=None, max_seconds=60.0):
    """Step several machines side by side (never one alone: they may wait for
    each other over the network) until each has reached addr count times."""
    for m in machines: m.set_breakpoint(addr)
    hits = [0] * len(machines)
    end = max(m.now() for m in machines) + int(max_seconds * 4_000_000)
    while min(hits) < count:
        for i, m in enumerate(machines):
            if hits[i] >= count: continue
            a = m.step()
            if a is not None:
                if a == addr:
                    hits[i] += 1
                    if on_hit: on_hit(i, m)
        if min(m.now() for m in machines) > end: raise TimeoutError(f'hits {hits}')
