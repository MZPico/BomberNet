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
   of every key), the game port's two joysticks, the speaker, Alt+X to DOS;
   the title and lobby; play in DOSBox-X. A CGA renderer. Done, see
   below.
2. **The network**: `tcp.h` over a WiFi modem on a COM port (the 8250/16550
   UART, interrupt-driven), DOSBox-X's own modem emulation for the tests;
   cross-play with every machine; the production relay. Done, see below.
3. **Release**: `BOMBER.EXE` and a disk image; the relay on the command
   line (`/RELAY=`, no `set_relay.py`); the README. Done, see below.
4. Ethernet through a packet driver, our own small TCP/IP; AdLib/Sound
   Blaster FM tones. Done, see below. Optional: Tandy, Hercules, MDA
   renderers.

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
colours, four bitplanes in parallel at A000:0000. A cell is written plane
by plane through the Map Mask: the colour's planes get the glyph's rows,
the others zeros (step 0 drew through Set/Reset and the latches; see step
2 for why not any more). The
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

ESC leaves a match; Alt+X on the title ends the program (the keyboard, the
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
- Solid is a player, textured an enemy. Player 1 green, player 2
  yellow; players 3 and 4 the same colours hollow (each block of the
  figure with its centre cut out: rows 2 and 6, columns 2 and 6), their
  status-line marks (the digit, the figure icon) inverse; the text always
  solid.
- The enemies' four colours (8 types: two shapes, four colours each, an
  enemy cycling through them) as checkerboards of two palette colours,
  by their codes (C0-DFh): red solid (red and black would look like the
  pillars), magenta olive (red and green), cyan lime (green and yellow),
  yellow dim yellow (yellow and black). A checkerboard is two masks, a
  row's parity choosing, as cheap as a solid colour.
- The bombs (60-7Fh, both halves) red and yellow: the game's blue would
  be palette 0's green, player 1's. Fire solid yellow.
- On the title the legend's figures (the mosaic glyphs, found at the
  start: every pixel in four 3 x 3 blocks, each full or empty) textured
  the same way in palette 1: dim magenta, magenta, dim cyan, pink.

A first version made every enemy red; the variety was asked for and
chosen from a mockup of a composite frame (the enemy in its four
colours, both shapes, two bombs). Dithering the players had been tried
and dropped (the status line could not match).

Tested: every hash equal (dm4, coop4) on DOSBox-X's `machine=cga`; the
play test passes; the title, a deathmatch and a co-op frame look as the
mockups.

**Speed on an XT.** In C the CGA flush took 37 ms of an XT's frame (DOSBox
cycles 300), the frame's work 61 ms: too slow. The CGA's fast path: when
the screen switches (title or game) every code's 8 screen words are
worked out (its glyph, players 3 and 4 hollow, its colour's row masks:
a 4 KB table), so a cell is 8 word copies (MOVSW, the banks 2000h apart),
its offset from a table (no division); the changed cells are found by
REPE CMPSW (`cga_next`), both in Watcom's inline assembly. The status
line and the players' recolouring keep the C path (cga_cell). DOSBox's
cycles count instructions, not an 8088's clocks (a division costs it as
much as a move), so the XT figures are a guide:

| dm4, cycles 300, 8086 | Median | 90th percentile | Over 60 ms | The flush |
|---|---|---|---|---|
| CGA in C | 61.0 ms | 82.8 ms | 272 of 498 | 37.5 ms |
| CGA, the fast path | 37.6 ms | 51.5 ms | 29 | 8.5 ms |
| CGA, the fast path, no state hash (a local game) | 34.0 ms | 45.8 ms | 12 | 8.4 ms |
| EGA in C, no state hash | 52.1 ms | 68.8 ms | 90 | 33.2 ms |
| EGA, the fast path, no state hash | 30.4 ms | 41.0 ms | 8 | 8.4 ms |

The EGA's fast path is the CGA's in planar form: every code's colour (the
adapter's register: a VGA's DAC entry, an EGA's hand-chosen one) and
glyph worked out when the screen switches, a cell in assembly (an OUT to
the Map Mask, the 8 rows; an OUT, 8 zeros), the same REPE CMPSW scan and
offset table. On a 286 (cycles 3000) the flush is 0.9 ms a frame (2.8
before), a full redraw 42 ms (85-100).

An XT now plays at full speed on either adapter but for a short catch-up
in big explosions; a round start is 0.5-0.7 s (the tables, the 1000
cells).

## Step 2: the network over a WiFi modem

**The driver** (`c/platform/pc/tcp_modem.c`): the Atari ST's Hayes logic
(AT, ATE0, ATNET0, ATDThost:port, CONNECT and its LF dropped, "+++" with
its guard seconds, ATH, NO CARRIER in the stream) over the PC's UART
(8250 or 16550, its FIFO when there is one), driven by its interrupt both
ways: a 4 KB ring for what comes in, a 1 KB ring for what goes out (the
transmitter's interrupt empties it), so the game never waits for the line.
The modem is looked for on COM1 and COM2 at 19200 and 115200 bps, the
first that answers AT with OK; a port where nothing at all answers costs
0.4 s. The UART's interrupt is given back at the end, after hanging up.

**The test bench.** DOSBox-X emulates the modem itself (`serial1=modem`:
its ATDT host:port opens a TCP connection), so no emulator of ours is
needed. `tools/pc_match.py`: two PCs, each on an X display of its own
(the keys go to the window with the focus), through the relay (`RELAY=`),
the adapter `PC_VIDEO=`; `tools/crossplay.py` takes `pc`. The test builds
(`tools/build_pc.py --net host|join`) log the room's state as the ST's do:
formatted into a far RAM buffer, written every 32 frames, the map on the
hashed frames' lines. DOSBox caches the mounted folder's file list, so the
joiner's `C:\CODE.TXT` is made before the start with "????", and the
build takes it only when it holds four letters.

**Results** (`tools/crossplay.py 300`, the local relay; the PC in DOSBox-X
in real time, VGA, cycles 3000; every 16th frame's hash, and the maps at
every frame both machines have one, compared):

| Host | Joiner | Frames | Hashed frames | Mismatches | Delay |
|---|---|---|---|---|---|
| PC | ZX Spectrum | 305 | 19 | 0 | 2 |
| ZX Spectrum | PC | 317 | 19 | 0 | 2 |
| PC | Sharp MZ-800 | 312 | 19 | 0 | 2 |
| PC | Amstrad CPC | 305 | 18 | 0 | 2 |
| PC | Commodore 64 | 305 | 19 | 0 | 3 |
| PC | Atari ST | 323 | 20 | 0 | 2 |
| PC | Amiga | 308 | 19 | 0 | 2 |
| Amiga | PC | 304 | 18 | 0 | 1 |

`tools/pc_match.py`: two PCs, 2542 frames in 150 s, 159 hashed frames, 0
mismatches; 4 players (2 local each), 1056 frames; on CGA, 711 frames;
through the production relay, 1007 frames at 15.7 frames a second: all 0
mismatches. The lobby's delay: 2-3 (120-180 ms).

Two harness matters found on the way, neither the PC's: with the MZ
(stepped slowly) a ready tap of SPACE reached the game, player 1 bombed
itself, and the "wins the round" message waited for fire forever (the
harness now fires on both machines when no game frame comes for a
while); and the maps were compared at different frames (the logs keep one
every 16 frames), which showed a C64 run as different: they are now
compared at the frames both machines have.

**Found by a user** (DOSBox-X on Windows, with this port's conf; its own
default conf and plain DOSBox were fine): stripes wherever black should
be. The cells were drawn through Set/Reset with the latches holding zeros,
loaded from a byte past the screen; zeroing that byte every frame cured
VGA but not EGA there. So the latches are not used any more: a cell is
written plane by plane through the Map Mask (the colour's planes the
glyph, the others zeros: 16 memory writes and 2 port writes, the flush
2.8-3.0 ms a frame at cycles 3000 against 2.6). Nothing hidden is left to
an emulator.

## Step 3: the release files

`tools/build_pc.py --img` writes, beside `BOMBER.EXE`, `bombernet.img`: a
360 KB 5.25" floppy image (40 tracks, two sides, 9 sectors; media byte
FD), which every PC drive and the floppy emulators read, made by
`tools/make_st_disk.py --pc` (the ST's 360 KB layout is the PC's own).
On it `BOMBER.EXE` and `README.TXT` (`c/platform/pc/README.TXT`, CR LF:
the keys, the options, the network, DOSBox-X's settings). Not bootable:
that would need a DOS on it. Its boot sector says so and boots again on a
key (29 bytes of code: the message by INT 10h, a key, INT 19h), instead
of hanging. The relay is a command-line option, so nothing patches the
files.

Tested in DOSBox-X: the image mounted as A: (`imgmount a bombernet.img -t
floppy`) lists both files and starts the game; booted (`boot
bombernet.img`), it shows the message. mtools reads it (`minfo`: 2 heads,
40 cylinders, FD) and gives `BOMBER.EXE` back byte for byte.

Still open: real machines (an XT's speed, the joysticks, a packet driver
on a real card, a WiFi modem's firmware), Tandy, Hercules, MDA.

## Ethernet

Any network card with a DOS packet driver (the FTP Software
specification: Crynwr's drivers, the makers' own), loaded before the game,
for example `NE2000 0x60 3 0x300`. The game looks for it on interrupts 60h
to 80h (the "PKT DRVR" signature) and takes two packet types, IP and ARP;
`tcp_pc.c` uses it in preference to the modem. On top of it a TCP/IP of
our own, `tcp_pkt.c`, about 600 lines:

- **The driver's upcall** (in its interrupt, twice a frame: a buffer, then
  "filled") is a few instructions of inline assembly that hand out one of
  16 far slots of 1514 bytes (24 KB from the far heap, not in the .EXE).
  The game takes the frames out whenever it reads the network.
- **ARP:** one entry, the next hop; requests for our address answered.
  **IPv4** without options or fragments; **ICMP** echo answered.
- **DHCP** (the address, the mask, the gateway, the DNS server) on the first
  connection, unless `/IP=` is given; **DNS**: one A record (the built-in
  relay is a name).
- **TCP:** one client connection. Every segment acknowledged at once;
  data beyond a gap kept (one island, in the receive ring) until the gap
  comes again; go-back-N retransmission on a timeout from the measured
  round trip (Karn, 100 ms to 3 s) or on the second duplicate ACK. ESC or
  the end sends RST and gives the packet types back (else the driver would
  call into freed memory).

**Test bench.** DOSBox-X's NE2000 with its slirp back end
(`[ne2000] ne2000=true backend=slirp`): the guest gets 10.0.2.15 by DHCP,
the host is 10.0.2.2, DNS 10.0.2.3. `PC_NET=pkt tools/pc_match.py` and
`tools/crossplay.py` (which uses it) set that up, with Crynwr's
`NE2000.COM` (GPL, from crynwr.com's pktd11.zip, fetched once into
`build/pc/pktdrv`). `CFLAGS_PC=-dPKT_DROP=n` loses every n-th segment
going out and every (n+2)-th coming in, for the retransmission. The test
builds log a line with the TCP state when the game stands still 2 s.

**Windows.** DOSBox-X's Visual Studio build has no slirp ("Backend not
supported in this build: slirp" in its log). Its NE2000 is then there for
the packet driver, but with nothing behind it, and the game freezes when
it first sends: tried in the 2026.10.01 release under Wine, after a user's
report. The MinGW build of the same release has slirp 4.9.3 and hosts a
room on the production relay at once (also under Wine). With Linux's
`backend=nothing` the game does not freeze: DHCP gives up (3 tries of
1.5 s) and the lobby says NO CONNECTION.

**Results** (0 mismatches in all):

| match | frames a second |
|---|---|
| PC-PC, local relay, 2 min, 7 runs | 16.3 |
| PC-PC, production relay (by DNS) | 16.2 |
| PC-ZX, MZ-PC, PC-ST | fields identical |
| 1 segment in 40 lost | 15.8 |
| 1 in 7 lost going out, 1 in 9 coming in | 9.3 |
| XT speed (cycles 300), CGA | 6.1 (the test's log: 1.5 s a write) |

Some runs seemed to stand still after about 90 frames. That was the
test, not the network: its last SPACE to ready up can land in the game,
the player drops a bomb and dies at about frame 90, and the game's frame
counter (what `NET.LOG` and the frames a second count) stops at the end
screen while the lockstep goes on (the relay's log has every input up to
the end of the run). The stall lines with the TCP state showed it: the
connection healthy, data flowing both ways. The sixteen slots and the kept
island stay (cheap, and better on a busy real network), but they did not
fix a hang. The network test logs once a game frame (a line a loop had
slowed an XT to a crawl).

## Network play on an XT (2026-10-24)

A user's XT at 4.77 MHz (DOSBox-X, 240 cycles) was slow in network play
and fine alone. Measured with an XT host (300 cycles, EGA) and a fast
joiner, so that the XT never waits for the other (one `net_poll` a frame);
the network test build times each frame's work, the core's network calls
(`nettime.c`: netdev_soft's entry points renamed and wrapped) and the TCP
layer (`tcp_pc.c`), in the timer chip's counts:

| per game frame, median | modem | Ethernet |
|---|---|---|
| the core's network calls | 11.1 ms | 22.3 ms |
| of them the TCP layer and driver | 9.7 -> 7.6 ms | 19.9 -> 17.5 ms |
| Ethernet: packets sent (driver call) | | 2 -> 1 (4.7 -> 2.6 ms) |
| Ethernet: frames received, handled | | 2 (8.7 ms) |

The game alone takes about 30 ms a frame there (step 0). What changed:

- **The modem's NO CARRIER check**: every byte received shifted a 14-byte
  window and compared it again; a matcher's state now, one compare a byte.
- **Delayed ACKs (Ethernet)**: in-order data is acknowledged by our next
  segment, the keys, which go every 60 ms; a bare ACK only after 70 ms (at
  40 ms it went alone, just before the keys). A gap, old data or a FIN is
  still acknowledged at once (the sender's fast retransmit counts on it).
  Loss test (one segment in 7 out, 1 in 9 in): plays on, hashes equal.

**The test log misled at first.** The per-frame line (300 characters, 32-bit
decimals: a library division on the 8086) and the 2000 hex digits of the
map every 16th frame cost an XT 10 to 20 ms a frame; the frame sync's
"work" read 52 ms where the game and the network take about 40.
`-dNETLOG_SPARSE` logs only the hashed frames' lines, without the map; the
pace is then the clock across 16 frames:

| XT host, EGA, fast joiner | modem | Ethernet |
|---|---|---|
| 300 cycles | 60.0 ms a frame (full speed) | 60.9 ms |
| 240 cycles (the user's) | 65 ms | 75 ms |

Those 240-cycle figures came from short runs (a player died at frame 130,
the test's own SPACE: the network test build now ignores fire in a game).

**In assembly then (the PC's files only; the shared code is as it was):**
the one's complement checksum (ADD, ADC 0 a word), network byte order
inline (`#pragma aux`: a load and XCHG), the TCP rings filled and emptied in
two block copies, the modem's bytes into its ring in blocks (one interrupt
lock a block, not a byte), and the packet driver called directly (flags
pushed, a far call through its vector) instead of through `int86x`. At 300
cycles, per frame: the modem's TCP layer 7.6 -> 3.2 ms, the core's network
calls 11.1 -> 7.0; Ethernet's 17.5 -> 14.6 and 22.3 -> 18.7, the received
frames 8.8 -> 6.0. Two minutes each at 240 cycles (the user's), with a
light log, all 2017 frames:

| 240 cycles, per game frame | before | after |
|---|---|---|
| modem, median | 60.0 ms | 60.0 ms |
| Ethernet, median | 68.2 ms | 63.2 ms |
| Ethernet, overall | 72.6 ms | 66.9 ms |

(Overall includes the test log's disk writes and the stage starts' map
generation, up to about 200 ms a 16-frame stretch.) So at 240 cycles the
modem keeps full pace and Ethernet about 95 %; the release build, without
the test's timing calls, a little better. Normal and lossy matches: hashes
equal.

## AdLib and Sound Blaster

The game's sound is short square-wave tones (the MZ's 8253 divider and a
length). Where an FM chip answers (an AdLib's or any Sound Blaster's OPL2,
or an OPL3, at 388h, else at the BLASTER variable's port + 8) the tones
are FM, else the speaker; `/SPEAKER` keeps the speaker. Not the Sound
Blaster's DSP: an exact square wave from it needs DMA and its interrupt,
several times the code, for little gain.

- **Detection:** the AdLib timer test (timer 1 started, its flag up within
  3 ms), after the game's 1 kHz timer is running.
- **The voice:** one channel, two operators; the modulator at twice the
  frequency (the sidebands on odd harmonics only, as a square's), 24 dB
  down; held while the key is on, the key off from the timer's interrupt
  as the speaker's is. The octave and F-number from the top octave down
  while the F-number has room: the pitches within 0.2 %.
- **Writes:** 6 status reads after a register's index, 35 after its
  value (AdLib's 3.3 and 23 microseconds), interrupts off around a tone's
  three writes. ESC or the end: every key off, every operator quiet.

`tools/pc_play.py --sound fm` gives DOSBox-X a Sound Blaster 16 with the
speaker muted (`--sound speaker`: no Sound Blaster). Both pass: 5 beeps,
12.9 to 13.4 ms (13 asked), 2121 and 1423 Hz (2123 and 1425 asked). The
harmonics, through the 9th, are within 2 dB of DOSBox-X's speaker's.

## The command line

`BOMBER.EXE` takes options, `/` or `-`, in any case (`BOMBER /?` lists them):

```
/RELAY=host[:port]   the relay (port 8765 if not given; else the built-in one)
/CGA /EGA /VGA       the adapter's look (else the one found): /CGA on an EGA or
                     VGA draws the CGA palettes, /EGA on a VGA the EGA colours
/COM1 /COM2          the WiFi modem's port (else both are tried)
/BAUD=n              the modem's speed (else 19200, then 115200)
/NOSOUND             no sound
/SPEAKER             the PC speaker (else an AdLib or Sound Blaster's FM)
/NET=PKT /NET=MODEM  Ethernet or the modem only (else Ethernet where a packet
                     driver is loaded, the modem otherwise)
/IP= /MASK= /GW= /DNS=  Ethernet's addresses (else DHCP's)
```

An unknown option prints the list and returns to DOS with error level 1.
On the PC the relay is a command-line option rather than a patched field:
a batch file or a shortcut carries it, and `set_relay.py` is not needed.

## Risks and open questions

- Speed on an 8088 is an estimate until a tester runs it.
- The XT figures are DOSBox estimates (it counts instructions, not an
  8088's clocks) until a tester has a real XT.
