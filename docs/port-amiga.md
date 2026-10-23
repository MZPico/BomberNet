# BomberNet on the Amiga: plan and progress

The first 68000 port (`docs/portability.md` has the rules; the core's
big-endian hashing, `PLAT_HASH_PORTABLE`, is already tested on PowerPC).

- CPU: 68000 at 7.09 MHz (PAL; 7.16 NTSC), 16/32-bit: the busiest frame
  is expected far under the 60 ms the Z80 and 6502 machines nearly fill.
- Screen: a bitmap, 320 x 256 on PAL (320 x 200 NTSC), 4 bitplanes = 16
  colours: 40 x 25 cells of 8 x 8 pixels drawn from pre-rendered glyphs, as
  the CPC port does, any colour a cell.
- Network: `bsdsocket.library`, the one socket API every Amiga TCP/IP
  stack provides (Roadshow, AmiTCP, Miami, Genesis) over any hardware
  (Plipbox, X-Surf, A2065, PCMCIA Ethernet and Wi-Fi on the A600/A1200);
  FS-UAE and WinUAE implement it themselves with the host's network.
- Requirements: Kickstart 2.04 or later (the stacks need it), 512 KB of
  chip RAM and 512 KB more recommended, any Amiga (OCS, ECS, AGA).

## Steps

0. **Measure first**: the toolchain; the core built for the 68000; the
   screen (an Intuition screen, the cell renderer); a recording replayed in
   FS-UAE with the hashes checked and the frames timed. Results written to
   a host directory mounted as an Amiga drive. Done, see below.
1. **The platform layer**: keyboard (two key sets, the room code's
   letters), joysticks, Paula tones, PAL/NTSC frame timing, memory; the
   title and lobby; play in FS-UAE. Done, see below.
2. **The network**: `tcp.h` over `bsdsocket.library`; the lobby's delay;
   cross-play with every machine through the local relay (FS-UAE's own
   `bsdsocket` emulation), then the production relay. Done, see below.
3. **Release**: an executable plus an ADF; `set_relay.py` on the executable
   (a plain file: the field is contiguous) and the ADF; hardware testers;
   the README and the catalog. Done but the testers and the catalog, see
   below.

## Design

**An OS program, not a takeover.** The game runs from Workbench or the
Shell on a screen of its own and leaves the OS running: the TCP/IP stack
is an OS process and `bsdsocket.library` needs multitasking. So:
- the screen is an Intuition custom screen (320 x 200 of 4 bitplanes; on
  PAL 320 x 256 with the field in the top 200 lines or centred) with a
  borderless backdrop window for input events;
- the frame clock is a vertical-blank interrupt server (`AddIntServer`,
  `INTB_VERTB`) counting TV frames: a frame is 3 PAL or 4,3,4,3,4 NTSC
  ones, as on the other machines; the game waits for it with a busy loop
  or `WaitTOF`;
- the keyboard comes as raw key events (IDCMP `RAWKEY`) from the window:
  the matrix state is kept from the down/up events, so several keys at
  once work as on the other machines (cursor keys and SPACE; W A S D and
  E; letters, DEL and ESC for the room code);
- the joysticks are read from the hardware (`JOY0DAT`/`JOY1DAT`, the CIA
  fire bits), as most Amiga games do, port 2 first;
- the tones go through Paula: one channel (allocated through
  `audio.device` once, then the registers), a looped square-wave sample
  at the tone's period, switched off by the vertical-blank server after
  the tone's length, as on the C64 (the MZ's lengths);
- memory: the screen's bitmap in chip RAM (allocated by Intuition), the
  program anywhere; no fixed addresses.

**The screen.** Each logical cell is one of the MZ's 8 x 8 glyphs in one
of 16 colours: the glyph's 8 bytes go to the four bitplanes (a byte to
each plane where the colour has the bit set, zero where not), 32 byte
writes a cell, by the CPU: the flush of a few changed cells a frame costs
nothing measurable, a stage's full redraw about 32 KB of writes, a few
milliseconds. The blitter is not needed. The palette: black, the MZ's
seven inks, the four players' own colours (as on the C64), and spares.
Tables from `tools/make_amiga_tables.py` as for the other machines.

**The network.** `tcp.h` over sockets: `gethostbyname` at `tcp_open`,
`socket`/`connect`, the socket set non-blocking (`IoctlSocket FIONBIO`),
`send`, `recv` (0 bytes with `EWOULDBLOCK` is "nothing"; a closed
connection is -1), `CloseSocket`. The stack receives in the background,
so a message is taken by the first poll after it arrives (the Atari's and
the C64's fixed behaviour). The WebSocket client and the room protocol
are shared, with binary input lines. Without a stack running (no
`bsdsocket.library`), the game plays locally.

**The toolchain.** Either bebbo's gcc for AmigaOS (`m68k-amigaos-gcc`,
gcc 6, built from source) or vbcc with the NDK (binary releases). Step 0
takes whichever builds here within the hour; the core compiles with
sccz80, llvm-mos and gcc already, so either should do.

**The test bench.** FS-UAE on the invisible X display, a host directory
mounted as a hard drive (`DH1:`) for recordings and results, its
`bsdsocket.library` emulation for the network, screenshots by `x11grab` as
with Altirra. FS-UAE has no remote monitor like VICE's: test builds are
driven by compiled-in recordings (the hashes checked, the frames timed in
the vertical-blank count and the CIA timers) and by settings preset in
the build (hosting a room on SPACE, as the Atari's `NETTEST_HOST`), and
write their figures to `DH1:`. A Kickstart ROM is needed: 3.1 ideally;
FS-UAE's free AROS replacement ROM is tried first.

**Distribution.** The executable (runs from any disk or hard drive, on
Workbench by icon or from the Shell) and a bootable ADF with it and a
`startup-sequence` for floppy-only Amigas; with a TCP/IP stack already
running for a network match.

## Step 0: the core on the 68000, measured

**Toolchain.** bebbo's gcc for AmigaOS, prebuilt in the Docker image
`amigadev/crosstools:m68k-amigaos` (gcc 6.5, the NDK, libnix): bebbo's
own GitHub repositories are gone, so the build scripts that fetch them
(and their forks) no longer work, and vbcc's Linux binaries were not
found where expected. `tools/amiga-cc` runs the compiler in the container
with the repository mounted; `tools/build_amiga.py` builds
`build/amiga/bomber`, an AmigaOS executable (`-O2 -noixemul -m68000`).

**Test bench.** FS-UAE 3.1 (the Ubuntu package) on the invisible X display
with its bundled AROS replacement ROM (boots to a shell; Intuition screens
and the timer work), a host directory as `DH0:` whose `Startup-Sequence`
runs the program; `tools/amiga_test.py`. No Kickstart image was needed.

**The platform so far** (`c/platform/amiga/plat_amiga.c`): an Intuition
screen of 320 x 200 in 4 bitplanes, the cells drawn by `amiga_cell` (68000
assembly: the glyph's 8 bytes to each plane where the colour has the bit
set), the vertical-blank server counting TV frames, the E clock for the
timing; `tables.c` from `tools/make_amiga_tables.py` (79 glyphs, the MZ's
inks and the players' colours in a 16-colour palette); the test mode
`AMIGA_TEST` as on the C64. Keys, joysticks and tones are step 1.

**Correct.** Every recording replays with every hash equal, through the
big-endian field-by-field hash (`PLAT_HASH_PORTABLE`, its first real
68000): dm4, coop4, dm2, coop1, dm2s, coop2s, the 2995 frames of
coop2-score, and dm4 on an NTSC machine.

**Frame work**, an A500 (68000, 7 MHz, 1 MB chip + 8 MB fast; fast RAM
makes no difference):

| Recording | Median | 90th percentile | Over 60 ms |
|---|---|---|---|
| dm4 (4 players, deathmatch) | 27.1 ms | 34.7 ms | 3 of 498 |
| coop4 | 27.1 ms | 35.0 ms | 5 of 498 |
| dm2 | 24.5 ms | 30.2 ms | 7 of 498 |
| coop2-score (2995 frames) | 25.5 ms | 33.8 ms | 26 |
| dm4, NTSC | 25.9 ms | 33.9 ms | 3 |
| dm4, an A1200 (68020, 14 MHz) | 6.9 ms | 8.4 ms | 2 |

Of a frame about 20 ms is the flush's pass over the 1000 cells (20 us a
cell), the logic the rest; without the hash 1.5 ms less. The frames over
60 ms are the round starts: a full redraw of the 1000 cells takes 360 ms
(360 us a cell: 32 byte writes with indexed addressing on a 68000, an
A1200 86 ms), a visible pause, for step 1 to shorten (two cells a word,
or the blitter); and the frame after a round start, about 90 ms (the map).
So the 68000 has the headroom expected: half the frame free for the
stack's work in step 2.

## Step 1: the platform layer, played in FS-UAE

Input (`plat_amiga.c`): a borderless backdrop window on the screen takes
the raw key events (IDCMP `RAWKEY`), and the state of every key is kept
from the down and up events, so several keys at once work as on the other
machines.

| Source | Keys |
|---|---|
| key set A, "CURSOR AND SPACE" | the cursor keys; SPACE fires (RETURN when another player has key set B: "CURSOR AND RET") |
| key set B, "WASD AND E" | W A S D, E fires |
| joystick port 2, joystick port 1 | read from the hardware (JOYxDAT, the CIA's fire bits) |
| text entry (the room code) | the letters; BACKSPACE or DEL deletes, ESC cancels |

ESC leaves a network match (BREAK elsewhere). The mouse pointer is hidden
on the game's screen. On PAL the screen is 256 lines with the field
centred (a 200-line screen let the screen behind show through the bottom
56 lines); on NTSC 200.

Sound: one Paula channel, allocated through `audio.device` (so other
programs' sound keeps working), plays a square-wave sample at the MZ's
pitch (period = ratio * 4 / 5) for the tone's length, silence after it;
the vertical-blank server switches the DMA off a frame or two later. Not
heard yet by anyone (FS-UAE here has no sound output).

The flush compares four cells at a time (32-bit words) for rows 0-23, and
draws a cell by `amiga_cell` with plain displacements (the rows 40 bytes
apart): a normal frame's flush is 6 ms (20 before).

Tested (`tools/amiga_play.py`, FS-UAE in real time, the keys sent to its
window by `xdotool`, the program logging the players' places to `DH0:` in
an `AMIGA_PLAYTEST` build with a 4-player deathmatch preset): the title,
SPACE to start, key set A in all four directions, key set B, and joystick
port 2 (FS-UAE's keyboard joystick) in all four directions; the audio
channel allocated. All pass.

Frame work, dm4 on an A500: median 12.5 ms, 90th percentile 32.9 ms
(27.1 and 34.7 in step 0). A round start's full redraw is still 313 ms
(360 before): the cost is not in the stores, perhaps in FS-UAE's chip
RAM timing; a known pause once a round, to be looked at after the network
(a row-wise redraw, or real hardware's figure).

## Step 2: the network in FS-UAE

**The driver** (`c/platform/amiga/tcp_bsdsocket.c`): `tcp.h` over
`bsdsocket.library` (version 3 or later), one socket, set non-blocking by
`IoctlSocket(FIONBIO)` after the connect: the stack receives in the
background, a poll takes what has arrived, a send that would block waits
20 ms (`Delay(1)`) and tries again. Without the library the game plays
locally. The relay is patchable: `tools/set_relay.py` on the executable
(the field is in the data hunk, the port in the 68000's byte order; the
file is recognised by its hunk magic).

**Memory.** The program runs the core on a 64 KB stack of its own,
switched to by `StackSwap` in a wrapper `main` (the core's `main` is
compiled as `bomber_main`): the Shell's default of 4 KB is too small, and
libnix ignores `__stack`. `mem.c` provides `memcpy` and `memset` (byte or
word moves): libnix's go to exec's `CopyMem`, and the AROS ROM's `CopyMem`
faults on a 68000 when source and destination are not aligned alike,
which the network's byte buffers bring about every frame. `pr_WindowPtr`
is set to -1 so no requester can pop up over the game's screen.

**Test builds.** `tools/build_amiga.py --net host|join` (`AMIGA_NETTEST`):
a preset menu (host or join a room of `PLAYERS` with `LOCAL` local
players; a joiner takes the room code from `DH0:code.txt`), the room's
state logged to `DH0:net.log` every frame (the hash, the seat table, the
map unless `AMIGA_NETTEST_NOMAP`), and a trap handler that shows a CPU
exception's number and PC as binary on the screen's top row and halts,
for a screenshot (the Amiga has no debugger to attach here).

**The ROM.** FS-UAE's bundled AROS ROM (2021) crashes on an emulated
68000 (A500) in the middle of a network match: an address error in its
`graphics.hidd`; an A1200 (68020) never crashed. The AROS nightly ROM of
2026-10-22 (`aros-rom.bin` and `aros-ext.bin` from the m68k boot ISO,
passed as `AMIGA_KICKSTART` and `AMIGA_KICKSTART_EXT`) has the bug fixed,
and all the results below are with it on an A500. No Kickstart image is
needed, the test bench stays free.

**Results.**

- `tools/amiga_match.py`: two A500s in FS-UAE, each on an X display of
  its own (`:97` and `:98`: a window alone has the keyboard, two windows
  on one display did not take `xdotool`'s keys), through the local relay,
  both in real time. 2018 game frames in 150 s, 127 hashed frames compared,
  0 mismatches; 4 players (2 local on each Amiga), 981 frames, 0
  mismatches. The lobby measures the delay as 1 (one game frame, 60 ms)
  between the two.
- `tools/crossplay.py 300 amiga zx`: the Amiga hosting a Spectrum
  (`tools/zxemu.py`, stepped by the harness), 300 frames, 18 hashed frames compared, 0
  mismatches, the maps identical at the end.
- Speed: a 2-Amiga match runs at the full 16.6 game frames a second
  (50 Hz / 3) with the network; the 68000's half-frame headroom of step 0
  holds with FS-UAE's `bsdsocket`. (Logging the 2000-character map every
  frame through the emulated hard drive halved it, so the match test
  leaves the map out.)
- The replay (`tools/amiga_test.py`, dm4) and the play test
  (`tools/amiga_play.py`) pass unchanged with the network linked in.

Not yet: the production relay, cross-play with the other machines, and
real hardware; see step 3.

## Step 3: release files, every machine, the production relay

**Release files.** `tools/build_amiga.py --adf` writes, beside the
executable `bomber`, `bombernet.adf`: an OFS floppy made by amitools'
`xdftool` (in the project's venv), bootable, with `bomber` and an
`s/startup-sequence` that runs it. It boots to the title on an A500 with
the AROS ROM. A hard-drive Amiga copies `bomber` anywhere and runs it
from the Shell (a Workbench icon is not made yet). Either way a TCP/IP
stack must be running before the game starts, for the network rows.

**The relay.** `tools/set_relay.py` patches the executable and the ADF:
on the disk it finds `bomber` through the root block's hash table, follows
its data blocks (488 bytes each after a 24-byte header; the field may
cross two) and redoes each block's checksum. Checked by patching an ADF,
reading the file back with `xdftool` and booting the disk.
`tools/amiga_test.py --adf` replays a recording from the floppy, `DH0:`
only taking the results: dm2, all hashes equal.

**Cross-play** (`tools/crossplay.py 300`, the local relay; the Amiga in
FS-UAE in real time, the other machine stepped by the harness; the hashes
of every 16th frame compared, the maps compared at the end):

| Host | Joiner | Frames | Hashed frames | Mismatches | Delay |
|---|---|---|---|---|---|
| Amiga | Sharp MZ-800 (mz800emu, MZPico) | 300 | 18 | 0 | 2 |
| Amiga | Amstrad CPC (M4) | 300 | 18 | 0 | 1 |
| Amiga | Commodore 64 (VICE, WiC64) | 300 | 18 | 0 | 1 |
| Amiga | ZX Spectrum (Spectranet) | 300 | 18 | 0 | 1 |
| ZX Spectrum | Amiga | 300 | 18 | 0 | 1 |
| Sharp MZ-800 | Amiga | 300 | 18 | 0 | 1 |
| Amiga | Amiga (`tools/amiga_match.py`) | 2018 | 127 | 0 | 1 |

The fields were identical at the end of every run. The Atari is not in
the harness (its emulator runs under Wine), so it has not met the Amiga;
it meets the others through the same relay protocol.

Twice in the first runs the MZ hosting the Amiga ended about 100 s into
the match with the MZ's abort flag 5 (`NETST_DROPPED`): the local relay
had ended the Amiga's session, while the Amiga itself kept playing (its
screen and log showed no crash). Six reruns, with the relay's ping and
pong frames logged, with them unlogged and with its keepalive pings off,
all ran the full 300 frames; every ping was answered. The cause is not
known; to watch for on hardware.

**The production relay** (`RELAY=api.mzpico.com:80 tools/amiga_match.py
60`): two Amigas, 1004 frames at the full 16.7 game frames a second, 63
hashed frames compared, 0 mismatches; the lobby's delay 2 (120 ms, as on
the other machines over the Internet).

**WinUAE.** The same files run in WinUAE on Windows: its own
`bsdsocket.library` (Misc: bsdsocket.library), the AROS nightly ROM or a
Kickstart, `bomber` from a directory hard drive or the ADF in DF0:.
Started and looked at by hand on Windows.

**Hardware testers** need: an Amiga with Kickstart 2.04 or later (the
program opens Intuition and graphics version 37), memory (tested with 1 MB
of chip and 8 MB of fast RAM; less is untested), a
network card or adapter with a TCP/IP stack (Roadshow, AmiTCP or Miami;
`bsdsocket.library` open before the game starts), and the ADF written to
a floppy or `bomber` copied to a hard drive. What to report: the title and
the game's look, the sound (never heard by anyone yet: FS-UAE here has no
sound output), the keys and joysticks, the lobby's DELAY against the
browser player, and a full match without DESYNC or CONNECTION LOST.

Still open: a Workbench icon; the round start's full redraw (313 ms on an
A500, see step 1); Kickstart 1.3 is not supported (the program needs
version 37 libraries and quits at once without them).

## Risks and open questions

- Kickstart 1.3 machines (an unexpanded A500 of 1987-1990) have no
  `bsdsocket` stack, and the program needs version 37 libraries: they
  cannot run it at all. Stated, not solved.
- The vertical-blank server and the hardware joystick reads are the usual
  Amiga practice but not strictly OS-clean; a stack that needs the CPU
  (Roadshow is light) shares it with the game: the frame time with the
  network is measured in step 2.
- The AROS ROM: FS-UAE's bundled one has a 68000 bug (step 2); the 2026
  nightly works. A real Kickstart was never needed in the test bench, but
  nobody has run the program under one yet: that is the hardware testers'
  first report.
- Toolchain build time and quirks (bebbo's gcc is a large build).
- The Atari ST could follow on the same toolchain family and renderer,
  if a network device worth supporting appears.
