#!/usr/bin/env python3
"""Two PCs (DOSBox-X, each with its own emulated Hayes modem on COM1:
serial1=modem, whose ATDT host:port opens a TCP connection) play a network
match through the relay, both in real time, untouched while they play;
their state hashes are compared at the end from the logs the PC_NETTEST
builds write (C:\\NET.LOG: the room's state every frame, written every 32).

  tools/pc_match.py [seconds]             default 30
  PLAYERS=4 LOCAL_HOST=2 LOCAL_JOIN=2 ... seats, as in tools/crossplay.py
  RELAY=api.mzpico.com:80                 the relay (default the local one, 127.0.0.1:8765)
  PC_VIDEO=cga|ega|vga  PC_CYCLES=3000    the adapter, DOSBox-X's speed
  PC_CPU=8088                             DOSBox-X's CPU type (default 286)
  PC_CYCLES_JOIN=3000                     the joiner's speed, if not PC_CYCLES (an XT host, a fast joiner)
  PC_NET=pkt                              Ethernet instead: an NE2000 (DOSBox-X's, slirp) and Crynwr's
                                          packet driver NE2000.COM (GPL; fetched from crynwr.com once,
                                          kept in build/pc/pktdrv); the local relay is then 10.0.2.2,
                                          slirp's address for the host
  AMIGA_DISPLAY=:97 AMIGA_DISPLAY2=:98    the two X displays (Xvfb), one PC each: the keys go to the window with the focus
"""
import os, shutil, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import set_relay

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
os.chdir(ROOT)
SECS = int(sys.argv[1]) if len(sys.argv) > 1 else 30
DISPLAYS = {'host': os.environ.get('AMIGA_DISPLAY', ':97'), 'join': os.environ.get('AMIGA_DISPLAY2', ':98')}
VIDEO = os.environ.get('PC_VIDEO', 'vga')
NET = os.environ.get('PC_NET', 'modem')
PKTDRV = os.path.join(ROOT, 'build', 'pc', 'pktdrv', 'NE2000.COM')


def packet_driver():
    """Crynwr's NE2000.COM, from the packet driver collection (pktd11.zip, GPL)"""
    if os.path.exists(PKTDRV): return PKTDRV
    import io, urllib.request, zipfile
    data = urllib.request.urlopen('http://crynwr.com/drivers/pktd11.zip', timeout=60).read()
    os.makedirs(os.path.dirname(PKTDRV), exist_ok=True)
    open(PKTDRV, 'wb').write(zipfile.ZipFile(io.BytesIO(data)).read('NE2000.COM'))
    return PKTDRV


class PC:
    def __init__(self, role, display=None):
        self.env = dict(os.environ, DISPLAY=display or DISPLAYS[role])
        out = os.path.join(ROOT, 'build', 'pc', f'match_{role}')
        benv = dict(os.environ, PLAYERS=os.environ.get('PLAYERS', '2'),
                    LOCAL=os.environ.get('LOCAL_HOST' if role == 'host' else 'LOCAL_JOIN', '1'))
        r = subprocess.run([sys.executable, 'tools/build_pc.py', '--net', role, '--out', out], capture_output=True, text=True, env=benv)
        if r.returncode: sys.exit(f'build ({role}) failed: ' + r.stdout[-2000:] + r.stderr[-1000:])
        relay = os.environ.get('RELAY', '127.0.0.1:8765')
        if NET == 'pkt': relay = relay.replace('127.0.0.1', '10.0.2.2')   # slirp: the host's own address
        set_relay.main([f'{out}/BOMBER.EXE', relay])
        self.cdir = f'{out}/c'
        shutil.rmtree(self.cdir, ignore_errors=True)
        os.makedirs(self.cdir)
        shutil.copy(f'{out}/BOMBER.EXE', f'{self.cdir}/BOMBER.EXE')
        if NET == 'pkt': shutil.copy(packet_driver(), self.cdir)
        open(f'{self.cdir}/CODE.TXT', 'w').write('????')        # made now: DOSBox caches the folder's files (the joiner waits for letters)
        conf = f'{out}/dosbox.conf'
        open(conf, 'w').write(f"""[sdl]
output=surface
autolock=false
[dosbox]
machine={'cga' if VIDEO == 'cga' else 'ega' if VIDEO == 'ega' else 'svga_s3'}
memsize=1
[cpu]
core=normal
cputype={os.environ.get('PC_CPU', '286')}
cycles=fixed {os.environ.get('PC_CYCLES_JOIN' if role == 'join' else 'PC_CYCLES', os.environ.get('PC_CYCLES', '3000'))}
[mixer]
nosound=true
[joystick]
joysticktype=none
[serial]
serial1={'disabled' if NET == 'pkt' else 'modem'}
serial2=disabled
[ne2000]
ne2000={'true' if NET == 'pkt' else 'false'}
nicbase=300
nicirq=3
macaddr=random
backend=slirp
[autoexec]
mount c "{self.cdir}"
c:
{'NE2000 0x60 3 0x300' if NET == 'pkt' else ''}
BOMBER.EXE
""")
        self.p = subprocess.Popen(['dosbox-x', '-conf', conf, '-fastlaunch', '-nopromptfolder', '-defaultmapper'],
                                  stdout=open(f'{out}/dosbox.log', 'w'), stderr=subprocess.STDOUT, env=self.env)
        t = time.time()
        while time.time() - t < 60 and not self.state(): time.sleep(0.5)
        if not self.state(): sys.exit(f'the {role} PC did not start (see {out}/dosbox.log)')
        time.sleep(1)
        wid = subprocess.run(['xdotool', 'search', '--onlyvisible', '--pid', str(self.p.pid)], capture_output=True, text=True, env=self.env).stdout.split()
        self.wid = wid[-1] if wid else None

    def lines(self):
        try:
            with open(f'{self.cdir}/NET.LOG', 'rb') as f: return f.read().decode('latin-1').split('\n')
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

    def key(self, name, down):
        if self.wid: subprocess.run(['xdotool', 'windowfocus', '--sync', self.wid], env=self.env, capture_output=True)
        subprocess.run(['xdotool', 'keydown' if down else 'keyup', name], env=self.env)

    def tap(self, name='space'):
        self.key(name, True); time.sleep(0.4); self.key(name, False)

    def close(self): self.p.kill()


if __name__ == '__main__':
    host = PC('host'); join = PC('join')
    try:
        time.sleep(1)
        for _ in range(8):                                    # host a room: SPACE until the code changes
            host.tap(); time.sleep(3)
            if host.state().get('code') not in (None, 'AAAA'): break
        code = host.state()['code']; print('host room', code, flush=True)
        open(f'{join.cdir}/CODE.TXT', 'w').write(code)
        time.sleep(2)
        for _ in range(8):                                    # join it: SPACE (the code is the build's), until seated
            join.tap(); time.sleep(1); join.tap(); time.sleep(3)
            if join.state().get('slot') == '1': break
        print('join slot', join.state().get('slot'), flush=True)
        time.sleep(4)
        for _ in range(12):                                   # ready: the joiner, then the host, until both are in the game
            if join.state().get('title') != '0': join.tap()     # a press in the game drops a bomb (the player dies at about frame 90)
            time.sleep(0.6)
            if host.state().get('title') != '0': host.tap()
            time.sleep(0.6)
            for _ in range(8):
                if host.state().get('title') == '0' and join.state().get('title') == '0': break
                time.sleep(0.5)
            else: continue
            break
        if host.state().get('title') != '0' or join.state().get('title') != '0': sys.exit('the match did not start')
        print(f'in game: delay host {host.state()["delay"]} join {join.state()["delay"]}', flush=True)
        t0 = time.time(); f0 = int(host.state()['frame']); time.sleep(SECS)
        time.sleep(3)                                         # the logs come every 32 frames
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
