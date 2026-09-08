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

## Phase 1 - local 2 players on the keyboard, cooperative

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

## Phase 2 - deathmatch mode

1. Arena variant: optional enemies off, symmetric spawn corners, brick count
   parameter, round timer from `time_left`.
2. Win rule: last player alive, or most kills when time runs out; rounds and
   match score; rematch screen.
3. Scoring: kill = points to the bomb owner, self-kill = penalty.
4. Test with the host simulator using scripted inputs for 2 bots.

## Phase 3 - joysticks and 4 players

1. MZ-800 joystick ports F0h/F1h (`IN`): verify the bit layout on mz800emu
   (`input_send_joystick`) and document it; a plain MZ-700 has no joystick
   ports, so joysticks are an MZ-800 feature (also in MZ-700 mode).
2. Input sources JOY1/JOY2; menu assignment; auto-detect by "press fire".
3. Bomb table 5 -> 8, enemy spawn rules for 4 players, 4-player HUD.
4. Performance pass: profile a 4-player frame in the emulator; keep the
   frame under 915 ticks (candidates: sprite drawing in asm, HUD only when
   values change).

## Phase 4 - determinism harness (prerequisite for network)

1. `state_hash()` over players, bombs, enemies, map layer, RNG seed.
2. Input recording/replay in the host simulator; replay the same input file
   on the Z80 build in mz800emu and compare hashes per frame (read via
   `mem_read`). Any divergence is a bug to fix here, not later.
3. Rule: game logic never reads hardware directly; inputs enter only through
   the per-frame input vector, the seed only through the match setup.

## Phase 5 - MZPico network transport

1. Protocol spec (shared doc in this repo): new command family on ports
   0x40/0x41: `NET_STATUS`, `NET_CREATE_ROOM`, `NET_JOIN_ROOM(code)`,
   `NET_SEND(frame, input)`, `NET_POLL` -> inputs of all players up to frame N,
   `NET_LEAVE`; results follow `COMMAND_RESULT_*` like `mz-comm.h`.
2. Relay service on mzpico.com (extend the FastAPI cloud repo or a sibling
   service): rooms with 4-character codes, WebSocket per player, broadcast of
   per-frame inputs, seed distribution, timeouts. Stateless beyond a room.
3. Pico W firmware: WebSocket client for the relay, the NET command handlers,
   ring buffer of received inputs (needs the `USE_PICO_W` build).
4. Virtual MZPico device in the WASM emulator on mzpico.com: same ports and
   command handlers, forwarding to a browser WebSocket. Developed first,
   because it is the fastest place to test the protocol end to end.
5. Test: two browser emulators in one room exchanging inputs with a trivial
   echo program before the game touches it.

## Phase 6 - lockstep netcode in the game

1. Input delay of 2-3 frames (120-180 ms) so peers rarely stall; local input
   is queued for frame N+delay, remote inputs are awaited before frame N runs.
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
