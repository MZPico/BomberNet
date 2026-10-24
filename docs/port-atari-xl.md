# BomberNet on the Atari XL/XE: plan and progress

The first port to a processor other than the Z80, chosen before the C64
(see `docs/portability.md` for the rules it follows):

- CPU: 6502 at 1.77 MHz, about 1.5 MHz left in a text mode after ANTIC's
  display DMA; the C64 has 0.94 MHz.
- Network: FujiNet, one widespread Wi-Fi device, TCP through its `N:` device;
  it can be emulated (FujiNet-PC with Altirra or atari800).
- A strong scene in Czechia and Poland.

## Steps

0. **Feasibility on the 6502**: the core built with llvm-mos, recordings
   replayed in its simulator, frames timed. Done, see below.
1. **6502 helpers** (`c/cpu/6502/`) and the screen flush in assembly;
   measure again, with the network code (`netdev_soft.c`, `ws.c`) against a
   relay inside the program. Done, see below.
2. **The screen**: 40 x 25 cells (a display list for the 25th row), colours
   per cell against hardware sprites for the players; mock-ups to choose
   from, then the cell tables from the MZ characters. Done: design D
   (`tools/make_atari_tables.py`).
3. **The platform layer**: display list, character set, flush, keyboard and
   two joysticks, POKEY tones, frame timing, memory map; replays identical,
   then screenshots and play in atari800. Done, see below.
4. **The network**: `tcp.h` over FujiNet `N:TCP`, emulated in a test harness;
   its cost per frame (the serial bus), lockstep and cross-play tests,
   production relay, FujiNet-PC. In progress, see below.
5. **Hardware test, release.** Then the C64 on the same 6502 work. The
   network build ships with 0.3.0 untested on a real FujiNet (the emulated
   chain is ten times slower than real time): the measured SIO cost makes a
   match 5 to 18 % slow, which a real FujiNet is to confirm before the
   background SIO is written.

## Step 0: the core on a 6502

`tools/sim6502.py <recording>` builds the core with llvm-mos (SDK 23.3.0,
`mos-sim-clang -O2`) for its 6502 simulator (`mos-sim`) with the measurement
platform `c/platform/sim6502/`: the recording is compiled in, every game
frame's state hash is compared with it, and each frame's work (from the
start of the flush to the next frame sync) is counted in CPU cycles
(`clock()`, the simulator's cycle counter). The flush is C: each changed
cell's code and colour into screen and colour memory, as a C64 or an Atari
would take them. Options: `--hash-off` (a standalone game), `--profile`
(cycles per function and game frame), `CFLAGS6502=-DFLUSH_NONE` (the frame
without the flush).

Correctness: the core compiles unchanged with a third compiler and replays
every recording with identical hashes: coop1, coop2s, coop4, dm2, dm2s, dm4
and 1500 frames of coop2-score, 0 mismatches. The program (core, MZ tables,
a recording, no network) is 41-49 KB.

Time per game frame, standalone, 4 players (dm4; coop4 and 1 player alike):

| | cycles | Atari (1.5 MHz) | C64 (0.94 MHz) |
|---|---|---|---|
| all, C flush | 110,000 | 74 ms | 118 ms |
| without the flush | 82,000 | 55 ms | 87 ms |

Where the cycles go (profile, 4 players, without the flush):

| | cycles per frame |
|---|---|
| `memcpy`: the playfield copy, 960 bytes (46 cycles a byte) | 44,600 |
| `memset`: the measurement's clearing of the draw buffer, 1000 bytes | 31,300 |
| all the game logic together | about 10,000 |

The flush in C adds about 60,000 (60 cycles a cell). So the game logic is
cheap on the 6502 (llvm-mos compiles it well); the frame is the library's
generic byte loops (16-bit counters, about 30-46 cycles a byte) and the C
flush over all 1000 cells. Writing the copy as a C loop with an 8-bit index
does not help: LLVM turns it back into a `memcpy` call (57 ms, slower).
llvm-mos's `memcpy` and `memset` are weak symbols, so a port can supply its
own: page-unrolled 6502 loops of about 10 cycles a byte, a per-CPU helper
under the rules, with no change to the core.

Estimate with the helpers (step 1 measures it): copy about 10,000, an
assembly flush of about 12-15 cycles a cell 15,000, logic 10,000:
about 37,000 cycles, 25 ms on an Atari and 40 ms on a C64 for a standalone
4-player frame. A network match adds the hash (measured: about 5,700) and
the network code and device, so the Atari has headroom and the C64 will be
tight, which confirms the Atari first. A core change that tracks changed
cells instead of copying and comparing the whole screen every frame would
help every machine further, but is not needed for the Atari.

## Step 1: 6502 assembly where the cycles were, the network code measured

- `c/cpu/6502/loops.s`: `composite_frame` (the playfield copy, about 11
  cycles a byte) and `composite_map`, under `PLAT_ASM_COMPOSITE` as on the
  Z80; the C in `core/video.c` stays the reference. Not a replacement of
  the library's `memcpy`: that would tie us to llvm-mos's calling
  convention for library routines, these take no arguments.
- `c/platform/sim6502/flush.s`: the measurement platform's flush in
  assembly, as a real port's would be: four quarters of 250 cells with
  absolute,x, the cell tables split into 256-byte code and colour halves
  (`sim_codes`, `sim_attrs`), about 25 cycles a cell.
- `c/platform/sim6502/tcp_fake.c`: a relay inside the program behind
  `tcp.h` (the WebSocket handshake, `create`, `ready`, and for each of our
  input lines the other slots' lines for that frame). With `SIM_NET` the
  platform opens a room through the real `net_detect`/`net_create`/
  `net_ready` and does a network frame's work every game frame: `net_send`
  of our keys, the other machines' lines through the WebSocket parser,
  `net_poll`, `net_hash`.
- `tools/sim6502.py` with `CFLAGS6502="-DSIM_ASM"` (the assembly),
  `-DSIM_NET -DSIM_SLOTS=n` (the network with n machines), `-Os` for size.

Correct: with the assembly, dm4, coop4, dm2s and 1500 frames of coop2-score
replay with 0 mismatches.

Busy 4-player deathmatch (dm4), median (90th percentile) of a frame's work:

| | Atari (~1.5 MHz) | C64 (0.94 MHz) |
|---|---|---|
| standalone, `-O2` | 27.6 ms (29.7) | 44.1 ms (47.5) |
| standalone, `-Os` | 29.4 ms (32.0) | 46.9 ms (51.1) |
| network, 2 machines, `-Os`, hash on | 33.6 ms (35.5; 99th 40.2) | 53.7 ms (56.6; 99th 64.2) |
| network, 4 machines, `-Os`, hash on | 34.0 ms (35.9; 99th 40.6) | 54.3 ms (57.3; 99th 64.8) |

The network figures hold the state hash (about 3.8 ms on the Atari) and
the network code, which is only about 1 ms on the 6502 (on the Z80, with
sccz80 and the Spectranet calls, 8 ms); not yet the device: FujiNet's
serial bus is step 4. The Atari has about 25 ms of a 60 ms frame left for
it, the C64 about 5.

Size (llvm-mos, per source file): the core and the network code are about
46 KB of code at `-O2` (`netdev_soft.c` 14.5 KB), 30 KB at `-Os`, plus
about 3 KB of tables and constants and 4 KB of variables: about 37 KB at
`-Os`, comparable with the Z80 builds (40 KB). That fits an Atari XL/XE
with BASIC off ($0700-$BFFF, less the screen, character set and display
list); the RAM under the OS ROM is there if needed. The build will be
`-Os`, 2 ms slower than `-O2`.

## Step 3: the platform layer

`c/platform/atari/` (`plat_atari.c`, `atari.s`, the generated `tables.c`),
built by `tools/build_atari.py` (llvm-mos `mos-atari8-dos-clang -Os`, an
`.xex` for DOS or an emulator, 27 KB, BASIC off). Memory: the program
from $2000 to about $95A0; the screen at $AC00 and the sprites at $B000,
fixed, under the C stack (MEMTOP $BC1F down by 1 KB), so that their
alignment wastes nothing; the build checks that the program ends below
the screen.

Colours: the MZ-700's, as near as the PAL palette has them (measured in
atari800): red $36, magenta $4A (hue 4: the MZ's magenta is pink, hue 5
is violet), pale yellow $EE, cyan $9A; players green $BA, yellow $EC,
white $0E, blue $78.

- Screen, design D: one 40 x 25 screen memory. In a game a display list of
  24 rows of ANTIC mode 4 (the field) and one of mode 2 (the status line);
  a DLI at the end of row 22 moves the four sprites to the status line (row
  23, the wall, has no sprite data, so nothing shows half-way), one at the
  end of row 23 switches font and colours. The title and lobby: the logo
  (rows 0-5) in mode 4, red and magenta as on the MZ, the rest mode 2 text
  (a DLI); the legend's five icons in their MZ colours by the four sprites
  and the missiles as a fifth. Mode 2 cannot colour the menu's text.
- Players: the four hardware sprites (single-line PMG), their shapes taken
  from the codes the core drew at their place, rebuilt only when these
  change. On the status line the same sprites colour each player's lives
  icon and count (`plat_hud_colour`). The player's number before the score
  stays white: it is not next to the icon, and four sprites have no room
  for a second piece each.
- The vertical blank is ours (VVBLKI), not the OS's: the clock, the
  hardware registers from the OS's shadows, the key debounce delay and
  SIO's timer 1, a tenth of the OS's two stages. The OS stays on for the
  keyboard IRQ, BREAK and (step 4) SIO.
- Input: two joysticks (PORTA, TRIG0/1); the keyboard as one player, the
  arrow keys without CTRL, fire SHIFT (or SPACE). POKEY tracks one key at a
  time: on a real machine SPACE is not seen while an arrow is held; SHIFT
  has its own line and works while walking. Fire is a press (one bomb
  each), sampled in the vertical blank: down for 2 blanks (40 ms), up for
  2 frames before the next, and not a press that began as another
  direction key came. That last rule is for Altirra's arrow key mode
  "Ctrl/Shift states mapped directly" (needed for SHIFT held before an
  arrow): it asserts SHIFT again while an arrow pressed with SHIFT is held
  and the next goes down, which dropped a bomb at each quick turn after a
  bomb (found with `-DFIREDEBUG`: fire count, SHIFT, KBCODE on the status
  line). Text entry from the OS's key code (CH).
- Sound: POKEY voices 1-3, tones that do not stop the game, a voice per kind
  as on the CPC.
- Timing: 3 TV frames (PAL), 4,3,4,3,4 on NTSC (60 ms on average).
- `--test recording.bnr` (`ATARI_TEST`): the recording compiled in and
  replayed from the title on, the state hash compared every game frame. At
  the end the background turns green (identical, no late frame), yellow
  (identical, some frames late) or red (a hash differed), and the status
  line shows, in units of 128 us (two scanlines, 60 ms = 469): late
  frames, average frame work, its flush, the longest frame. `--frames n`
  takes only the first n frames (500 frames do not fit beside the program
  any more; 400 do).
  `CFLAGS_ATARI=-DTEST_HASH_OFF` times a standalone game (no hash, nothing
  checked).

Correct: dm4, coop4, dm2, coop1, coop2s and dm2s replay with identical
hashes in atari800 (800XL, PAL); coop2-score does not fit in memory with the
program.

The CPU is slower than the 1.5 MHz assumed in steps 0-1: ANTIC's character
modes take 40 font bytes every scanline and the names on each row's first,
so about 1.1 MHz is left. The first build took 62 ms a frame (coop4). Three
changes brought it to 41:

- the flush no longer clears the playfield in a game: `composite_frame`
  overwrites rows 0-23 at the start of every game frame anyway (the
  contract in `core/platform.h` says so now; the Z80 machines can drop
  their clearing too);
- our own vertical blank instead of the OS's;
- sprite shapes rebuilt only when they change.

Frame work in atari800 (PAL), average (longest):

| | 4 players (coop4) | 2 players (dm2) |
|---|---|---|
| hash every 16 frames, as in a network match | 40.6 ms (52) | 37.2 ms (24) |
| standalone, no hash | 34.7 ms | 33.7 ms |

The flush is 14 ms of it. Late frames: only the round and stage starts
(map generation), as on the Z80 machines. About 20 ms of the 60 are left
for FujiNet in step 4. (The recordings that hash every frame, coop1, coop2s
and dm2s, take 75-90 ms: a test setting, not a game.)

## Step 4: the network over FujiNet (in progress)

`tools/build_atari.py --net` builds the network version: the software
network device and WebSocket client of the other machines
(`netdev_soft.c`, `ws.c`) over `c/platform/atari/tcp_fujinet.c`, FujiNet's
network device `N1:` (SIO device 71h: open `N:TCP://host:port/`, status,
read, write, close).

Two things had to change for it.

**Memory.** The network code, and the lobby it brings in, add about 18 KB.
The game now runs with the OS off, its ROM switched out, which makes the
RAM under it usable (`c/platform/atari/hiram.ld`):

| | |
|---|---|
| $2000-$B1FF | the program (network build: to about $AC10) |
| $B200-$B3FF | the C stack (from $B400 down) |
| $B400 | the screen |
| $B800 | sprites; their unused first $300 bytes hold the network device's variables |
| $C000-$CFFF | the four fonts |
| $D800-$FFF9 | the network device and WebSocket code (13 KB with the fonts) |

The `.xex` carries the code under the ROM as extra segments that load
into a buffer and are copied up by `atari_hicopy` through INITAD, so any
loader handles it. Nothing of the OS runs during the game:
`atari_nmi` takes the DLIs and the vertical blank, interrupts stay
masked, and the keyboard, BREAK and the serial port are polled (POKEY
latches their requests). The sprite table is compressed (13 shapes and
an index instead of 2 KB).

**SIO.** The OS's SIO routine is in the ROM, so `c/platform/atari/sio.s`
is our own, polled. What it took, found against FujiNet-PC:
- POKEY latches a request only if it is enabled when it happens: "output
  needed" is enabled before the first byte, and each one is acknowledged
  before the next byte is written;
- "output done" is not latched and was not reliable: the end of a send is
  the last "needed" plus a byte's time, measured with VCOUNT (the CPU's
  speed varies with ANTIC's DMA);
- receiving must be on within 850 us of the last byte sent (the device's
  earliest answer), and right after a command frame;
- STIMER after setting the baud divisor, AUDCTL kept: a stale timer
  garbled the first bits, and an emulated bus then told FujiNet another
  baud rate;
- tones on POKEY voices 1 and 2 only: 3 and 4 are the serial clock.

**Tested** in Altirra 4.40 (under wine) with FujiNet-PC (the firmware's PC
build) and the NetSIO hub (fujinet-emulator-bridge), against the local
relay: the Atari hosts a room through FujiNet (the WebSocket upgrade, the
JSON messages), a Spectrum (FuseX, Spectranet) joins it, both ready, and
the match runs; it lost the connection after 40 frames, because the
emulated chain is about ten times slower than real time (Altirra waits for
each NetSIO round trip). So this setup shows that the protocol is right;
it cannot show the real cost of a frame. A real FujiNet answers a command
within a few milliseconds.

**High speed.** `tcp_present` asks FujiNet for its high-speed index
(command '?', answered at 19,200 baud) and switches to it (the POKEY
divisor is the index; FujiNet's default 8 is about 57,600 baud). A failed
command is tried again, then at the other speed (FujiNet drops back to
19,200 after bad command frames and after a reset); an open renegotiates.
During SIO the vertical blank is short (the clock, the sprites, the
registers the DLIs change): at 57,600 baud a byte comes every 174 us, and
the full one let a byte be lost. Hosting and joining work at high speed in
the emulated chain.

**The cost.** The emulated chain cannot measure it (the Atari makes about
a frame a second there; a real-time Spectrum then gives up). From the
protocol (command frame, 'A', data, checksum, 'C'; FujiNet answers within
about a millisecond) and the byte time at 57,600 baud, a frame's SIO with
the JSON input lines, the CPU waiting throughout:

| | 2 players | 4 players |
|---|---|---|
| status (12 bytes on the bus) | 4.5 ms | 4.5 ms |
| read the others' input lines (60 / 164 bytes) | 13 ms | 31 ms |
| write ours (56 bytes) | 12 ms | 12 ms |
| total, against about 20 ms left of the frame | 30 ms | 48 ms |

So the JSON lines over SIO do not fit, not even for two. What was done:

- **Binary input lines** (`NET_BINARY_INPUT`, the relay's `bin=1`, see
  `docs/net-protocol.md`): our line out is 12 bytes with its WebSocket
  header, each other player's 9. The relay change is on staging only
  (mz-catalog, committed locally); the reference relay has it too, and a
  mixed room (the Atari binary, a Spectrum JSON) plays through it.
- One status command a frame, not two: after a read that took all that was
  waiting, the next `tcp_recv` answers "nothing" without asking.
- The command frame's t0 and t1 at their minimums by VCOUNT (about 0.8 ms
  each instead of 1.1).
- FujiNet's PROCEED line was looked at and left: FujiNet asserts it only
  5 ms after the last status and then toggles it every 100 ms, while in a
  match data waits every frame.

Robustness, found in long emulated matches: a status or the speed query
is repeated after any failure; a NAK (to the command or to our data frame)
always means "not done" and is repeated; a read or a write that failed
after it was accepted is not (it would lose or double bytes); data blocks
are received by a tight loop (about 45 cycles a byte: a byte comes every
174 us, and a DLI waiting for its scan line takes up to 60). With that, an
Atari (binary) and a Spectrum (JSON) played 840 frames through the local
relay with no failed command.

**The cost, measured.** `tools/build_atari.py --bench` builds
`c/platform/atari/sio_bench.c`: the game's screen with its DMA and DLIs,
and a network frame's SIO through `tcp_fujinet.c` and `sio.s` (our 12-byte
input line out, a status, a read), 64 times each for 2 and 4 players. In
Altirra with `tools/atari/fakefn.atdevice`, a FujiNet stand-in that answers
in emulated time with the firmware's own delays (250 us before 'A' and 'C',
1 ms from 'C' to a data frame at high speed, 850 us after our data frame),
at FujiNet's default high speed (index 8):

| SIO a network frame | 2 players | 4 players |
|---|---|---|
| gaps by the SIO spec (t0, t1 0.8 ms; t3 1.7 ms) | 27.1 ms | 30.5 ms |
| t0 by the spec, t1 and t3 short (the build) | 24.8 ms | 28.9 ms |

(the write alone 9.0 ms, a status alone 6.9 ms.) Three commands a frame
cost most of it: a status cannot be skipped (FujiNet answers a read with
nothing waiting with an error, and a short read with a short data frame),
and t0 must stay (FujiNet's idle loop empties its serial input until it sees
COMMAND). A command measures about 1.7 ms longer than its bytes and the
firmware's delays add up to; part of that may be how Altirra feeds a
device's bytes to POKEY.

**How long a message waits.** The C64's first drivers started a read only
when the game's poll found nothing and used its bytes a frame later, which
made a message a frame or two late each way (a lobby delay of 300 ms;
`docs/port-c64.md`). The Atari's driver asks FujiNet at the poll itself (a
status, then the read), so a message pending at FujiNet should be taken by
the next poll, within a frame. Checked with the benchmark's latency section
(the fake FujiNet's 'J' makes 9 bytes arrive; Altirra's device threads do
not sleep, so they arrive at once, which the benchmark uses: in even frames
before its poll, in odd frames after it): of 64 arrivals, 32 were taken in
their own frame, 32 at the next frame's poll, none later. The "drained"
shortcut never skipped a frame's status.

With the game's own work (37 ms a 2-player, 41 ms a 4-player network frame)
that is about 63 ms and 71 ms against 60: a match with an Atari would run 5
and 18 % slow (the Spectrum 48K in 4-player matches: 12-16 % late). The
way to fit: SIO in the background, by interrupts, while the game computes
(the CPU then pays about 50 cycles a byte, 3-4 ms a frame).

Test aids: `-DNETTEST_HOST` (the menu set to host 2 players, detection
3 s late for FujiNet-PC's restart), `-DNETTEST_TIME` (on the status line
over the scores: SIO time a frame, average and longest of 16, in 128 us
units; failed commands, and the last one's command and result). Memory:
the WebSocket client's variables and FujiNet's open buffer in the OS's
pages 4-6 ($0400-$06FF, free with the OS off).
