# BomberNet on the IBM PC (DOS): plan and progress

A real-mode DOS program for any PC with CGA, EGA or VGA: the same
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
   the title and lobby; play in DOSBox-X. A CGA renderer. Done, see
   below.
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

## Step 1: keys, joysticks, the speaker

**Keyboard.** The game's own INT 9 handler while it runs: the byte from
port 60h, acknowledged the XT's way (port 61h bit 7, harmless on an AT),
the interrupt ended, the BIOS never seeing it; the state of every key kept
from the make and break codes (set 1: the same numbers as the Atari ST's,
so the key tables are the ST's). An enhanced keyboard's E0 prefix (its
own cursor keys) is skipped, and its fake shift (E0 2A) ignored.

| Source | Keys |
|---|---|
| key set A, "CURSOR AND SPACE" | the cursor keys; SPACE fires (RETURN when another player has key set B) |
| key set B, "WASD AND E" | W A S D, E fires |
| joysticks A and B | the game port (201h): two sticks, two buttons each |
| text entry (the room code) | the letters; BACKSPACE or DELETE deletes, ESC cancels |

ESC leaves a match; on the title it ends the program (the keyboard, the
timer, the speaker and the screen mode given back). The joysticks: a write
to 201h starts the four timers, each axis's bit stays 1 for a time that
grows with the stick's position; counted in loops of the reading code
against the centre counted the same way at the start (so the CPU's speed
cancels out); an axis not back within 4000 loops has no stick, and the
JOYSTICK row is NONE.

**Speaker.** The timer chip's channel 2 in square-wave mode, the divider
ratio * 1193182 / 1108400 (the MZ's own 8253 counts 1108400 Hz), port 61h
switching it on; the 1 kHz interrupt switches it off when the tone's
length is up (len * 238 / 256 ms, plus one).

**Tested** (`tools/pc_play.py`, DOSBox-X in real time, the keys by xdotool,
a `PC_PLAYTEST` build logging the players' places to `C:\PLAY.LOG`): the
title, SPACE, key set A and key set B in all four directions, ESC out of
the match and out of the program (back at the DOS prompt, text mode).
The sound: DOSBox-X records its output while the program runs
(`DX-CAPTURE /A BOMBER.EXE` in the autoexec; SDL's dummy audio driver,
no sound device; its Ctrl+F6 and a mapper binding did nothing here), and
each beep is measured: five keys, five beeps, 13.6-13.9 ms (13 asked),
2124 Hz (2123 asked) and 1424 (1425). The pitch is the spectrum's peak:
counting zero crossings, as on the ST, read DOSBox's filtered speaker
signal wrongly at 2 kHz (1700-1960 Hz, a different figure every beep).

The joysticks are not tested: DOSBox-X emulates the game port only with a
host joystick (a mapper binding of keys to its axes did nothing without
one), WSL has no `/dev/uinput` for a virtual one, and there is no real
one here. The code is the usual one; a tester's report will tell.

## The CGA

Mode 4, 320 x 200 in 4 colours, the even lines at B800:0000 and the odd
ones at B800:2000, 80 bytes a line. A glyph's row is one word: each bit
doubled into a 2-bit pixel by a 256-entry table, ANDed with the colour's
mask (0x5555 times the colour). The adapter: no EGA answers INT 10h
AH=12h, and the BIOS's equipment word is not mono (an MDA or a Hercules
gets a message instead).

The look was chosen from mockups of four ways (drawn from a frame the VGA
build recorded, cell by cell): 4-colour graphics with palette 0 or 1, the
160 x 100 16-colour trick, 16-colour text mode; then dithered colour
pairs (a checkerboard of two palette colours, as cheap as a solid one),
and players told apart by form. Dithering broke the text (one-pixel
strokes) and could not give the status line's digits the players'
colours. The choice:

- The title in palette 1 (cyan, magenta, white): the logo keeps its
  magenta. The game in palette 0 (green, red, yellow): the field's red.
  The palette is chosen through the BIOS (INT 10h AH=0Bh) at each switch.
- The enemies all red (by their codes), so red is danger and green and
  yellow are players.
- Player 1 green, player 2 yellow; players 3 and 4 the same colours
  hollow (each block of the figure with its centre cut out: rows 2 and 6,
  columns 2 and 6), their status-line marks (the digit, the figure
  icon) inverse.

Tested: every hash equal (dm4, coop4) on DOSBox-X's `machine=cga`; the
play test passes; the title, a deathmatch and a co-op frame look as the
mockups. Speed: at cycles 3000 a frame's work is 5.4 ms (median), at 300
(an XT) 61 ms, the flush 37 ms of it: an XT with a CGA is slower than with
an EGA (the 2-bit pixels: a table and a word a row, in C).

## Risks and open questions

- Speed on an 8088 is an estimate until a tester runs it.
- An XT is too slow in C on either adapter (CGA 61 ms a frame, EGA
  49): the flush in assembly would be the remedy, if testers want it.
