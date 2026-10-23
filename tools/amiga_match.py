#!/usr/bin/env python3
"""Two Amigas (FS-UAE, its bsdsocket.library) play a network match through
the local relay, both in real time, untouched while they play; their state
hashes are compared at the end from the logs the AMIGA_NETTEST builds
write (DH0:net.log: the room's state every frame).

  tools/amiga_match.py [seconds]          default 30
  AMIGA_KICKSTART=rom AMIGA_KICKSTART_EXT=ext   a Kickstart (or AROS ROM) for FS-UAE; else FS-UAE's own AROS ROM
  AMIGA_MODEL=A500 ...                    the model (default A500)
  AMIGA_DISPLAY=:97 AMIGA_DISPLAY2=:98    the two X displays (Xvfb), one Amiga each
  PLAYERS=4 LOCAL_HOST=2 LOCAL_JOIN=2 ... seats, as in tools/crossplay.py
  RELAY=api.mzpico.com:80                 the relay (default the local one, 127.0.0.1:8765)
"""
import os, shutil, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import set_relay

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
os.chdir(ROOT)
SECS = int(sys.argv[1]) if len(sys.argv) > 1 else 30
for _n, _f in (('AMIGA_KICKSTART', 'aros-rom.bin'), ('AMIGA_KICKSTART_EXT', 'aros-ext.bin')):   # the AROS nightly ROM in build/amiga/rom
    if os.path.exists(f'{ROOT}/build/amiga/rom/{_f}'): os.environ.setdefault(_n, f'{ROOT}/build/amiga/rom/{_f}')   # (FS-UAE's bundled one crashes on a 68000)
DISPLAYS = {'host': os.environ.get('AMIGA_DISPLAY', ':97'), 'join': os.environ.get('AMIGA_DISPLAY2', ':98')}   # one invisible X display each:


class Amiga:
    def __init__(self, role):
        self.env = dict(os.environ, DISPLAY=DISPLAYS[role])
        out = os.path.join(ROOT, 'build', 'amiga', f'match_{role}')
        benv = dict(os.environ, PLAYERS=os.environ.get('PLAYERS', '2'),
                    LOCAL=os.environ.get('LOCAL_HOST' if role == 'host' else 'LOCAL_JOIN', '1'),
                    CFLAGS_AMIGA=os.environ.get('CFLAGS_AMIGA', '') + ' -DAMIGA_NETTEST_NOMAP')   # the map log would halve the speed
        r = subprocess.run([sys.executable, 'tools/build_amiga.py', '--net', role, '--out', out], capture_output=True, text=True, env=benv)
        if r.returncode: sys.exit(f'build ({role}) failed: ' + r.stderr[-2000:])
        set_relay.main([f'{out}/bomber', os.environ.get('RELAY', '127.0.0.1:8765')])
        self.dh0 = f'{out}/dh0'
        for d in ('C', 'S'): os.makedirs(f'{self.dh0}/{d}', exist_ok=True)
        shutil.copy(f'{out}/bomber', f'{self.dh0}/C/bomber')
        open(f'{self.dh0}/S/Startup-Sequence', 'w').write('C:bomber\n')
        for f in ('net.log', 'code.txt'):
            if os.path.exists(f'{self.dh0}/{f}'): os.remove(f'{self.dh0}/{f}')
        cmd = ['fs-uae', f'--base-dir={ROOT}/build/amiga/fsuae-base', f'--amiga-model={os.environ.get("AMIGA_MODEL", "A500")}',
               '--chip-memory=1024', '--fast-memory=8192', f'--hard-drive-0={self.dh0}', '--bsdsocket-library=1', '--fullscreen=0',
               '--video-sync=0', '--automatic-input-grab=0', '--joystick-port-1=none', '--window-width=480', '--window-height=300']
        if os.environ.get('AMIGA_KICKSTART'): cmd.append(f'--kickstart-file={os.environ["AMIGA_KICKSTART"]}')
        if os.environ.get('AMIGA_KICKSTART_EXT'): cmd.append(f'--kickstart-ext-file={os.environ["AMIGA_KICKSTART_EXT"]}')
        self.p = subprocess.Popen(cmd, stdout=open(f'{out}/fsuae.log', 'w'), stderr=subprocess.STDOUT, env=self.env)
        t = time.time()
        while time.time() - t < 90 and not (os.path.exists(f'{self.dh0}/net.log') and os.path.getsize(f'{self.dh0}/net.log') > 0):
            time.sleep(1)
        if not os.path.exists(f'{self.dh0}/net.log'): sys.exit(f'the {role} Amiga did not start')
        time.sleep(1)
        self.wid = subprocess.run(['xdotool', 'search', '--onlyvisible', '--pid', str(self.p.pid)], capture_output=True, text=True, env=self.env).stdout.split()[-1]

    def state(self):
        with open(f'{self.dh0}/net.log', 'rb') as f:
            f.seek(0, 2); n = f.tell(); f.seek(max(0, n - 8192)); lines = f.read().decode('latin-1').split('\n')
        for l in reversed(lines):
            p = l.split()
            if len(p) >= 20 and p[0] == 'frame': return dict(zip(p[0:20:2], p[1:20:2]))
        return {}

    def hashes(self):
        """frame -> hash for the hashed frames (every 16th), from the whole log"""
        h = {}
        for l in open(f'{self.dh0}/net.log', 'rb').read().decode('latin-1').split('\n'):
            p = l.split()
            if len(p) >= 20 and p[0] == 'frame' and p[3] == '0':
                d = dict(zip(p[0:20:2], p[1:20:2])); f = int(d['frame'])
                if f % 16 == 1: h[f - 1] = d['hash']          # the hash of frame f-1 is computed at its end, logged with the next
        return h

    def tap(self):
        subprocess.run(['xdotool', 'windowfocus', '--sync', self.wid], env=self.env, capture_output=True)
        subprocess.run(['xdotool', 'keydown', 'space'], env=self.env); time.sleep(0.3); subprocess.run(['xdotool', 'keyup', 'space'], env=self.env)

    def close(self): self.p.kill()


host = Amiga('host'); join = Amiga('join')
try:
    before = host.state().get('code')
    host.tap()
    for _ in range(40):
        if host.state().get('code') not in (before, None): break
        time.sleep(0.5)
    code = host.state()['code']; print('host room', code, flush=True)
    open(f'{join.dh0}/code.txt', 'w').write(code)
    time.sleep(2); join.tap(); time.sleep(1); join.tap()
    for _ in range(40):
        if join.state().get('slot') == '1': break
        time.sleep(0.5)
    print('join slot', join.state().get('slot'), flush=True)
    time.sleep(4)
    for _ in range(12):                                   # ready: the joiner, then the host, until both are in the game
        join.tap(); time.sleep(0.5); host.tap(); time.sleep(0.5)
        for _ in range(6):
            if host.state().get('title') == '0' and join.state().get('title') == '0': break
            time.sleep(0.5)
        else: continue
        break
    if host.state().get('title') != '0' or join.state().get('title') != '0': sys.exit('the match did not start')
    print(f'in game: delay host {host.state()["delay"]} join {join.state()["delay"]}', flush=True)
    t0 = time.time(); time.sleep(SECS)
    hs, js = host.state(), join.state()
    hh, jh = host.hashes(), join.hashes()
    common = sorted(set(hh) & set(jh))
    mism = [f for f in common if hh[f] != jh[f]]
    print(f'host frame {hs["frame"]} abort {hs["abort"]}, join frame {js["frame"]} abort {js["abort"]}; '
          f'{len(common)} hashed frames compared, {len(mism)} mismatches'
          + (f' (first at {mism[0]})' if mism else '') + f'; {float(hs["frame"]) / (time.time() - t0):.1f} frames a second', flush=True)
finally:
    host.close(); join.close()
