#!/usr/bin/env python3
"""Run an ST_TEST build (tools/build_st.py --test) in Hatari and print its
figures: the recording replayed with the hashes checked, each game frame's
work (flush .. next frame sync) timed by the ST's own MFP timer, so the
times are the emulated machine's even with Hatari fast-forwarding.

  tools/st_test.py [build/st/BOMBER.PRG] [--60hz] [--png out.png] [--machine st|ste]

Hatari on the X display AMIGA_DISPLAY (an Xvfb, :97), with EmuTOS
(build/st/rom/etos192uk.img: 50 Hz; with --60hz etos192us.img, which sets
60 Hz as a US machine; an STE etos256uk.img; else ST_TOS); a directory under build/st is
the ST's drive C: (Hatari's GEMDOS drive), the program in its AUTO folder;
the program writes C:\\RESULT.TXT."""
import os, shutil, subprocess, sys, time

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
args = [a for i, a in enumerate(sys.argv[1:], 1) if not a.startswith('--') and sys.argv[i - 1] not in ('--png', '--machine')]
exe = args[0] if args else os.path.join(ROOT, 'build', 'st', 'BOMBER.PRG')
png = sys.argv[sys.argv.index('--png') + 1] if '--png' in sys.argv else os.path.join(ROOT, 'build', 'st', 'test.png')
machine = sys.argv[sys.argv.index('--machine') + 1] if '--machine' in sys.argv else 'st'
img = 'etos192us.img' if '--60hz' in sys.argv else 'etos192uk.img'   # EmuTOS sets 50 or 60 Hz by its country
if machine != 'st': img = 'etos256uk.img'                          # Hatari runs the 192 KB image (a TOS 1.x) on a plain ST only
tos = os.environ.get('ST_TOS') or next((p for p in (os.path.join(ROOT, 'build', 'st', 'rom', img),
                                                    os.path.expanduser(f'~/src/emutos/{img}')) if os.path.exists(p)), None)
if not tos: sys.exit('no TOS image: build EmuTOS (~/src/emutos, make 192) or set ST_TOS')
hd = os.path.join(ROOT, 'build', 'st', 'hd')
shutil.rmtree(hd, ignore_errors=True)
os.makedirs(os.path.join(hd, 'AUTO'))
shutil.copy(exe, os.path.join(hd, 'AUTO', 'BOMBER.PRG'))
result = os.path.join(hd, 'RESULT.TXT')
cfg = os.path.join(ROOT, 'build', 'st', 'hatari.cfg')            # a configuration of its own: never the user's ~/.config/hatari
open(cfg, 'w').write('')
cmd = ['hatari', '--configfile', cfg, '--tos', tos, '--machine', machine, '--memsize', '1', '--harddrive', hd,
       '--fast-forward', 'yes', '--sound', 'off', '--confirm-quit', 'no', '--statusbar', 'no', '--drive-led', 'no', '--window', '--monitor', 'rgb']
env = dict(os.environ, DISPLAY=os.environ.get('AMIGA_DISPLAY', ':97'))   # the invisible X display (Xvfb), never the desktop's
log = open(os.path.join(ROOT, 'build', 'st', 'hatari.log'), 'w')
p = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT, env=env)
t0 = time.time()


def complete():
    """the result file written out: its header line says how many frame lines follow"""
    try: lines = open(result).read().split('\n')
    except FileNotFoundError: return False
    if not lines[0].startswith('frames '): return False
    return len([l for l in lines[1:] if l.strip()]) >= int(lines[0].split()[1])


try:
    shot = png + '.part.png'
    while time.time() - t0 < 600 and not complete() and p.poll() is None:
        time.sleep(1)                                             # a picture every second (fast-forward: a run takes seconds);
        if complete(): break                                      # the last one before the result shows the game
        subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'x11grab', '-video_size', '1024x768', '-i', env['DISPLAY'],
                        '-frames:v', '1', shot], env=env)
        if os.path.exists(shot): os.replace(shot, png)
finally:
    p.kill()
if not complete(): sys.exit(f'no result after {time.time() - t0:.0f} s (Hatari log: build/st/hatari.log)')
lines = open(result).read().split('\n')
head = dict(zip(lines[0].split()[::2], map(int, lines[0].split()[1::2])))
rows = [list(map(int, l.split())) for l in lines[1:] if l.strip()]
work = sorted(r[0] for r in rows)
hz = head['hz'] / 1000.0
q = lambda f: work[min(len(work) - 1, int(len(work) * f))]
print(f'{os.path.basename(exe)} ({"60 Hz" if head["hz60"] else "50 Hz"}, {machine.upper()}): {head["frames"]} game frames, hashes '
      f'{"MISMATCH at offset " + str(head["bad"]) if head["bad"] else "all equal"}, late {head["late"]}')
print(f'  work a frame: median {q(.5) / hz:.1f} ms, p90 {q(.9) / hz:.1f}, p99 {q(.99) / hz:.1f}, max {work[-1] / hz:.1f}; '
      f'over 60 ms {sum(w / hz > 60 for w in work)}; the flush {head["flush"] / head["frames"] / hz:.2f} ms on average')
slow = [(i, round(r[0] / hz, 1), round(r[1] / hz, 1)) for i, r in enumerate(rows) if r[0] / hz > 60]
if slow: print(f'  frames over 60 ms (index, work, of it the flush): {slow[:6]}')
print(f'  screenshot {png}; {time.time() - t0:.0f} s')
sys.exit(1 if head['bad'] else 0)
