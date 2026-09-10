"""MZPico NET relay: rooms, per-frame input broadcast, hashes, messages.

Protocol: docs/net-protocol.md (section "Relay"). One WebSocket per device.
Runs beside the mzpico.com cloud repository service:

    uvicorn relay:app --host 0.0.0.0 --port 8765

Stateless beyond rooms; a room dies 60 s after its last message. Room codes
are 4 letters from a 24-letter alphabet (no I/O) and are namespaced per
(game id): the same code can exist for two different games.
"""
import asyncio, json, random, time
from fastapi import FastAPI, WebSocket, WebSocketDisconnect

app = FastAPI(title="MZPico NET relay")

ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ"
ROOM_TTL = 60.0
HISTORY = 256

E_BUILD, E_ROOM, E_NOROOM, E_NOLINK, E_PARAM, E_FULL = 6, 7, 8, 9, 10, 11


class Member:
    def __init__(self, ws: WebSocket, slot: int):
        self.ws, self.slot, self.ready = ws, slot, False


class Room:
    def __init__(self, game: int, build: int, slots: int, nbytes: int, settings: str, code: str):
        self.game, self.build, self.slots, self.nbytes, self.settings, self.code = game, build, slots, nbytes, settings, code
        self.members: dict[int, Member] = {}
        self.spectators: list[WebSocket] = []
        self.running = False
        self.seed = 0
        self.inputs: dict[int, dict[int, str]] = {}   # frame -> slot -> hex
        self.hashes: dict[int, dict[int, int]] = {}
        self.last = time.monotonic()

    def touch(self):
        self.last = time.monotonic()

    def ready_mask(self):
        return sum(1 << m.slot for m in self.members.values() if m.ready)

    async def broadcast(self, msg: dict, exclude: WebSocket | None = None):
        data = json.dumps(msg)
        for m in list(self.members.values()) + [Member(s, -1) for s in self.spectators]:
            if m.ws is exclude:
                continue
            try:
                await m.ws.send_text(data)
            except Exception:
                pass

    def trim(self):
        for d in (self.inputs, self.hashes):
            for f in [f for f in d if f < max(d) - HISTORY]:
                del d[f]


rooms: dict[tuple[int, str], Room] = {}


def new_code(game: int) -> str:
    while True:
        code = "".join(random.choice(ALPHABET) for _ in range(4))
        if (game, code) not in rooms:
            return code


async def reaper():
    while True:
        await asyncio.sleep(10)
        now = time.monotonic()
        for key, room in list(rooms.items()):
            if now - room.last > ROOM_TTL:
                await room.broadcast({"op": "dropped", "slot": -1, "reason": "timeout"})
                del rooms[key]


@app.on_event("startup")
async def _start():
    asyncio.create_task(reaper())


async def err(ws: WebSocket, code: int, text: str):
    await ws.send_text(json.dumps({"op": "error", "code": code, "text": text}))


@app.websocket("/net")
async def net(ws: WebSocket):
    await ws.accept()
    room: Room | None = None
    me: Member | None = None
    try:
        while True:
            try:
                msg = json.loads(await ws.receive_text())
            except json.JSONDecodeError:
                await err(ws, E_PARAM, "bad json")
                continue
            op = msg.get("op")

            if op == "create":
                if room:
                    await err(ws, E_PARAM, "already in a room")
                    continue
                game, build = int(msg.get("game", 0)), int(msg.get("build", 0))
                slots, nbytes = int(msg.get("slots", 4)), int(msg.get("bytes", 1))
                if not (1 <= slots <= 4 and 1 <= nbytes <= 4):
                    await err(ws, E_PARAM, "slots 1..4, bytes 1..4")
                    continue
                code = new_code(game)
                room = Room(game, build, slots, nbytes, str(msg.get("settings", "")), code)
                rooms[(game, code)] = room
                me = Member(ws, 0)
                room.members[0] = me
                await ws.send_text(json.dumps({"op": "room", "code": code, "slot": 0, "slots": slots, "bytes": nbytes, "settings": room.settings}))
                await room.broadcast({"op": "members", "count": 1, "ready": 0})

            elif op == "join":
                if room:
                    await err(ws, E_PARAM, "already in a room")
                    continue
                game, build, code = int(msg.get("game", 0)), int(msg.get("build", 0)), str(msg.get("code", "")).upper()
                r = rooms.get((game, code))
                if not r:
                    await err(ws, E_ROOM, "room unknown")
                    continue
                if r.build != build:
                    await err(ws, E_BUILD, "build mismatch")
                    continue
                if len(r.members) >= r.slots or r.running:
                    r.spectators.append(ws)
                    room = r
                    await ws.send_text(json.dumps({"op": "room", "code": code, "slot": -1, "slots": r.slots, "bytes": r.nbytes, "settings": r.settings, "spectator": True}))
                    continue
                slot = min(s for s in range(r.slots) if s not in r.members)
                me = Member(ws, slot)
                r.members[slot] = me
                room = r
                room.touch()
                await ws.send_text(json.dumps({"op": "room", "code": code, "slot": slot, "slots": r.slots, "bytes": r.nbytes, "settings": r.settings}))
                await room.broadcast({"op": "members", "count": len(room.members), "ready": room.ready_mask()})

            elif room is None:
                await err(ws, E_NOROOM, "not in a room")

            elif op == "ready":
                if me is None:
                    await err(ws, E_PARAM, "spectator")
                    continue
                me.ready = bool(msg.get("ready", 1))
                room.touch()
                await room.broadcast({"op": "members", "count": len(room.members), "ready": room.ready_mask()})
                if not room.running and len(room.members) == room.slots and all(m.ready for m in room.members.values()):
                    room.running = True
                    room.seed = random.randint(1, 0xFFFF)
                    room.inputs.clear(); room.hashes.clear()
                    await room.broadcast({"op": "start", "seed": room.seed, "frame": 0})

            elif op == "input":
                if me is None or not room.running:
                    await err(ws, E_NOROOM, "not running")
                    continue
                frame, data = int(msg.get("frame", -1)), str(msg.get("data", ""))
                if frame < 0 or len(data) != 2 * room.nbytes:
                    await err(ws, E_PARAM, "bad input")
                    continue
                room.touch()
                room.inputs.setdefault(frame, {})[me.slot] = data
                room.trim()
                await room.broadcast({"op": "input", "frame": frame, "slot": me.slot, "data": data}, exclude=ws)

            elif op == "hash":
                if me is None:
                    continue
                frame, h = int(msg.get("frame", -1)), int(msg.get("hash", 0)) & 0xFFFF
                room.touch()
                hs = room.hashes.setdefault(frame, {})
                hs[me.slot] = h
                if len(set(hs.values())) > 1:
                    await room.broadcast({"op": "desync", "frame": frame})

            elif op == "msg":
                to, data = int(msg.get("to", -1)), str(msg.get("data", ""))
                if len(data) > 64:
                    await err(ws, E_PARAM, "msg too long")
                    continue
                room.touch()
                out = {"op": "msg", "from": me.slot if me else -1, "data": data}
                if to < 0:
                    await room.broadcast(out, exclude=ws)
                elif to in room.members:
                    await room.members[to].ws.send_text(json.dumps(out))

            elif op == "leave":
                break

            elif op == "ping":
                await ws.send_text(json.dumps({"op": "pong", "t": msg.get("t")}))

            else:
                await err(ws, E_PARAM, "unknown op")
    except WebSocketDisconnect:
        pass
    finally:
        if room:
            if me is not None and room.members.get(me.slot) is me:
                del room.members[me.slot]
                await room.broadcast({"op": "dropped", "slot": me.slot})
                if room.running:
                    room.running = False
            elif ws in room.spectators:
                room.spectators.remove(ws)
            if not room.members and not room.spectators:
                rooms.pop((room.game, room.code), None)
