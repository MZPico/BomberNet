"""Measure every allocated byte, including BSS; reserve 512 bytes below SP."""
import re, sys
from pathlib import Path
failed = False
for filename in sys.argv[1:]:
    text = Path(filename).read_text()
    symbols = dict((m[0], int(m[1],16)) for m in re.findall(r'^([^\s]+)\s+= \$([0-9A-Fa-f]+)', text, re.M))
    origin = symbols['__head']
    end = symbols['__BSS_END_tail']
    spare = 65535-end
    print('%s: origin %d (0x%04x), allocation end %d (0x%04x)' % (filename, origin, origin, end, end))
    print('Total allocation: %d bytes; free below SP: %d; reserve: 512' % (end-origin, spare))
    if end < origin or spare < 512:
        print('FAIL: allocation overruns RAM or the stack reserve')
        failed = True
if failed:
    sys.exit(1)
