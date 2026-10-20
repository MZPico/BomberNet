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
2. **The network**: the device's `tcp.h`, its cost per frame, lockstep and
   cross-play tests, the production relay.
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

VICE: built from source into `~/src/vice/dist` (`--enable-headlessui
--with-libcurl`; it needs `xa` and `dos2unix`). VICE 3.9 crashes when it
logs in colour to a pipe, so it runs with `+logcolorize`.
