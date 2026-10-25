#!/usr/bin/env python3
"""Two browser builds (two Chromium tabs, tools/webpage.py) play a network
match through the relay: the host's tab hosts, the joiner's tab types the
room code as a player would, both ready up; then they play untouched and
their state hashes (every 16th frame) are compared, and the pace measured.

  tools/web_match.py [seconds]          default 30
  RELAY=ws://127.0.0.1:8765             the relay (default the local one); RELAY=page: the page's own choice
  WEB_URL=https://staging.mzpico.com/bombernet/   the page (default build/web served here); with RELAY=page
                                        an https page uses its own origin's relay
  PLAYERS=4 LOCAL_HOST=2 LOCAL_JOIN=2   seats (the menu's rows; default 2 players, 1 each)
"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import webpage

ROOT = webpage.ROOT
SECS = int(sys.argv[1]) if len(sys.argv) > 1 else 30
RELAY = os.environ.get('RELAY', 'ws://127.0.0.1:8765')


class Web:
    """one browser build in a tab; the same reading as the other machines' test logs"""
    def __init__(self, port, label):
        q = '?trace=1' + ('' if RELAY == 'page' else f'&relay={RELAY}') + (f"&runahead={os.environ['RUNAHEAD']}" if os.environ.get('RUNAHEAD') else '')
        self.label = label
        url = os.environ.get('WEB_URL') or f'http://127.0.0.1:{port}/bombernet.html'
        self.t = webpage.Tab(url + q)
        for _ in range(100):
            try:
                if self.t.eval('!!(window.Module && Module._web_state)'): break
            except RuntimeError: pass
            time.sleep(0.2)
        time.sleep(1)

    def state(self):
        s = self.t.eval('UTF8ToString(Module._web_state())') or ''
        p = s.split()
        return dict(zip(p[0::2], p[1::2]))

    def hashes(self):
        return {f - 1: '%04x' % h for f, h in (self.t.eval('Module.bombernetHashes || []') or [])}   # f - 1's hash

    def tap(self, k='Space'): self.t.tap(k); time.sleep(0.25)
    def close(self): self.t.close()


def menu(m, net_steps):
    """NETWORK (the second row) net_steps to the right: 1 HOST, 2 JOIN; the PLAYERS and LOCAL rows as asked"""
    m.tap('Down')
    for _ in range(net_steps): m.tap('Right')


if __name__ == '__main__':
    port = webpage.serve(os.path.join(ROOT, 'build', 'web'))
    host, join = Web(port, 'host'), Web(port, 'join')
    try:
        menu(host, 1)
        players = int(os.environ.get('PLAYERS', 2))
        host.tap('Down')                                       # PLAYERS (2 is the network default)
        for _ in range(players - 2): host.tap('Right')
        host.tap('Space')                                      # host a room
        for _ in range(100):
            code = host.state().get('code', '').strip()
            if host.state().get('title') == '1' and len(code) == 4 and code != 'AAAA': break
            time.sleep(0.2)
        print('host room', code, flush=True)
        menu(join, 2); join.tap('Space')                       # join: the code screen
        time.sleep(0.5)
        for _ in range(4): join.tap('Backspace')
        for ch in code: join.tap(ch)
        join.tap('Space')
        time.sleep(4)
        for _ in range(15):                                    # ready: the joiner, then the host, until both play
            if join.state().get('title') != '0': join.tap()
            time.sleep(0.8)
            if host.state().get('title') != '0': host.tap()
            for _ in range(10):
                if host.state().get('title') == '0' and join.state().get('title') == '0': break
                time.sleep(0.3)
            else: continue
            break
        hs, js = host.state(), join.state()
        if hs.get('title') != '0' or js.get('title') != '0': sys.exit(f'the match did not start: host {hs} join {js}')
        print(f'in game: delay host {hs["delay"]} join {js["delay"]}', flush=True)
        t0, f0 = time.time(), int(hs['frame'])
        time.sleep(SECS)
        hs, js = host.state(), join.state()
        hh, jh = host.hashes(), join.hashes()
        common = sorted(set(hh) & set(jh))
        mism = [f for f in common if hh[f] != jh[f]]
        fps = (int(hs['frame']) - f0) / (time.time() - t0)
        print(f'host frame {hs["frame"]} abort {hs["abort"]} delay {hs["delay"]} lead {hs.get("lead", "-")}, join frame {js["frame"]} abort {js["abort"]} lead {js.get("lead", "-")} '
              f'delay {js["delay"]}; {len(common)} hashed frames compared, {len(mism)} mismatches'
              + (f' (first at {mism[0]})' if mism else '') + f'; {fps:.1f} frames a second', flush=True)
        host.t.screenshot(os.path.join(ROOT, 'build', 'web', 'match_host.png'))
        sys.exit(1 if mism or hs['abort'] != '0' or js['abort'] != '0' or not common else 0)
    finally:
        host.close(); join.close()
