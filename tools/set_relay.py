#!/usr/bin/env python3
"""Point a BomberNet release file at another relay (relay/relay.py on a LAN):
the Spectrum, CPC and Atari builds keep the relay behind the marker BNRELAY
(net_relay_cfg in c/common/netdev_soft.c: the host in 46 bytes, 0-padded,
then the port, 16 bits little-endian); this rewrites it.

  tools/set_relay.py FILE... HOST[:PORT]      PORT 8765 when left out (relay.py's WebSocket)
  tools/set_relay.py FILE...                  show the relay of each file
  tools/set_relay.py FILE... api.mzpico.com:80   back to the production relay

Files: .tap (ZX Spectrum; the block checksum is redone), .dsk (CPC disk image,
standard or extended; the field may cross sectors), .cpc (an AMSDOS file),
.xex (Atari), .prg and .d64 (C64; on the disk the file BOMBERNET, or its
first program, whose sector chain the field may cross), an AmigaOS
executable (the port's bytes in the 68000's order). The MZ-800 takes its relay from its card: mzpico.ini
([cloud] net_relay, net_port) on an MZPico, /unicard/network.cfg on a Unicard.
"""
import os, sys

MARK = b'BNRELAY\0'
FIELD = 56                               # the marker (8), the host (46), the port (2)
HOST = 46


def tap_map(d):
    """the TAP's block contents as one stream: (file offset of each byte), and a fix-up for the checksums"""
    offs, blocks, i = [], [], 0
    while i + 2 <= len(d):
        n = d[i] | d[i + 1] << 8
        blocks.append((i + 2, n))                    # flag, data, checksum
        offs += range(i + 3, i + 1 + n)              # the data only
        i += 2 + n

    def fix(b):
        for start, n in blocks:
            x = 0
            for v in b[start:start + n - 1]: x ^= v
            b[start + n - 1] = x
    return offs, fix


def dsk_map(d):
    """the disk's sectors in logical order (track, side, sector id) as one stream"""
    ext = d.startswith(b'EXTENDED')
    if not ext and not d.startswith(b'MV - CPC'): raise ValueError('not a DSK image')
    tracks, sides = d[0x30], d[0x31]
    pos, secs = 0x100, []
    for t in range(tracks * sides):
        size = d[0x34 + t] * 256 if ext else d[0x32] | d[0x33] << 8
        if not size: continue
        h = d[pos:pos + 0x100]
        if not h.startswith(b'Track-Info'): raise ValueError(f'track {t}: no Track-Info')
        data = pos + 0x100
        for k in range(h[0x15]):
            c, s_, r, n = h[0x18 + 8 * k:0x1c + 8 * k]
            ln = (h[0x1e + 8 * k] | h[0x1f + 8 * k] << 8) if ext else 128 << h[0x14]
            secs.append(((c, s_, r), data, ln))
            data += ln
        pos += size
    offs = []
    for _, start, ln in sorted(secs): offs += range(start, start + ln)
    return offs, None


def d64_map(d, name=b'BOMBERNET'):
    """the data bytes of the disk's file called name (or its first program) in order: the
    sector chain from the directory (track 18); a sector holds 254 bytes after its link"""
    spt = [21] * 17 + [19] * 7 + [18] * 6 + [17] * 5          # sectors a track, tracks 1-35
    if len(d) < 174848 or len(d) > 175531 + 683: raise ValueError('not a 35-track D64 image')
    first = [0]
    for n in spt: first.append(first[-1] + n)
    sec = lambda t, s_: (first[t - 1] + s_) * 256
    chain, t, s_ = [], 18, 1                                    # the directory
    while t:
        at = sec(t, s_)
        for e in range(8):
            entry = d[at + 32 * e:at + 32 * (e + 1)]
            if entry[2] & 7 == 2 and entry[2] & 0x80:           # a closed PRG
                fname = entry[5:21].rstrip(b'\xa0')
                if fname == name or not chain: chain = [(entry[3], entry[4])]
                if fname == name: break
        else:
            t, s_ = d[at], d[at + 1]
            continue
        break
    if not chain: raise ValueError('no program on the disk')
    offs, t, s_ = [], chain[0][0], chain[0][1]
    while t:
        at = sec(t, s_)
        nt, ns = d[at], d[at + 1]
        offs += range(at + 2, at + 2 + (254 if nt else ns - 1))
        t, s_ = nt, ns
    return offs, None


def xex_map(d):
    """the segments' contents in file order; each byte also by its address, to check the field is contiguous"""
    offs, addrs, i = [], [], 0
    while i + 4 <= len(d):
        if d[i] == 0xff and d[i + 1] == 0xff: i += 2; continue
        a, e = d[i] | d[i + 1] << 8, d[i + 2] | d[i + 3] << 8
        offs += range(i + 4, i + 5 + e - a); addrs += range(a, e + 1)
        i += 5 + e - a
    return offs, addrs


def big_endian(d):
    """an AmigaOS executable (a hunk file, 000003F3 first): its 68000 reads the port's two bytes high first"""
    return d[:4] == b'\x00\x00\x03\xf3'


def load(path):
    d = bytearray(open(path, 'rb').read())
    low = path.lower()
    fix, addrs = None, None
    if low.endswith('.tap'): offs, fix = tap_map(d)
    elif low.endswith('.dsk'): offs, fix = dsk_map(d)
    elif low.endswith('.d64'): offs, fix = d64_map(d)
    elif low.endswith('.xex'): offs, addrs = xex_map(d)
    else: offs = list(range(len(d)))                 # .cpc and anything plain
    stream = bytes(d[o] for o in offs)
    k = stream.find(MARK)
    if k < 0: return d, None, fix
    if stream.find(MARK, k + 1) >= 0: raise ValueError('the marker is there twice')
    if addrs and addrs[k + FIELD - 1] - addrs[k] != FIELD - 1: raise ValueError('the field crosses segments')
    return d, offs[k:k + FIELD], fix


def show(path, d, at):
    raw = bytes(d[o] for o in at)
    port = int.from_bytes(raw[54:56], 'big' if big_endian(d) else 'little')
    print(f'{path}: {raw[8:8 + HOST].split(b"\0")[0].decode("ascii", "replace")}:{port}')


def main(argv):
    if not argv: print(__doc__); return 2
    target = None
    if len(argv) > 1 and not os.path.exists(argv[-1]):
        target = argv.pop()
        if ':' not in target: target += ':8765'
        host, port = target.rsplit(':', 1)
        if not host or not port.isdigit() or not 0 < int(port) < 65536 or not all(32 < ord(c) < 127 for c in host):
            print(f'not a host:port: {target}'); return 2
        if len(host) >= HOST: print(f'host too long, {HOST - 1} characters at most'); return 2
    rc = 0
    for path in argv:
        try:
            d, at, fix = load(path)
        except (OSError, ValueError) as e:
            print(f'{path}: {e}'); rc = 1; continue
        if at is None:
            print(f'{path}: no relay field' + (' (the MZ-800 takes the relay from its card: mzpico.ini or '
                                              '/unicard/network.cfg)' if path.lower().endswith('.mzf') else '')); rc = 1
            continue
        if target:
            new = MARK + host.encode().ljust(HOST, b'\0') + int(port).to_bytes(2, 'big' if big_endian(d) else 'little')
            for o, v in zip(at, new): d[o] = v
            if fix: fix(d)
            open(path, 'wb').write(d)
        show(path, d, at)
    return rc


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
