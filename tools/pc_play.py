#!/usr/bin/env python3
"""The PC build played in DOSBox-X, in real time: the title, SPACE to start
a preset 4-player deathmatch (a PC_PLAYTEST build: tools/build_pc.py
--play), then key set A (cursor keys; this preset fires with RETURN) and
key set B (W A S D, E); each must move its player; ESC leaves the match,
and ESC on the title ends the program. The keys go to DOSBox-X's window
by xdotool (the window alone on its X display); the program logs the
players' places to C:\\PLAY.LOG. Then the sound: DOSBox-X records its
output (DX-CAPTURE /A, the dummy SDL audio driver: no sound device) while
the menu's cursor moves, and each beep's length (its envelope) and pitch
(its spectrum's peak) are measured against what the game asked for.
The joysticks are not tested: DOSBox-X emulates the game port only with a
host joystick, and there is none here.

  tools/pc_play.py [build/pc/BOMBER.EXE] [--cycles n] [--video ega|vga]
"""
import glob, os, shutil, subprocess, sys, time, wave

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
arg = lambda name, default: sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default
args = [a for i, a in enumerate(sys.argv[1:], 1) if not a.startswith('--') and not sys.argv[i - 1].startswith('--')]
exe = args[0] if args else os.path.join(ROOT, 'build', 'pc', 'BOMBER.EXE')
cycles, video = arg('--cycles', '3000'), arg('--video', 'vga')
DISPLAY = os.environ.get('AMIGA_DISPLAY', ':97')   # the invisible X display (Xvfb), never the desktop's
env = dict(os.environ, DISPLAY=DISPLAY)
cdir = os.path.join(ROOT, 'build', 'pc', 'c')
fails = []


def check(what, ok):
    print(f'{"ok  " if ok else "FAIL"} {what}', flush=True)
    if not ok: fails.append(what)


def start(capture=None):
    shutil.rmtree(cdir, ignore_errors=True)
    os.makedirs(cdir)
    shutil.copy(exe, os.path.join(cdir, 'BOMBER.EXE'))
    if capture: shutil.rmtree(capture, ignore_errors=True); os.makedirs(capture)
    conf = os.path.join(ROOT, 'build', 'pc', 'dosbox-play.conf')
    open(conf, 'w').write(f"""[sdl]
output=surface
autolock=false
[dosbox]
machine={'ega' if video == 'ega' else 'svga_s3'}
memsize=1
captures={capture or cdir}
[cpu]
core=normal
cputype=286
cycles=fixed {cycles}
[mixer]
nosound={'false' if capture else 'true'}
rate=44100
[speaker]
pcspeaker={'true' if capture else 'false'}
[joystick]
joysticktype=none
[autoexec]
mount c "{cdir}"
c:
{'DX-CAPTURE /A ' if capture else ''}BOMBER.EXE
""")
    p = subprocess.Popen(['dosbox-x', '-conf', conf, '-fastlaunch', '-nopromptfolder', '-nolog', '-defaultmapper'],
                         stdout=open(os.path.join(ROOT, 'build', 'pc', 'dosbox-play.log'), 'w'), stderr=subprocess.STDOUT,
                         env=dict(env, SDL_AUDIODRIVER='dummy'))
    t = time.time()
    while time.time() - t < 60 and not (os.path.exists(f'{cdir}/PLAY.LOG') and os.path.getsize(f'{cdir}/PLAY.LOG') > 0):
        time.sleep(0.5)
    time.sleep(1)
    wid = subprocess.run(['xdotool', 'search', '--onlyvisible', '--pid', str(p.pid)], capture_output=True, text=True, env=env).stdout.split()
    if wid: subprocess.run(['xdotool', 'windowfocus', '--sync', wid[-1]], env=env, capture_output=True)
    return p


def key(name, down): subprocess.run(['xdotool', 'keydown' if down else 'keyup', name], env=env)


def hold(keys, t=0.9):
    for k in keys: key(k, True)
    time.sleep(t)
    for k in keys: key(k, False)
    time.sleep(0.4)


def state():
    lines = [l for l in open(os.path.join(cdir, 'PLAY.LOG')).read().split('\n') if l.strip()]
    v = list(map(int, lines[-2 if len(lines) > 1 else -1].split()))   # the last line may be half written
    return v[0], v[1], [(v[2 + 3 * i], v[3 + 3 * i], v[4 + 3 * i]) for i in range(4)], v[14]


def screenshot(name):
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'x11grab', '-video_size', '1024x768', '-i', DISPLAY,
                    '-frames:v', '1', os.path.join(ROOT, 'build', 'pc', name)], env=env)


p = start()
try:
    time.sleep(2)
    check('title screen', state()[1] == 1)
    screenshot('title.png')
    hold(['space'], 0.3)
    t = time.time()
    while time.time() - t < 20 and (state()[1] != 0 or state()[0] < 40): time.sleep(0.5)
    check('SPACE starts the game', state()[1] == 0)
    screenshot('game.png')
    moves = [(0, ['Right'], 0, +1), (0, ['Left'], 0, -1), (0, ['Down'], 1, +1), (0, ['Up'], 1, -1),
             (1, ['a'], 0, -1), (1, ['d'], 0, +1), (1, ['w'], 1, -1), (1, ['s'], 1, +1)]   # from the top left and bottom right corners
    for n, (pl, keys, axis, sign) in enumerate(moves):     # out and back in pairs: back held longer, to the corner itself
        before = state()[2][pl]
        hold(keys, 1.6 if n & 1 else 0.9)
        after = state()[2][pl]
        check(f'player {pl + 1} {"+".join(keys)}: {before[:2]} -> {after[:2]}', (after[axis] - before[axis]) * sign > 0)
    hold(['Return', 'e'], 0.2)
    time.sleep(0.5); screenshot('bombs.png')
    hold(['Escape'], 0.3); time.sleep(1)                   # ESC: leaves the match (to the title)
    t = time.time()
    while time.time() - t < 10 and state()[1] != 1: time.sleep(0.5)
    check('ESC leaves the match', state()[1] == 1)
    hold(['Escape'], 0.3); time.sleep(3)                   # ESC on the title: back to DOS
    screenshot('dos.png')
finally:
    p.kill()


def beeps(path):
    """(start s, length ms, pitch Hz) of each burst of sound: the length from the envelope, the pitch the spectrum's peak"""
    import numpy as np
    w = wave.open(path)
    rate, ch = w.getframerate(), w.getnchannels()
    x = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2')[::ch].astype(float)
    x -= x.mean()
    loud = np.abs(x) > np.abs(x).max() * 0.3
    out, i, n = [], 0, len(x)
    while i < n:
        if not loud[i]: i += 1; continue
        a = last = i
        while i < n and i - last <= rate * 0.002:        # 2 ms of quiet ends a beep
            if loud[i]: last = i
            i += 1
        seg = x[a:last + 1]
        spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 1 << 16))
        f = np.fft.rfftfreq(1 << 16, 1 / rate)
        band = (f > 200) & (f < 8000)
        out.append((a / rate, (last - a + 1) / rate * 1000, f[band][spec[band].argmax()]))
    return out


p = start(os.path.join(ROOT, 'build', 'pc', 'capture'))       # the sound: the menu's beeps recorded, ESC ends it
keys = [('Down', 0x020a), ('Down', 0x020a), ('Up', 0x020a), ('Right', 0x030a), ('Left', 0x030a)]
try:
    time.sleep(2)
    for k, _ in keys: hold([k], 0.25); time.sleep(0.3)
    hold(['Escape'], 0.3); time.sleep(3)
finally:
    p.kill()
wavs = glob.glob(os.path.join(ROOT, 'build', 'pc', 'capture', '*.wav'))
got = beeps(wavs[0]) if wavs else []
check(f'{len(got)} beeps recorded for {len(keys)} keys', len(got) == len(keys))
for (k, ratio), (t, ms, hz) in zip(keys, got):
    want = 1108400 / ratio
    check(f'{k}: {ms:.1f} ms (13 asked), {hz:.0f} Hz ({want:.0f} asked)', 11 <= ms <= 16 and abs(hz - want) < want * 0.02)
print('all inputs work' if not fails else f'{len(fails)} failed')
sys.exit(1 if fails else 0)
