#!/usr/bin/env python3
"""End-to-end test of the MZPico NET protocol: two headless mz800emu instances
(Unicard with [UNICARD] mzpico_mode = 1, net_relay = 127.0.0.1:8766) talk
through the local reference relay, driven by port I/O from the MCP driver.

  build/venv/bin/uvicorn --app-dir relay relay:app --port 8765 &
  MZ800EMU_CFG=build/emucfg_net tools/nettest.py
"""
import sys, time, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu import Emu
CMD, DATA = 0x50, 0x51
fails = 0
def check(cond, what):
    global fails
    print(('ok   ' if cond else 'FAIL ') + what)
    if not cond: fails += 1

class Dev:
    def __init__(self):
        self.e = Emu(); self.e.load_mzf('build/c/bomber.mzf')
    def out(self, port, v): self.e.data('io_write', {'port': port, 'value': v})
    def inp(self, port): return self.e.data('io_read', {'port': port})['value']
    def status(self):
        self.out(CMD, 0x03); return [self.inp(CMD) for _ in range(4)]
    def do(self, c, params=b'', n=0, timeout=5):
        self.out(CMD, c)
        for b in params: self.out(DATA, b)
        t = time.time()
        while True:
            st = self.status()
            if st[0] & 0x40 and time.time() - t < timeout: time.sleep(0.05); continue
            break
        if st[0] & 0x80: return st, None
        return st, (bytes(self.inp(DATA) for _ in range(n)) if st[0] & 0x02 else b'')
w16 = lambda v: bytes([v & 255, v >> 8])

a, b = Dev(), Dev()
try:
    st, d = a.do(0x06, n=4); check(d and d[2] == 0x4d, 'REVD identifies an MZPico')
    st, d = a.do(0x95, n=16); check(d and d[3] & 0x08, 'INFO has the NET feature bit')
    st, d = a.do(0xa1, w16(0x424e) + w16(0x0101) + bytes([2, 1, 2, 7, 9]) + bytes(14), n=6)
    check(d and len(d) == 6 and d[4] == 0x0d, 'CREATE returns a room code'); code = d[:4].decode()
    st, d = b.do(0xa2, w16(0x424e) + w16(0x0202) + code.encode() + b'\r', n=20)
    check(st[0] & 0x80 and st[2] == 6, 'JOIN with another build -> error 6')
    st, d = b.do(0xa2, w16(0x424e) + w16(0x0101) + code.encode() + b'\r', n=20)
    check(d and d[0] == 1 and d[4:6] == bytes([7, 9]), 'JOIN -> slot 1 with the settings')
    a.do(0xa4, bytes([1]), n=4); b.do(0xa4, bytes([1]), n=4); time.sleep(0.3)
    st, da = a.do(0xa4, bytes([1]), n=4); st, db = b.do(0xa4, bytes([1]), n=4)
    check(da == db and da[:2] != b'\xff\xff', 'READY -> both get the same seed')
    st, d = a.do(0xa0, n=8); check(d[0] == 3 and d[2] == 2, 'STATUS RUNNING with 2 members')
    for f in range(3):
        a.do(0xa5, w16(f) + bytes([0x10 + f, 0, 0, 0])); b.do(0xa5, w16(f) + bytes([0x20 + f, 0, 0, 0]))
    time.sleep(0.3)
    st, d = a.do(0xa6, w16(1), n=4); check(d[:2] == w16(2) and list(d[2:4]) == [0x11, 0x21], 'POLL(1) has both inputs, avail 2')
    st, d = b.do(0xa6, w16(7), n=4); check(d[:2] == w16(2), 'POLL(7) reports avail 2 only')
    a.do(0xa7, w16(3) + w16(1)); b.do(0xa7, w16(3) + w16(2)); time.sleep(0.3)
    st, d = a.do(0xa0, n=8); check(d[0] == 4, 'hash mismatch -> DESYNC')
    a.do(0xa8, bytes([1, 2]) + b'HI' + bytes(30)); time.sleep(0.3)
    st, d = b.do(0xa9, n=34); check(d[0] == 0 and d[2:4] == b'HI', 'message A -> B via RECV')
    st, d = b.do(0xa9, n=34); check(d[0] == 0xff, 'RECV with nothing pending')
    b.do(0xa3); time.sleep(0.3)
    st, d = a.do(0xa0, n=8); check(d[0] == 5, 'LEAVE -> the other side is DROPPED')
finally:
    a.e.close(); b.e.close()
print('FAILURES:', fails); sys.exit(1 if fails else 0)
