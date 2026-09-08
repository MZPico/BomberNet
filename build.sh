#!/bin/sh
# Rebuild bomber.mzf from bomber.asm and verify it against the original.
# Needs: pasmo (apt install pasmo)
set -e
cd "$(dirname "$0")"
mkdir -p build
pasmo --bin bomber.asm build/bomber.bin
# MZF header: attr 01, 17-byte name " F1200\r", size, load addr, exec addr, 104 zero bytes
python3 - <<'PY'
import struct
body = open('build/bomber.bin', 'rb').read()
name = b' F1200\r'.ljust(17, b'\0')
hdr = b'\x01' + name + struct.pack('<HHH', len(body), 0x1200, 0x1200)
hdr = hdr.ljust(128, b'\0')
open('build/bomber.mzf', 'wb').write(hdr + body)
print('build/bomber.mzf: %d bytes (body %d)' % (128 + len(body), len(body)))
PY
if [ -f bomber.mzf ]; then
  if cmp -s bomber.mzf build/bomber.mzf; then echo "identical to original bomber.mzf"; else echo "DIFFERS from original bomber.mzf"; fi
fi
