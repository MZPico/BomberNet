#!/usr/bin/env python3
"""A Chromium tab driven over its debugging port (the DevTools protocol), for
the browser build's tests (tools/web_play.py, tools/web_match.py,
tools/crossplay.py's web machine): open a page, keys, JavaScript, screenshots.
Chromium runs on the invisible X display (:97, as tools/netbench.py starts
it; its own profile: Chromium ignores the debugging port on the default one).
CHROMIUM: the browser (default the snap's chromium); CHROMIUM_FLAGS: more
flags (CI: --no-sandbox, where the runner gives no user namespaces).
serve(dir) serves a build directory over HTTP on 127.0.0.1 for the page."""
import base64, functools, http.server, itertools, json, os, socket, subprocess, sys, threading, time, urllib.request
from websockets.sync.client import connect

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
CHROMIUM = os.environ.get('CHROMIUM', '/snap/bin/chromium')
PORT = 9222


def chromium():
    """Chromium with its debugging port (started once, left running)."""
    try: socket.create_connection(('127.0.0.1', PORT), timeout=1).close(); return
    except OSError: pass
    env = dict(os.environ, DISPLAY=os.environ.get('WEB_DISPLAY', ':97'))
    profile = os.path.expanduser('~/snap/chromium/common/netbench-profile')
    subprocess.Popen([CHROMIUM, f'--remote-debugging-port={PORT}', f'--user-data-dir={profile}', '--no-first-run',
                      '--autoplay-policy=no-user-gesture-required', '--ignore-gpu-blocklist', '--enable-unsafe-swiftshader',
                      '--disable-background-timer-throttling', '--disable-renderer-backgrounding',
                      '--disable-backgrounding-occluded-windows', *os.environ.get('CHROMIUM_FLAGS', '').split(),
                      'about:blank'], env=env,
                     stdout=open(os.path.join(ROOT, 'build', 'chromium.log'), 'w'), stderr=subprocess.STDOUT)
    for _ in range(60):
        try: socket.create_connection(('127.0.0.1', PORT), timeout=1).close(); return
        except OSError: time.sleep(0.5)
    sys.exit('Chromium did not come up')


def serve(directory, port=0):
    """an HTTP server for directory in a thread: its port"""
    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *a): pass
        def end_headers(self):
            self.send_header('Cache-Control', 'no-store')
            super().end_headers()
    httpd = http.server.ThreadingHTTPServer(('127.0.0.1', port), functools.partial(Quiet, directory=directory))
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd.server_address[1]


def _http(path, method='GET'):
    req = urllib.request.Request(f'http://127.0.0.1:{PORT}{path}', method=method)
    return json.loads(urllib.request.urlopen(req, timeout=10).read())


KEYS = {'Up': ('ArrowUp', 'ArrowUp', 38), 'Down': ('ArrowDown', 'ArrowDown', 40), 'Left': ('ArrowLeft', 'ArrowLeft', 37),
        'Right': ('ArrowRight', 'ArrowRight', 39), 'Space': (' ', 'Space', 32), 'Enter': ('Enter', 'Enter', 13),
        'Escape': ('Escape', 'Escape', 27), 'Backspace': ('Backspace', 'Backspace', 8)}


class Tab:
    def __init__(self, url):
        chromium()
        self.target = _http(f'/json/new?{urllib.parse.quote(url, safe="")}', 'PUT')
        self.ws = connect(self.target['webSocketDebuggerUrl'], max_size=None, open_timeout=10)
        self.ids = itertools.count(1)
        self.call('Page.enable'); self.call('Runtime.enable')
        self.call('Emulation.setFocusEmulationEnabled', enabled=True)   # keys reach it with no window focus

    def call(self, method, **params):
        i = next(self.ids)
        self.ws.send(json.dumps({'id': i, 'method': method, 'params': params}))
        while True:
            m = json.loads(self.ws.recv(timeout=30))
            if m.get('id') == i:
                if 'error' in m: raise RuntimeError(f'{method}: {m["error"]}')
                return m.get('result', {})

    def eval(self, expr):
        r = self.call('Runtime.evaluate', expression=expr, returnByValue=True, awaitPromise=True)
        if 'exceptionDetails' in r: raise RuntimeError(r['exceptionDetails'].get('text', 'exception'))
        return r.get('result', {}).get('value')

    def key(self, name, down):
        if name in KEYS: k, code, vk = KEYS[name]
        else: k, code, vk = name.lower(), f'Key{name.upper()}', ord(name.upper())   # a letter
        self.call('Input.dispatchKeyEvent', type='keyDown' if down else 'keyUp', key=k, code=code,
                  windowsVirtualKeyCode=vk, nativeVirtualKeyCode=vk)

    def tap(self, name='Space', hold=0.3):
        self.key(name, True); time.sleep(hold); self.key(name, False)

    def screenshot(self, path):
        open(path, 'wb').write(base64.b64decode(self.call('Page.captureScreenshot', format='png')['data']))

    def close(self):
        try: self.ws.close()
        except Exception: pass
        try: urllib.request.urlopen(f'http://127.0.0.1:{PORT}/json/close/{self.target["id"]}', timeout=5).read()
        except Exception: pass


import urllib.parse   # (after the class: quote is used at run time)
