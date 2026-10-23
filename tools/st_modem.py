#!/usr/bin/env python3
"""A WiFi modem for Hatari's serial port: what an ESP8266 with Hayes
firmware (Zimodem, WiFi232) does for an Atari ST, emulated on a pty.

  tools/st_modem.py LINK [--log file] [--baud 19200] [--trace]

LINK becomes a symlink to the pty's slave side: Hatari opens it as the ST's
RS-232 port (--rs232-in LINK --rs232-out LINK). Command mode: AT..., the
answers OK / ERROR; ATDT host:port (or ATDhost:port, with or without
quotes) opens a TCP connection, CONNECT 19200, and the line is a plain byte
stream; a second's quiet, "+++", a second's quiet: back to command mode
(OK), the call still up: ATH hangs up, ATO goes back online. The far end closing: NO CARRIER. Echo is on until ATE0,
as on a real modem. Bytes to the ST are paced at the line's speed (8N1:
baud / 10 bytes a second), as a real modem's serial side would; the log
has every command and call, and the bytes counted each way (--trace: the
bytes themselves).
"""
import os, select, socket, sys, termios, time, tty

link = sys.argv[1]
logf = open(sys.argv[sys.argv.index('--log') + 1], 'a') if '--log' in sys.argv else None
baud = int(sys.argv[sys.argv.index('--baud') + 1]) if '--baud' in sys.argv else 19200
GUARD = 1.0
TRACE = '--trace' in sys.argv
BURST = float(os.environ.get('ST_MODEM_BURST', 64))   # bytes written to the pty at once, at most


def log(msg):
    if logf: logf.write(f'{time.time():.3f} {msg}\n'); logf.flush()


master, slave = os.openpty()
tty.setraw(slave)
attrs = termios.tcgetattr(slave)
attrs[3] &= ~termios.ECHO
termios.tcsetattr(slave, termios.TCSANOW, attrs)
if os.path.lexists(link): os.remove(link)
os.symlink(os.ttyname(slave), link)       # the slave kept open here: Hatari's closing and reopening it never hangs up the pty
log(f'pty {os.ttyname(slave)} as {link}, {baud} bps')

sock, online, echo, cmd = None, False, True, b''
to_st = bytearray()                       # waiting to go to the ST, paced
budget, last_pace = 0.0, time.time()
plus, plus_since, last_from_st = 0, 0.0, 0.0
tx_count = rx_count = 0


def say(text):                            # a result code to the ST
    to_st.extend(b'\r\n' + text.encode() + b'\r\n')


def hang_up(why):
    global sock, online
    online = False
    if sock:
        sock.close(); sock = None
        log(f'hung up ({why}); to the ST {rx_count} bytes, from it {tx_count}')


def dial(arg):
    global sock, online, rx_count, tx_count
    arg = arg.strip().strip('"')
    host, _, port = arg.rpartition(':')
    if not host: host, port = arg, '23'
    try:
        s = socket.create_connection((host, int(port)), timeout=10)
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        s.setblocking(False)
    except (OSError, ValueError) as e:
        log(f'dial {arg}: {e}'); say('NO CARRIER'); return
    hang_up('a new call')
    sock, online, rx_count, tx_count = s, True, 0, 0
    log(f'dial {arg}: connected')
    say(f'CONNECT {baud}')


def command(line):
    global echo, online
    log(f'command {line!r}')
    u = line.upper()
    if not u.startswith('AT'): return
    body = line[2:]
    if body.upper().startswith('DT') or body.upper().startswith('DP'): dial(body[2:]); return
    if body.upper().startswith('D') and len(body) > 1: dial(body[1:]); return
    if u.startswith('ATH'): hang_up('ATH'); say('OK'); return
    if u.startswith('ATO'):
        if sock: online = True; log('back online'); say(f'CONNECT {baud}')
        else: say('NO CARRIER')
        return
    if u.startswith('ATE0'): echo = False
    if u.startswith('ATE1'): echo = True
    say('OK')                             # ATZ, AT&F, ATNET0, ATV1 ... all accepted


while True:
    now = time.time()
    if online and plus == 3 and now - last_from_st >= GUARD:   # the guard after "+++": command mode, the call kept
        log('escape: command mode'); plus, online, cmd = 0, False, b''
        say('OK')
    budget = min(budget + (now - last_pace) * baud / 10, BURST); last_pace = now
    n = int(min(budget, len(to_st)))
    if n:
        os.write(master, bytes(to_st[:n])); del to_st[:n]; budget -= n
    rl = [master] + ([sock] if sock and len(to_st) < 4096 else [])   # the far end read while the ST keeps up
    r, _, _ = select.select(rl, [], [], 0.002 if to_st else 0.05)
    if master in r:
        try: data = os.read(master, 4096)
        except OSError: data = b''
        if TRACE and data: log(f'ST> {data!r}')
        for b in data:
            c = bytes([b])
            if online:                    # online: to the far end, "+++" after a quiet second held back
                if c == b'+' and (plus or time.time() - last_from_st >= GUARD) and plus < 3:
                    plus += 1; last_from_st = time.time(); continue
                if plus:
                    sock.sendall(b'+' * plus); tx_count += plus; plus = 0
                sock.sendall(c); tx_count += 1
                last_from_st = time.time()
            else:
                last_from_st = time.time()
                if echo: to_st.extend(c)
                if c == b'\r':
                    line, cmd = cmd.decode('latin-1').strip(), b''
                    if line: command(line)
                elif c in (b'\x08', b'\x7f'): cmd = cmd[:-1]
                elif c != b'\n': cmd += c
    if sock and sock in r:
        try: data = sock.recv(4096)
        except BlockingIOError: data = None
        except OSError: data = b''
        if data == b'':
            hang_up('the far end closed'); say('NO CARRIER')
        elif data and online:
            if TRACE: log(f'<ST {data!r}')
            to_st.extend(data); rx_count += len(data)
        elif data: log(f'{len(data)} bytes from the far end in command mode, dropped')
