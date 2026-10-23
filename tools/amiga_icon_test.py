#!/usr/bin/env python3
"""The Amiga's icons and its start from Workbench, in FS-UAE with the AROS
ROM (which has icon.library but no Workbench to click in):

1. tools/amiga/icontest.c reads bomber.info and disk.info through
   icon.library, as Workbench does, logs their fields and draws their
   images in a window (a screenshot);
2. tools/amiga/wblaunch.c starts the game as Workbench does on a
   double-click (LoadSeg, a new process with the icon's stack, the
   WBStartup message); the title must come up, and ESC on it must end the
   program with the message replied.

  tools/amiga_icon_test.py          (builds build/amiga/bomber and the icons first)
"""
import os, subprocess, sys, time

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
os.chdir(ROOT)
T = 'build/amiga/icontest'
DISPLAY = os.environ.get('AMIGA_DISPLAY', ':97')   # the invisible X display (Xvfb), never the desktop's
env = dict(os.environ, DISPLAY=DISPLAY)
fails = []


def check(what, ok):
    print(f'{"ok  " if ok else "FAIL"} {what}', flush=True)
    if not ok: fails.append(what)


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode: sys.exit(f'{cmd[0]} failed: ' + r.stderr[-2000:])


os.makedirs(f'{T}/dh0/S', exist_ok=True)
run([sys.executable, 'tools/build_amiga.py', '--out', 'build/amiga/icontest/game'])
run([sys.executable, 'tools/make_amiga_icon.py', f'{T}/dh0/bomber.info', f'{T}/dh0/disk.info'])
for prog in ('icontest', 'wblaunch'):
    run(['tools/amiga-cc', '-O2', '-noixemul', '-m68000', '-Wall', f'tools/amiga/{prog}.c', '-o', f'{T}/dh0/{prog}'])
os.replace(f'{T}/game/bomber', f'{T}/dh0/bomber')


def amiga(startup, until, then=None, secs=90):
    """FS-UAE with DH0: running startup; until(): done waiting; then(pid): keys and screenshots"""
    open(f'{T}/dh0/S/Startup-Sequence', 'w').write(startup)
    cmd = ['fs-uae', f'--base-dir={ROOT}/build/amiga/fsuae-base', '--amiga-model=A500', '--chip-memory=1024', '--fast-memory=8192',
           f'--hard-drive-0={ROOT}/{T}/dh0', '--fullscreen=0', '--video-sync=0', '--automatic-input-grab=0', '--joystick-port-1=none']
    for opt, f in (('kickstart-file', 'aros-rom.bin'), ('kickstart-ext-file', 'aros-ext.bin')):   # the AROS nightly ROM
        if os.path.exists(f'build/amiga/rom/{f}'): cmd.append(f'--{opt}={ROOT}/build/amiga/rom/{f}')
    p = subprocess.Popen(cmd, stdout=open(f'{T}/fsuae.log', 'w'), stderr=subprocess.STDOUT, env=env)
    try:
        t = time.time()
        while time.time() - t < secs and not until(): time.sleep(1)
        if then: then(p.pid)
    finally:
        p.kill()


def shot(name):
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'x11grab', '-video_size', '1024x768', '-i', DISPLAY,
                    '-frames:v', '1', f'{T}/{name}.png'], env=env)


def text(name):
    try: return open(f'{T}/dh0/{name}').read()
    except FileNotFoundError: return ''


for f in ('icon.txt', 'launch.txt'):
    if os.path.exists(f'{T}/dh0/{f}'): os.remove(f'{T}/dh0/{f}')
amiga('DH0:icontest\n', lambda: 'drawn' in text('icon.txt'), lambda pid: (time.sleep(3), shot('icons')))
icon = text('icon.txt')
print(icon.strip())
check('bomber.info: a tool, 8 KB of stack, two 77 x 22 images of 2 planes',
      'bomber.info: magic e310 version 1 type 3 stack 8192 gadget 77x22 flags 0006' in icon and 'image 77x22x2 select yes 77x22x2' in icon)
check('disk.info: a disk with its drawer, one 40 x 20 image', 'disk.info: magic e310 version 1 type 1' in icon
      and 'image 40x20x2 select no drawer yes' in icon)


def esc(pid):
    time.sleep(10)
    shot('wb_title')
    wid = subprocess.run(['xdotool', 'search', '--onlyvisible', '--pid', str(pid)], capture_output=True, text=True, env=env).stdout.split()
    if wid: subprocess.run(['xdotool', 'windowfocus', '--sync', wid[-1]], env=env, capture_output=True)
    subprocess.run(['xdotool', 'keydown', 'Escape'], env=env); time.sleep(0.3); subprocess.run(['xdotool', 'keyup', 'Escape'], env=env)
    t = time.time()
    while time.time() - t < 15 and 'replied' not in text('launch.txt'): time.sleep(1)


amiga('DH0:wblaunch\n', lambda: 'started' in text('launch.txt'), esc)
launch = text('launch.txt')
check('started from Workbench with the stack of the icon', 'icon stack 8192' in launch and 'started' in launch)
check('ESC on the title ends it, the start message replied', 'replied' in launch)
print(f'screenshots: {T}/icons.png, {T}/wb_title.png')
print('all good' if not fails else f'{len(fails)} failed')
sys.exit(1 if fails else 0)
