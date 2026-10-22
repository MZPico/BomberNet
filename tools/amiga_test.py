#!/usr/bin/env python3
"""Run an AMIGA_TEST build (tools/build_amiga.py --test) in FS-UAE and print
its figures: the recording replayed with the hashes checked, each game
frame's work (flush .. next frame sync) as wall time on the emulated Amiga.

  tools/amiga_test.py [build/amiga/bomber] [--ntsc] [--png out.png] [--model A500|A1200]

FS-UAE (the Ubuntu package) on the X display DISPLAY (an Xvfb, :97), with
its AROS replacement ROM unless KICKSTART names a Kickstart ROM image; a
directory under build/amiga is the Amiga's DH0: with the program and a
Startup-Sequence that runs it; the program writes DH0:result.txt."""
import os, shutil, subprocess, sys, time

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
args = [a for i, a in enumerate(sys.argv[1:], 1) if not a.startswith('--') and sys.argv[i - 1] not in ('--png', '--model')]
exe = args[0] if args else os.path.join(ROOT, 'build', 'amiga', 'bomber')
png = sys.argv[sys.argv.index('--png') + 1] if '--png' in sys.argv else os.path.join(ROOT, 'build', 'amiga', 'test.png')
model = sys.argv[sys.argv.index('--model') + 1] if '--model' in sys.argv else 'A500'
dh0 = os.path.join(ROOT, 'build', 'amiga', 'dh0')
base = os.path.join(ROOT, 'build', 'amiga', 'fsuae-base')
for d in (os.path.join(dh0, 'C'), os.path.join(dh0, 'S'), base): os.makedirs(d, exist_ok=True)
shutil.copy(exe, os.path.join(dh0, 'C', 'bomber'))
open(os.path.join(dh0, 'S', 'Startup-Sequence'), 'w').write('C:bomber\necho "done" >DH0:done.txt\n')
for f in ('result.txt', 'done.txt'):
    try: os.remove(os.path.join(dh0, f))
    except FileNotFoundError: pass
cmd = ['fs-uae', f'--base-dir={base}', f'--amiga-model={model}', '--chip-memory=1024', f'--fast-memory={os.environ.get("FASTMEM", "8192")}',
       f'--hard-drive-0={dh0}', '--fullscreen=0', '--video-sync=0', '--automatic-input-grab=0', '--floppy-drive-speed=0',
       '--ntsc-mode=1' if '--ntsc' in sys.argv else '--ntsc-mode=0']
if os.environ.get('KICKSTART'): cmd.append(f'--kickstart-file={os.environ["KICKSTART"]}')
env = dict(os.environ, DISPLAY=os.environ.get('AMIGA_DISPLAY', ':97'))   # the invisible X display (Xvfb), never the desktop's
log = open(os.path.join(ROOT, 'build', 'amiga', 'fsuae.log'), 'w')
p = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT, env=env)
t0 = time.time()
result = os.path.join(dh0, 'result.txt')
try:
    while time.time() - t0 < 600 and not os.path.exists(os.path.join(dh0, 'done.txt')) and p.poll() is None:
        time.sleep(1)
        if 30 < time.time() - t0 < 32:                           # the game under way: a picture of it
            subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'x11grab', '-video_size', '1024x768', '-i', env['DISPLAY'],
                            '-frames:v', '1', png], env=env)
    time.sleep(1)
finally:
    p.kill()
if not os.path.exists(result): sys.exit(f'no result after {time.time() - t0:.0f} s (FS-UAE log: build/amiga/fsuae.log)')
lines = open(result).read().split('\n')
head = dict(zip(lines[0].split()[::2], map(int, lines[0].split()[1::2])))
rows = [list(map(int, l.split())) for l in lines[1:] if l.strip()]
work = sorted(r[0] for r in rows)
hz = head['hz'] / 1000.0
q = lambda f: work[min(len(work) - 1, int(len(work) * f))]
print(f'{os.path.basename(exe)} ({"NTSC" if head["ntsc"] else "PAL"}, {model}): {head["frames"]} game frames, hashes '
      f'{"MISMATCH at offset " + str(head["bad"]) if head["bad"] else "all equal"}, late {head["late"]}')
print(f'  work a frame: median {q(.5) / hz:.1f} ms, p90 {q(.9) / hz:.1f}, p99 {q(.99) / hz:.1f}, max {work[-1] / hz:.1f}; '
      f'over 60 ms {sum(w / hz > 60 for w in work)}; the flush {head["flush"] / head["frames"] / hz:.2f} ms on average')
slow = [(i, round(r[0] / hz, 1), round(r[1] / hz, 1)) for i, r in enumerate(rows) if r[0] / hz > 60]
if slow: print(f'  frames over 60 ms (index, work, of it the flush): {slow[:6]}')
print(f'  screenshot {png}; {time.time() - t0:.0f} s')
sys.exit(1 if head['bad'] else 0)
