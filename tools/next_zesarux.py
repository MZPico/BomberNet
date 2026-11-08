#!/usr/bin/env python3
"""The 128K tape on a whole emulated ZX Spectrum Next (ZEsarUX) against the
emulated 48K Spectrum of tools/zxemu.py, through the local relay.

  tools/next_zesarux.py host|join [frames]     default 600

The Next boots from the Next system's SD card image (ZEsarUX's tbblue.mmc,
copied to build/next with BOMBER.TAP from build/zx128 added), past the
video mode test and the welcome, into NextZXOS's Browser, and loads the
tape with the TAP loader's "128K mode", as a player would. Its UART is
bridged to tools/next_esp.py, an ESP's AT firmware on a pseudo-terminal
that connects to the local relay. host: the Next hosts and the 48K joins;
join: the 48K hosts and the Next types the room code. Then both walk for
the frames given; the game checks the hashes itself (DESYNC would end the
match), and the verdict is that neither machine aborted. Screenshots of
the Next: build/next/*.bmp.

Needs ZEsarUX built in ~/src/zesarux/src (ZESARUX) with
tools/ci/zesarux-uartbridge.patch (its --uartbridge option), Xvfb on :97
(NEXT_DISPLAY), the local relay on 8765, mtools, and build/zx128 and
build/zx (or ZX48_BUILD). ZEsarUX's remote protocol on port 10123."""
import os, socket, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from zxemu import ZX, sym_from_map

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
OUT = os.path.join(ROOT, 'build', 'next')
ZDIR = os.environ.get('ZESARUX', os.path.expanduser('~/src/zesarux/src'))
ZX48 = os.environ.get('ZX48_BUILD', os.path.join(ROOT, 'build', 'zx'))
PORT = 10123
ROLE = sys.argv[1] if len(sys.argv) > 1 else 'host'
N = int(sys.argv[2]) if len(sys.argv) > 2 else 600
K_SPACE, K_ENTER, K_DOWN = 128, 129, 144          # ZEsarUX's util_teclas


def zrcp(*lines, wait=0.6):
    """send commands to ZEsarUX's remote protocol; their replies, the prompts left out"""
    s = socket.create_connection(('127.0.0.1', PORT)); s.settimeout(3)
    def reply():
        got = b''
        try:
            while not got.endswith(b'command> '):
                d = s.recv(65536)
                if not d: raise ConnectionError('ZEsarUX closed its remote protocol connection')
                got += d
        except socket.timeout: pass
        return got[:-len(b'command> ')] if got.endswith(b'command> ') else got
    reply()                                       # the welcome
    out = ''
    for l in lines:
        s.sendall(l.encode() + b'\n'); time.sleep(wait); out += reply().decode('latin-1')
    s.close()
    return out


def key(code, n=1):
    for _ in range(n):
        zrcp(f'send-keys-event {code} 1', wait=0.15); zrcp(f'send-keys-event {code} 0', wait=0.35)


def shot(name): zrcp(f'save-screen {OUT}/{name}.bmp')


def next_var(name, n=1):
    """n bytes of the Next's game: BANKM (5B5Ch) and the bytes in one read, one instant, and counted only with
    bank 0 at C000h (bank 6 may stand in there); 255 (or None for n > 1) when no read had bank 0 in"""
    a = sym_from_map(os.path.join(ROOT, 'build', 'zx128', 'bomber.map'), name)
    for _ in range(50):
        d = bytes.fromhex(zrcp(f'read-memory 23388 {a - 23388 + n}').split()[0])
        if len(d) == a - 23388 + n and (a < 0xc000 or d[0] & 7 == 0): return d[-1] if n == 1 else d[-n:]
        time.sleep(0.05)
    return 255 if n == 1 else None


os.makedirs(OUT, exist_ok=True)
mmc = os.path.join(OUT, 'tbblue.mmc')
subprocess.run(['cp', os.path.join(ZDIR, 'tbblue.mmc'), mmc], check=True)
subprocess.run(['mcopy', '-o', '-i', mmc + '@@1048576', os.path.join(ROOT, 'build', 'zx128', 'bomber.tap'), '::/BOMBER.TAP'], check=True)
esp = subprocess.Popen([sys.executable, os.path.join(ROOT, 'tools', 'next_esp.py'), os.path.join(OUT, 'esp.pty'),
                        '--log', os.path.join(OUT, 'esp.log'), '--relay', '127.0.0.1:8765'])
time.sleep(1)
emu = subprocess.Popen(['./zesarux', '--noconfigfile', '--machine', 'TBBlue', '--enable-mmc', '--enable-divmmc-ports',
                        '--mmc-file', mmc, '--mmc-no-persistent-writes', '--tbblue-autoconfigure-sd-already-asked',
                        '--enable-remoteprotocol', '--remoteprotocol-port', str(PORT), '--uartbridge', os.path.join(OUT, 'esp.pty'),
                        '--vo', 'xwindows', '--ao', 'null', '--nosplash', '--disablefooter'], cwd=ZDIR,
                       env=dict(os.environ, DISPLAY=os.environ.get('NEXT_DISPLAY', ':97')),
                       stdout=open(os.path.join(OUT, 'zesarux.log'), 'w'), stderr=subprocess.STDOUT)
try:
    time.sleep(10)
    key(K_ENTER); time.sleep(30)                  # the video mode test; NextZXOS boots
    key(K_SPACE); time.sleep(8)                   # past the welcome
    key(K_ENTER); time.sleep(10)                  # the menu: Browser, given time to list the card
    key(K_DOWN, 16); time.sleep(1)                # down to BOMBER.TAP (the card's root: 15 entries above it)
    key(K_ENTER); time.sleep(3)                   # the TAP loader's menu
    zrcp('send-keys-ascii 150 49'); time.sleep(25)  # 1: 128K mode
    shot('title')
    if next_var('_title_mode') != 1: sys.exit('the Next is not at the title (or its memory reads wrong)')

    M = os.path.join(ZX48, 'bomber.map'); S = lambda n: sym_from_map(M, n)
    z = ZX(spectranet=True, relay=('127.0.0.1', 8765)); z.load(os.path.join(ZX48, 'bomber'))
    F = S('_flush_screen'); z.set_breakpoint(F)
    def frames(n):
        for _ in range(n): z.run_until(F, max_seconds=30)
    def frames_for(sec):
        t = time.time()
        while time.time() - t < sec: frames(1)
    def tap(k, n=4): z.press(k); frames(n); z.release(k); frames(1)
    def ntap(c, pause=0.6): zrcp(f'send-keys-ascii 200 {c}'); frames_for(pause)   # the 48K keeps running meanwhile
    g8 = lambda n: z.read(S(n), 1)[0]; g16 = lambda n: int.from_bytes(z.read(S(n), 2), 'little')
    frames(4)
    if ROLE == 'host':
        ntap(97); ntap(112)                           # the Next: A (down to NETWORK), P (HOST)
        ntap(32, 12)                                  # SPACE: the room
        shot('lobby')
        nc = sym_from_map(os.path.join(ROOT, 'build', 'zx128', 'bomber.map'), '_net_code')
        for _ in range(20):                           # the Next's room code, from its memory (retried: bank 6 may be
            code = bytes.fromhex(zrcp(f'read-memory {nc} 4').split()[0][:8]).decode('latin-1')   # paged in at C000h)
            if len(code) == 4 and code.isalpha() and code.isupper(): break
            frames_for(0.3)
        z.poke(S('_menu_net'), bytes([2])); z.poke(S('_net_code'), code.encode())
        tap('SPACE'); frames(4); tap('SPACE')
        for _ in range(600):
            if g8('_net_slot') == 1: break
            frames(1)
        frames_for(3)
        tap('SPACE'); ntap(32, 1)                     # the 48K ready, then the Next (the host last)
    else:
        z.poke(S('_menu_mode'), bytes([1])); z.poke(S('_menu_players'), bytes([2]))
        z.poke(S('_menu_net'), bytes([1])); z.poke(S('_menu_local'), bytes([1]))
        before = z.read(S('_net_code'), 4)
        tap('SPACE')
        for _ in range(600):
            if z.read(S('_net_code'), 4) != before: break
            frames(1)
        code = z.read(S('_net_code'), 4).decode()
        ntap(97); ntap(112); ntap(112)                # the Next: A, P, P (JOIN)
        ntap(32, 2)                                   # the code screen
        for ch in code: ntap(ord(ch.lower()))
        typed = next_var('_net_code', 4)
        if typed != code.encode():                    # a key press lost in the emulator's queue: the code put right
            print(f'the Next typed {typed!r}, not {code}: corrected in its memory', flush=True)
            a = sym_from_map(os.path.join(ROOT, 'build', 'zx128', 'bomber.map'), '_net_code')
            zrcp(f'write-memory {a} ' + ' '.join(str(c) for c in code.encode()))
        ntap(32, 4)                                   # join
        for _ in range(600):
            if g8('_s_members') >= 2: break
            frames(1)
        if g8('_s_members') < 2:
            shot('join'); sys.exit(f'the Next did not join room {code} (build/next/join.bmp)')
        frames_for(3)
        ntap(32, 1); tap('SPACE')                     # the Next ready, then the 48K (the host last)
    print('room', code, flush=True)
    for k in range(1500):                             # ready taps repeated until both are in the game (a tap that
        if g8('_title_mode') == 0 and (k % 25 or next_var('_title_mode') == 0): break   # lands in the lobby's redraw is lost)
        frames(1)
        if k % 150 == 149:
            if g8('_title_mode'): tap('SPACE')
            if next_var('_title_mode'): ntap(32, 0.5)
    print('in game: title_mode', g8('_title_mode'), 'Next', next_var('_title_mode'), 'delay', g8('_net_delay'), flush=True)
    z.press('P' if ROLE == 'join' else 'O'); zrcp('send-keys-event 111 1')   # both walk
    for i in range(N):
        frames(1)
        if g8('_net_abort'): break
        if i == N // 2: shot('game')
    frames_for(1)                                     # the Next's own verdict too: an abort it saw last may not have reached the 48K
    next_abort = next_var('_net_abort')
    ok = g8('_net_abort') == 0 and g8('_title_mode') == 0 and next_abort == 0
    print(f'end: 48K frame {g16("_frame_no")}, abort {g8("_net_abort")}, Next abort {next_abort}: {"passed" if ok else "FAILED"}', flush=True)
    zrcp('send-keys-event 111 0')
finally:
    emu.terminate(); esp.terminate()
sys.exit(0 if ok else 1)
