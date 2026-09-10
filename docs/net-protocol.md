# MZPico NET protocol (draft 2, 2026-09-11)

Multiplayer transport for Sharp MZ programs through an MZPico (physical, on a
Pico W) or through the Unicard emulation of mz800emu (native or WASM on
mzpico.com). It is a game-independent service: rooms, an opaque per-frame
input broadcast for lockstep games, and opaque messages. BomberNet is the
first client.

## Placement

Vendor commands of the MZPico's Unicard-compatible repository device
(ports 0x50 command/status, 0x51 data; contract per
https://www.sharpwiki.cz/doku.php?id=en:unicard:z15mzfrepo and
`MZPico-firmware/docs/unicard-migration-plan.md`). Codes 0xA0-0xA9; the
MZPico range 0x90-0x9A is taken by the manager extensions, the Unicard
documents nothing above 0x72, mz800emu's uc3 socket commands sit at
0x80-0x89.

Conventions inherited from the repository protocol: one command byte on
0x50; parameters streamed on 0x51 in the documented order, strings end at
the first byte < 0x20; WORD little-endian; the 4-byte status record (bit 0
BUSY, bit 1 CMD_OUTPUT, bit 6 IN_PROGRESS, bit 7 ERROR; byte 2 = error code
on ERROR); output read from 0x51 after CMD_OUTPUT; STORNO cancels.

Detection: REVD subtype byte = 0x4D ('M'), then INFO (0x95) feature bit
`NET` (bit 3 of the feature byte, proposed). Without both, a program must
run offline.

## Model

- A **room** is created by one device for one **game id** with an opaque
  **settings** blob; others join with the 4-character room code. The relay
  namespaces codes per game id, so codes of different games never collide.
- Each member gets a **slot** 0..slots-1 in join order (creator = 0). A
  room declares `slots` (1..4) and `bytes per slot` (1..4) at creation;
  every member sends `bytes per slot` bytes of input per frame and reads
  `slots * bytes` per frame. The relay never interprets input bytes.
- Members signal **ready**; when all slots are filled and ready the relay
  fixes the **seed** (WORD) and the **start frame** and the state becomes
  RUNNING. Frames are numbered from 0 by the game; the relay stores the
  last 256 frames per room.
- **Hashes** are opaque WORDs; the relay compares the values reported for
  the same frame and flags DESYNC when they differ.
- **Messages** are opaque byte strings (<= 32 bytes) delivered to one slot
  or all others, outside the frame stream (lobby, chat, rematch, turn-based
  games that do not use frames at all).
- A **build id** (WORD) is checked at join: mismatch is refused, because
  lockstep needs identical code on every peer.
- The device holds ring buffers filled by its network side (Pico core 0 or
  the emulator's JS/socket glue); NETSTATUS, NETSEND, NETPOLL, NETHASH,
  NETMSG are answered from them immediately. Only NETCREATE and NETJOIN
  wait for the relay and use IN_PROGRESS (poll status bit 6, then read).

## Commands

| code | name | input | output |
|---|---|---|---|
| 0xA0 | NETSTATUS | - | 8 bytes: state, slot, members, ready mask, rtt/10 ms, frames buffered, msgs pending, last error |
| 0xA1 | NETCREATE | game WORD, build WORD, slots, bytes per slot, settings len (0..16), 16 settings bytes (fixed size) | room code (4 chars + 0x0D), slot (=0) |
| 0xA2 | NETJOIN | game WORD, build WORD, string code | 20 bytes: slot, slots, bytes per slot, settings len, 16 settings bytes |
| 0xA3 | NETLEAVE | - | - |
| 0xA4 | NETREADY | 1 byte (0/1) | seed WORD, start frame WORD (0xFFFF,0xFFFF while waiting) |
| 0xA5 | NETSEND | frame WORD, 4 input bytes (fixed size; `bytes per slot` used) | - |
| 0xA6 | NETPOLL | frame WORD | avail WORD, then `slots * bytes` bytes (valid when avail >= frame; else zeros) |
| 0xA7 | NETHASH | frame WORD, hash WORD | - |
| 0xA8 | NETMSG | to (slot or 0xFF = all), len (1..32), 32 bytes (fixed size) | - |
| 0xA9 | NETRECV | - | 34 bytes: from (0xFF = none), len, 32 bytes |

States (NETSTATUS byte 0): 0 NO_LINK (no WiFi / relay unreachable),
1 READY (linked, not in a room), 2 IN_ROOM (waiting for members/ready),
3 RUNNING, 4 DESYNC, 5 DROPPED (a member left or timed out), 6 SPECTATOR
(joined a full room; receives frames, sends nothing).

Errors (status byte 2 on ERROR): 6 build mismatch, 7 room unknown/full,
8 not in a room, 9 not linked, 10 bad parameter, 11 buffer full.

## Relay

Production: a Durable Object in the mzpico.com site Worker (Cloudflare),
`wss://mzpico.com/net` for the browser emulator, `ws://api.mzpico.com/net`
for the Pico W (plain-HTTP host). Reference and local test double:
`relay/relay.py` (FastAPI). One socket per device. JSON frames:

```
-> {"op":"create","game":G,"build":B,"slots":S,"bytes":N,"settings":"<hex>"}
<- {"op":"room","code":"ABCD","slot":0}
-> {"op":"join","game":G,"build":B,"code":"ABCD"}
<- {"op":"room","code":"ABCD","slot":1,"slots":S,"bytes":N,"settings":"<hex>"}
<- {"op":"members","count":2,"ready":3}          (broadcast on change)
-> {"op":"ready","ready":1}
<- {"op":"start","seed":12345,"frame":0}          (broadcast when all ready)
-> {"op":"input","frame":F,"data":"<hex>"}
<- {"op":"input","frame":F,"slot":s,"data":"<hex>"}   (relayed to the others)
-> {"op":"hash","frame":F,"hash":H}
<- {"op":"desync","frame":F}
-> {"op":"msg","to":s|-1,"data":"<hex>"}
<- {"op":"msg","from":s,"data":"<hex>"}
-> {"op":"leave"}   <- {"op":"dropped","slot":s}
<- {"op":"error","code":6..11,"text":"..."}
```

Rooms die 60 s after their last message. Codes are 4 letters from a
26-letter alphabet without I/O. A room is bound to (game, build).

## Frame budget

Parameter blobs have fixed sizes so the device can use a plain byte-count
parameter rule (the Unicard parser knows only bytes and strings).

Per game frame a lockstep client does NETSEND (cmd + 6 bytes) and NETPOLL
(cmd + 2 bytes, status, 2 + S*N bytes): for S=4, N=1 about 20 EXWAIT port
accesses, well under 0.1 ms.

## Lockstep recipe (what BomberNet does in phase 6)

Input delay D (2-3 frames). Before running frame F: NETSEND(F+D, local
input); NETPOLL(F) until avail >= F (stall with a "waiting" indicator
after a few polls); copy the S*N bytes into the game's per-frame input
vector; run the frame; every `hash_period` frames NETHASH(F, state_hash).
Local players on one machine occupy consecutive slots and send N bytes
each (one NETSEND per local slot; the relay keys inputs by socket + slot).

## Implementations

- Z80 client: BomberNet `c/uc.c`, `c/net.c`.
- Reference relay: BomberNet `relay/relay.py` (WebSocket `/net` on 8765 and
  JSON lines on TCP 8766 for the native emulator); production: Durable Object.
- Device: mz800emu `wasm` branch, `hw-generic/unicard/unimgr_net.c`
  (`[UNICARD] mzpico_mode = 1`, `net_relay = host:port`); Emscripten exports
  `mz_wasm_net_push(line)`, `mz_wasm_net_pop()`, `mz_wasm_net_link(1|0)` for
  the page's WebSocket. Verified 2026-09-11: two headless instances through
  the local relay via port I/O (`tools/nettest.py`).
- Firmware: pending (phase 5 step 5).

## Open points

- `NET` feature bit number in INFO: to be assigned by the firmware.
- Whether the Pico W can hold a WebSocket to mzpico.com through lwIP with
  the existing REST client, or a raw TCP framing is needed for the relay.
- Spectator slots are a relay-only feature; the device needs no change.
