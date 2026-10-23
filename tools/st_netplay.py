#!/usr/bin/env python3
"""The Atari ST game with its network, to play by hand: Hatari in a window
on the desktop (under WSL: WSLg puts it on the Windows desktop), its
serial port a WiFi modem emulated by tools/st_modem.py, the game
(build/st/BOMBER.PRG, built if missing) pointed at the production relay.

  tools/st_netplay.py [--relay host:port] [--machine st|ste] [--display :0]

F12 opens Hatari's settings, ESC on the title quits the game, closing the
window ends it all. The modem's log: build/st/netplay/modem.log.
"""
import os, shutil, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import set_relay

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
os.chdir(ROOT)
arg = lambda name, default: sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default
relay = arg('--relay', 'api.mzpico.com:80')
machine = arg('--machine', 'st')
display = arg('--display', os.environ.get('DISPLAY', ':0'))
out = os.path.join(ROOT, 'build', 'st', 'netplay')
os.makedirs(out, exist_ok=True)

exe = os.path.join(ROOT, 'build', 'st', 'BOMBER.PRG')
if not os.path.exists(exe):
    r = subprocess.run([sys.executable, 'tools/build_st.py'], capture_output=True, text=True)
    if r.returncode: sys.exit('build failed: ' + r.stderr[-2000:])
hd, link = os.path.join(out, 'hd'), os.path.join(out, 'serial')
shutil.rmtree(hd, ignore_errors=True)
os.makedirs(os.path.join(hd, 'AUTO'))
shutil.copy(exe, os.path.join(hd, 'AUTO', 'BOMBER.PRG'))
set_relay.main([os.path.join(hd, 'AUTO', 'BOMBER.PRG'), relay])
tos = os.path.join(ROOT, 'build', 'st', 'rom', 'etos192uk.img' if machine == 'st' else 'etos256uk.img')
if not os.path.exists(tos): sys.exit(f'no {tos}: build EmuTOS (docs/port-atari-st.md)')

if os.path.lexists(link): os.remove(link)
modem = subprocess.Popen([sys.executable, 'tools/st_modem.py', link, '--log', os.path.join(out, 'modem.log')])
t = time.time()
while not os.path.exists(link) and time.time() - t < 5: time.sleep(0.1)
cfg = os.path.join(out, 'hatari.cfg')
if not os.path.exists(cfg): open(cfg, 'w').write('')            # Hatari keeps what is changed with F12 here
print(f'Hatari on {display}, relay {relay}; close its window to end', flush=True)
try:
    subprocess.run(['hatari', '--configfile', cfg, '--tos', tos, '--machine', machine, '--memsize', '1', '--harddrive', hd,
                    '--rs232-in', link, '--rs232-out', link, '--joy0', 'none', '--joy1', 'none', '--confirm-quit', 'no',
                    '--window', '--zoom', '2'], env=dict(os.environ, DISPLAY=display))
finally:
    modem.kill()
