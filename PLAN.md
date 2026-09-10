# Bomberman multiplayer plan

Target: cooperative and deathmatch modes, up to 4 players locally (2 on the
keyboard, 2 on joysticks), network play between physical MZ-800 + MZPico and
the online emulator on mzpico.com, and high scores on mzpico.com.

Base: the C port in `c/` (z88dk, same environment as the MZPico Manager).
Verification tools already in place: host simulator (`c/host/sim.c`) and
headless mz800emu driven by `tools/emu.py` (cycle benchmarks, screenshots,
memory pokes, profiler).

## Guiding decisions

- **Lockstep netcode on a deterministic simulation.** Every peer runs the same
  game code; only inputs (one byte per player per frame) and the map seed cross
  the network. This keeps traffic tiny, needs no state sync, and works the same
  for a physical MZ-800 and for the browser emulator. Cost: all peers must be
  bit-identical in behaviour, so determinism is verified with a state hash
  before any network code is written.
- **One MZ-side protocol for both worlds.** The game talks to "the MZPico" on
  ports 0x40/0x41 (as the Manager does). On real hardware that is the Pico W
  firmware; on mzpico.com it is a virtual MZPico device inside the WASM
  emulator. Both forward to the same relay service.
- **Frame budget.** A frame is 915 ticks (58.6 ms); logic uses ~44 ms today.
  Four players, more bombs and the network poll must fit in the remaining
  ~15 ms, otherwise `FRAME_TICKS` goes up for the multiplayer modes only.
- **Every phase ends playable and tested in the emulator.**

## Phase 0 - player abstraction (no visible change) - DONE 2026-09-08

1. `player_t` record: x, y, state, anim, score, lives, input source, colour,
   bombs allowed, alive flag. `players[4]`, `player_count`.
2. Input source enum: KBD_A (cursor + SPACE), KBD_B (second key set), JOY1,
   JOY2, NET, NONE. `input_read(source)` returns the 5-bit key mask.
3. Move all `player_x/y/state` uses to `players[i]`; bombs get an `owner`
   field; kill credit goes to the owner.
4. Per-player draw: logical codes for 4 colour variants. The unused walking
   rows 0xA0-0xBF (32 codes) hold players 2-4 (2 frames x 4 chars each);
   `game_table` gets their attributes. Death animation stays shared.
5. Regression: emulator benchmark and screenshots must match the current
   build; host simulator with 1 player unchanged.

Result: `players[4]` / `player_count`, `input.c` samples every active player's
source once per frame into `players[i].keys` (the logic never touches hardware),
bombs carry `owner`, enemies chase the nearest alive player, kill credit goes to
the owner of the closest exploding bomb, each player has its own death counter,
`generate_map` places all active players and keeps bricks away from each of them.
Players 2..4 use tiles A0h..ABh (patched into `game_table` by `extract_data.py`).
Verified: 212k cycles/frame (unchanged), identical screenshot, controlled kill
scenario in the emulator (bomb, walk away, enemy dies, score +12 to the owner,
`enemies_left` 0, stage cleared).

## Phase 1 - local 2 players on the keyboard, cooperative - DONE 2026-09-08

1. Second key set on the matrix, e.g. W/S/A/D + a fire key; `mz_keys` scans
   the needed rows once per frame and returns both masks. Check matrix
   ghosting with 3 simultaneous keys on the emulator's keyboard model and on
   real hardware; pick keys on different rows/columns to avoid it.
2. Rules for coop: shared stage, shared enemies, individual scores and lives,
   stage clears when enemies are gone, stage restarts when all players are
   dead. Player-player collision: none. Fire kills any player.
3. Title screen becomes a menu: mode, player count, input assignment; the
   original title stays as the attract screen.
4. HUD: row 24 shows scores/lives compactly for 2 players (4 later).
5. Test: two-player session in the emulator via `tools/emu.py` (press keys of
   both sets, verify positions and kill credit from memory).

Result: key set B = W/A/S/D + E (`mz_keys_b`, rows F2h/F4h of the matrix);
title menu line "PLAYERS < n >" (cursor LEFT/RIGHT, 1..2) and a key legend;
2-player HUD "P1 score lives  P2 score lives  Tnnnn enemies Sstage" with new HUD
letters at 92h..99h; finished players are hidden; new BOMBERNET logo composed
from the original glyphs by `tools/make_logo.py`. Verified in the emulator:
menu selects 2 players, P1 moves on cursor keys, P2 moves W/A/S/D and drops a
bomb with E (owner 1), VRAM rows checked as text (the emulator's PNG capture of
text-heavy screens is unreliable, so rows are dumped from D000h instead).

## Phase 2 - deathmatch mode - DONE 2026-09-08

1. Arena variant: optional enemies off, symmetric spawn corners, brick count
   parameter, round timer from `time_left`.
2. Win rule: last player alive, or most kills when time runs out; rounds and
   match score; rematch screen.
3. Scoring: kill = points to the bomb owner, self-kill = penalty.
4. Test with the host simulator using scripted inputs for 2 bots.

Result: MODE on the title toggles with cursor UP/DOWN (deathmatch forces 2
players). Arena = the normal map without monsters, bonus or exit; players start
in opposite corners. A kill (fire from another player's bomb, decided by the
closest exploding bomb) gives +100 on the score and a kill; self-kill costs 50.
A round ends when at most one player is left alive (after the death animation)
or when the timer runs out; winner = last one standing, else most kills, else
draw. Result message in the arena ("PLAYER n WINS THE ROUND / THE MATCH",
"DRAW"), fire continues; first to 3 rounds wins the match and the game returns
to the title. HUD shows round wins instead of lives. Letters W K V X were added
to the game-mode table (8Eh 8Fh 9Eh 9Fh) and `hud_text` prints ASCII messages.
Verified in the emulator with a scripted round (P1 bombs P2 in the corner, runs
clear): kill credited, P2 dies, message shown, wins=1, next round from corners.

## Phase 3 - joysticks and 4 players - DONE 2026-09-08 (MZ-1X03 untested on hardware)

1. MZ-800 joystick ports F0h/F1h (`IN`): verify the bit layout on mz800emu
   (`input_send_joystick`) and document it; a plain MZ-700 has no joystick
   ports, so joysticks are an MZ-800 feature (also in MZ-700 mode).
2. Input sources JOY1/JOY2; menu assignment; auto-detect by "press fire".
3. Bomb table 5 -> 8, enemy spawn rules for 4 players, 4-player HUD.
4. Performance pass: profile a 4-player frame in the emulator; keep the
   frame under 915 ticks (candidates: sprite drawing in asm, HUD only when
   values change).

Result: joystick type on the title (W/S cycle NONE / MZ 800 / MZ 1X03).
MZ-800/MZ-1500 digital sticks are read on ports F0h/F1h with 8255 port A bit
5/6 lowered for the read (`mz_joy800`). The MZ-700's MZ-1X03 analogue sticks
(E008h bits 1..4) are measured once per frame right after the VBLK falling
edge: 64 samples ~105 T apart, low count < 22 = left/up, > 46 = right/down,
switches sampled during display; with that type the frame limiter targets 880
ticks and then waits for the vblank edge, so frames are exactly 3 vblanks.
Every player row on the title picks its own input (keyboard A/B, joystick 1/2, no
duplicates; joysticks only with a type selected, max players 4 with joysticks, else 2); bomb slots 8; compact HUD for 3-4 players ("n dddd0<man>c", time
shown only with 3). Verified in mz800emu with `[JOY] joyN_type = NUM_KEYPAD`
in the private config: player 3 moves on the emulated stick and drops a bomb
(owner 2). Frame cost in 4-player deathmatch: 174k cycles average (49 ms) with
peaks to 262k during multiple explosions, against a 208k budget; average fits,
peaks stretch the frame - the blast code is the candidate for an asm pass.
The MZ-1X03 path cannot be exercised in the MZ-800 emulator build; it needs a
real MZ-700 (or the mz700 emulator build) to calibrate the thresholds.

## Phase 4 - determinism harness (prerequisite for network) - IN PROGRESS 2026-09-10

1. `state_hash()` over players, bombs, enemies, map layer, RNG seed.
2. Input recording/replay in the host simulator; replay the same input file
   on the Z80 build in mz800emu and compare hashes per frame (read via
   `mem_read`). Any divergence is a bug to fix here, not later.
3. Rule: game logic never reads hardware directly; inputs enter only through
   the per-frame input vector, the seed only through the match setup.

Implementation: `match_seed` seeds the RNG in `run_game` (timers and animation
phases reset there too); `input_poll` takes `replay_keys[4]` when `replay_active`
(this is also the future network path); `compute_state_hash` (16-bit rotate/xor/add
over players, bombs, enemies, map layer, RNG seed, time, frame_no and every logic
scalar) runs after `stage_start` and at the end of every `hash_period`-th frame.
Recording format `.bnr`: 16-byte header (BNR1, seed, mode, players, inputs[4],
hash period), then 6 bytes per game frame: keys[4] for the coming frame and the
hash as it stood at that flush. Host: `SIM_RECORD=f SIM_MODE= SIM_PLAYERS= SIM_SEED=`
records with the bot, `SIM_REPLAY=f` replays and compares. Z80: `tools/replay.py f`
pokes the menu and seed, feeds the keys at every flush and compares the hash.
Host replay: coop 3994 frames and deathmatch 2996 frames, 0 mismatches.

## Phase 5 - MZPico network transport (reworked 2026-09-10 for the Unicard protocol)

References: the Unicard MZF repository protocol,
https://www.sharpwiki.cz/doku.php?id=en:unicard:z15mzfrepo (ports 0x50/0x51,
0x52/0x53 reserved and unused; commands documented up to 0x72 USARTBPS; the
uc3 socket commands 0x80-0x89 appear only in mz800emu's `unimgr_commands.h`),
so 0xA0-0xA7 collides with nothing documented.

Context: the firmware replaced `pico_mgr` (ports 0x40-0x44) with a Unicard-
compatible device on ports 0x50 (command/status) and 0x51 (data), see
`~/src/MZPico-firmware/docs/unicard-migration-plan.md`: one command byte,
parameters streamed on the data port (strings end at a byte < 0x20), a 4-byte
status record (BUSY, CMD_OUTPUT, ..., bit 6 IN_PROGRESS for core-0 work, bit 7
ERROR), MZPico vendor commands in 0x90-0xEF (0x90-0x9A taken: LISTVOL,
GETCONFIG, WIFISTATUS, INFO, SETSORT, SERVEDSUM, MOUNTS, SETCONFIG, COPY).
mz800emu carries the reference Unicard emulation (`hw-generic/unicard/unimgr.c`)
and its WASM build is what runs on mzpico.com, so "the virtual MZPico" is that
emulation plus the same vendor commands, not a separate device. The Unicard's
own uc3 socket commands (TCPOPEN.. 0x80-0x89) are not used: they are generic
sockets, unimplemented on both sides, and would push framing onto the Z80.

Decisions:
- The game talks to the device with the manager's existing Z80 client
  (`external/manager/mz-comm.c`: `uc_cmd`, `uc_wr`, `uc_rd`, `uc_status4`,
  `uc_read`, `uc_write`), copied into `c/` like `console.c`. Detection = REVD
  with subtype 'M' (0x4D), then INFO feature bit NET; no device or no NET bit
  = the NETWORK menu row reads NONE and everything else works offline.
- Room/lockstep logic lives in the device (firmware core 0 / emulator glue);
  the Z80 only sends its inputs and asks for the input vector of a frame.
- All NET commands except CREATE/JOIN are answered from core-1-side ring
  buffers that core 0 fills and drains, so they never set IN_PROGRESS and cost
  about 16 EXWAIT port accesses per frame (< 0.1 ms of the 58 ms frame).
  CREATE/JOIN wait for the relay and use the existing IN_PROGRESS rule.

Vendor commands 0xA0-0xA7 (input -> output; WORD little-endian):

| code | name | in -> out | purpose |
|---|---|---|---|
| 0xA0 | NETSTATUS | - -> 6 bytes: state (0 no link, 1 ready, 2 in room, 3 running, 4 desync, 5 dropped), slot, players in room, ready mask, rtt/10 ms, frames buffered | polled on the title and once per game frame |
| 0xA1 | NETCREATE | build WORD, mode, players -> room code (4 chars, 0x0D), slot | async (IN_PROGRESS) |
| 0xA2 | NETJOIN | string code, build WORD -> slot, mode, players, seed WORD | async; build mismatch -> ERROR code 6 |
| 0xA3 | NETLEAVE | - -> - | |
| 0xA4 | NETREADY | 1 byte -> start frame WORD (0xFFFF while waiting) | all ready -> relay fixes seed and start |
| 0xA5 | NETSEND | frame WORD, keys byte -> - | local player's input for frame N+delay |
| 0xA6 | NETPOLL | frame WORD -> avail WORD, keys[4] | inputs of all slots for that frame; avail < frame means wait |
| 0xA7 | NETHASH | frame WORD, hash WORD -> - | relay compares; mismatch -> state 4 |

Relay (mzpico.com, beside the cloud repo service, FastAPI WebSocket): rooms
with 4-character codes, one socket per device, JSON frames {create, join,
ready, input, hash, leave}; broadcasts inputs per frame, chooses the seed,
enforces one build id per room, drops a room after 60 s of silence.
Stateless beyond a room. The browser build connects directly with a
WebSocket; the Pico W with lwIP + a minimal WebSocket client.

Steps (each ends tested):
1. Protocol doc in this repo (`docs/net-protocol.md`) and PR to the firmware
   plan; agree the command codes before any code.
2. Z80 client: copy `mz-comm.c`, add `net.c` (detect, NETSTATUS, the seven
   commands), NETWORK row on the title (NONE / MZPICO ready / room code).
   Test: stub device in the host simulator and in the mz800emu Unicard
   emulation answering REVD/INFO/NETSTATUS.
3. Relay service with a Python test client; two clients exchange inputs.
4. mz800emu: vendor commands in `unimgr.c` behind a transport callback -
   native build uses a TCP/WebSocket socket to the relay (lets the phase 4
   harness drive two emulator instances through a local relay), WASM build
   uses a JS WebSocket. This is done before the firmware because it is the
   fastest end-to-end path and the mzpico.com deliverable.
5. Pico W firmware: `unicard.cpp` handlers, core-0 WebSocket client, ring
   buffers; needs the `USE_PICO_W` build and a Deluxe W board on the bench.
6. Test: an echo program exchanging inputs between two browser emulators,
   then browser + physical MZ-800.

## Phase 6 - lockstep netcode in the game

1. Input delay of 2-3 frames (120-180 ms) so peers rarely stall; local input
   is queued for frame N+delay (NETSEND), remote inputs are awaited before
   frame N runs (NETPOLL until avail >= N, then replay_keys[] = keys[4] and
   the phase-4 input path does the rest); NETHASH every hash_period frames.
2. Match setup screen: create/join room, show code, ready state, seed from
   the relay; disconnect and timeout handling (pause, then forfeit).
3. Mixed sessions: physical MZ-800 with MZPico and mzpico.com browser in the
   same room; local players on one machine plus remote players.
4. Test: hash comparison across two emulator instances driven by
   `tools/emu.py` through the relay.

## Phase 7 - high scores on mzpico.com

1. Endpoint `POST /scores` (name, score, mode, players, build id) and
   `GET /scores?mode=` in the cloud service; simple abuse limits.
2. `HISCORE_SUBMIT` / `HISCORE_LIST` commands through the MZPico (firmware and
   virtual device share the REST call).
3. Name entry screen at game over; top-10 table on the title screen when a
   MZPico is present; offline build keeps the local hi-score.

## Phase 8 - polish

Pause, sound per player event, rematch flow, joystick autodetect, attract
demo with recorded inputs (free with the replay system), README and protocol
doc updates.

## Risks and open points

- **Determinism across builds:** every peer must run the same version; the
  build id is exchanged at room join and mismatches are refused.
- **Keyboard ghosting:** 4 keys held on a matrix without diodes can register a
  phantom fifth key; key-set choice must be tested on real hardware.
- **Hardware needed for physical network play:** Pico W based board and the
  WiFi firmware build.
- **Frame budget:** more players and bombs plus the network poll; measured per
  phase in the emulator, with `FRAME_TICKS` as the safety valve.
- **Latency:** the relay adds one hop; lockstep with 2-3 frames of input delay
  tolerates roughly 150 ms round trip; beyond that the game visibly stalls.
