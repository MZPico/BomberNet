#!/usr/bin/env python3
"""A logging proxy between a local emulator and the production relay.

Every message is logged with the time it passed here (JSON lines: t, dir
"out" = from the local machine, "in" = from the relay, op, frame, slot, data),
so round trips can be timed from next to the local machine.

  lines  mz800emu's MZPico card speaks JSON lines over TCP (its net_relay
         setting): each line becomes a WebSocket message to the relay. The
         relay's URL is chosen from the first line, as the card firmware does:
         /net?game=G&create=1 or /net?game=G&code=CODE.
  ws     a WebSocket client (the Spectrum's): the bytes pass unchanged (the
         Host line of the upgrade is rewritten to the relay's name); frames
         are decoded on the side for the log.

  tools/netproxy.py lines|ws LISTEN_PORT LOG [relay host, default api.mzpico.com]
"""
import asyncio, json, socket, sys, time

MODE, PORT, LOG = sys.argv[1], int(sys.argv[2]), sys.argv[3]
RELAY = sys.argv[4] if len(sys.argv) > 4 else 'api.mzpico.com'
log = open(LOG, 'a', buffering=1)


def note(direction, text):
    try: m = json.loads(text)
    except ValueError: m = {}
    log.write(json.dumps({'t': round(time.monotonic(), 6), 'wall': round(time.time(), 3), 'dir': direction,
                          'op': m.get('op'), 'frame': m.get('frame'), 'slot': m.get('slot'),
                          'from': m.get('from'), 'data': m.get('data') if m.get('op') == 'msg' else None,
                          'code': m.get('code')}) + '\n')


async def lines_client(reader, writer):
    import websockets
    nodelay(writer)
    first = await reader.readline()
    if not first: return
    m = json.loads(first)
    path = f"/net?game={m.get('game', 0)}" + ('&create=1' if m.get('op') == 'create' else f"&code={m.get('code', '')}")
    async with websockets.connect(f'ws://{RELAY}{path}', ping_interval=None, compression=None) as ws:
        note('out', first.decode().strip()); await ws.send(first.decode().strip())

        async def up():
            while (line := await reader.readline()):
                t = line.decode(errors='replace').strip()
                if t: note('out', t); await ws.send(t)
            await ws.close()

        async def down():
            async for msg in ws:
                if isinstance(msg, bytes): continue
                note('in', msg); writer.write((msg + '\n').encode()); await writer.drain()
        await asyncio.gather(up(), down(), return_exceptions=True)
    writer.close()


class Frames:
    """Decodes WebSocket frames from a byte stream (for the log only)."""
    def __init__(self, direction, handshake):
        self.buf, self.direction, self.handshake = b'', direction, handshake

    def feed(self, data):
        self.buf += data
        if self.handshake:                       # HTTP request or response first
            i = self.buf.find(b'\r\n\r\n')
            if i < 0: return
            self.buf = self.buf[i + 4:]; self.handshake = False
        while len(self.buf) >= 2:
            op, n, p = self.buf[0] & 0x0f, self.buf[1] & 0x7f, 2
            masked = self.buf[1] & 0x80
            if n == 126:
                if len(self.buf) < 4: return
                n, p = int.from_bytes(self.buf[2:4], 'big'), 4
            elif n == 127:
                if len(self.buf) < 10: return
                n, p = int.from_bytes(self.buf[2:10], 'big'), 10
            key = b''
            if masked: key, p = self.buf[p:p + 4], p + 4
            if len(self.buf) < p + n: return
            payload = self.buf[p:p + n]
            if masked: payload = bytes(b ^ key[i % 4] for i, b in enumerate(payload))
            self.buf = self.buf[p + n:]
            if op == 1: note(self.direction, payload.decode(errors='replace'))


def nodelay(*writers):
    for w in writers: w.get_extra_info('socket').setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)


async def ws_client(reader, writer):
    r2, w2 = await asyncio.open_connection(RELAY, 80)
    nodelay(writer, w2)
    up_frames, down_frames = Frames('out', True), Frames('in', True)

    async def up():
        req = b''                                # the upgrade request arrives in pieces: whole, then rewritten
        while b'\r\n\r\n' not in req and (d := await reader.read(4096)): req += d
        req = req.replace(b'Host: 127.0.0.1\r\n', b'Host: ' + RELAY.encode() + b'\r\n')
        up_frames.feed(req); w2.write(req); await w2.drain()
        while (d := await reader.read(4096)):
            up_frames.feed(d); w2.write(d); await w2.drain()
        w2.close()

    async def down():
        while (d := await r2.read(4096)):
            down_frames.feed(d); writer.write(d); await writer.drain()
        writer.close()
    await asyncio.gather(up(), down(), return_exceptions=True)


async def main():
    srv = await asyncio.start_server(lines_client if MODE == 'lines' else ws_client, '127.0.0.1', PORT)
    async with srv: await srv.serve_forever()

asyncio.run(main())
