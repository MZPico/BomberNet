# Network timing: measurements and the two fixes for 0.2.0

Played on FuseX against the mzpico.com play page, the game felt sluggish.
This page records how the network timing was measured, what was found, and
what changed so that 0.2.0 (on the play page and on local machines) plays
smoothly.

## How a match paces itself

- Lockstep: every frame each machine sends its keys for frame F + d and waits
  for everybody's keys of frame F. d is the input delay: a key acts d frames
  (d x 60 ms) after it is pressed.
- The host chooses d in the lobby: it pings every 10 lobby frames, the
  joiners echo, and the round trip counted in lobby frames sets d.
- In a match both directions share 2d frames: each side's keys have d frames
  to reach the other. When they arrive late the receiving machine waits: a
  hitch.

## Method

`tools/netbench.py` runs two machines in real time and times every message:

- mz800emu with its MZPico card, paced at real speed (`MZ800EMU_REALTIME=1`,
  a local patch: headless it has no audio clock to pace it);
- FuseX with the real Spectranet firmware (see docs/port-zx-spectrum.md);
- the play page in Chromium (`tools/web_join.mjs`).

Against the local relay the relay logs every message (`RELAY_TIMING=<file>`).
Through the production relay each local machine talks through its own
logging proxy (`tools/netproxy.py`). Measured:

- the host's lobby round trip, and the delay it chose;
- in the game, each machine's margin: how long before it needed the other's
  keys for frame F they arrived. Negative means it waited.
- the game's pace, frames per second (16.7 is every frame on time).

The windows leave out the moments when the driver presses keys on FuseX (it
stops the machine at its keyboard routine for that).

## Findings

1. **0.1.1 under-measured the link.** Its lobby frame took 179 ms on the MZ
   (slow text drawing), so a round trip counted about one frame, and 0.1.1
   chose the minimum delay of 2 almost regardless of the link. After step 6
   of the Spectrum port the lobby frame is 58 ms. The same link now counts
   2 to 4 frames, and the old rule, ceil(rtt / 2) + 1, chose 3 (180 ms).
   Through the production relay the margins at delay 3 were 100 to 190 ms:
   one frame more than needed.
2. **The MZ is 2.4 % faster than the Spectrum.** The MZ keeps the original
   game's frame, 58.6 ms; the Spectrum's frame is three PAL TV frames, 59.9 ms.
   In lockstep the faster machine catches up with the slower one within
   seconds and then plays with no margin, so every late packet stalls it.
   MZ against Spectrum, the MZ waited in 12 to 23 % of the frames while the
   Spectrum had 140 to 280 ms to spare.
3. The Spectrum side is not slow: a Spectrum joiner answers a ping within 51 to
   57 ms (median) on the local relay, and ZX against ZX waits in about 1 % of
   the frames. Early FuseX figures of 4 to 6 frames of delay came from the
   test driver's key presses.

## The fixes (0.2.0)

- The delay is ceil(rtt / 2), the longest of the last four round trips, at
  least 2 (`lobby_apply_rtt` in `core/bomber.c`). A lobby sample already
  includes up to a frame of polling at either end; that is the margin.
- In a network match the MZ paces at the Spectrum's frame, 935 ticks of its
  8253 counter (59.9 ms, `FRAME_TICKS_NET` in `platform/mz/plat_mz.c`).
  Local play keeps the original 58.6 ms.

## Results through the production relay

Round trip: the host's lobby ping, median. TCP connect from the test machine to
the relay: 32 to 54 ms.

| Pairing | Delay before | Delay after | Round trip | Waiting after |
|---|---|---|---|---|
| MZ - MZ | 3, 3, 2 | 2, 2, 2 | 86 to 92 ms | host never; joiner mostly under 13 ms, one of 27 to 57 ms per 20 s |
| MZ host - Spectrum | 3, 3, 3 | 2, 2, 2 | 90 to 117 ms | MZ waits median 7 to 37 ms; Spectrum practically never |
| Spectrum host - MZ | 2, 2, 2 | 2, 2, 2 | 94 to 111 ms | see below |
| Spectrum - Spectrum | 2, 3, 2 | 3, 2, 2 | 98 to 125 ms | 1.4 % of the frames |
| 0.1.1 - play page (today) | 2, 2, 2 | | 169 ms | pace set by the test browser (software rendering), not the network |

In the game every machine held the full pace (16.3 to 16.7 frames per second)
whenever its emulator ran at real speed. FuseX as a host ran at 92 to 97 % of
real time on the test machine; the MZ beside it then waits for the slower
emulator. That is the emulator: a real Spectrum and a real MZ run at their
nominal clocks, and with equal frames neither falls behind the other.

On the local relay (no internet) every pairing chooses the minimum delay, 2.

## Not changed

- The minimum delay stays 2 (120 ms), as in 0.1.1.
- Mixed versions still play (the delay is announced by the host), but 0.1.1
  measures in its slow lobby frames: 0.2.0 should replace the play page's
  0.1.1 when it is released.
