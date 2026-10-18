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
  least 2 (`lobby_apply_rtt` in `core/bomber.c`; the minimum is 1 since
  the next section). A lobby sample already
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

## A delay of 1 (after 0.2.0)

PR #2 (the ESP-01 Spectrum port) sent its own keys with a delay of 1 in rooms
of delay 2, so its player saw their moves 60 ms sooner than everybody else.
That is consistent everywhere, since each key carries its frame number: which
delay a machine uses for its own keys is its own business. What a pair of
machines needs is that their two delays together cover the round trip.

Measured on the production relay, ZX against ZX (both on the test machine,
50 ms from the relay), with a test build in which a machine can lead:

| Delays (host / joiner) | Frames | Waited | Lost per frame | Waits over 20 ms | Frames over 90 ms | Margin, lowest 10 % |
|---|---|---|---|---|---|---|
| 2 / 2 (0.2.0) | 2002 | 3.8 % | 0.9 ms | 0.7 % | 2.9 % | 74 ms |
| 2 / 1 (PR #2) | 2746 | 6.2 % | 1.0 ms | | 2.7 % | 64 ms |
| 1 / 1 | 1392 | 8.4 % | 1.1 ms | | 2.4 % | 16 ms |

A lead takes its 60 ms from the pair's common slack, and the waiting moves
mostly to the leading machine (7.6 to 15.9 % of its frames). On the local
relay 1 / 1 played like 2 / 2 (waits 3.9 and 0.3 % against 3.5 and 0.9 %,
from the emulators' different speeds).

So the minimum delay is now 1 for everybody, chosen by the same rule: a
round trip of at most 2 lobby frames gives 1. Results with the new rule:

| Pairing | Lobby round trip (median, p90) | Delay chosen | Waits | Waits over 20 ms | Frames over 90 ms |
|---|---|---|---|---|---|
| ZX - ZX, local relay | 48, 87 ms (joiner) | 1 | | | 4.2 to 5.6 % (as at delay 2: emulator speeds) |
| ZX - ZX, production | 148, 167 ms | 1 | 11.8 % | 2.0 % | 2.5 % |
| MZ host - ZX, production | 152, 196 ms | 2 | | | |
| ZX host - MZ, production | 109, 138 ms | 2 | | | |

Through the production relay from here a room gets 1 or 2 depending on the
moment's round trip; farther players get 2 or more, as before. A 0.2.0
joiner raises an announced 1 to its own minimum of 2 for its keys and still
plays in step (`B_BUILD=build/v020/build/zx tools/lockstep_zx.py`: delays
1 and 2, 0 mismatches). BUILD_ID is unchanged.

## Sending the keys earlier (measured, not done)

PR #2 also sends its keys right after the frame starts, before the screen
work. During replays (dm2, dm4, coop4, 400 frames each) that work, from
`flush_screen` to `input_poll`, takes a median of 33 to 37 ms on the
Spectrum, 30 to 32 ms on the CPC and 32 to 33 ms on the MZ (p90 43 to 50 ms).
So the others would get about 32 ms more margin. But the keys are read where
they are sent: read 32 ms earlier, they also act 32 ms later, so at the same
delay the game only feels slower. It pays only as an in-between step, delay
1 with the early send (about 92 ms of lag) for links too slow for delay 1
(60 ms) that now get 2 (120 ms). Not worth a second timing mode for now.

## Not changed

- Mixed versions still play (the delay is announced by the host), but 0.1.1
  measures in its slow lobby frames. 0.2.0 replaced 0.1.1 on the play page
  on 29 September 2026.
