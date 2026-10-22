"""VICE (x64sc) driven from Python for the C64 port's tests: started
headless, controlled through its text remote monitor.

  v = Vice('build/c64/bomber.prg', warp=False)
  v.mem(addr, n); v.poke(addr, data)
  v.key('SPACE', 1); v.joy(2, 'up', 'fire')     # held until changed
  v.run(seconds); v.screenshot('x.png'); v.close()
  Vice(prg, wic64=True, log='vice.log')          # a WiC64 on the user port (its trace to log)
  Vice(prg, uci=True)                            # an Ultimate's network (its command interface, the VICE patch)

Keys go into the keyboard matrix through a local patch of the monitor's
keybuf command (keybuf "#row col value", see docs/port-c64.md), as the
program scans the matrix itself, and the joysticks through the same patch
(keybuf "#J port bits").

VICE: x64sc 3.9 (default ~/src/vice/dist/bin/x64sc; VICE=path). It runs with
+logcolorize: 3.9 crashes logging in colour to a pipe."""
import os, re, socket, subprocess, time

X64 = os.environ.get('VICE', os.path.expanduser('~/src/vice/dist/bin/x64sc'))
SDK = os.environ.get('LLVM_MOS', os.path.expanduser('~/src/llvm-mos-dl/llvm-mos'))

# key name -> (row, column) of the C64's matrix
KEYS = {'DEL': (0, 0), 'RETURN': (0, 1), 'CRSR_RIGHT': (0, 2), 'F7': (0, 3), 'F1': (0, 4), 'F3': (0, 5), 'F5': (0, 6),
        'CRSR_DOWN': (0, 7), 'LSHIFT': (1, 7), 'RSHIFT': (6, 4), 'SPACE': (7, 4), 'RUNSTOP': (7, 7), 'CTRL': (7, 2)}
for r, cols in ((1, '3WA4ZSE'), (2, '5RD6CFTX'), (3, '7YG8BHUV'), (4, '9IJ0MKON'), (5, '+PL-.:@,')):
    for c, ch in enumerate(cols): KEYS.setdefault(ch, (r, c))
for ch, rc in (('1', (7, 0)), ('2', (7, 3)), ('Q', (7, 6))): KEYS[ch] = rc
JOY = {'up': 1, 'down': 2, 'left': 4, 'right': 8, 'fire': 16}


def symbols(prg):
    """the program's symbols (llvm-nm of its .elf)"""
    out = subprocess.run([f'{SDK}/bin/llvm-nm', prg + '.elf'], capture_output=True, text=True).stdout
    return {p[2]: int(p[0], 16) for p in (l.split() for l in out.splitlines()) if len(p) == 3}


class Vice:
    def __init__(self, prg, warp=False, ntsc=False, port=None, wic64=False, log=None, uci=False):
        self.port = port or int(os.environ.get('VICE_MON_PORT', 6510))
        self.prg = os.path.abspath(prg)
        self.sym = symbols(self.prg)
        cmd = [X64, '+logcolorize', '-default', '-sounddev', 'dummy', '-remotemonitor', '-remotemonitoraddress',
               f'ip4://127.0.0.1:{self.port}',
               '-autostartprgmode', '1', '-ntsc' if ntsc else '-pal', *(['-warp'] if warp else []),
               *(['-userportdevice', '23'] if wic64 else []),     # USERPORT_DEVICE_WIC64
               *(['-wic64trace', '-wic64tracelevel', '1'] if wic64 and log else []), '-autostart', self.prg]
        out = open(log, 'w') if log else subprocess.DEVNULL
        env = dict(os.environ, UCI_EMU='1') if uci else None      # the Ultimate's command interface (the VICE patch)
        self.p = subprocess.Popen(cmd, stdout=out, stderr=subprocess.STDOUT, env=env)
        self.s = None
        for _ in range(300):                          # up to 30 s (a busy host)
            try: self._connect(); break
            except OSError: time.sleep(0.1)
        self.cmd('r')                                 # the machine stops while connected
        for p in (1, 2): self.joy(p)

    # ---- the monitor: the machine stands still from a command until run()
    # The first answer on a connection is a prompt, the answer and a prompt;
    # later ones the answer and a prompt.
    def _read(self, want=b''):
        buf, prompts = b'', 2 if self.fresh else 1
        self.fresh = False
        while not (re.search(rb'\(C:\$[0-9a-f]{4}\) $', buf) and want in buf
                   and len(re.findall(rb'\(C:\$[0-9a-f]{4}\) ', buf)) >= prompts):
            d = self.s.recv(65536)
            if not d: break
            buf += d
        return buf.decode('latin-1')

    def _connect(self):
        self.s = socket.create_connection(('127.0.0.1', self.port), timeout=10)
        self.fresh = True

    def cmd(self, c, want=b''):
        if self.s is None: self._connect()
        self.s.sendall(c.encode() + b'\n')
        return self._read(want)

    def run(self, seconds=0):
        """let the machine run (the monitor closed), for seconds of host time"""
        if self.s: self.s.sendall(b'x\n'); self.s.close(); self.s = None
        if seconds: time.sleep(seconds)

    def mem(self, addr, n):
        if isinstance(addr, str): addr = self.sym[addr]
        out = bytearray()
        last = f'>C:{addr + (n - 1) // 16 * 16:04x}'.encode()
        for line in self.cmd(f'm {addr:04x} {addr + n - 1:04x}', last).splitlines():
            m = re.search(r'>C:([0-9a-f]{4})\s+((?:[0-9a-f]{2}\s+)+)', line)
            if m: out += bytes(int(x, 16) for x in m.group(2).split()[:16])
        return bytes(out[:n])

    def u8(self, addr): return self.mem(addr, 1)[0]
    def u16(self, addr): return int.from_bytes(self.mem(addr, 2), 'little')

    def poke(self, addr, data):
        if isinstance(addr, str): addr = self.sym[addr]
        self.cmd(f'> {addr:04x} ' + ' '.join(f'{b:02x}' for b in data))

    def key(self, name, down=1):
        r, c = KEYS[name]
        self.cmd(f'keybuf "#{r} {c} {down}"')

    def joy(self, port, *dirs):
        """port 1 or 2 held in these directions (and fire); none: centred"""
        self.cmd(f'keybuf "#J {port} {sum(JOY[d] for d in dirs)}"')

    def screenshot(self, path):
        self.cmd(f'screenshot "{os.path.abspath(path)}" 2')
        time.sleep(0.5)

    def close(self):
        try:
            if self.s is None: self._connect()
            self.s.sendall(b'quit\n'); time.sleep(0.5)
        except OSError: pass
        self.p.kill()
