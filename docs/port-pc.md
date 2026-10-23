# BomberNet on the IBM PC (DOS): plan and progress

A real-mode DOS program for any PC with EGA or VGA (CGA later): the same
core in 16-bit C (OpenWatcom), a new platform layer, the network over a
WiFi modem on a COM port first (the Atari ST's transport, in DOSBox-X
with its own modem), Ethernet through a packet driver later.

- Machines: an 8088 PC/XT upwards; DOS 3 or later (or FreeDOS); EGA or
  VGA; 640 KB is ample (the program is about 40 KB).
- Sound: the PC speaker, which is the 8253/8254 timer the MZ-800 has too:
  a tone's divider is the MZ's scaled from 1.1084 MHz to 1.19318 MHz.

## Steps

0. **Measure first**: the toolchain; the core built for 16-bit DOS; the
   EGA/VGA renderer; a recording replayed in DOSBox-X with the hashes
   checked and the frames timed. Done, see below.
1. **The platform layer**: the keyboard (its own INT 9 handler, the state
   of every key), the game port's two joysticks, the speaker, ESC to DOS;
   the title and lobby; play in DOSBox-X. A CGA renderer.
2. **The network**: `tcp.h` over a WiFi modem on a COM port (the 8250/16550
   UART, interrupt-driven), DOSBox-X's own modem emulation for the tests;
   cross-play with every machine; the production relay.
3. **Release**: `BOMBER.EXE` and a disk image; `set_relay.py` on both; the
   README.
4. Optional: Ethernet through a packet driver (NE2000 in DOSBox-X, our own
   small TCP/IP); Tandy, Hercules, MDA renderers.

## Design

**Toolchain.** OpenWatcom v2, the Linux snapshot from GitHub unpacked in
`~/src/openwatcom` (no installation): `wcc` 16-bit, small model (`-ms`),
8086 code (`-0`), `wlink system dos`. `tools/build_pc.py` builds
`build/pc/BOMBER.EXE`. Watcom compiles C89 by default (no declarations
after statements). The core hashes field by field (`PLAT_HASH_PORTABLE`):
Watcom pads structures.

**Test bench.** DOSBox-X 2024.03 (Ubuntu's package) on the invisible X
display, with a configuration of its own: the adapter (`machine=ega` or
`svga_s3`), the CPU type, a fixed cycle count; a host directory as drive
C:, the program run from the autoexec; it writes `C:\RESULT.TXT`.
`tools/pc_test.py` runs it. DOSBox-X is not cycle-exact: `cycles` is its
count of instructions a millisecond, so the times are those of a CPU of
roughly that speed (DOSBox's guides: an 8088 at 4.77 MHz about 300, a 286
at 8 MHz about 1000, at 12 MHz about 2500). Hardware testers will give the
real figures. No BIOS images are needed.

**The platform** (`c/platform/pc/plat_pc.c`): mode 0Dh, 320 x 200 in 16
colours, four bitplanes in parallel at A000:0000. A cell is drawn by the
EGA's own logic: Set/Reset holds the colour for all four planes, the
latches hold zeros (loaded by reading a byte past the screen that is never
written), and each of the glyph's 8 rows is one Bit Mask write and one
memory write: the colour where the row has bits, black elsewhere. The
glyphs and colours are the Amiga's tables. A VGA shows the palette exactly
(its DAC). An EGA in a 200-line mode has only the CGA's 16 colours; the
nearest of them made players 3 and 4 both grey, so the colours are chosen
by hand: the field in the dark shades (dark red bricks, a dark magenta
border), the players in bright ones of their own (green, brown for
orange, light blue, light red).

Time: the timer chip's channel 0 at 1000 Hz (divider 1193) while the game
runs, the BIOS's 18.2 Hz tick still called on time (a counter of what is
owed it); a game frame is 60 of its ticks. Finer times come from the
counter itself (1.19 MHz, 0.84 us), latched.

## Step 0: the core on DOS, measured

**Correct.** Every recording replays with every hash equal: dm4, coop4,
dm2, coop1, dm2s, coop2s and the 2995 frames of coop2-score on a VGA;
dm4 on an EGA, and at the slower settings below. (The first replay
mismatched from frame 1: the recording is a far array, and the small
model's `memcpy` read the replay keys from the wrong segment;
`_fmemcpy`.)

**Frame work** (DOSBox-X, the times its timer chip shows, so those of a CPU
of roughly that speed):

| Setting | Median | 90th percentile | Over 60 ms | The flush |
|---|---|---|---|---|
| dm4, VGA, cycles 3000 (a 286 at 12 MHz or so) | 4.3 ms | 5.3 ms | 0 of 498 | 2.6 ms |
| dm4, EGA, cycles 3000 | 4.2 ms | 5.2 ms | 0 | 2.6 ms |
| dm4, VGA, cycles 1000 (a 286 at 8 MHz) | 13.0 ms | 15.7 ms | 3 | 7.7 ms |
| dm4, VGA, cycles 300, 8086 (an XT, 4.77 MHz) | 52.8 ms | 64.6 ms | 72 | 31.8 ms |
| dm4, EGA, cycles 300, 8086 | 49.3 ms | 60.1 ms | 50 | 29.6 ms |
| coop2-score, VGA, cycles 3000 (2995 frames) | 4.1 ms | 4.9 ms | 0 | 2.8 ms |

A full redraw (a round start) is under 60 ms at cycles 3000, 160 ms at
1000, 650 ms at 300. So a 286 plays it with room to spare; an XT is at
the edge: about a frame in seven late, the flush (the C loop around the
cells) more than half of it. An assembly flush and cell loop would win an
XT its frames back: left until a tester's figures say how real DOSBox's
300 cycles are.

## Risks and open questions

- Speed on an 8088 is an estimate until a tester runs it.
- CGA has 4 colours in 320 x 200: the players need telling apart.
