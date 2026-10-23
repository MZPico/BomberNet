#!/usr/bin/env python3
"""Run a PC_TEST build (tools/build_pc.py --test) in DOSBox-X and print its
figures: the recording replayed with the hashes checked, each game frame's
work (flush .. next frame sync) timed by the PC's own timer chip.

  tools/pc_test.py [build/pc/BOMBER.EXE] [--cycles n] [--cpu 8086|286|386] [--video ega|vga] [--png out.png]

DOSBox-X is not cycle-exact: --cycles is its fixed count of instructions a
millisecond (DOSBox's rough guides: 8088 4.77 MHz about 300, a 286 at
8 MHz about 1000, a 286 at 12 MHz about 2500, a 386DX at 33 MHz about
7000), so the times are those of a CPU of that speed, approximately.
Default 3000. A directory under build/pc is drive C:; the program writes
C:\\RESULT.TXT. The X display AMIGA_DISPLAY (an Xvfb, :97)."""
import os, shutil, subprocess, sys, time

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
arg = lambda name, default: sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default
args = [a for i, a in enumerate(sys.argv[1:], 1) if not a.startswith('--') and not sys.argv[i - 1].startswith('--')]
exe = args[0] if args else os.path.join(ROOT, 'build', 'pc', 'BOMBER.EXE')
cycles, cpu, video = arg('--cycles', '3000'), arg('--cpu', '286'), arg('--video', 'vga')
png = arg('--png', os.path.join(ROOT, 'build', 'pc', 'test.png'))
cdir = os.path.join(ROOT, 'build', 'pc', 'c')
shutil.rmtree(cdir, ignore_errors=True)
os.makedirs(cdir)
shutil.copy(exe, os.path.join(cdir, 'BOMBER.EXE'))
result = os.path.join(cdir, 'RESULT.TXT')
conf = os.path.join(ROOT, 'build', 'pc', 'dosbox.conf')
open(conf, 'w').write(f"""[sdl]
output=surface
autolock=false
[dosbox]
machine={'ega' if video == 'ega' else 'svga_s3'}
memsize=1
fastbioslogo=true
startbanner=false
[cpu]
core=normal
cputype={'8086' if cpu == '8086' else '286' if cpu == '286' else '386'}
cycles=fixed {cycles}
[mixer]
nosound=true
[speaker]
pcspeaker=false
[autoexec]
mount c "{cdir}"
c:
BOMBER.EXE
""")
env = dict(os.environ, DISPLAY=os.environ.get('AMIGA_DISPLAY', ':97'))   # the invisible X display (Xvfb), never the desktop's
log = open(os.path.join(ROOT, 'build', 'pc', 'dosbox.log'), 'w')
p = subprocess.Popen(['dosbox-x', '-conf', conf, '-fastlaunch', '-nopromptfolder', '-nolog'],
                     stdout=log, stderr=subprocess.STDOUT, env=env)
t0 = time.time()


def complete():
    try: lines = open(result).read().split('\n')
    except FileNotFoundError: return False
    if not lines[0].startswith('frames '): return False
    return len([l for l in lines[1:] if l.strip()]) >= int(lines[0].split()[1])


try:
    shot = png + '.part.png'
    while time.time() - t0 < 900 and not complete() and p.poll() is None:
        time.sleep(1)
        if complete(): break
        subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'x11grab', '-video_size', '1024x768', '-i', env['DISPLAY'],
                        '-frames:v', '1', shot], env=env)
        if os.path.exists(shot): os.replace(shot, png)
finally:
    p.kill()
if not complete(): sys.exit(f'no result after {time.time() - t0:.0f} s (DOSBox-X log: build/pc/dosbox.log)')
lines = open(result).read().split('\n')
head = dict(zip(lines[0].split()[::2], map(int, lines[0].split()[1::2])))
rows = [list(map(int, l.split())) for l in lines[1:] if l.strip()]
work = sorted(r[0] for r in rows)
hz = head['hz'] / 1000.0
q = lambda f: work[min(len(work) - 1, int(len(work) * f))]
print(f'{os.path.basename(exe)} ({video.upper()}, {cpu}, cycles {cycles}): {head["frames"]} game frames, hashes '
      f'{"MISMATCH at offset " + str(head["bad"]) if head["bad"] else "all equal"}, late {head["late"]}')
print(f'  work a frame: median {q(.5) / hz:.1f} ms, p90 {q(.9) / hz:.1f}, p99 {q(.99) / hz:.1f}, max {work[-1] / hz:.1f}; '
      f'over 60 ms {sum(w / hz > 60 for w in work)}; the flush {head["flush"] / head["frames"] / hz:.2f} ms on average')
slow = [(i, round(r[0] / hz, 1), round(r[1] / hz, 1)) for i, r in enumerate(rows) if r[0] / hz > 60]
if slow: print(f'  frames over 60 ms (index, work, of it the flush): {slow[:6]}')
print(f'  screenshot {png}; {time.time() - t0:.0f} s')
sys.exit(1 if head['bad'] else 0)
