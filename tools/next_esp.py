#!/usr/bin/env python3
"""An ESP8266 with its AT firmware on a pseudo-terminal: the ZX Spectrum
Next's Wi-Fi module for an emulator whose Next UART is bridged to a host
character device (ZEsarUX's --uartbridge, patched in by tools/ci/
zesarux-uartbridge.patch).

  tools/next_esp.py LINK [--log file] [--relay host:port] [--trace]

LINK becomes a symlink to the pty's slave side. The firmware is what
tools/zxemu.py's Esp01 answers, in real time: AT, ATE0 (echo until then),
AT+CIPMUX/CIPDINFO/CIPRECVMODE/CIPMODE, AT+CIPSTART="TCP","host",port (a
real connection; --relay sends it there instead, the local relay in the
tests), AT+CIPCLOSE, AT+CIPSEND in CIPMODE=1: transparent, every byte to
the socket and back, until "+++" alone (20 ms of silence before and
after). Anything else answers ERROR. Bytes to the Next are paced at
115200 Bd (11520 a second). The log has every command and connection
(--trace: the bytes too)."""
import os, re, select, socket, sys, termios, time, tty

link = sys.argv[1]
logf = open(sys.argv[sys.argv.index('--log') + 1], 'a') if '--log' in sys.argv else None
relay = None
if '--relay' in sys.argv:
    h, _, p = sys.argv[sys.argv.index('--relay') + 1].rpartition(':')
    relay = (h, int(p))
TRACE = '--trace' in sys.argv
RATE = 11520.0                            # bytes a second at 115200 Bd, 8N1
GUARD = 0.020


def log(msg):
    if logf: logf.write(f'{time.time():.3f} {msg}\n'); logf.flush()


master, slave = os.openpty()
tty.setraw(slave)
attrs = termios.tcgetattr(slave)
attrs[3] &= ~termios.ECHO
termios.tcsetattr(slave, termios.TCSANOW, attrs)
if os.path.lexists(link): os.remove(link)
os.symlink(os.ttyname(slave), link)       # the slave kept open here: the emulator closing it never hangs up the pty
log(f'pty {os.ttyname(slave)} as {link}')

sock, echo, cipmode, transparent = None, True, 0, False

line = bytearray()
to_next = bytearray()
budget, last_pace = 0.0, time.time()
plus, last_rx = 0, 0.0


def say(text):
    to_next.extend(text.encode() if isinstance(text, str) else text)


def close():
    global sock
    if sock: sock.close(); sock = None


def command(c):
    global echo, cipmode, transparent, sock, plus
    log(f'command {c!r}')
    ok = '\r\nOK\r\n'
    if c == 'AT': say(ok)
    elif c == 'ATE0': echo = False; say(ok)
    elif c in ('AT+CIPMUX=0', 'AT+CIPDINFO=0', 'AT+CIPRECVMODE=0', 'AT+CIPRECVMODE=1'): say(ok)
    elif c in ('AT+CIPMODE=0', 'AT+CIPMODE=1'): cipmode = int(c[-1]); say(ok)
    elif c == 'AT+CIPCLOSE':
        if sock: close(); say('CLOSED\r\n' + ok)
        else: say('\r\nERROR\r\n')
    elif c.startswith('AT+CIPSTART="TCP","'):
        m = re.match(r'AT\+CIPSTART="TCP","([^"]+)",(\d+)$', c)
        if not m: say('\r\nERROR\r\n'); return       # malformed: refused, as the firmware does, relay or not
        try:
            s = socket.create_connection(relay if relay else (m.group(1), int(m.group(2))), timeout=10)
            s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            s.settimeout(5)                    # sends wait for room; reads only after select says there is data
            close(); sock = s; say('CONNECT\r\n' + ok); log(f'connected to {s.getpeername()}')
        except OSError as e:
            log(f'connect: {e}'); say('\r\nERROR\r\nCLOSED\r\n')
    elif c == 'AT+CIPSEND' and cipmode == 1 and sock:
        say(ok + '\r\n>'); transparent = True; plus = 0
    else: say('\r\nERROR\r\n')


def from_next(data):
    global plus, last_rx, transparent
    for b in data:
        now = time.time()
        if transparent:
            if b == 0x2b and (plus or now - last_rx >= GUARD) and plus < 3:
                plus += 1                  # perhaps the escape: held back
            else:
                out = b'+' * plus + bytes([b]); plus = 0
                if sock:
                    try: sock.sendall(out)
                    except OSError: close()
            last_rx = now
            continue
        last_rx = now
        if echo: say(bytes([b]))
        if b == 10:
            c = line.decode('latin-1').strip(); line.clear()
            if c: command(c)
        else: line.append(b)


while True:
    now = time.time()
    budget = min(budget + (now - last_pace) * RATE, 64.0); last_pace = now
    if to_next and budget >= 1:
        n = min(int(budget), len(to_next))
        os.write(master, bytes(to_next[:n])); del to_next[:n]; budget -= n
    if plus and time.time() - last_rx >= GUARD:
        if plus == 3: transparent = False; log('+++: back to commands')
        elif sock:                             # a + or ++ that ended a payload: data after all
            try: sock.sendall(b'+' * plus)
            except OSError: close()
        plus = 0
    rl = [master] + ([sock] if sock and transparent and len(to_next) < 512 else [])
    r, _, _ = select.select(rl, [], [], 0.002)
    if master in r:
        d = os.read(master, 4096)
        if TRACE: log(f'from the Next {d!r}')
        from_next(d)
    if sock and sock in r:
        try: d = sock.recv(4096)
        except OSError: d = b''
        if d:
            if TRACE: log(f'to the Next {d!r}')
            say(d)
        else:
            log('the far end closed'); close(); transparent = False; say('CLOSED\r\n')
