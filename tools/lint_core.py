#!/usr/bin/env python3
"""Checks of the shared and Z80 code that no compiler makes: the patterns that
broke a port before. Comments and strings are left out. Exit 1 on a find.
 - "x + ++y" / "x - --y": sccz80 (the Z80 builds) reads it as "x++ + y" (it
   once advanced the frame counter in netplay.c; docs/net-timing.md)."""
import glob, os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
FILES = [f for p in ('core', 'common', 'cpu/z80', 'platform/zx', 'platform/mz', 'platform/cpc')
         for f in sorted(glob.glob(os.path.join(ROOT, 'c', p, '*.c')))]
CODE = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
BAD = re.compile(r'(\+\s+\+\+|-\s+--)\s*[A-Za-z_(]')
fails = 0
for f in FILES:
    src = open(f, encoding='latin-1').read()
    code = CODE.sub(lambda m: re.sub(r'[^\n]', ' ', m.group(0)), src)   # comments and strings blanked, lines kept
    for m in BAD.finditer(code):
        line = code.count('\n', 0, m.start()) + 1
        print(f'{os.path.relpath(f, ROOT)}:{line}: "{m.group(1)}": sccz80 reads "a + ++b" as "a++ + b": write two statements')
        fails += 1
print('lint: ok' if not fails else f'lint: {fails} found')
sys.exit(1 if fails else 0)
