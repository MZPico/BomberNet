# BomberNet on the Commodore 64: plan and progress

The second 6502 port, after the Atari XL/XE (`docs/port-atari-xl.md`), on
the same work: llvm-mos (its `c64` target), the 6502 helpers in
`c/cpu/6502/`, `netdev_soft.c` and `ws.c` with binary input lines.

- CPU: 6510 at 0.985 MHz (PAL; 1.023 NTSC), about 5 % of it taken by the
  video chip's character fetches in a text mode.
- Screen: the text mode is the MZ-700's: 40 x 25 cells of 8 x 8 pixels, one
  colour a cell (colour RAM) on a common background, black as in every MZ
  cell of the game and the title. The MZ characters are used as they are.
- Memory: llvm-mos loads the program at $0801 and unmaps BASIC, so
  $0801-$CFFF is free (50 KB); the screen and the character set go into the
  RAM under the KERNAL ROM, which the CPU only writes.
- Network: a `tcp.h` for one of WiC64 (Wi-Fi on the user port; VICE 3.9
  emulates its TCP commands), the Ultimate's command interface, or a
  SwiftLink-type serial cartridge with a Wi-Fi modem; chosen in step 2.

## Steps

0. **Measure first**: the platform's screen and frame clock, the game
   replaying recordings in VICE, its frames timed. Done, see below.
1. **The platform layer**: keyboard (two key sets, the room code's
   letters), the title and lobby screens, tones, joysticks; play in VICE.
   Done, see below; play on a real C64 or by people in VICE still to come.
2. **The network**: the device's `tcp.h`, its cost per frame, lockstep and
   cross-play tests, the production relay. Done in VICE, see below: the
   WiC64 runs in the background, a match at full speed with the firmware's
   timing; a real WiC64 still to confirm it.
3. **Hardware test, release.** `set_relay.py` learns `.d64` (the sector
   chain); a `.prg` already works as a plain file.

## Step 0: the platform measured in VICE

`c/platform/c64/`:
- `plat_c64.c`: the platform layer so far (screen, frame clock, SID tones,
  joysticks, SPACE and RUN/STOP) and the test mode `C64_TEST`: a recording
  compiled in, replayed from the title, every game frame's hash checked and
  its work timed in cycles (CIA 2's timer A counts cycles, timer B its
  underflows: from the start of the flush to the next frame sync, the
  measure of the Atari and the Spectrum);
- `c64.s`: the game screen's flush and the raster interrupt that counts TV
  frames (a frame is 3 PAL ones, or 4,3,4,3,4 NTSC ones);
- `tables.c`: from `tools/make_c64_tables.py`, the MZ characters (79 of 256
  glyphs for the game and the title together) and each logical code's
  screen code and colour.

The players are characters in their own colours (`plat_player_colour`
writes colour RAM), as on the MZ: no sprites are needed for them.

`tools/build_c64.py --test recording.bnr` builds it (24.9 KB with a
500-frame recording); `tools/c64_test.py` runs it in VICE (x64sc 3.9,
headless, warp: the figures are emulated cycles), reads the figures through
VICE's remote monitor and saves a screenshot.

Correct: every recording replays with every hash equal: dm4, coop4, dm2,
coop1, dm2s, coop2s and the 2996 frames of coop2-score.

Frame work on a PAL C64, hashing every 16 frames as in a network match:

| Recording | Median | 90th percentile | 99th | Over 60 ms |
|---|---|---|---|---|
| dm4 (4 players, deathmatch) | 43.0 ms | 48.8 ms | 62.0 ms | 7 of 499 |
| coop4 (4 players) | 44.1 ms | 51.6 ms | 65.7 ms | 5 of 499 |
| dm2 (2 players) | 41.7 ms | 45.3 ms | 63.5 ms | 5 of 499 |
| coop2-score (2 players, 2996 frames) | 42.5 ms | 47.3 ms | 60.2 ms | 32 of 2996 |
| dm4 without the hash (a standalone game) | 37.2 ms | 43.6 ms | 55.3 ms | 3 of 499 |
| dm4 on an NTSC C64 | 41.8 ms | 47.8 ms | 60.6 ms | 5 of 499 |

The frames over 60 ms are the round and stage starts (map generation), as
on the other machines. The recordings that hash every frame (coop1, dm2s,
coop2s: a test setting, not a game) take over 66 ms a frame, as on the
Atari.

The flush is 14.3 ms of a frame. Written first as the measurement
platform's (`c/platform/sim6502/flush.s`: every cell cleared and compared,
about 25 cycles a cell) it was 24.9 ms, and a dm4 median of 53.4 ms; done
as the Atari's (rows 0-23 not cleared, `composite_frame` overwrites them;
an unchanged cell 11 cycles) it saves 10 ms.

So the C64 is faster than estimated (44-48 ms for 4 players): the text
mode costs it less than ANTIC's costs the Atari. A network match adds the
network code (about 1 ms on the Atari, so about 1.5 ms here) and the
device: about 15 ms are left at the median and 10 at the 90th percentile.
That decides the device in step 2: WiC64 or the Ultimate would fit, a
serial cartridge too if the bytes come in by interrupt. If more is needed:
the hash in assembly (`PLAT_ASM_HASH`, about 6 ms of the 43 are the
hash's frames), or the core tracking changed cells instead of comparing
all 960 every frame.

## Step 1: the platform layer

Input (`plat_c64.c`): the program scans CIA 1's keyboard matrix itself (the
KERNAL's scan is off).

| Source | Keys |
|---|---|
| key set A, "CURSOR AND SPACE" | the two cursor keys for right and down, with either SHIFT for left and up; SPACE fires (RETURN when another player has key set B: "CURSOR AND RET", as the CPC's COPY) |
| key set B, "WASD AND E" | W A S D, E fires |
| joysticks 1 and 2 | the two control ports |
| text entry (the room code) | the letters; INST/DEL deletes, RUN/STOP cancels |

RUN/STOP leaves a network match, as BREAK does elsewhere; RESTORE does
nothing (its NMI goes to an RTI: the KERNAL's would stop the program).
Joystick 1 shares its lines with the matrix's columns: what it holds down
is left out of the scan, so it makes no phantom keys. Joystick 2 shares
the rows, which a scan cannot tell apart (as in most C64 games): a
keyboard player and a joystick in port 2 can disturb each other's keys,
so beside a keyboard player a joystick player best uses port 1.

The title and the lobby are the MZ's screens as they are (the title
table's characters in its colours), and so is the game's status line, with
the players' numbers, lives and scores in their colours.

Tested in VICE (`tools/c64_play.py`, real time): the title, the menu by the
cursor keys (down, SHIFT + down for up), SPACE to start, then a 4-player
deathmatch where every input moves its player: key set A in all four
directions, key set B, joystick 1 and joystick 2; bombs from all four. A
scratch program checked text entry: all 26 letters, INST/DEL, RUN/STOP, and
joystick 1 held without phantom keys. `tools/build_c64.py` also writes
`build/c64/bomber.d64` (VICE's `c1541`), which autostarts from the drive.
The recordings still replay with equal hashes (dm4: median 42.5 ms).

Tones: the MZ's tone dividers on the SID (triangle, voices 1 and 2), the
game running on, as on the CPC and the Atari; not yet heard by anyone.

`tools/vice.py` drives VICE: start it headless, read and write memory
through the remote monitor, hold keys and joysticks, take screenshots. The
monitor has no command for the keyboard matrix (its `keybuf` types into
the KERNAL's buffer, which the game does not read) and its joystick
simulation device did not work, so VICE carries a small local patch
(`tools/c64/vice-3.9-bombernet.patch`): `keybuf "#row col 1|0"` holds a
matrix key down or lets it go, `keybuf "#J port bits"` sets a joystick.

## Step 2: the WiC64

`c/platform/c64/tcp_wic64.c` and `c64.s`: `tcp.h` over a WiC64's TCP
commands (open "host:port", read, write, close; the firmware's version to
find it), its "revised" protocol as VICE 3.9 emulates it and as the
official library (github.com/WiC64-Team/wic64-library) does it: PA2 high
and port B out to send ('R', the command, the length, the payload), each
byte answered by FLAG2; PA2 low and port B in to receive (FLAG2, a dummy
read, then the status, the length, the payload, each byte after FLAG2). A
TCP read answers at once with what is waiting, nothing when nothing is
(wic64-firmware `TcpClient::read`, up to 8 KB). The network is in every
build (39.5 KB); without a WiC64 the game finds none.

**Why in the background.** The firmware pauses around every transfer
(`userport.cpp`, `service.cpp`; FreeRTOS at 1 kHz): 3 ms after a transfer,
1 ms before it sends, 5 ms between the parts of a split one. A TCP write
takes about 16 ms with them, a read about 12 ms, 21 with data. A first
driver waited for each command, as the library does: two commands a frame,
about 37 ms of waiting, so frames of about 95 ms (below). But the C64's own
part is only the bytes, about 40 a frame.

**The background driver.** While a connection is open, CIA 2's FLAG2 is an
NMI and each one moves the command in progress a step (`c64.s`): the next
byte out, the switch of direction, the dummy read, the status and length,
the next byte in. The WiC64 answers a byte within microseconds, so the next
FLAG2 comes while the handler still runs: that nested NMI only notes it,
and the first handler takes the step before it leaves (at most two deep).
`tcp_send` appends to one of two buffers, and one write command sends all
that was queued; `tcp_recv` hands out what the last read brought and asks
for the next (it answers "nothing" meanwhile). A command starts 2 ms after
the last one ended (CIA 1's timer B), as VICE's WiC64 sends one more FLAG2
after a reply's last byte; the raster interrupt starts whatever was missed.
An error status, a read bigger than its 2 KB buffer, or a command stuck for
2 s counts as a lost connection. Opening, closing and finding the WiC64
still wait for it (FLAG2 polled, the NMI off).

**Measured in VICE.** Stock VICE answers a TCP read or write a TV frame
later and repeats a read's reply every 30 frames unasked, which says
nothing about a real WiC64. The local VICE patch therefore has
`WIC64_FW_TIMING=1`: TCP reads and writes answer after the firmware's
pauses above (12 or 21 ms, 16 ms), once. `tools/c64_match.py` plays two
C64s against each other through the local relay, both in real time and
untouched for 30 s, with a `C64_NETTIME` build (`CFLAGS_C64=-DC64_NETTIME
tools/build_c64.py --out build/c64nt`):

| | game frames a second (16.7 is full speed) | a frame's work, waits included | late frames |
|---|---|---|---|
| waiting for each command (the first driver, 79c98cc) | 10.4 (62 %) | 95 ms | all |
| in the background, 2 players | 16.7 (100 %) | 57 ms, 2.8 ms idle | 4-7 of 500 |
| in the background, 2+2 players | 16.4 (98 %) | 60 ms, 1.3 ms idle | 31 of 491 |

About 2.1 WiC64 commands and 42 FLAG2 NMIs a frame; the NMIs cost about
3 ms of a frame. The late frames are the round starts, as without the
network. On real hardware the firmware's timing is the source's, not
measured: to be confirmed on a real WiC64.

Tested in VICE with its WiC64 (`-userportdevice 23`) against the local
relay (`tools/crossplay.py`, machine `c64`): the C64 hosting a Spectrum
(1+1 and 2+2 players), the CPC and the 128K with its ESP-01, and joining a
Spectrum and an MZ-800 (JSON lines): 300 frames each, every hash equal, no
aborts; two C64s for 500 frames (`tools/c64_match.py`).

VICE: built from source into `~/src/vice/dist` (`--enable-headlessui
--with-libcurl`; it needs `xa` and `dos2unix`), with
`tools/c64/vice-3.9-bombernet.patch` applied (the monitor's keys and
joysticks, the WiC64's firmware timing). VICE
3.9 crashes when it logs in colour to a pipe, so it runs with
`+logcolorize`.
