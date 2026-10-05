# BomberNet for eLeMeNt / MB03+ and ESP-01

128K Spectrum port by Milan "Hood" Stava, based on
[BomberNet by Martin / MZPico](https://github.com/MZPico/BomberNet) and
Bomber Man by Hudson Soft. UART definitions originate in WiFi BIOS 2.0
by Busy and Hood (`WifiBios20_MB_EL.a80`, included as a hardware reference).
These credits do not grant additional rights to the original game.

## Hardware-tested release 19

[19Bomberman.tap](../../runable/19Bomberman.tap) is the unchanged final release.
SHA-256: `1f035fa436f94478cadc513336311968a4fd003ee8ed2d76c00b1116ea1361bc`.

Enable ordinary 128K Spectrum paging with port 7FFD unlocked. ESP-01 must
already be associated with Wi-Fi. UART and ESP baud rates must match; the
game preserves both baud and credentials. 115200 baud was used during
hardware testing. Hardware RTS/CTS is not required. Load the TAP from its
beginning. A persistent red border means the bank probe or tape load failed.

NETWORK uses the original HOST/JOIN room-code workflow and the public relay
at `api.mzpico.com:80`; no relay changes are required. BREAK (CAPS SHIFT +
SPACE) leaves an offline or online match, with socket cleanup before another HOST.
The purple status bar retains the original glyphs/positions. Multiplayer
labels and life/win counts use player ink; scores and other text stay black.
Where a six-pixel glyph shares an eight-pixel attribute cell with a score,
that cell stays black. Single-player text is black.

## Memory and transport

The game occupies bank 0 from 24000; the driver occupies bank 6 from C000h.
The bridge linked first stays below C000h, staging data at 23808 (192 bytes).
Bank calls use a separate stack and restore the caller page, stack, IX and IY.
The packer checks both BSS ends against FDFFh, reserving 512 bytes for stacks.
The printer buffer at 23296 is not used.

AT setup/recovery includes a guarded escape from an inherited transparent
connection; passive receive support is required. The active WebSocket uses
raw transparent UART, a 4096-byte banked ring and bounded FIFO work. The
hardware UART RX buffer is 2048 bytes. Baud changes are not a game speed setting.

`ESP_FAST128` and `ESP_STREAM` select native hot loops, early input transmission,
AY effects, HUD colors and this transport. Ordinary Spectranet, MZ and CPC
builds do not enable them. The shared `run_game` reset clears stale abort/hash
state before a new game. Rules, build ID and hash phase are retained. Room
delay is fixed before priming; a two-frame room uses one local lookahead
frame, while larger delays are retained.

## Build and reproduce

Requires z88dk (classic sccz80, `zcc` on PATH, normal `ZCCCFG`), Python 3,
and SjASMPlus 1.20.3:

```sh
SJASMPLUS=/path/to/sjasmplus sh ports/esp01/build128.sh
```

Output: `build/esp01-128/bombernet_esp01_128_alpha19.tap`, BASIC name starts
with `19`. This is a **fresh source build**, not the byte-identical hardware
release. z88dk revision `731173ca7fd9ef81823f044cffe7d8e2192f3866` produced a
50,651-byte TAP, 39,542-byte game image (BSS end F836h), and 10,902-byte driver
allocation. Emulator checks passed; hardware retesting is required before
replacing the final release.

The release historically used exact-address assembly overlays. Reproduce it
byte-for-byte with:

```sh
SJASMPLUS=/path/to/sjasmplus sh ports/esp01/reproduce19.sh
```

This unpacks preserved link images/maps, applies release-19 corrections,
repacks and compares the TAP. The reference archive is a reproducibility
fixture, not a substitute for source compilation.

## Validation and remaining checks

After a source build:

```sh
python3 -m pip install zx==0.13.15 fastapi==0.142.2 uvicorn==0.54.0 websockets==16.0
python3 ports/esp01/tests/test_break19.py
python3 ports/esp01/tests/test_stream128.py
python3 ports/esp01/tests/test_hud128.py
ESP_MATCH=ports/esp01/tests/match_break19.py python3 ports/esp01/tests/test_relay128.py
```

Release-specific `test_alpha19_hud.py` instead runs after `reproduce19.sh`;
its fixed-address traps refer to those preserved maps.

Both builds passed offline COOP/DM exit/restart, inherited ESP recovery,
binary ring wrap/full ring, bounded TX failure, stable HUD attributes for
one to four players and four emulated devices over the actual local relay
(simultaneous first keys, ten matching hashes, BREAK cleanup and fresh HOST).
The 1200-frame host simulation matched current upstream output exactly.
Four physical Elements have **not** yet been tested. Emulator timing is not
a real-network latency benchmark.

An already-played upstream browser can retain inactive hit coordinates
included in its hash. ESP clears its coordinates at seed time; an old peer
retaining different values can still DESYNC. Restarting the browser was
reported to help. This draft does not suppress hashes or claim to fix state
in another platform. Cross-platform canonicalization needs an upstream decision.

Portable review candidates: stale-match reset, compositor, input formatting,
JSON layout guard, and empty-object fast paths. Native core replacements
remain conditional inline assembly. Move them behind platform hooks and
validate MZ/CPC equivalence before enabling them on those platforms.
