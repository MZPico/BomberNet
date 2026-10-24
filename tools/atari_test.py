#!/usr/bin/env python3
"""Run an ATARI_TEST build (tools/build_atari.py --test) in atari800 and read
its verdict: the build replays its recording from the title on, checks every
hash, and at the end turns the border green (all equal), yellow (all equal,
some frames over 60 ms of the emulated machine's time) or red (a hash
differed, from that frame on). The border is read from a screenshot.

  tools/atari_test.py [build/atari/bomber.xex] [--png out.png]

atari800 (Ubuntu's package; ATARI800 to name another) as an 800XL, PAL, with
its built-in AltirraOS (no ROM to download), fast-forwarded, on the X display
AMIGA_DISPLAY (an Xvfb, :97), with a configuration of its own."""
import os, subprocess, sys, time
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
args = [a for i, a in enumerate(sys.argv[1:], 1) if not a.startswith('--') and sys.argv[i - 1] != '--png']
xex = args[0] if args else os.path.join(ROOT, 'build', 'atari', 'bomber.xex')
out = os.path.join(ROOT, 'build', 'atari')
png = sys.argv[sys.argv.index('--png') + 1] if '--png' in sys.argv else os.path.join(out, 'test.png')
cfg = os.path.join(out, 'atari800.cfg')                         # never the user's ~/.atari800.cfg
open(cfg, 'w').write('')
env = dict(os.environ, DISPLAY=os.environ.get('AMIGA_DISPLAY', ':97'))   # the invisible X display, never the desktop's
cmd = [os.environ.get('ATARI800', 'atari800'), '-config', cfg, '-no-autosave-config', '-xl', '-xl-rev', 'altirra', '-pal',
       '-nobasic', '-nosound', '-windowed', '-turbo', '-run', xex]
log = open(os.path.join(out, 'atari800.log'), 'w')
p = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT, env=env)
t0 = time.time()


def border():
    """the border's colour (just inside the window's top left corner), or None before the window shows"""
    shot = png + '.part.png'
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'x11grab', '-video_size', '1024x768', '-i', env['DISPLAY'],
                    '-frames:v', '1', shot], env=env)
    if not os.path.exists(shot): return None
    os.replace(shot, png)
    im = Image.open(png).convert('RGB')
    box = im.getbbox()
    return im.getpixel((box[0] + 4, box[1] + 4)) if box else None


def verdict(c):
    """black (still playing: COLOR4 is 0 in the game), red, green or yellow (by hue)"""
    if c is None or max(c) < 40: return None
    r, g, b = c
    if r > g * 1.5 and r > b: return 'red'
    if g > r * 1.2: return 'green'
    return 'yellow'


v = None
try:
    while time.time() - t0 < 120 and p.poll() is None:
        time.sleep(1)
        v = verdict(border())
        if v:
            time.sleep(1); v = verdict(border())               # a frame later: the figures drawn too
            break
finally:
    p.kill()
if not v: sys.exit(f'no verdict after {time.time() - t0:.0f} s (log: build/atari/atari800.log, screen: {png})')
print(f'{os.path.basename(xex)}: hashes {"MISMATCH" if v == "red" else "all equal"}'
      f'{", some frames over 60 ms" if v == "yellow" else ""}; screenshot {png}; {time.time() - t0:.0f} s')
sys.exit(1 if v == 'red' else 0)
