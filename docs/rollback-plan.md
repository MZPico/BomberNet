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
leaves 0. The PC's `/ROLLBACK=OFF` can pin it for comparisons.

Memory: one snapshot of the simulation state, about 1.5 KB (the map's 1000
cells, the players, enemies, bombs, timers, the random seed, the stage's
variables, the hash accumulators), plus nothing per predicted frame: they
are re-simulated from the snapshot each step. The state hash's own list of
what it covers (`hash_records`, `hash_scalars`, `map_layer`) is the list of
what the snapshot holds; a variable left out shows up as a desync in the
cross-play tests, never silently.

## In the code

Common code, compiled where `plat_config.h` sets `PLAT_RUNAHEAD`; the
other builds unchanged, byte for byte on the Spectrum.

- `core/bomber.c`: `frame()` split into the key dealing and `frame_logic()`,
  which the run-ahead calls several times a step. The picture: the drawing
  calls inside the logic draw into `draw_buf` as now; only the last
  simulated frame's drawing is kept (composite_frame before each
  simulated frame, as a frame does now).
- `core/snapshot.c`: `snap_save()`, `snap_restore()` over the listed
  variables, and `sim_quiet`, set during re-simulation so `plat_tone` stays
  silent (a tone wrapper in the core; tones sound only for the newest
  frame).
- `core/netplay.c`: next to `net_lockstep_poll`, the run-ahead step: send
  own keys for A + d; take every complete frame the device has (the device
  keeps WIN = 16 frames, enough for d <= 8); re-simulate and snapshot;
  predict and simulate to A + L; the lead's rule above; the in-match delay
  figures (margins, waits) measured on the authoritative frames as now.
- The hash: `hash_frame_step` runs in the authoritative pass only, and its
  accumulator is in the snapshot.
- `frame_no`, the timers and the random seed are in the snapshot; the
  recordings' replays do not use the run-ahead (no network), so every
  replay stays identical.

Tests: the cross-play tool with a run-ahead machine against a lockstep one
(`web` or `pc` against `mz` or `zx`): hashes and fields identical, as the
authoritative states are the lockstep's by construction. The host
simulator with two processes through the reference relay with an injected
delay (a `RELAY_DELAY` for the test) exercises the predictions and
corrections deterministically; a screenshot test on the browser shows a
remote turn's correction. About two to three days with the tests.

## Not in this plan

- Predicting fire, or smoothing corrections: a cell is the game's unit, so
  a correction is a jump of one cell at most; nothing to smooth.
- The Z80 and 6502 machines: no time for a second logic step, and the
  Spectrum no memory for a snapshot.
- Any change for the lockstep machines or the relay.
