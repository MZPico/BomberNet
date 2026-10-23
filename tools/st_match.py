#!/usr/bin/env python3
"""Two Atari STs (Hatari, each with a WiFi modem emulated on its serial
port: tools/st_modem.py) play a network match through the relay, both in
real time, untouched while they play; their state hashes are compared at
the end from the logs the ST_NETTEST builds write (C:\\NET.LOG: the room's
state every frame).

  tools/st_match.py [seconds]             default 30
  PLAYERS=4 LOCAL_HOST=2 LOCAL_JOIN=2 ... seats, as in tools/crossplay.py
  RELAY=api.mzpico.com:80                 the relay (default the local one, 127.0.0.1:8765)
  ST_MACHINE=ste                          the machine (default st)
  ST_DISPLAY=:98                          the X display (default AMIGA_DISPLAY, :97)
"""
import os, shutil, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import set_relay

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
os.chdir(ROOT)
SECS = int(sys.argv[1]) if len(sys.argv) > 1 else 30
DISPLAY = os.environ.get('ST_DISPLAY') or os.environ.get('AMIGA_DISPLAY', ':97')   # the invisible X display (Xvfb), never the desktop's
MACHINE = os.environ.get('ST_MACHINE', 'st')
TOS = os.path.join(ROOT, 'build', 'st', 'rom', 'etos192uk.img' if MACHINE == 'st' else 'etos256uk.img')


class ST:
    """an ST_NETTEST build in Hatari with its modem; keys as scan codes through Hatari's command FIFO"""
    def __init__(self, role, nomap=True):
        out = os.path.join(ROOT, 'build', 'st', f'match_{role}')
        benv = dict(os.environ, PLAYERS=os.environ.get('PLAYERS', '2'),
                    LOCAL=os.environ.get('LOCAL_HOST' if role == 'host' else 'LOCAL_JOIN', '1'))
        if nomap: benv['CFLAGS_ST'] = os.environ.get('CFLAGS_ST', '') + ' -DST_NETTEST_NOMAP'
        r = subprocess.run([sys.executable, 'tools/build_st.py', '--net', role, '--out', out], capture_output=True, text=True, env=benv)
        if r.returncode: sys.exit(f'build ({role}) failed: ' + r.stderr[-2000:])
        set_relay.main([f'{out}/BOMBER.PRG', os.environ.get('RELAY', '127.0.0.1:8765')])
        self.hd, self.fifo, link = f'{out}/hd', f'{out}/hatari.fifo', f'{out}/serial'
        shutil.rmtree(self.hd, ignore_errors=True)
        os.makedirs(f'{self.hd}/AUTO')
        shutil.copy(f'{out}/BOMBER.PRG', f'{self.hd}/AUTO/BOMBER.PRG')
        for f in (self.fifo, f'{out}/modem.log', link):
            if os.path.lexists(f): os.remove(f)       # a link left by the last run would point at a dead pty
        self.modem = subprocess.Popen([sys.executable, 'tools/st_modem.py', link, '--log', f'{out}/modem.log']
                                      + (['--trace'] if os.environ.get('ST_MODEM_TRACE') else []))
        t = time.time()
        while not os.path.exists(link) and time.time() - t < 5: time.sleep(0.1)
        cfg = f'{out}/hatari.cfg'
        open(cfg, 'w').write('')
        self.p = subprocess.Popen(['hatari', '--configfile', cfg, '--tos', TOS, '--machine', MACHINE, '--memsize', '1',
                                   '--harddrive', self.hd, '--fast-forward', 'no', '--sound', 'off', '--confirm-quit', 'no',
                                   '--statusbar', 'no', '--drive-led', 'no', '--window', '--cmd-fifo', self.fifo,
                                   '--rs232-in', link, '--rs232-out', link],
                                  stdout=open(f'{out}/hatari.log', 'w'), stderr=subprocess.STDOUT, env=dict(os.environ, DISPLAY=DISPLAY))
        t = time.time()
        while time.time() - t < 60 and not self.state(): time.sleep(0.5)
        if not self.state(): sys.exit(f'the {role} ST did not start (see {out}/hatari.log)')

    def lines(self):
        try:
            with open(f'{self.hd}/NET.LOG', 'rb') as f: return f.read().decode('latin-1').split('\n')
        except FileNotFoundError: return []

    def state(self):
        for l in reversed(self.lines()):
            p = l.split()
            if len(p) >= 20 and p[0] == 'frame': return dict(zip(p[0:20:2], p[1:20:2]))
        return {}

    def hashes(self):
        """frame -> hash for the hashed frames (every 16th), from the whole log"""
        h = {}
        for l in self.lines():
            p = l.split()
            if len(p) >= 20 and p[0] == 'frame' and p[3] == '0':
                d = dict(zip(p[0:20:2], p[1:20:2])); f = int(d['frame'])
                if f % 16 == 1: h[f - 1] = d['hash']          # the hash of frame f-1 is computed at its end, logged with the next
        return h

    def key(self, code, down):
        with open(self.fifo, 'w') as f: f.write(f'hatari-event {"keydown" if down else "keyup"} {code}\n')

    def tap(self, code=0x39):
        self.key(code, True); time.sleep(0.4); self.key(code, False)

    def close(self): self.p.kill(); self.modem.kill()


if __name__ == '__main__':
    host = ST('host'); join = ST('join')
    try:
        time.sleep(2)
        for _ in range(8):                                    # host a room: SPACE until the code changes
            host.tap(); time.sleep(3)
            if host.state().get('code') not in (None, 'AAAA'): break
        code = host.state()['code']; print('host room', code, flush=True)
        open(f'{join.hd}/CODE.TXT', 'w').write(code)
        time.sleep(2)
        for _ in range(8):                                    # join it: SPACE (the code is typed in by the build) until seated
            join.tap(); time.sleep(1); join.tap(); time.sleep(3)
            if join.state().get('slot') == '1': break
        print('join slot', join.state().get('slot'), flush=True)
        time.sleep(4)
        for _ in range(12):                                   # ready: the joiner, then the host, until both are in the game
            join.tap(); time.sleep(0.6); host.tap(); time.sleep(0.6)
            for _ in range(6):
                if host.state().get('title') == '0' and join.state().get('title') == '0': break
                time.sleep(0.5)
            else: continue
            break
        if host.state().get('title') != '0' or join.state().get('title') != '0': sys.exit('the match did not start')
        print(f'in game: delay host {host.state()["delay"]} join {join.state()["delay"]}', flush=True)
        t0 = time.time(); f0 = int(host.state()['frame']); time.sleep(SECS)
        hs, js = host.state(), join.state()
        hh, jh = host.hashes(), join.hashes()
        common = sorted(set(hh) & set(jh))
        mism = [f for f in common if hh[f] != jh[f]]
        print(f'host frame {hs["frame"]} abort {hs["abort"]}, join frame {js["frame"]} abort {js["abort"]}; '
              f'{len(common)} hashed frames compared, {len(mism)} mismatches'
              + (f' (first at {mism[0]})' if mism else '') + f'; {(int(hs["frame"]) - f0) / (time.time() - t0):.1f} frames a second', flush=True)
        sys.exit(1 if mism or hs['abort'] != '0' or js['abort'] != '0' or not common else 0)
    finally:
        host.close(); join.close()
