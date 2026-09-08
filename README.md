# BomberNet — Bomberman for Sharp MZ-700/800 (disassembly, C port, multiplayer)

Sharp MZ-700 machine-code tape image (attribute 01, name ` F1200`), Z80,
load and exec address `1200h`, body `2010h` bytes (`1200h`–`320Fh`).
A Hudson Soft Bomberman variant (copyright line "(C) 1983 HUDSON SOFT INC").

Files:

| file | purpose |
|---|---|
| `bomber.mzf` | original tape image (untouched) |
| `bomber.asm` | complete, commented source; reassembles byte-identical |
| `build.sh` | `pasmo` assemble + MZF header wrap + compare with the original |
| `build/` | output of `build.sh` |

`./build.sh` prints `identical to original bomber.mzf` when the round trip is exact.

## Memory map

| range | what |
|---|---|
| `0000h`–`0FFFh` | monitor ROM. Used: `GETKY` 001Bh (key or 0, no wait), `MSTA` 0044h (start tone), `MSTP` 0047h (stop tone) |
| `11A1h` | `RATIO`, monitor work byte pair: tone divider read by `MSTA` |
| `1200h`–`134Dh` | entry, new game, stage start, main loop, stage clear / time bonus / death handling |
| `134Eh` | `stage_table`: 5 × (enemy count, enemy behaviour period) |
| `1358h`–`1550h` | game over, helper frames, title screen |
| `1551h`–`1777h` | title logo (6×40 codes, FF terminated) and title strings (0 terminated) |
| `1778h`–`1E08h` | game logic: time, pickups, spawns, enemies, bombs, blast |
| `1E0Dh`–`20FFh` | player, HUD, walls |
| `2100h`–`2112h` | HUD strings |
| `2113h`–`21DEh` | random, map generation |
| `21DFh`–`21FEh` | bonus/exit position variables, `reserved_cells`, 9 bytes of dead code |
| `21FFh`–`228Bh` | random generator, number/string printing, buffer address helpers |
| `228Ch`–`2673h` | `map_layer` 40×25 |
| `2674h`–`274Eh` | composite, map_addr, delay, beep, flush_screen, put_vram_char, clear_buffers |
| `274Fh`–`2B4Eh` | `draw_buffer` 40×25 (+24 pad) |
| `2B4Fh`–`2F4Eh` | `shadow_vram` 40×25 (+24 pad) |
| `2F4Fh`–`314Eh` | `game_table` 256 × (display code, attribute) |
| `314Fh`–`3202h` | `title_table` 90 × (display code, attribute) |
| `3203h`–`320Fh` | 13 unused zero bytes |
| `D000h` / `D800h` | VRAM characters / attributes (written only by `put_vram_char`) |

Stack grows down from `1200h`. Interrupts are disabled at entry and never re-enabled.
Everything in the file after the code (`map_layer`, `draw_buffer`, `shadow_vram`,
the variables at `26C5h`) is a snapshot of RAM at the moment the tape was written
(the shadow buffer still holds the title screen, the player variables show a
finished death). They can be replaced by `defs` if the snapshot is not wanted.

## Rendering model

Every cell holds a *logical code*, not an MZ display code. Two 40×25 layers exist:

- `map_layer` (`228Ch`): persistent stage content: bricks `80h`–`87h`, bombs, fire,
  exploded remains. The outer wall and pillars are **not** here.
- `draw_buffer` (`274Fh`): rebuilt every frame. `flush_screen` compares it with
  `shadow_vram`, writes only changed cells to VRAM through the translation table,
  and clears the draw buffer to spaces while doing so.

Frame composition order (see `main_loop`): HUD text → walls and pillars → HUD icons →
`composite_map` (non-space map cells over the draw buffer) → bombs/fire → bonus → exit →
enemies → player. Because each drawer reads the cell it is about to overwrite,
collision is detected by inspection of the draw buffer at draw time:

| reader | condition | effect |
|---|---|---|
| `put_enemy_char` | cell ≥ `E0h` (fire) | enemy state 2 (dying) |
| `put_player_char` | cell ≥ `C0h` (enemy or fire) | player state 6 (dying) |
| `put_bomb_char` | cell ≥ `E0h` | chain reaction: state 4, timer 6 |
| `put_bonus_char` / `put_exit_char` | cell ≥ `E0h` | item destroyed, 4 enemies spawn there once |
| `move_player` | any of the 2×2 ahead is `80h`, `88h`, `89h` | move refused |
| `enemy_probe` | either char ahead ≥ `80h` in either layer | move refused |

Coordinates are always `B` = row (Y), `C` = column (X). `draw_addr` and `map_addr`
turn (B,C) into a buffer pointer in BC. Sprites are 2×2 chars; `put_tile` writes
`A, A+1 / A+16, A+17`, i.e. tiles live on a 16-wide sheet.

`put_vram_char` contains the self-modified word `mode_patch` (`2712h`):
`00 00` (title) lets codes `< 5Ah` go through `title_table` (letters, box drawing,
logo blocks); `18 09` (game) forces `game_table` for everything.

### Logical code map (game_table)

| codes | meaning |
|---|---|
| `00`–`09` | digits 0–9 (numbers are written as raw digit codes) |
| `0A 0B 1A 1B` | BONUS tile |
| `0E 0F 1E 1F` | EXIT tile |
| `10`–`19` | HUD glyphs S C O R E B O N U S; `30 31` = T G |
| `20` | space; `21` = ":" |
| `22`–`3F` | enemy death animation frames (2×2, `state*2+1Eh`) |
| `40`–`5F` | player death animation frames (`4Eh - (state-6)*2`) |
| `60 64 68 6C` | bomb ticking frames (+ anim offset 0/2) |
| `80`–`87` | brick, burning stages; reaches `88h` → removed |
| `88` | outer wall (indestructible), `89` pillar |
| `8A`, `8C` | player standing frames; `90` lives icon, `91` enemy icon |
| `A0`–`AF` | player walking frames per direction (never selected, see quirks) |
| `C0`–`DF` | enemies: `type*4 + C0h + anim` |
| `E0 E1 F0 F1` | explosion centre; `E2`–`EE` explosion arms per phase |

## Execution flow

```
start (1200)
  di, sp=1200, clear_buffers, score=hi_score=0
  └─ title_screen (13A3)
       mode_patch = nop nop; draw logo/legend/demo sprites every frame
       GETKY == SPACE ─► new_game (1213): score=0, stage=1, lives=3
            └─ stage_start (1223)
                 time=1000, clear flags, load_stage_params, clear_enemies,
                 spawn_enemies, clear_bombs, clear_buffers, clear_map,
                 draw_walls, generate_map, composite_map, mode_patch = jr +9
                 └─ main_loop (1275)  ── one frame ──
                      tick_timers (also flush_screen: previous frame → VRAM)
                      draw_hud, draw_walls, draw_hud_icons, composite_map
                      update_bombs, draw_bombs, place_bomb, player_anim
                      enemy_ai, draw_bonus, draw_exit, draw_enemies
                      reveal_bonus, reveal_exit, draw_player
                      spawn_from_hit, check_pickups, time_tick
                      life_lost      ─► player_dead (1322): lives--, 0 ► game_over ► title
                      exit_touched   ─► exit_taken (1312): 5 frames, same stage again
                      stage_cleared  ─► 20 frames, time_bonus_loop, next_stage ► stage_start
                      else loop
```

Timing is purely frame based (busy loop, no interrupts). `tmr_*` pairs at `1ECBh`
are `[counter, period]` advanced once per frame by `tick_timers`.

## Data structures

**enemy_table** (`1BDEh`), 7 bytes each, `FFh` terminated, 8 slots (4 used at start,
up to 4 more after an explosion hits the bonus/exit):

| off | field |
|---|---|
| 0 | state: 0 free, 1 alive, 2..9 dying animation |
| 1,2 | X, Y (screen chars) |
| 3 | type 0..3: sprite colour and behaviour; decremented each countdown expiry, wraps 3 |
| 4 | countdown, reloaded from `enemy_period` |
| 5 | direction 0 left, 1 right, 2 up, 3 down |
| 6 | unused |

Type 0 moves every frame straight toward the player (`.ea_chase`). Types 1..3 move
only every 4th enemy tick: they chase when the countdown's low nibble is 0, pick a
random direction every 4th count, otherwise keep going and re-roll when blocked.
Kill value: `type*4+2 + rand(1..4)` points (displayed ×10).

**bomb_table** (`1C7Fh`), 4 bytes each, 5 slots, `FFh` terminated:
state 0 free, 1..4 ticking (7 frames each), 5..12 explosion phases (one per frame),
13 erase, then free; X, Y; timer.

**blast_pattern** (`1D02h`): four arms (left, right, up, down) × 8 `(dY,dX)` pairs, first
the 4 chars of the top row then the 4 of the bottom row. Reach is 2 cells. A solid
wall on the first char skips the row, a brick after char 1 or 3 stops the arm there.
Bricks burn one stage per explosion frame (`80h → 87h → removed`).

**generate_map** (`2135h`): player at a random grid cell (col 2..16, row 2..8); then 50
bricks at random grid cells rejecting: within 1 cell of the player, matching
column/row parity (pillars and always-open corridors), and `reserved_cells`
(corner exits). The first brick hides the BONUS, the last one the EXIT.

**Player** (`26C6h`…): `player_state` 0/1 standing, 6..13 dying. `stage_cleared`
is what advances the stage: all enemies dead. Touching the EXIT regenerates the
current stage without points. Touching the BONUS scores `(rand & 3Fh)*2 | 10h`.

## Quirks worth knowing before adding features

- `move_player` `1FDDh`: `ld a,e` is a dead store; the direction never reaches
  `player_state`, so walking tiles `A0h`–`AFh` and states 2..5 are unused.
- Running out of time does not kill the player: the bricks and both items just vanish.
- Title legend point values (200-160 … 50-10) do not match the code's scoring.
- `1E09h` (4 bytes) and `21F6h` (9 bytes) are unreachable leftovers.
- `tmr_unused` (`1ED3h`) and `unused_26c5` are never read.
- `enemy_table` has 8 slots but `spawn_from_hit` searches only 4 free ones per hit.
- `print_num5` always appends a fixed `0` digit; the HUD shows every score ×10.
- The stack lives directly below `1200h`, inside the monitor's free area.
- Only one key is read per frame via `GETKY`, so bomb (SPACE) and movement cannot
  happen in the same frame; no joystick support.

## C port (`c/`)

A migration of the game to C in the same environment as the MZPico-800-Manager
and its menu: z88dk `sccz80`, `+mz` target, `REGISTER_SP=0xd000`, built with the
same CMake pattern (`z88dk.zcc +mz -lm -create-app ...`, output renamed to `.mzf`).
`c/console.c` and `c/console.h` are the Manager's files, copied unchanged.

```
mkdir -p build/c && cd build/c && cmake ../../c && make      # -> build/c/bomber.mzf
```

| file | content |
|---|---|
| `c/game.h` | constants, records (`player_t`, `enemy_t`, `bomb_t`, `ftimer_t`), globals, all prototypes |
| `c/input.c` | input sources (keyboard sets, joysticks, network) sampled once per frame into `players[i].keys` |
| `c/mzio.c` | Z80 inline asm only where it matters: keyboard matrix scan (several keys at once), tone via monitor MSTA/MSTP, `flush_screen` diff + table translation, `composite_map` |
| `c/video.c` | layers, 2x2 tile helpers, digit/string output, buffer clears |
| `c/map.c` | walls and pillars, random brick layout (same rules as the original) |
| `c/enemy.c` | spawn, AI (type cycle, chase, random walk), drawing and death |
| `c/bomb.c` | placement, ticking, explosion phases, blast propagation and brick burning |
| `c/player.c` | up to 4 players: setup, drawing, movement, death animation, bonus and exit tiles |
| `c/bomber.c` | title screen, stage life cycle, HUD, frame order identical to the original main loop |
| `c/data.c/.h` | generated by `tools/extract_data.py` from `bomber.mzf`: translation tables (plus player 2..4 sprites and HUD letters), BOMBERNET logo (`tools/make_logo.py`), blast pattern, strings |
| `c/host/sim.c` | host build of the whole game logic with a key bot; use it to test changes without an emulator |

What changed on purpose:

- **Input** reads the 8255 keyboard matrix directly (rows F6h/F7h), so SPACE and a
  direction work in the same frame; the original read one key through GETKY.
- **Random numbers** use a 16-bit xorshift; the original mixed in the Z80 R register.
- The self-modifying `mode_patch` became the `title_mode` variable.
- Walls and pillars live in the map layer (the original repainted them into the
  draw buffer every frame); `map_walls()` is called after every `clear_map()`.
- **Frame pacing** is fixed to the original's measured frame time: `mz_frame_sync`
  in `mzio.c` waits on 8253 counter 1 (15611 Hz, programmed free-running) until
  `FRAME_TICKS` (915 = 58.6 ms) have passed since the previous frame. Change
  `FRAME_TICKS` in `game.h` to retune the game speed; the game logic stays
  frame-based like the original.
- Everything else (collision-at-draw-time, layer semantics, scoring, stage flow,
  quirks such as the exit restarting the stage) is kept, with comments marking the
  natural hook points for new features (walking animation, more bombs, blast range).

Controls: player 1 cursor keys + SPACE, player 2 W/A/S/D + E; choose the player
count on the title with cursor LEFT/RIGHT.

Bugs fixed against the first C version, both found with the emulator: the BONUS and
EXIT tiles were visible from the start (the original clears both flags after building
the map), and a lone dying enemy never disappeared because `tmr_enemy_die` was ticked
twice per frame (the original ticks only four timers globally; `tmr_explode` and
`tmr_enemy_die` are advanced by their users).

Host simulation:

```
cd c && cc -O1 -g -fsanitize=address,undefined -DHOST -I. host/sim.c video.c data.c \
   game.c map.c enemy.c bomb.c player.c bomber.c -o build/sim && build/sim 30000 2
```

The key bot is random, so it mostly blows itself up; use the emulator scenarios in
`tools/` style scripts for targeted checks (see PLAN.md, phase 0).
It prints text dumps of the screen (walls `#`, pillars `+`, bricks `%`, fire `*`,
bombs `o`, enemies `E`, player `P`, bonus `B`, exit `X`) and statistics.
### Performance (measured in mz800emu, MZ-800 in MZ-700 mode, during play)

| build | Z80 cycles per frame | ms | fps |
|---|---|---|---|
| original asm | 207,888 | 58.6 | 17.1 |
| C, first version | 312,627 | 88.1 | 11.3 |
| C, walls in map layer + row-offset table | 223,482 | 63.0 | 15.9 |
| C, + rewritten flush/composite asm, no divisions | 154,667 | 43.6 | 22.9 |
| C, with frame limiter (`FRAME_TICKS` 915) | 210,637 | 59.4 | 16.8 |

So the game logic uses about 44 ms of the 59 ms frame; the rest is headroom for
new features before the pacing would slip.

The flush loop no longer swaps AF' per cell and derives the VRAM address from the
shadow pointer with a constant delta; both flush and composite are unrolled 8x.
Digits are printed by subtraction (sccz80's division helper cost ~8k cycles/frame).

### Emulator workflow (`tools/emu.py`)

`tools/emu.py` drives a headless `mz800emu` over its MCP pipe transport (JSONL on
stdin/stdout, no Python MCP wrapper needed):

```
tools/emu.py bench bomber.mzf --at 0x26DA --frames 60 --tap SPACE --keys LEFT --png shot.png
tools/emu.py bench build/c/bomber.mzf --map build/c/bomber.map --frames 60 --tap SPACE --keys LEFT
```

`bench` loads the MZF (monitor ROM stays mapped), taps SPACE to leave the title,
holds a key, and measures cycles between arrivals at the frame flush routine.
Key names are the emulator's (`SPACE`, `LEFT`, `RIGHT`, `UP`, `DOWN`). The emulator
binary path comes from `$MZ800EMU` (default `~/src/mz800emu/build/build-mz800emu/mz800emu`).
To expose the same emulator as MCP tools inside Claude Code, run
`~/src/mz800emu/mcp-server/mcpinit.sh` and copy its generated `.mcp.json` into this project.
