#!/usr/bin/env python3
"""The ST build played in Hatari, in real time: the title, SPACE to start a
preset 4-player deathmatch (an ST_PLAYTEST build: tools/build_st.py
--play), then key set A (cursor keys, SPACE), key set B (W A S D, E), and
the joysticks in ports 1 and 0 (Hatari's keyboard joystick, a run each:
the host's arrow keys and right Ctrl then are the joystick's, so they are
sent through X by xdotool); each input must move its player. The keys go
in as ST scan codes through Hatari's command FIFO; the program logs the
players' places to C:\\PLAY.LOG every frame. Last the sound: Hatari records
its output to a WAV (SDL's dummy audio driver: no sound device needed)
while the menu's cursor moves, and each beep's pitch and length are
measured against what the game asked for.

  tools/st_play.py [build/st/BOMBER.PRG]
"""
import os, shutil, struct, subprocess, sys, time, wave

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
exe = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'st', 'BOMBER.PRG')
hd = os.path.join(ROOT, 'build', 'st', 'hd')
fifo = os.path.join(ROOT, 'build', 'st', 'hatari.fifo')
DISPLAY = os.environ.get('AMIGA_DISPLAY', ':97')   # the invisible X display (Xvfb), never the desktop's
env = dict(os.environ, DISPLAY=DISPLAY)
tos = next((p for p in (os.path.join(ROOT, 'build', 'st', 'rom', 'etos192uk.img'), os.path.expanduser('~/src/emutos/etos192uk.img'))
            if os.path.exists(p)), None)
SCAN = {'Up': 0x48, 'Down': 0x50, 'Left': 0x4b, 'Right': 0x4d, 'space': 0x39, 'w': 0x11, 'a': 0x1e, 's': 0x1f, 'd': 0x20, 'e': 0x12}
fails = []


def check(what, ok):
    print(f'{"ok  " if ok else "FAIL"} {what}', flush=True)
    if not ok: fails.append(what)


class Hatari:
    def __init__(self, joy_port, wav=None):
        shutil.rmtree(hd, ignore_errors=True)
        os.makedirs(os.path.join(hd, 'AUTO'))
        shutil.copy(exe, os.path.join(hd, 'AUTO', 'BOMBER.PRG'))
        if os.path.exists(fifo): os.remove(fifo)
        cfg = os.path.join(ROOT, 'build', 'st', 'hatari.cfg')
        open(cfg, 'w').write(f'[Sound]\nbEnableSound = TRUE\nnPlaybackFreq = 44100\nszYMCaptureFileName = {wav}\n' if wav else '')
        cmd = ['hatari', '--configfile', cfg, '--tos', tos, '--machine', 'st', '--memsize', '1', '--harddrive', hd,
               '--fast-forward', 'no', '--confirm-quit', 'no', '--statusbar', 'no', '--drive-led', 'no',
               '--window', '--monitor', 'rgb', '--cmd-fifo', fifo, '--joy0', 'none', '--joy1', 'none']
        if joy_port is not None: cmd[cmd.index(f'--joy{joy_port}') + 1] = 'keys'
        if not wav: cmd += ['--sound', 'off']
        self.p = subprocess.Popen(cmd, stdout=open(os.path.join(ROOT, 'build', 'st', 'hatari-play.log'), 'w'),
                                  stderr=subprocess.STDOUT, env=dict(env, SDL_AUDIODRIVER='dummy'))
        t = time.time()
        while time.time() - t < 60 and not (os.path.exists(f'{hd}/PLAY.LOG') and os.path.getsize(f'{hd}/PLAY.LOG') > 0):
            time.sleep(0.5)
        time.sleep(1)
        wid = subprocess.run(['xdotool', 'search', '--onlyvisible', '--pid', str(self.p.pid)], capture_output=True, text=True, env=env).stdout.split()
        if wid: subprocess.run(['xdotool', 'windowfocus', '--sync', wid[-1]], env=env, capture_output=True)

    def command(self, what):
        with open(fifo, 'w') as f: f.write(what + '\n')

    def event(self, what): self.command(f'hatari-event {what}')

    def key(self, name, down): self.event(f'{"keydown" if down else "keyup"} {SCAN[name]}')

    def xkey(self, name, down): subprocess.run(['xdotool', 'keydown' if down else 'keyup', name], env=env)

    def close(self): self.p.kill(); time.sleep(1)


def hold(h, keys, t=0.9, x=False):
    for k in keys: (h.xkey if x else h.key)(k, True)
    time.sleep(t)
    for k in keys: (h.xkey if x else h.key)(k, False)
    time.sleep(0.4)


def state():
    """the last logged frame: (frame, title_mode, [(x, y, state)] x 4, flags)"""
    lines = [l for l in open(os.path.join(hd, 'PLAY.LOG')).read().split('\n') if l.strip()]
    v = list(map(int, lines[-2 if len(lines) > 1 else -1].split()))   # the last line may be half written
    return v[0], v[1], [(v[2 + 3 * i], v[3 + 3 * i], v[4 + 3 * i]) for i in range(4)], v[14]


def screenshot(name):
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'x11grab', '-video_size', '1024x768', '-i', DISPLAY,
                    '-frames:v', '1', os.path.join(ROOT, 'build', 'st', name)], env=env)


def game(joy_port):
    h = Hatari(joy_port)
    try:
        time.sleep(2)
        what = f' (keyboard joystick in port {joy_port})' if joy_port is not None else ''
        check(f'title screen{what}', state()[1] == 1)
        if joy_port is None:
            check('the YM in use', state()[3] & 2)
            screenshot('title.png')
        hold(h, ['space'], 0.3)            # long enough to be seen by a 60 ms frame (key set A fires with RETURN here)
        t = time.time()
        while time.time() - t < 20 and (state()[1] != 0 or state()[0] < 40): time.sleep(0.5)
        check('SPACE starts the game', state()[1] == 0)
        if joy_port is None:
            screenshot('game.png')
            moves = [(0, ['Right'], 0, +1), (0, ['Left'], 0, -1), (0, ['Down'], 1, +1), (0, ['Up'], 1, -1),
                     (1, ['a'], 0, -1), (1, ['d'], 0, +1), (1, ['w'], 1, -1), (1, ['s'], 1, +1)]   # from the top left and bottom right corners
        elif joy_port == 1:                        # player 3, from the top right corner: along its row first (a stop
            moves = [(2, ['Left'], 0, -1), (2, ['Right'], 0, +1), (2, ['Down'], 1, +1), (2, ['Up'], 1, -1)]   # short of it is a pillar's column)
        else:                                      # player 4, from the bottom left corner: up its column first
            moves = [(3, ['Up'], 1, -1), (3, ['Down'], 1, +1), (3, ['Right'], 0, +1), (3, ['Left'], 0, -1)]
        for n, (pl, keys, axis, sign) in enumerate(moves):    # out and back in pairs: back held longer, to the corner itself
            before = state()[2][pl]
            hold(h, keys, 1.6 if n & 1 else 0.9, x=joy_port is not None)
            after = state()[2][pl]
            check(f'player {pl + 1} {"+".join(keys)}: {before[:2]} -> {after[:2]}', (after[axis] - before[axis]) * sign > 0)
        if joy_port is None:
            hold(h, ['space', 'e'], 0.2)
            time.sleep(0.5); screenshot('bombs.png')
    finally:
        h.close()


def beeps(path):
    """(start s, length ms, pitch Hz) of each burst of sound in the WAV"""
    w = wave.open(path)
    rate, ch = w.getframerate(), w.getnchannels()
    raw = w.readframes(w.getnframes())
    x = struct.unpack('<%dh' % (len(raw) // 2), raw)[::ch]
    mean = sum(x) / len(x)
    x = [v - mean for v in x]
    th = max(abs(v) for v in x) * 0.3
    found, start, last = [], None, None
    for i, v in enumerate(x):
        if abs(v) > th:
            if start is None: start = i
            last = i
        elif start is not None and i - last > rate * 0.002:   # 2 ms of quiet ends a beep
            found.append((start, last)); start = None
    if start is not None: found.append((start, last))
    out = []
    for a, b in found:                     # the pitch: from the first rising edge to the last, over the edges between
        rises = [i for i in range(a + 1, b + 1) if x[i - 1] < 0 <= x[i]]
        hz = (len(rises) - 1) * rate / (rises[-1] - rises[0]) if len(rises) > 2 else 0
        out.append((a / rate, (b - a + 1) / rate * 1000, hz))
    return out


def sound():
    """the menu's beeps: Up/Down 0x020a, Left/Right 0x030a, 14 units (13 ms)"""
    wav = os.path.join(ROOT, 'build', 'st', 'sound.wav')
    if os.path.exists(wav): os.remove(wav)
    h = Hatari(None, wav)
    keys = [('Down', 0x020a), ('Down', 0x020a), ('Up', 0x020a), ('Right', 0x030a), ('Left', 0x030a)]
    try:
        time.sleep(2)
        h.command('hatari-shortcut recsound'); time.sleep(0.5)
        for k, _ in keys: hold(h, [k], 0.25); time.sleep(0.3)
        h.command('hatari-shortcut recsound'); time.sleep(1)
    finally:
        h.close()
    got = beeps(wav) if os.path.exists(wav) else []
    check(f'{len(got)} beeps recorded for {len(keys)} keys', len(got) == len(keys))
    for (k, ratio), (t, ms, hz) in zip(keys, got):
        want = 1108400 / ratio
        check(f'{k}: {ms:.1f} ms (13 asked), {hz:.0f} Hz ({want:.0f} asked)', 11 <= ms <= 16 and abs(hz - want) < want * 0.04)


game(None)
game(1)
game(0)
sound()
print('all inputs work' if not fails else f'{len(fails)} failed')
sys.exit(1 if fails else 0)
