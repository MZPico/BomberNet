#!/usr/bin/env python3
"""An Atari ST floppy image (.ST: the sectors in order) with files on it:
FAT12, single sided, 80 tracks of 9 sectors (360 KB), which every ST's
drive reads. TOS runs the programs in the boot disk's AUTO folder at
startup, so no boot code is needed: AUTO\\BOMBER.PRG starts the game.

  tools/make_st_disk.py out.st [ATARI\\NAME=hostfile ...] [--label NAME]

e.g. tools/make_st_disk.py build/st/bombernet.st AUTO\\BOMBER.PRG=build/st/BOMBER.PRG

The layout (TOS's own for a 360 KB disk): the boot sector, two FATs of 2
sectors, a root directory of 112 entries (7 sectors), then the data in
clusters of 2 sectors (cluster 2 at sector 12). The boot sector's checksum
is left off 0x1234: not executable, TOS goes on to the AUTO folder.
"""
import struct, sys, time

SECTOR, SPT, TRACKS, SIDES = 512, 9, 80, 1
TOTAL = SPT * TRACKS * SIDES                  # 720 sectors
SPC, FAT_SECTORS, ROOT_ENTRIES = 2, 2, 112
ROOT_SECTOR = 1 + 2 * FAT_SECTORS             # 5
DATA_SECTOR = ROOT_SECTOR + ROOT_ENTRIES * 32 // SECTOR   # 12
CLUSTERS = (TOTAL - DATA_SECTOR) // SPC       # 354
CLUSTER = SPC * SECTOR


def dos_time(t):
    lt = time.localtime(t)
    return (lt.tm_hour << 11 | lt.tm_min << 5 | lt.tm_sec // 2,
            (lt.tm_year - 1980) << 9 | lt.tm_mon << 5 | lt.tm_mday)


def raw_entry(name11, attr, cluster, size, t):
    tm, dt = dos_time(t)
    return name11 + bytes([attr]) + bytes(10) + struct.pack('<HHHI', tm, dt, cluster, size)


def entry(name, attr, cluster, size, t):
    base, _, ext = name.upper().partition('.')
    if len(base) > 8 or len(ext) > 3: raise ValueError(f'{name}: not an 8.3 name')
    return raw_entry(base.ljust(8).encode() + ext.ljust(3).encode(), attr, cluster, size, t)


def make(files, label='BOMBERNET'):
    """files: {'AUTO\\\\BOMBER.PRG': bytes, 'README.TXT': bytes}; folders one level deep"""
    img = bytearray(TOTAL * SECTOR)
    fat = [0xff8, 0xfff] + [0] * CLUSTERS
    next_free = [2]
    now = time.time()

    def alloc(data):
        """the data in a chain of free clusters: its first cluster (0 for nothing)"""
        n = max(1, -(-len(data) // CLUSTER))
        first = next_free[0]
        for i in range(n):
            c = first + i
            if c >= CLUSTERS + 2: raise ValueError('the disk is full')
            fat[c] = c + 1 if i < n - 1 else 0xfff
            at = (DATA_SECTOR + (c - 2) * SPC) * SECTOR
            img[at:at + CLUSTER] = data[i * CLUSTER:(i + 1) * CLUSTER].ljust(CLUSTER, b'\0')
        next_free[0] = first + n
        return first

    root, folders = [], {}
    if label: root.append(entry(label[:8] + ('.' + label[8:11] if len(label) > 8 else ''), 0x08, 0, 0, now))
    for path, data in files.items():
        parts = path.replace('/', '\\').split('\\')
        if len(parts) == 1: root.append(entry(parts[0], 0x00, alloc(data) if data else 0, len(data), now))
        else: folders.setdefault(parts[0], []).append((parts[1], data))
    for folder, items in folders.items():           # a folder: one cluster of 32 entries, "." and ".." first
        first = next_free[0]
        table = [raw_entry(b'.          ', 0x10, first, 0, now), raw_entry(b'..         ', 0x10, 0, 0, now)]
        next_free[0] += 1                            # the folder's own cluster, filled below
        for name, data in items: table.append(entry(name, 0x00, alloc(data) if data else 0, len(data), now))
        if len(table) > CLUSTER // 32: raise ValueError(f'{folder}: too many files')
        fat[first] = 0xfff
        at = (DATA_SECTOR + (first - 2) * SPC) * SECTOR
        img[at:at + CLUSTER] = b''.join(table).ljust(CLUSTER, b'\0')
        root.append(entry(folder, 0x10, first, 0, now))
    if len(root) > ROOT_ENTRIES: raise ValueError('too many files in the root')
    img[ROOT_SECTOR * SECTOR:ROOT_SECTOR * SECTOR + len(root) * 32] = b''.join(root)

    packed = bytearray(FAT_SECTORS * SECTOR)        # FAT12: two entries in three bytes
    for i in range(0, len(fat), 2):
        a, b = fat[i], fat[i + 1] if i + 1 < len(fat) else 0
        packed[i // 2 * 3:i // 2 * 3 + 3] = bytes([a & 0xff, (a >> 8) | ((b & 0xf) << 4), b >> 4])
    for k in range(2):
        at = (1 + k * FAT_SECTORS) * SECTOR
        img[at:at + len(packed)] = packed

    boot = bytearray(SECTOR)                        # BRA.S, the OEM bytes, a serial number, the BPB (little-endian)
    boot[0:2] = b'\x60\x1c'
    boot[2:8] = b'BNET  '
    boot[8:11] = struct.pack('<I', int(now) & 0xffffff)[:3]
    boot[11:30] = struct.pack('<HBHBHHBHHHH', SECTOR, SPC, 1, 2, ROOT_ENTRIES, TOTAL, 0xf8, FAT_SECTORS, SPT, SIDES, 0)
    s = sum(struct.unpack('>256H', bytes(boot))) & 0xffff
    if s == 0x1234: boot[510] ^= 1                  # never executable by chance
    img[0:SECTOR] = boot
    return bytes(img)


def read(img, path):
    """a file's bytes from an image this module wrote (or any TOS FAT12 floppy): 'RESULT.TXT', 'AUTO\\X.PRG'"""
    sec, spc, res, nfats, nroot, total, _, spf = struct.unpack('<HBHBHHBH', img[11:24])
    fat_at, root_at = res * sec, (res + nfats * spf) * sec
    data_at, csize = root_at + nroot * 32, spc * sec
    def nxt(c):
        v = img[fat_at + c * 3 // 2] | img[fat_at + c * 3 // 2 + 1] << 8
        return v >> 4 if c & 1 else v & 0xfff
    def data(c, size):
        out = b''
        while 2 <= c < 0xff0 and len(out) < size: out += img[data_at + (c - 2) * csize:data_at + (c - 1) * csize]; c = nxt(c)
        return out[:size]
    def find(at, n, name):
        base, _, ext = name.upper().partition('.')
        want = base.ljust(8).encode() + ext.ljust(3).encode()
        for i in range(n):
            e = img[at + 32 * i:at + 32 * (i + 1)]
            if e[0] == 0: break
            if e[:11] == want and e[0] != 0xe5: return e[26] | e[27] << 8, struct.unpack('<I', e[28:32])[0], e[11]
        return None
    parts, at, n = path.replace('/', '\\').split('\\'), root_at, nroot
    for k, name in enumerate(parts):
        hit = find(at, n, name)
        if not hit: return None
        c, size, attr = hit
        if k == len(parts) - 1: return data(c, size)
        at, n = data_at + (c - 2) * csize, csize // 32      # a folder of one cluster (what this module writes)


if __name__ == '__main__':
    args = [a for i, a in enumerate(sys.argv[1:], 1) if not a.startswith('--') and sys.argv[i - 1] != '--label']
    label = sys.argv[sys.argv.index('--label') + 1] if '--label' in sys.argv else 'BOMBERNET'
    files = {}
    for spec in args[1:]:
        name, _, host = spec.partition('=')
        files[name] = open(host, 'rb').read()
    open(args[0], 'wb').write(make(files, label))
    print(f'{args[0]}: {TOTAL * SECTOR} bytes, {", ".join(files)}')
