#!/usr/bin/env python3
"""Drive mz800emu headless over its MCP pipe (JSONL) transport.

Usage examples:
  tools/emu.py run bomber.mzf --frames 300 --keys SPACE --text
  tools/emu.py bench bomber.mzf --at 0x26DA --frames 200
  tools/emu.py bench build/c/bomber.mzf --map build/c/bomber.map --sym _flush_screen --frames 200

`bench` measures Z80 cycles between successive arrivals at an address
(the frame flush routine), i.e. the cost of one game frame.
"""
import argparse, json, os, subprocess, sys, time

EMU = os.environ.get('MZ800EMU', os.path.expanduser('~/src/mz800emu/build/build-mz800emu/mz800emu'))
CPU_HZ = 3546900  # MZ-800 PAL Z80 clock


class Emu:
    def __init__(self, exe=EMU):
        self.p = subprocess.Popen([exe, '--mcp-pipe', '--headless', '--no-first-run-windows'],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, text=True, bufsize=1,
                                  cwd=os.path.dirname(exe))
        self.req_id = 0
        self.hello = json.loads(self.p.stdout.readline())
        self.commands = set(self.hello['commands'])
        # wait until the emulated machine is up
        for _ in range(100):
            r = self.call('get_state', check=False)
            if r.get('success'):
                break
            time.sleep(0.1)

    def call(self, cmd, data=None, check=True):
        self.req_id += 1
        req = {'req_id': self.req_id, 'cmd': cmd}
        if data is not None:
            req['data'] = data
        self.p.stdin.write(json.dumps(req) + '\n')
        self.p.stdin.flush()
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError('emulator exited')
            r = json.loads(line)
            if r.get('type') == 'response' and r.get('req_id') == self.req_id:
                break
        if check and not r.get('success'):
            raise RuntimeError(f'{cmd}: {r.get("error")}')
        return r

    def data(self, cmd, data=None):
        return self.call(cmd, data)['data']

    def close(self):
        try:
            self.call('shutdown', check=False)
        except Exception:
            pass
        self.p.stdin.close()
        self.p.wait(timeout=5)

    # ---- helpers
    def load_mzf(self, path):
        """Load body at its load address and jump to exec (monitor ROM stays mapped)."""
        hdr = open(path, 'rb').read(128)
        exec_addr = hdr[22] | (hdr[23] << 8)
        self.call('pause')
        self.data('media_load_mzf', {'path': os.path.abspath(path)})
        self.data('set_register', {'reg': 'SP', 'value': 0x10F0})
        self.data('set_register', {'reg': 'PC', 'value': exec_addr})
        return exec_addr

    def press(self, key):
        return self.data('input_press_key', {'key': key})

    def release(self, key=''):
        return self.data('input_release_key', {'key': key})

    def run_frames(self, n):
        return self.data('run', {'frames': n})

    def run_until(self, addr, max_cycles=50_000_000):
        """Start running until PC == addr and wait for the CPU to stop there."""
        self.data('run_until_addr', {'addr': addr, 'max_cycles': max_cycles})
        while True:
            r = self.call('get_registers', check=False)   # fails while running
            if r.get('success') and r['data']['PC'] == addr:
                return r['data']
            time.sleep(0.001)

    def cycles(self):
        return self.data('get_raster_pos')['total_cycles']

    def regs(self):
        return self.data('get_registers')

    def text(self):
        d = self.data('get_video_text_dump')
        return d

    def mem(self, addr, n):
        return self.data('mem_read', {'addr': addr, 'len': n})

    def screenshot(self, path):
        return self.data('screenshot_save_to_file', {'path': os.path.abspath(path), 'format': 'png'})


def cycles_of(r):
    for k in ('total_cycles', 'cycles', 'tstates', 'total_tstates'):
        if k in r:
            return r[k]
    return None


def sym_from_map(path, name):
    for line in open(path):
        parts = line.split()
        if len(parts) >= 3 and parts[0] == name and parts[1] == '=':
            return int(parts[2].lstrip('$'), 16)
    raise SystemExit(f'{name} not in {path}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('mode', choices=['run', 'bench', 'probe'])
    ap.add_argument('mzf')
    ap.add_argument('--frames', type=int, default=200)
    ap.add_argument('--keys', default='', help='comma separated keys held during the run (e.g. SPACE,CURSOR_LEFT)')
    ap.add_argument('--tap', default='', help='keys pressed only during warm-up (e.g. SPACE to start the game)')
    ap.add_argument('--at', default='', help='frame address (hex) for bench')
    ap.add_argument('--map', default='')
    ap.add_argument('--sym', default='_flush_screen')
    ap.add_argument('--text', action='store_true')
    ap.add_argument('--png', default='')
    ap.add_argument('--warmup', type=int, default=30)
    a = ap.parse_args()

    e = Emu()
    try:
        exec_addr = e.load_mzf(a.mzf)
        print(f'loaded {a.mzf}, exec {exec_addr:04X}')
        if a.mode == 'probe':
            print(json.dumps(e.regs())[:600])
            print(json.dumps(e.run_frames(5))[:600])
            print(json.dumps(e.text())[:800])
            return
        if a.mode == 'run':
            r = e.run_frames(a.frames)
            print('run:', json.dumps(r)[:300])
        else:
            addr = int(a.at, 16) if a.at else sym_from_map(a.map, a.sym)
            print(f'measuring frames at {addr:04X}')
            for _ in range(a.warmup):
                e.run_until(addr)
            for k in [k for k in a.tap.split(',') if k]:
                e.press(k)
            for _ in range(5):
                e.run_until(addr)
            for k in [k for k in a.tap.split(',') if k]:
                e.release(k)
            for k in [k for k in a.keys.split(',') if k]:
                e.press(k)
            for _ in range(10):
                e.run_until(addr)
            samples = []
            prev = e.cycles()
            for _ in range(a.frames):
                e.run_until(addr)
                c = e.cycles()
                samples.append(c - prev)
                prev = c
            samples.sort()
            avg = sum(samples) / len(samples)
            print(f'frames={len(samples)} avg={avg:.0f} cycles ({avg / CPU_HZ * 1000:.1f} ms, {CPU_HZ / avg:.1f} fps) '
                  f'min={samples[0]} median={samples[len(samples) // 2]} max={samples[-1]}')
        if a.text:
            t = e.text()
            lines = t.get('lines') or t.get('text') or t
            if isinstance(lines, list):
                print('\n'.join(lines))
            else:
                print(lines)
        if a.png:
            print(e.screenshot(a.png))
    finally:
        e.close()


if __name__ == '__main__':
    main()
