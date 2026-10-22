#!/usr/bin/env python3
"""The Amiga build played in FS-UAE, in real time: the title, SPACE to
start a preset 4-player deathmatch (an AMIGA_PLAYTEST build:
tools/build_amiga.py --play), then key set A (cursor keys, SPACE), key set
B (W A S D, E) and joystick port 2 (FS-UAE's keyboard joystick: the
arrow keys with the right ALT, which then do not reach the Amiga as keys,
so the joystick is tested in a run of its own); each input must move its
player. The keys go to FS-UAE's window by xdotool on the X display; the
program logs the players' places to DH0:play.log every frame.

  tools/amiga_play.py [build/amiga/bomber]
"""
import os, shutil, subprocess, sys, time

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
exe = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'amiga', 'bomber')
dh0 = os.path.join(ROOT, 'build', 'amiga', 'dh0')
base = os.path.join(ROOT, 'build', 'amiga', 'fsuae-base')
DISPLAY = os.environ.get('AMIGA_DISPLAY', ':97')   # the invisible X display (Xvfb), never the desktop's
env = dict(os.environ, DISPLAY=DISPLAY)
fails = []


def check(what, ok):
    print(f'{"ok  " if ok else "FAIL"} {what}', flush=True)
    if not ok: fails.append(what)


def start(joystick):
    for d in (os.path.join(dh0, 'C'), os.path.join(dh0, 'S'), base): os.makedirs(d, exist_ok=True)
    shutil.copy(exe, os.path.join(dh0, 'C', 'bomber'))
    open(os.path.join(dh0, 'S', 'Startup-Sequence'), 'w').write('C:bomber\n')
    for f in ('play.log',):
        try: os.remove(os.path.join(dh0, f))
        except FileNotFoundError: pass
    cmd = ['fs-uae', f'--base-dir={base}', '--amiga-model=A500', '--chip-memory=1024', '--fast-memory=8192',
           f'--hard-drive-0={dh0}', '--fullscreen=0', '--video-sync=0', '--automatic-input-grab=0', '--floppy-drive-speed=0',
           '--window-width=640', '--window-height=400']
    if joystick: cmd += ['--joystick-port-1=keyboard', '--joystick-port-1-mode=joystick']
    else: cmd += ['--joystick-port-1=none']
    log = open(os.path.join(ROOT, 'build', 'amiga', 'fsuae-play.log'), 'w')
    p = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT, env=env)
    t = time.time()
    while time.time() - t < 60:                                  # until the program logs (the title screen)
        time.sleep(1)
        if os.path.exists(os.path.join(dh0, 'play.log')) and os.path.getsize(os.path.join(dh0, 'play.log')) > 0: break
    time.sleep(1)
    wid = subprocess.run(['xdotool', 'search', '--name', 'FS-UAE'], capture_output=True, text=True, env=env).stdout.split()
    if wid: subprocess.run(['xdotool', 'windowfocus', '--sync', wid[0]], env=env, capture_output=True)
    return p


def key(name, down):
    subprocess.run(['xdotool', 'keydown' if down else 'keyup', name], env=env)


def hold(keys, t=0.9):
    for k in keys: key(k, True)
    time.sleep(t)
    for k in keys: key(k, False)
    time.sleep(0.4)


def state():
    """the last logged frame: (frame, title_mode, [(x, y, state)] x 4)"""
    lines = [l for l in open(os.path.join(dh0, 'play.log')).read().split('\n') if l.strip()]
    v = list(map(int, lines[-1].split()))
    return v[0], v[1], [(v[2 + 3 * i], v[3 + 3 * i], v[4 + 3 * i]) for i in range(4)]


def screenshot(name):
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'x11grab', '-video_size', '1024x768', '-i', DISPLAY,
                    '-frames:v', '1', os.path.join(ROOT, 'build', 'amiga', name)], env=env)


def game(joystick):
    p = start(joystick)
    try:
        time.sleep(2)
        check(f'title screen{" (keyboard joystick)" if joystick else ""}', state()[1] == 1)
        flags = int(open(os.path.join(dh0, 'play.log')).read().split('\n')[0].split()[-1])
        if not joystick: check(f'an audio channel allocated (channel {flags >> 2})', flags & 2)
        if not joystick: screenshot('title.png')
        hold(['space'], 0.15)
        t = time.time()
        while time.time() - t < 20 and (state()[1] != 0 or state()[0] < 40): time.sleep(0.5)
        check('SPACE starts the game', state()[1] == 0)
        if not joystick: screenshot('game.png')
        if joystick:
            moves = [(2, ['Left'], 0, -1), (2, ['Right'], 0, +1), (2, ['Down'], 1, +1), (2, ['Up'], 1, -1)]   # from the top right corner: along the
                                                                                                          # corner's row first (a stop short of it is a pillar's row)
        else:
            moves = [(0, ['Right'], 0, +1), (0, ['Left'], 0, -1), (0, ['Down'], 1, +1), (0, ['Up'], 1, -1),
                     (1, ['a'], 0, -1), (1, ['d'], 0, +1), (1, ['w'], 1, -1), (1, ['s'], 1, +1)]   # from the bottom right corner
        for pl, keys, axis, sign in moves:
            before = state()[2][pl]
            hold(keys)
            after = state()[2][pl]
            check(f'player {pl + 1} {"+".join(keys)}: {before[:2]} -> {after[:2]}', (after[axis] - before[axis]) * sign > 0)
        if not joystick:
            hold(['space', 'e'], 0.2)
            time.sleep(0.5); screenshot('bombs.png')
        else:
            hold(['Alt_R'], 0.2)
    finally:
        p.kill()
        time.sleep(1)


game(False)
game(True)
print('all inputs work' if not fails else f'{len(fails)} failed')
sys.exit(1 if fails else 0)
