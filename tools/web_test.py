#!/usr/bin/env python3
"""The browser build's replays in Node.js: each recording compiled in (tools/build_web.py --test), replayed,
every state hash checked against it.  tools/web_test.py [recordings ...]   default tests/recordings/*.bnr"""
import glob, os, subprocess, sys
ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
nvm = os.path.expanduser('~/.nvm/versions/node')
node = os.path.join(nvm, sorted(os.listdir(nvm))[-1], 'bin', 'node') if os.path.isdir(nvm) else 'node'
fails = 0
for r in sys.argv[1:] or sorted(glob.glob(os.path.join(ROOT, 'tests', 'recordings', '*.bnr'))):
    b = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'build_web.py'), '--test', r], capture_output=True, text=True)
    if b.returncode: print(f'{os.path.basename(r):28} build failed: {b.stdout[-500:]}{b.stderr[-500:]}'); fails += 1; continue
    t = subprocess.run([node, os.path.join(ROOT, 'build', 'web-test', 'bombernet.js')], capture_output=True, text=True, timeout=600)
    print(f'{os.path.basename(r):28} {t.stdout.strip() or t.stderr.strip()[-300:]}')
    fails += t.returncode != 0
sys.exit(1 if fails else 0)
