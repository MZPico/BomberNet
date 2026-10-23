#!/usr/bin/env python3
"""The Amiga's Workbench icon (docs/port-amiga.md): the game's own figures,
the player and a bomb, drawn in the MZ's mosaic blocks, in the four
colours of Workbench 2 and 3 (0 grey, 1 black, 2 white, 3 blue).

  tools/make_amiga_icon.py --preview out.png     the images, as Workbench shows them
  tools/make_amiga_icon.py bomber.info disk.info the program's icon and the disk's (for the ADF)

Workbench's usual screen is high resolution, not interlaced: a pixel is
twice as tall as it is wide, so a block is 6 x 3 pixels to look square.
The second image (shown while the icon is selected) lights the fuse. The
disk's icon is a floppy whose label carries the bomb; selected, its
colours are inverted (Workbench does that itself).

The file is a DiskObject in the Workbench 2 layout: the header with its
Gadget, the drawer's window for a disk, the images (struct Image and the
bitplanes, rows padded to 16 bits), the default tool's string.
"""
import struct, sys

BW, BH, GW, GH = 6, 3, 2, 1          # a block and the gap after it, in pixels
PITCH_X, PITCH_Y = BW + GW, BH + GH

PLAYER = ['.XX.',
          'XXXX',
          '.XX.',
          'X..X']
BOMB = ['.XX.',
        'XXXX',
        'XXXX',
        '.XX.']

W, H = 4 * PITCH_X + 4 + 4 * PITCH_X + 9, 6 + 4 * PITCH_Y   # the player, a space, the bomb, room for the fuse's spark
BX, BY = 4 * PITCH_X + 4, 6                                    # the bomb's top left
PY = 6                                                        # the player stands level with the bomb


def blocks(img, shape, x0, y0, colour, shade=None):
    for r, row in enumerate(shape):
        for c, ch in enumerate(row):
            if ch != 'X': continue
            x, y = x0 + c * PITCH_X, y0 + r * PITCH_Y
            for yy in range(BH):
                for xx in range(BW): img[y + yy][x + xx] = colour
            if shade is not None:                          # a shadow right and below: the Workbench 2 look
                for yy in range(1, BH + 1):
                    if x + BW < W: img[y + yy][x + BW] = shade
                for xx in range(1, BW + 1):
                    if y + BH < H: img[y + BH][x + xx] = shade


def image(lit):
    img = [[0] * W for _ in range(H)]
    blocks(img, PLAYER, 0, PY, 2, shade=1)
    blocks(img, BOMB, BX, BY, 1)
    for xx in range(1, 4): img[BY + 1][BX + PITCH_X + xx] = 2   # the bomb's shine, in its top left block
    fx, fy = BX + 3 * PITCH_X - 2, BY - 1                   # the fuse: from the top right block up and right
    for i in range(5):
        img[fy - i // 2][fx + i] = 1
    sx, sy = fx + 5, fy - 3                                # the end of the fuse
    if lit:                                                # a spark: a white heart, blue rays (twice as long across)
        rays = [(dx, 0) for dx in (-4, -3, -2, 3, 4, 5)] + [(dx, dy) for dx in (0, 1) for dy in (-2, -1, 1, 2)]
        rays += [(-2, -1), (-3, -2), (3, -1), (4, -2), (-2, 1), (-3, 2), (3, 1), (4, 2)]
        for dx, dy in rays:
            if 0 <= sx + dx < W and 0 <= sy + dy < H: img[sy + dy][sx + dx] = 3
        for dx in (-1, 0, 1, 2): img[sy][sx + dx] = 2
    return img


DISK = ['XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX.',      # a 3.5" floppy: the shutter at the top, the label below
        'X......BBBBBBBBBBBBBBBBBBBBBB..........XX',
        'X......BB.......BBBBBB......B..........XX',
        'X......BB.......BBBBBB......B..........XX',
        'X......BB.......BBBBBB......B..........XX',
        'X......BBBBBBBBBBBBBBBBBBBBBB..........XX',
        'X......................................XX',
        'X..WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW..XX',
        'X..WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW..XX',
        'X..WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW..XX',
        'X..WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW..XX',
        'X..WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW..XX',
        'X..WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW..XX',
        'X..WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW..XX',
        'X..WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW..XX',
        'X..WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW..XX',
        'X..WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW..XX',
        'X..WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW..XX',
        'XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX',
        '.XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX']


def disk_image():
    img = [[{'X': 1, 'B': 3, 'W': 2}.get(c, 0) for c in row.ljust(40, '.')] for row in DISK]
    for r, row in enumerate(['.XX.', 'XXXX', 'XXXX', '.XX.']):   # the bomb on the label: blocks of 3 x 2, a gap after each
        for c, ch in enumerate(row):
            if ch == 'X':
                for yy in range(2):
                    for xx in range(3): img[7 + r * 3 + yy][13 + c * 4 + xx] = 1
    for i in range(5): img[8 - i // 2][24 + i] = 1             # its fuse, from the top right block
    return img


def planes(img):
    """struct Image (the data's address non-zero: an image follows) and its two bitplanes"""
    h, w = len(img), len(img[0])
    words = (w + 15) // 16
    data = b''
    for plane in range(2):
        for row in img:
            bits = [(row[x] >> plane) & 1 if x < w else 0 for x in range(words * 16)]
            data += bytes(sum(bits[i * 8 + b] << (7 - b) for b in range(8)) for i in range(words * 2))
    return struct.pack('>hhhhhIBBI', 0, 0, w, h, 2, 1, 3, 0, 0) + data


def string(t):
    t = t.encode() + b'\0'
    return struct.pack('>I', len(t)) + t


def disk_object(kind, images, w, h, default_tool=None, stack=0):
    """kind: 3 a tool, 1 a disk. Two images: the second while selected; one: inverted while selected."""
    NO_POSITION = 0x80000000
    gadget = struct.pack('>IhhhhHHHIIIiIHI', 0, 0, 0, w, h, 0x0006 if len(images) == 2 else 0x0004, 0x0003, 0x0001,
                         1, 1 if len(images) == 2 else 0, 0, 0, 0, 0, 1)    # UserData 1: the Workbench 2 layout
    head = struct.pack('>HH', 0xE310, 1) + gadget + struct.pack('>BBIIIIIII', kind, 0, 1 if default_tool else 0, 0,
                                                                     NO_POSITION, NO_POSITION, 1 if kind == 1 else 0, 0, stack)
    out = head
    if kind == 1:                                          # the drawer's window: NewWindow, then its scroll position
        out += struct.pack('>hhhhBBIIIIIIIhhHHH', 50, 30, 400, 120, 255, 255, 0, 0, 0, 0, 0, 0, 0, 90, 40, 0xffff, 0xffff, 1)
        out += struct.pack('>ii', 0, 0)
    for im in images: out += planes(im)
    if default_tool: out += string(default_tool)
    if kind == 1: out += struct.pack('>IH', 0, 0)            # DrawerData2: the Workbench 2 view (the defaults)
    return out


def preview(path):
    from PIL import Image
    pal = [(0xaa, 0xaa, 0xaa), (0, 0, 0), (0xff, 0xff, 0xff), (0x66, 0x88, 0xbb)]   # Workbench 2's default colours
    S = 4                                                   # 4 screen pixels a hires pixel across, 8 down
    imgs = [image(False), image(True), disk_image(), [[3 - v for v in row] for row in disk_image()]]
    out = Image.new('RGB', (S * (sum(len(i[0]) for i in imgs) + 16 * len(imgs) + 8), 2 * S * (H + 8)), pal[0])
    x0 = 4
    for img in imgs:
        for y in range(len(img)):
            for x in range(len(img[0])):
                for dy in range(2 * S):
                    for dx in range(S): out.putpixel((S * (x0 + x) + dx, 2 * S * (4 + y) + dy), pal[img[y][x]])
        x0 += len(img[0]) + 16
    out.save(path)


if __name__ == '__main__':
    if sys.argv[1:2] == ['--preview']: preview(sys.argv[2])
    else:
        open(sys.argv[1], 'wb').write(disk_object(3, [image(False), image(True)], W, H, stack=8192))
        if len(sys.argv) > 2: open(sys.argv[2], 'wb').write(disk_object(1, [disk_image()], 40, len(DISK), 'SYS:System/DiskCopy'))
