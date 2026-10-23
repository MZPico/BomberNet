# BomberNet on the Atari ST: plan and progress

The second 68000 machine, after the Amiga (docs/port-amiga.md): the same
core with the field-by-field state hash, the same 16-colour cells, a new
platform layer and a new network transport.

- Machines: any ST, STE, Mega ST, Mega STE with a colour monitor (low
  resolution, 320 x 200 in 16 colours); the TT and the Falcon run ST low
  resolution too. A mono monitor (SM124, 640 x 400) cannot show it.
- Requirements: TOS or EmuTOS (tested: EmuTOS only, so far); tested with
  1 MB of memory, 512 KB (a 520 ST) not yet.

## Steps

0. **Measure first**: the toolchain; the core built for TOS; the screen
   (low resolution, the cell renderer); a recording replayed in Hatari with
   the hashes checked and the frames timed. Done, see below.
1. **The platform layer**: the keyboard (the IKBD's make and break codes,
   the state of every key kept), the joysticks (the IKBD's joystick
   reports), the YM2149's tones, 50/60 Hz; the title and lobby; play in
   Hatari. Done, see below.
2. **The network**: `tcp.h` over a WiFi modem on the serial port (an
   ESP8266 with Hayes firmware: `ATDT host:port`, then a byte stream), in
   Hatari through its serial port and a modem emulator on the host; the
   lobby's delay; cross-play with every machine; the production relay.
3. **Release**: `BOMBER.PRG` and an `.ST` floppy image (the game in its
   AUTO folder); `set_relay.py` on both (the program is a plain file, the
   image FAT12); the README.
4. Optional: STiNG (the TCP/IP stack of NetUSBee and EtherNEC owners),
   tested on hardware only: Hatari has no network card.

## Design

**Toolchain.** Vincent Riviere's cross tools (`ppa:vriviere/ppa`,
`cross-mint-essential`): `m68k-atari-mint-gcc` 4.6.4 and MiNTLib, the
compiler most ST software is built with. `tools/build_st.py` builds
`build/st/BOMBER.PRG` (`-O2 -m68000 -std=gnu99`). MiNTLib's startup gives
the program the stack its `_stksize` asks for (64 KB): no stack switching
as on the Amiga.

**Test bench.** Hatari 2.4.1 (Ubuntu's package) on the invisible X
display, with EmuTOS (free, GPL) built from source in `~/src/emutos`
(Ubuntu's Hatari has no TOS image, and EmuTOS's downloads are on
SourceForge, which refused this machine): `make 192 UNIQUE=uk` (50 Hz),
`UNIQUE=us` (60 Hz: EmuTOS sets the rate by its country), `make 256
UNIQUE=uk` for the STE (Hatari runs a 192 KB image as a TOS 1.x, on a plain
ST only); each from a clean tree (`make clean` first, which also deletes
the image: copy it out). The images are kept in `build/st/rom`. A host
directory is the ST's drive C: (Hatari's GEMDOS drive) with the program in
its AUTO folder; the program writes `C:\RESULT.TXT`. `tools/st_test.py`
runs it: Hatari fast-forwards, and the times are still the ST's own, read
from its MFP timer.

**The platform** (`c/platform/atarist/plat_st.c`): a TOS program in
supervisor mode (the hardware registers), low resolution set if the
desktop was in medium; the Amiga's tables (`c/platform/amiga/tables.c`:
the glyphs, the colours, the palette, its 4-bit guns converted to the ST's
3 bits and the STE's fourth bit); the mouse pointer hidden (Line-A).

The screen's four bitplanes are interleaved word by word (a 16-pixel group
is four words, one for each plane), so a cell is one byte in each of four
words two bytes apart, and one `MOVEP.L` writes a row of a cell to all four
planes: `st_cell` takes the glyph's byte four times over from a table,
masks it to the colour's planes, and writes it, eight times.

Time: TOS's count of vertical blanks (`_frclock`), three to a game frame at
50 Hz, 4,3,4,3,4 at 60 Hz (the Amiga's NTSC pattern). The MFP's timer A
counts at 38400 Hz (26 us), set up by `Xbtimer`, an interrupt every 64
counts (1.7 ms): it ends the tones (step 1), and the test build times each
frame with it.

## Step 0: the core on TOS, measured

**Correct.** Every recording replays with every hash equal: dm4, coop4,
dm2, coop1, dm2s, coop2s and the 2995 frames of coop2-score on an ST at
50 Hz; dm4 on an ST at 60 Hz and on an STE.

**Frame work**, an ST (68000, 8 MHz, 1 MB):

| Recording | Median | 90th percentile | Over 60 ms | A500 median, 90th |
|---|---|---|---|---|
| dm4 (4 players, deathmatch) | 9.2 ms | 11.8 ms | 3 of 498 | 12.5, 32.5 ms |
| coop4 | 10.1 ms | 14.5 ms | 5 of 498 | |
| dm2 | 7.5 ms | 8.9 ms | 3 of 498 | |
| coop1 | 15.1 ms | 16.8 ms | 5 of 298 | |
| coop2-score (2995 frames) | 8.6 ms | 11.2 ms | 26 | |
| dm4, 60 Hz | 9.2 ms | 12.0 ms | 3 | |
| dm4, STE | 9.2 ms | 11.8 ms | 3 | |

The flush is 4.7 ms a frame on average (the A500's 6.2). The frames over
60 ms are the round starts: a full redraw of the 1000 cells is 193 ms
(the A500's 313), and the frame before it about 70 ms (the map). The
renderer is cheaper than estimated in the plan (about 45 ms for a full
redraw): the cost is in the loop around it (two divisions and a
multiplication a cell for its place, the call), not in the screen writes.
A known pause once a round, as on the Amiga, left as it is.

So the ST has more headroom than the A500: about a sixth of a game frame
is used.

## Step 1: the platform layer, played in Hatari

**Keyboard and joysticks.** Both come from the keyboard processor (the
IKBD) through the keyboard ACIA: a key's make and break codes, and, once
it is put in joystick mode (command 0x14), a packet for every change of a
joystick (0xfe port 0, 0xff port 1, then fire in bit 7 and the
directions in bits 0-3). The game replaces TOS's handler of those bytes
(`ikbdsys` in `Kbdvbase()`) with `st_ikbd`, which keeps the state of every
key and both joysticks and skips the other packets (the mouse, the clock,
the status); TOS's keyclick goes with it. The mouse is off while the game
runs (port 0 is a joystick), back on at the end (0x08).

| Source | Keys |
|---|---|
| key set A, "CURSOR AND SPACE" | the cursor keys; SPACE fires (RETURN when another player has key set B) |
| key set B, "WASD AND E" | W A S D, E fires |
| joystick port 1, joystick port 0 | the IKBD's joystick packets (port 0 is the mouse's) |
| text entry (the room code) | the letters; BACKSPACE or DELETE deletes, ESC cancels |

**Sound.** The YM2149's channel A plays a square wave at the MZ's pitch
(the period ratio * 125000 / 1108400, its 2 MHz over 16), volume 12, put
to 0 by timer A's interrupt when the tone's length is up. The mixer's
port bits (the floppy's select lines) are kept, and every register write
is made with the interrupts off, as the interrupt writes one too. Timer A
first ran every 256 counts (6.7 ms) and the beeps came out 6.7 to 14.4 ms
long for the 13 asked, by where they fell between two interrupts; every 64
counts (1.7 ms) they are 14.3 to 15.4 ms.

**Leaving the game.** ESC on the title screen ends the program, as on the
Amiga (ESC still cancels the room code and the lobby, and leaves a match).
The program runs in supervisor mode, entered by a wrapper `main` (the
core's is compiled as `bomber_main`); the end comes back to it by
`longjmp`, which restores the screen, the palette, the keyboard, the mouse
and the YM and leaves supervisor mode there. Leaving supervisor mode from
deeper down, in an `atexit` handler, crashed on the way out ("Panic:
Illegal Instruction": the return went to a wrong address), so the game
does not end through `exit()`. Started from the desktop it returns to the
desktop, in medium resolution too (switched to low and back).

**Tested** (`tools/st_play.py`, Hatari in real time, the program logging
the players' places to `C:\PLAY.LOG` in an `ST_PLAYTEST` build with a
4-player deathmatch preset): the title, SPACE to start, key set A and key
set B in all four directions, the joystick in port 1 and the one in port
0 in all four directions (Hatari's keyboard joystick, a run each); the
keys go in as ST scan codes through Hatari's command FIFO (`--cmd-fifo`,
`hatari-event keydown`), the joystick keys through X. Then the sound:
Hatari records its output to a WAV (`recsound`, SDL's dummy audio driver)
while the menu's cursor moves, and every beep is measured: five keys,
five beeps, 14.3-15.4 ms, the pitch within the YM's rounding (2161 Hz for
2123 asked, 1439 for 1425). By hand: ESC from the title back to the
desktop, from the AUTO folder, from the desktop, and from a desktop in
medium resolution. All recordings still replay with every hash equal.

## Risks and open questions

- The network: a WiFi modem runs at 19200 bps (about 1900 bytes/s); a
  4-player match needs about 600 bytes/s from the relay and 220 to it.
  TOS's serial receive buffer is small: it may need enlarging (`Iorec`).
- STiNG cannot be tested in Hatari.
- MiNTLib's stdio makes the program 141 KB; libcmini would make it
  smaller if that matters for a floppy (it does not: 720 KB).
