BomberNet for DOS
=================

Bomberman for up to four players, on one PC or over the network with
the other BomberNet machines (Sharp MZ-800, ZX Spectrum, Amstrad CPC,
Atari XL/XE, Commodore 64, Amiga, Atari ST and the browser player).

Needs: an 8088 PC or better, DOS 3 or later, 256 KB of memory, a CGA,
EGA or VGA card. An XT should keep up on a CGA or an EGA (measured in
an emulator: reports from real machines are welcome).

Start it:  BOMBER        (BOMBER /? lists the options)

Keys
----
  Key set A    the cursor keys, SPACE fires (RETURN when another
               player has key set B)
  Key set B    W A S D, E fires
  Joysticks    two on the game port
  ESC          leaves a match; on the title, back to DOS

Options
-------
  /RELAY=host[:port]   the relay (port 8765 if not given;
                       else api.mzpico.com, the public one)
  /CGA /EGA /VGA       the adapter's look (else the one found)
  /COM1 /COM2          the WiFi modem's port (else both are tried)
  /BAUD=n              the modem's speed (else 19200, then 115200)
  /NOSOUND             no sound
  /SPEAKER             the PC speaker (else an AdLib or Sound
                       Blaster's FM, when one is found)
  /NET=PKT /NET=MODEM  Ethernet or the modem only
  /IP= /MASK= /GW= /DNS=  Ethernet's addresses (else DHCP's)

The network
-----------
Ethernet: any network card with a DOS packet driver, loaded first. For
an NE2000 at 300h, IRQ 3, for example (Crynwr's free drivers,
http://crynwr.com/drivers/):

  NE2000 0x60 3 0x300
  BOMBER

The game finds the driver by itself and takes its address by DHCP
(else /IP= /MASK= /GW= /DNS=).

A WiFi modem (an ESP8266 with Hayes firmware: Zimodem, WiFi232,
RetroWiFi) on COM1 or COM2 at 19200 or 115200 bps, already joined to
your WiFi, with telnet translation off. The game finds it by itself
when no packet driver is loaded.

In DOSBox-X: [ne2000] ne2000=true, backend=slirp and the packet driver
for Ethernet; or [serial] serial1=modem for a modem.

Choose NETWORK in the menu, then HOST to open a room (its four letters
appear) or JOIN to enter one. Everybody presses SPACE when ready, the
host last. A relay of your own: relay/relay.py from the sources, then
BOMBER /RELAY=its address.

Sources and more: https://github.com/MZPico/BomberNet
