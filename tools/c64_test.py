#!/usr/bin/env python3
"""Run a C64_TEST build (tools/build_c64.py --test) in VICE and print its
figures: the recording replayed with the hashes checked, each game frame's
work (flush .. next frame sync) in cycles, as wall time on a PAL C64.

  tools/c64_test.py [build/c64/bomber.prg] [--ntsc] [--png out.png]

VICE: x64sc from VICE 3.9 (default ~/src/vice/dist/bin/x64sc, headless; +logcolorize: 3.9 crashes
logging in colour to a pipe), run
in warp (the figures are emulated cycles); its text remote monitor reads
c64_test once done is 1. VICE=path for another x64sc."""
import os, re, socket, subprocess, sys, time

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
SDK = os.environ.get('LLVM_MOS', os.path.expanduser('~/src/llvm-mos-dl/llvm-mos'))
X64 = os.environ.get('VICE', os.path.expanduser('~/src/vice/dist/bin/x64sc'))
args = [a for i, a in enumerate(sys.argv[1:], 1) if not a.startswith('--') and sys.argv[i - 1] != '--png']
prg = os.path.abspath(args[0] if args else os.path.join(ROOT, 'build', 'c64', 'bomber.prg'))
png = sys.argv[sys.argv.index('--png') + 1] if '--png' in sys.argv else os.path.join(ROOT, 'build', 'c64', 'test.png')
PORT = int(os.environ.get('VICE_MON_PORT', 6510))

nm = subprocess.run([f'{SDK}/bin/llvm-nm', prg + '.elf'], capture_output=True, text=True).stdout
T = int(re.search(r'^([0-9a-f]+) \w c64_test$', nm, re.M).group(1), 16)


class Monitor:
    """VICE's text remote monitor: each connection stops the machine until 'x'.
    Its first prompt comes only with the first answer, so an answer is read
    until its own text and the prompt after it."""
    def __init__(self):
        self.s = socket.create_connection(('127.0.0.1', PORT), timeout=10)

    def read_until(self, pat, want=b''):
        buf = b''
        while not (re.search(pat, buf) and want in buf):
            d = self.s.recv(65536)
            if not d: break
            buf += d
        return buf.decode('latin-1')

    def cmd(self, c, want=b''):
        self.s.sendall(c.encode() + b'\n')
        return self.read_until(rb'\(C:\$[0-9a-f]{4}\) $', want)

    def mem(self, addr, n):
        out = bytearray()
        last = f'>C:{addr + (n - 1) // 16 * 16:04x}'.encode()   # the answer's last line (16 bytes a line from addr)
        for line in self.cmd(f'm {addr:04x} {addr + n - 1:04x}', last).splitlines():
            m = re.search(r'>C:([0-9a-f]{4})\s+((?:[0-9a-f]{2}\s+)+)', line)
            if m: out += bytes(int(x, 16) for x in m.group(2).split()[:16])
        return bytes(out[:n])

    def go(self):
        self.s.sendall(b'x\n'); self.s.close()


cmd = [X64, '+logcolorize', '-default', '-sounddev', 'dummy', '-warp', '-remotemonitor', '-remotemonitoraddress', f'ip4://127.0.0.1:{PORT}',
       '-autostartprgmode', '1', '-ntsc' if '--ntsc' in sys.argv else '-pal', '-autostart', prg]
vice = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
t0 = time.time()
try:
    while True:
        time.sleep(2)
        try: m = Monitor()
        except OSError: continue
        head = m.mem(T, 12)
        if head[0] or time.time() - t0 > 600: break
        m.go()
    n = head[6] | head[7] << 8
    work = [w[0] | w[1] << 8 for w in zip(*[iter(m.mem(T + 12, 2 * n))] * 2)]
    m.cmd(f'screenshot "{os.path.abspath(png)}" 2'); time.sleep(1)
    m.s.sendall(b'quit\n')
finally:
    time.sleep(1); vice.kill()
hz = 1022727 if head[1] else 985248
bad, late, flush = head[2] | head[3] << 8, head[4] | head[5] << 8, int.from_bytes(head[8:12], 'little')
if not head[0]: sys.exit(f'not done after {time.time() - t0:.0f} s; {n} frames timed')
ws = sorted(work)
ms = lambda c: c / hz * 1000
q = lambda f: ws[min(len(ws) - 1, int(len(ws) * f))]
print(f'{os.path.basename(prg)} ({"NTSC" if head[1] else "PAL"}): {n} game frames, hashes {"MISMATCH at offset " + str(bad) if bad else "all equal"}, '
      f'late {late}')
print(f'  work a frame: median {ms(q(.5)):.1f} ms, p90 {ms(q(.9)):.1f}, p99 {ms(q(.99)):.1f}, max {ms(ws[-1]):.1f}'
      f'{" (or more: capped at 65535 cycles)" if ws[-1] == 0xffff else ""}; over 60 ms {sum(ms(w) > 60 for w in work)}; '
      f'the flush {ms(flush / n):.1f} ms on average')
print(f'  screenshot {png}; {time.time() - t0:.0f} s')
sys.exit(1 if bad else 0)
