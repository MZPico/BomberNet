#!/usr/bin/env python3
"""Two clients through the relay: create/join, ready -> start, 20 frames of
inputs, hashes (one deliberate desync), a message, a build mismatch, a
spectator, leave -> dropped. Usage: test_relay.py ws://127.0.0.1:8765/net"""
import asyncio, json, sys
import websockets

URL = sys.argv[1] if len(sys.argv) > 1 else "ws://127.0.0.1:8765/net"
GAME, BUILD = 0x424E, 0x0101


async def recv_op(ws, op, timeout=3):
    """Read until a frame with the given op arrives; returns it."""
    while True:
        m = json.loads(await asyncio.wait_for(ws.recv(), timeout))
        if m["op"] == op:
            return m


async def main():
    fails = 0
    def check(cond, what):
        nonlocal fails
        print(("ok   " if cond else "FAIL ") + what)
        if not cond: fails += 1

    async with websockets.connect(URL) as a, websockets.connect(URL) as b:
        await a.send(json.dumps({"op": "create", "game": GAME, "build": BUILD, "slots": 2, "bytes": 1, "settings": "0102"}))
        r = await recv_op(a, "room"); code = r["code"]
        check(r["slot"] == 0 and len(code) == 4, f"create -> room {code} slot 0")

        await b.send(json.dumps({"op": "join", "game": GAME, "build": 0x0202, "code": code}))
        e = await recv_op(b, "error"); check(e["code"] == 6, "join with wrong build -> error 6")

        await b.send(json.dumps({"op": "join", "game": GAME, "build": BUILD, "code": code}))
        r = await recv_op(b, "room"); check(r["slot"] == 1 and r["settings"] == "0102", "join -> slot 1, settings passed")
        while (m := await recv_op(a, "members"))["count"] < 2: pass
        check(m["count"] == 2, "creator sees 2 members")

        await a.send(json.dumps({"op": "ready", "ready": 1}))
        await b.send(json.dumps({"op": "ready", "ready": 1}))
        sa = await recv_op(a, "start"); sb = await recv_op(b, "start")
        check(sa["seed"] == sb["seed"] and sa["seed"] > 0, f"both get start with seed {sa['seed']:04x}")

        for f in range(20):
            await a.send(json.dumps({"op": "input", "frame": f, "data": "%02x" % (f & 0x1f)}))
            await b.send(json.dumps({"op": "input", "frame": f, "data": "%02x" % ((f * 3) & 0x1f)}))
        got_a = [await recv_op(a, "input") for _ in range(20)]
        got_b = [await recv_op(b, "input") for _ in range(20)]
        check(all(m["slot"] == 1 and m["frame"] == i and m["data"] == "%02x" % ((i * 3) & 0x1f) for i, m in enumerate(got_a)), "A receives B's 20 inputs in order")
        check(all(m["slot"] == 0 and m["frame"] == i for i, m in enumerate(got_b)), "B receives A's 20 inputs in order")

        await a.send(json.dumps({"op": "hash", "frame": 10, "hash": 0x1234}))
        await b.send(json.dumps({"op": "hash", "frame": 10, "hash": 0x1234}))
        await a.send(json.dumps({"op": "hash", "frame": 11, "hash": 0x1234}))
        await b.send(json.dumps({"op": "hash", "frame": 11, "hash": 0x9999}))
        d = await recv_op(a, "desync"); check(d["frame"] == 11, "hash mismatch at frame 11 -> desync")

        await a.send(json.dumps({"op": "msg", "to": 1, "data": "4869"}))
        m = await recv_op(b, "msg"); check(m["from"] == 0 and m["data"] == "4869", "message A -> B")

        async with websockets.connect(URL) as c:
            await c.send(json.dumps({"op": "join", "game": GAME, "build": BUILD, "code": code}))
            r = await recv_op(c, "room"); check(r.get("spectator") and r["slot"] == -1, "third joiner becomes spectator")
            await a.send(json.dumps({"op": "input", "frame": 20, "data": "1f"}))
            m = await recv_op(c, "input"); check(m["frame"] == 20, "spectator receives inputs")

        await b.send(json.dumps({"op": "leave"}))
        m = await recv_op(a, "dropped"); check(m["slot"] == 1, "leave -> dropped slot 1")

    async with websockets.connect(URL) as d:
        await d.send(json.dumps({"op": "join", "game": GAME + 1, "build": BUILD, "code": code}))
        e = await recv_op(d, "error"); check(e["code"] == 7, "same code under another game id -> unknown room")
    print("FAILURES:", fails)
    return fails

sys.exit(asyncio.run(main()))
