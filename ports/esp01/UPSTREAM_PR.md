# Add eLeMeNt / MB03+ ESP-01 128K port (release 19)

Adds BomberNet for eLeMeNt / MB03+ with ESP-01, without RTS/CTS. The branch is based on upstream c6d40a0 and preserves all existing platforms. The hardware-tested final TAP 19 remains unchanged.

- Bank-6 driver, fixed-RAM bridge and separate stacks, checked memory bounds.
- Bounded AT recovery and transparent UART WebSocket transport, 4096-byte software ring; preserves baud and Wi-Fi credentials.
- Conditional ESP hot-loop/input optimizations, AY effects, stable original-layout purple HUD and BREAK exit/socket cleanup.
- Shared stale-match abort/hash reset before starting another game.
- Build instructions, author credits, byte-identical release reproduction, fresh source build and emulator/relay workflow.

Local validation: clean source build using z88dk 731173ca7fd9ef81823f044cffe7d8e2192f3866 and SjASMPlus 1.20.3; TAP 50,651 bytes, game BSS end F836h. Both source and preserved release passed offline COOP/DM exit/restart, ring wrap/full ring, bounded TX failure, inherited transparent-mode recovery, one-to-four-player HUD colors and four emulated devices over the actual local relay (simultaneous first input, ten matching hash checkpoints, BREAK cleanup and fresh HOST). Host simulation matches upstream for 1200 frames; ordinary ZX/Spectranet code compiles.

Please keep this PR as a **draft**: four physical Elements and the fresh source build on hardware are not yet tested; MZ/CPC hardware and cross-platform gameplay of the rebased source build need validation. Native core replacements currently use conditional inline assembly; review platform hooks and equivalence before enabling them on other ports. A previously played browser may retain hash-visible inactive hit coordinates; restarting it was reported to help. This does not disable hashes or claim to fix stale state in another client.

See ports/esp01/README.md for full provenance, tests and limitations. The exact release reproducer uses preserved link images and overlays; this is explicitly separate from source compilation.

Credits: Bomber Man — Hudson Soft; BomberNet — Martin / MZPico; ESP port — Milan "Hood" Stava; WiFi BIOS reference — Busy and Hood.
