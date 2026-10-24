# BomberNet in the browser: WebAssembly

The game itself compiled to WebAssembly, with no emulated MZ-800 under it.
The play page on mzpico.com runs the MZ-800 build in an emulator
(mz800emu in WebAssembly); that emulator is paced by the browser's audio
callback and runs on the main thread, so it moves in bursts and stalls with
the page (docs/net-timing.md: the play page answered 300 to 700 ms late
while a browser warmed up). Compiled directly, the game runs its 60 ms
frames from the browser's clock and talks to the relay over the page's own
WebSocket.

## Steps

0. **Toolchain and replay**: Emscripten; the core and a platform layer;
   every recording replayed in Node.js with every state hash checked.
   Done, see below.
1. **The page**: the canvas, keys, gamepads, sound; the title, the menu,
   a local game. Done, see below.
2. **The network**: `ws.h` over the browser's WebSocket; two browsers in a
   match; cross-play with the other machines. Done, see below.
3. **On the site**: a page on staging.mzpico.com next to the emulated one,
   touch controls for phones. Done on staging, see below; production when
   it is reviewed.
4. Optional: rollback (local prediction) in the browser, where the CPU has
   room for it.

## Design

**Toolchain.** Emscripten 6.0.8 from `~/src/emsdk` (`tools/build_web.py`;
`EMSDK` to use another). `-O2`, 32-bit int, little-endian; the state hash
field by field (`PLAT_HASH_PORTABLE`), as on every port that is not a Z80.
The output: `build/web/bombernet.html`, `.js` (14 KB), `.wasm` (74 KB).

**The main loop.** The core's main never returns and waits in loops:
`plat_frame_sync`, `plat_delay`, and the network's own polling. A browser
must get its turn, or nothing is drawn and no message arrives. Emscripten's
Asyncify turns each wait into a yield (`emscripten_sleep`): the frame sync
sleeps until the next 60 ms mark of `emscripten_get_now()`, and `ws_poll`
yields whenever no message waits (the core's lockstep wait polls in a loop).
The core's `main` is compiled as `bomber_main` (it returns `void`); the
platform's `main` calls it.

**Screen.** The 40 x 25 cells of 8 x 8 with the Amiga's tables
(`c/platform/amiga/tables.c`: the MZ's glyphs and its 16 colours), as on the
ST and the PC; changed cells are written into a 320 x 200 RGBA buffer, and
the page puts it on its canvas once a frame (`Module.bombernetPresent`),
scaled with `image-rendering: pixelated`.

**Keys.** The page maps `KeyboardEvent.code` to small numbers
(`Module._web_key`): the cursor keys, SPACE, RETURN, ESC, BACKSPACE,
DELETE, the letters. Key set A is the cursor keys and SPACE (RETURN when
another player has key set B), key set B W A S D and E, as on the PC.
**Gamepads**: the Gamepad API, polled once a frame (`Module._web_pad`): the
d-pad or the left stick, any face button fires; the first one seen makes
the JOYSTICK row offer GAMEPAD.

**Sound.** A square wave of 1108400 / ratio Hz (the MZ's 8253) through Web
Audio, for len * 0.93 ms; the audio context starts with the first key press
(browsers allow sound only after a user gesture).

**The network.** `c/platform/web/ws_web.c` is `ws.h` over the page's
WebSocket; `common/netdev_soft.c` runs on it unchanged, with binary input
lines (`NET_BINARY_INPUT`). The relay: `?relay=ws://host:port` on the page,
else on an https page its own origin (`wss://<host>/net`: mzpico.com and
staging serve the relay there), else the game's built-in address.

**For the tests**, the page exports `web_state()` (the state as the other
machines' test logs give it) and `web_poke()` (the menu, the room code), and
with `?trace=1` keeps every 16th frame's hash and map.

## Step 0: the replays

`tools/web_test.py` builds each recording into a Node.js build
(`tools/build_web.py --test`: the recording compiled in, the keys fed from
it, every frame's hash checked) and runs it:

| Recording | Frames | Mismatches |
|---|---|---|
| coop1 | 299 | 0 |
| coop2-score | 2996 | 0 |
| coop2s | 319 | 0 |
| coop4 | 499 | 0 |
| dm2 | 499 | 0 |
| dm2s | 319 | 0 |
| dm4 | 499 | 0 |

## Step 1: the page

In Chromium (`tools/webpage.py`, the DevTools protocol, on the invisible X
display): the title, the menu by the keys (PLAYERS 2: key sets A and B),
a 2-player co-op game drawn and moving.

## Step 2: the network

`tools/web_match.py`: two tabs through the local relay; the joiner types
the room code as a player would. `tools/crossplay.py` has a `web` machine
(the tab driven through the same hooks).

Results (0 mismatches in all; the local relay unless said):

| Match | Delay | Result |
|---|---|---|
| browser - browser (two tabs) | 1 / 1 | 665 frames, 16.5 frames a second |
| browser host - Spectrum | 2 / 1 | fields identical |
| MZ-800 host - browser | 1 / 1 | fields identical |
| browser host - PC | 2 / 2 | fields identical |
| ST host - browser | 1 / 1 | fields identical |
| browser host - C64 | 4 / 4 (stock VICE's slow WiC64) | fields identical |
| Amiga host - browser | 1 / 1 | fields identical |
| browser - browser on staging (its own relay, wss, from the test machine) | 1 / 1 | 639 frames, 16.0 frames a second |

The emulated play page got 2 on the same path to the relay (docs/net-timing.md).

## Step 3: on the site

In mz-catalog `site/public/bombernet/` (`index.html`, `bombernet.js`,
`bombernet.wasm`; `_headers`: five minutes' caching, no cross-origin
isolation needed), at https://staging.mzpico.com/bombernet/ for now. On an
https page the game uses the site's own relay (`wss://<host>/net`), so it
meets everybody on that site's relay: the emulated play page there and any
machine set to that relay.

**Touch controls** (on a touch screen, or `?touch=1`): a d-pad, BOMB (SPACE,
and RETURN too: key set A fires with RETURN when two players share the
keyboard), BREAK (ESC: the game's prompts say BREAK, the MZ's key) and ABC,
which opens the phone's keyboard for a room code (each letter a key tap;
RETURN fires). Pointer events: one button per finger, held while pressed.
The canvas leaves room below it for them; under 440 px wide they are
smaller (all four fit at 360). Tested in Chromium at 360, 375 and 414 px
with touch emulation: the game started, the d-pad moving player 1 each way
(read from `web_state`), BOMB dropping one.

**The press that starts a game** dropped a bomb at once: the title reads
the keys itself and the game's input only from its first frame, and here
the switch is instant, so the key is still down (on the MZ the map takes
long enough). A fire key down when a game starts is ignored until it is
released (`fire_held` in plat_web.c).

The emulated play page's own touch layout for BomberNet (mz-catalog
`titles/bombernet/meta.yaml`) had BOMB and E (key set B's fire, useless
there) and no BREAK: now BOMB (Space) and BREAK (Escape, which mz800emu
maps to the MZ's BREAK).

**Not done**: the game does not show the delay it settles on; a hidden tab
is throttled by the browser (its timers once a second), so a player who
switches tabs stalls the match until they come back.
