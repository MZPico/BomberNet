# Run-ahead on the fast machines: a plan

The lockstep gives every key the room's input delay, d frames of 60 ms
(docs/net-timing.md): through the production relay d is 2 for most, so a
player's own character answers 120 ms late on top of the local chain. The
machines with time and memory to spare can hide that for their own player
without any change to the protocol or to the other machines: they show a
frame d steps ahead of the agreed one, exact for their own keys (already
sent) and guessed for the others', and correct the guess when the real keys
arrive. This is rollback as GGPO does it, with the lockstep as the
authority.

## The idea

Call A the authoritative frame: the last one for which every machine's keys
are known. A lockstep machine simulates A and shows it. A run-ahead machine
keeps a snapshot of the state at A and shows frame D = A + L, L the lead:

- its own keys for frames A+1 .. A+d are known, because the lockstep already
  sends them d frames ahead: a key read at step n is sent for frame n + d;
- the others' keys for those frames are predicted: the same direction as
  their last known frame, fire never (a bomb is never guessed; a real one
  appears when its frame becomes known, with its fuse that much shorter).

Every step: read the local keys and send them for frame A + d, as now. Poll
the device: for every authoritative frame that arrived, restore the
snapshot, simulate the new frames with the true keys (hashes at hash frames
come from these states), snapshot again. Then simulate A+1 .. D with the
predicted keys and draw D. The lockstep partners never see a difference:
the keys they get are the same, at the same time, and the hashes compare
the same states.

With L = d the player's own key acts in the frame simulated at the step it
is read: no added delay at all; what remains is the local chain (sampling,
the move rhythm, the picture: 75 to 100 ms on a fast machine). With L < d
its keys act d - L frames after being read, down to plain lockstep at L =
0: the lead can shrink when the machine is short of time, with nothing to
negotiate. The lead never exceeds d, and a machine whose authoritative
frame falls more than d behind waits, as the lockstep does.

## What the player sees

- Their own character: as standalone.
- The others: as late as today (their keys travel the same way), and a
  guess corrected when wrong: a turn shows up to d frames late, at most one
  cell of a jump (players step every second frame); a bomb pops in with up
  to d x 60 ms of its fuse gone (fuses are seconds); nothing else is
  guessed, enemies and explosions follow from known state. A remote player
  who stops is predicted still walking for d frames and steps back.
- Fairness: the run-ahead player reacts d frames sooner than a lockstep
  opponent, who sees the same game later. Both see the same game.

## The cost, and which machines

A step costs up to d + 1 logic steps (the re-simulation from the snapshot
and the new frame) and one flush; d is 1 or 2 on most links (the delay
adjustment keeps it low). The logic alone, dm4, from the frame-work
measurements in docs/port-*.md (the flush taken out):

| Machine | Logic a frame | Frame left after the flush and HUD | Run-ahead at d = 2 |
|---|---|---|---|
| browser | under 1 ms | about 58 ms | yes |
| PC, 286 at 12 MHz (VGA) | 2 ms | 56 ms | yes |
| PC, 286 at 8 MHz | 5 ms | 50 ms | yes |
| PC, XT 4.77 MHz (CGA flush 32 ms) | 21 ms | about 25 ms | no: 3 x 21 ms |
| Atari ST | 5 ms | 54 ms | yes |
| Amiga A1200 | 3 ms | 55 ms | yes |
| Amiga A500 (flush 20 ms) | 7 ms | 38 ms | yes (21 ms) |
| MZ-800, Spectrum, CPC, C64, Atari XL | 10 to 29 ms | 7 to 25 ms | no, plain lockstep |

So the run-ahead is for the browser, the PC from a 286 up, the Amiga and
the ST, and the PC must decide at run time: an XT stays in lockstep. The
rule is the same everywhere and needs no detection: the lead is allowed
while the step's work fits. At the match's start the lead is 0; each step
whose work (measured by the platform's clock, as the test builds do) leaves
room for one more logic step raises it by one, up to d; a step that ran
late lowers it by one. A 286 reaches d within a few frames; an XT never
leaves 0. The PC's `/NOROLLBACK` pins it at 0 for comparisons, the page's
`?runahead=0`.

Memory: one snapshot of the simulation state, about 1.5 KB (the map's 1000
cells, the players, enemies, bombs, timers, the random seed, the stage's
variables, the hash accumulators), plus nothing per predicted frame: they
are re-simulated from the snapshot each step. The state hash's own list of
what it covers (`hash_records`, `hash_scalars`, `map_layer`) is the list of
what the snapshot holds; a variable left out shows up as a desync in the
cross-play tests, never silently.

## In the code

Common code, compiled where `plat_config.h` sets `PLAT_RUNAHEAD` (the
browser, the PC, the Amiga, the ST); the other builds have the same
functions in the same order, 18 bytes more on the Spectrum.

- `core/game.c`: `tick_timers` ticks the timers only; `present_frame` is
  the flush, the colours and the wait. The snapshot (`snap_save`,
  `snap_restore`, `snap_map`) copies the listed variables: the players,
  enemies, bombs, the four timers, the random seed, the hash accumulators,
  the stage's and the items' variables, `frame_no`, the map.
- `core/bomber.c`: a frame is `present_frame`, then either the run-ahead
  step or `sim_frame_begin` (timers, status line, map into the draw
  buffer), the keys and `frame_logic`; `frame_logic(guessed)` neither
  counts nor hashes a guessed frame; `frame_ends` is what the stage and
  round loops look at.
- `core/netplay.c`: `net_runahead_step` beside `net_lockstep_poll`: our
  keys out for `net_frame + net_delay` as the lockstep sends them, the
  live state back to the snapshot when it is a guess, the agreed frames
  that arrived up to the one to show (quiet but the last, the delay's
  figures noted as the lockstep notes them), a wait as the lockstep's
  when the lead is used up, then the guessed frames from a fresh snapshot:
  our own keys as sent (kept by frame in `own_keys`), the others' last
  known direction, no fire. A guessed frame that ends the round is
  dropped: the agreed state back, the previous picture kept. The lead:
  `plat_frame_idle` (the ms the last sync waited; 0 when late) raises it
  by one above `RUNAHEAD_IDLE_MIN` (12 ms) and lowers it after a late
  frame with `RUNAHEAD_HOLD` (32) frames before the next try; never above
  the delay nor `net_runahead_cap`.
- `TONE` in the game's code is `plat_tone` unless `sim_quiet` (a frame
  simulated again, or guessed and not shown); the drop pose's blink is not
  set in a quiet frame.
- The platforms set `plat_frame_idle` in `plat_frame_sync`: the PC from
  its millisecond ticks, the browser from its clock, the Amiga from the E
  clock, the ST from its 38.4 kHz timer.
- Switches: the PC's `/NOROLLBACK`, the page's `?runahead=0` (the tests
  run both ways: `RUNAHEAD=0` for `tools/web_match.py` and
  `tools/crossplay.py`); the browser's test state shows the lead.
- The replays do not use the run-ahead (no network): every recording
  replays identically. The browser's trace of hashes and maps for the
  tests reports the agreed frame (`snap_map`).

## Not in this plan

- Predicting fire, or smoothing corrections: a cell is the game's unit, so
  a correction is a jump of one cell at most; nothing to smooth.
- The Z80 and 6502 machines: no time for a second logic step, and the
  Spectrum no memory for a snapshot.
- Any change for the lockstep machines or the relay.
