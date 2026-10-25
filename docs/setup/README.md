# Setting up BomberNet

One page per platform, with what it runs on, how to load it, what network
play needs on it, and the keys. The files are on the
[releases page](https://github.com/MZPico/BomberNet/releases) and on
[mzpico.com](https://mzpico.com/titles/bombernet/), where the game also
runs in the browser.

| Platform | Guide |
|---|---|
| Sharp MZ-800 (the MZ-700 plays locally) | [mz-800.md](mz-800.md) |
| ZX Spectrum 48K and up | [zx-spectrum-48k.md](zx-spectrum-48k.md) |
| eLeMeNt ZX in 128K mode, with an ESP-01 | [element-zx-128k-esp01.md](element-zx-128k-esp01.md) |
| Amstrad CPC | [amstrad-cpc.md](amstrad-cpc.md) |
| Atari XL/XE | [atari-xl.md](atari-xl.md) |
| Commodore 64 | [c64.md](c64.md) |
| Amiga | [amiga.md](amiga.md) |
| Atari ST | [atari-st.md](atari-st.md) |
| IBM PC, DOS | [pc-dos.md](pc-dos.md) |
| Browser | nothing to set up: [play](https://mzpico.com/bombernet/) |

The rest of this page is about network play, which works the same on all of
them.

## A room

1. On the title screen, one player sets NETWORK to **HOST** and presses
   SPACE. The game opens a room and shows its four-letter **code**.
2. The others get the code by any means (a message, a call), set NETWORK to
   **JOIN**, press SPACE and type the code. UP and DOWN also step through the
   letters, LEFT and RIGHT move along the code, DELETE steps back.
3. The lobby lists who is in. Everybody presses SPACE when ready, the host
   last; BREAK (ESC on most machines) cancels. The match starts on every
   machine at once.

**PLAYERS** on the host's title screen is the number of players in the whole
match, up to four. **LOCAL** on each machine is how many of them sit at that
machine: two people on one keyboard and two on another machine is as fine as
four machines with one each. Any platforms can share a room; every machine
runs the same game frame for frame.

Co-op or deathmatch is the host's choice (MODE). In a deathmatch the first to
win three rounds wins the match.

## The input delay

A key acts a few frames after it is pressed, the same number on every
machine, so that everybody's keys reach everybody in time: the lobby shows it
as DELAY, in milliseconds. The host measures the round trips in the lobby
and picks the delay; during the match it lowers the delay when every machine
has time to spare and raises it when everybody keeps waiting, never above
the lobby's figure. On a home network the delay is one or two frames (60 to 120 ms);
through the internet usually two or three.

The browser, the PC (from a 286 up), the Amiga and the Atari ST show your own
player without that delay: they run a few frames ahead with your keys, which
are already known, and guess the others' moves, correcting a wrong guess when
the real keys arrive (a cell of a jump at most; a bomb is never guessed). The
other platforms have no time for that and show everything after the delay.
`/NOROLLBACK` on the PC and `?runahead=0` on the browser page switch it off.

## The relay

The machines talk through a relay server; the game is built for
`api.mzpico.com`, the public one. Every network device but the Unicard
reaches it; nothing to configure.

For a home network without the internet, or to keep a party local, run the
reference relay from the sources on any PC that has Python (the command
as for a Linux or macOS shell; on Windows set the variable first with
`set RELAY_TCP_HOST=0.0.0.0`, then run the `uvicorn` line):

```
git clone https://github.com/MZPico/BomberNet
cd BomberNet/relay && pip install -r requirements.txt
RELAY_TCP_HOST=0.0.0.0 uvicorn relay:app --host 0.0.0.0 --port 8765
```

Then point each machine at that PC (say 192.168.1.10):

| Platform | Where |
|---|---|
| IBM PC | `BOMBER /RELAY=192.168.1.10` |
| Spectrum, CPC, Atari XL, C64, Amiga, Atari ST | `tools/set_relay.py <file> 192.168.1.10` rewrites the release file's relay address (`.tap`, `.dsk`, `.cpc`, `.xex`, `.prg`, `.d64`, the Amiga program or `.adf`, the ST's `.prg` or `.st`); without an address it shows the file's relay; `api.mzpico.com:80` sets the public relay again |
| MZ-800 with an MZPico | `mzpico.ini`: `[cloud]` `net_relay = 192.168.1.10`, `net_port = 8765` |
| MZ-800 with a Unicard | `/unicard/network.cfg`: `NETRELAYADDR`, `NETRELAYPORT` (8766, the relay's TCP port). A Unicard plays only through such a relay, never through the public one |
| Browser | the page on mzpico.com always uses the public relay |

## The messages

| Message | Meaning |
|---|---|
| NETWORK row greyed out, or missing | no network device was found when the game started; see the platform's guide |
| NO CONNECTION | the relay did not answer: no network, no DHCP, a wrong relay address, or a Wi-Fi device not joined to its network |
| ROOM NOT FOUND | no room has that code (rooms close when everybody leaves) |
| BUILD MISMATCH | the other machine runs a BomberNet whose game logic or protocol differs: the two must carry the same build id, which changes only with those (0.2.0 and 0.3.0 share it and play together) |
| NETWORK ERROR nn | the device refused a command; nn is its code |
| WAITING FOR THE OTHERS | you are ready; the match starts when everybody is |
| DESYNC | the machines disagree about the game, which they check every sixteen frames; the match ends. It should not happen: please report it with the platforms involved |
| CONNECTION LOST | a machine stopped answering; the match ends |
| MATCH LEFT | someone pressed BREAK |
