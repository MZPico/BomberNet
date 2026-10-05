"""Execute the compiled Z80 bridge/driver on native Spectrum 128 paging.

The UART peripheral is simulated, not the bank switch or driver C code.
Run after packing: python ports/esp01/tests/test_bank128.py
"""
import collections, socket, struct, sys, time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[3]/'tools'))
from zxemu import ZX, sym_from_map
from zx._data import Spectrum128
from zx._device import Dispatcher
from zx._beeper import Beeper
D=Path('build/esp01-128')
S=lambda n:sym_from_map(D/'bomber.map',n)
STREAM='_stream_active' in (D/'esp_bank.map').read_text()

class UART:
    def __init__(self, real=False):
        self.wire=collections.deque(); self.cmd=bytearray(); self.left=0
        self.sent=bytearray(); self.pos=0; self.payload=bytes(range(256))*2+bytes(range(88))
        self.reads=0; self.real=real; self.sock=None; self.remote=bytearray(); self.reported=False
        self.peer_closed=False; self.commands=[]
        self.raw=False;self.raw_delivered=False;self.escape=bytearray();self.last_tx=-100000000;self.now=lambda:0;self.escape_at=0
    def text(self,s): self.wire.extend(s.encode())
    def poll(self):
        if not self.sock: return
        try:
            data=self.sock.recv(4096)
            if data:
                if self.raw:self.wire.extend(data);return
                self.remote.extend(data)
                if not self.reported:
                    self.text('\r\n+IPD,%d\r\n'%len(self.remote));self.reported=True
            elif not self.peer_closed:
                self.peer_closed=True; self.text('\r\nCLOSED\r\n')
        except BlockingIOError: pass
        except ConnectionResetError:
            if not self.peer_closed:
                self.peer_closed=True;self.text('\r\nCLOSED\r\n')
    def available(self): return len(self.remote) if self.real else len(self.payload)-self.pos
    def input(self,port):
        if port==0x133b:
            self.poll(); self.reads+=1
            # Gaps split all response headers and binary data across calls.
            return int(bool(self.wire) and self.reads%3!=0)
        if port==0x143b: return self.wire.popleft()
        if port==0x773b: return 0x30
        if port==0x713b: return 1
        return None
    def output(self,port,byte):
        if port==0x7d3b and byte==1 and self.escape and self.now()-self.escape_at>70000:
            self.raw=False;self.escape.clear()
        if port!=0x133b: return
        if self.raw:
            if byte==43 and (self.escape or self.now()-self.last_tx>70000):
                self.escape.append(byte)
                if len(self.escape)==3:self.escape_at=self.now()
                return
            if self.escape:
                self.sent.extend(self.escape)
                if self.real:self.sock.sendall(self.escape)
                self.escape.clear()
            self.last_tx=self.now();self.sent.append(byte)
            if self.real:
                try:self.sock.sendall(bytes([byte]))
                except (BrokenPipeError,ConnectionResetError):
                    if not self.peer_closed:
                        self.peer_closed=True;self.text('\r\nCLOSED\r\n')
                self.sent.clear()
            elif not self.raw_delivered:
                self.wire.extend(self.payload);self.pos=len(self.payload);self.raw_delivered=True
            return
        if self.left:
            self.sent.append(byte);self.left-=1
            if not self.left:
                if self.real: self.sock.sendall(self.sent);self.sent.clear()
                self.text('\r\nSEND OK\r\n')
                if not self.real: self.text('+IPD,%d\r\n'%self.available())
            return
        self.cmd.append(byte)
        if byte!=10:return
        cmd=self.cmd.decode();self.cmd.clear();self.commands.append(cmd.strip())
        if cmd=='AT+CIPSEND\r\n':
            self.text('\r\nOK\r\n>');self.raw=True;self.raw_delivered=False
        elif cmd.startswith('AT+CIPSEND='):
            self.left=int(cmd.split('=')[1]);self.text('\r\nOK\r\n>')
        elif cmd=='AT+CIPRECVLEN?\r\n': self.text('\r\n+CIPRECVLEN:%d,0,0,0,0\r\nOK\r\n'%self.available())
        elif cmd.startswith('AT+CIPRECVDATA='):
            n=min(int(cmd.split('=')[1]),self.available());assert n>0
            self.text('\r\n+CIPRECVDATA,%d:'%n)
            if self.real:
                self.wire.extend(self.remote[:n]);del self.remote[:n]
                if not self.remote:self.reported=False
            else:
                self.wire.extend(self.payload[self.pos:self.pos+n]);self.pos+=n
            self.text('\r\nOK\r\n')
            if not self.real and self.pos==len(self.payload):self.text('CLOSED\r\n')
        elif cmd.startswith('AT+CIPSTART='):
            if self.real:
                self.remote.clear();self.reported=False;self.peer_closed=False
                self.sock=socket.create_connection(('127.0.0.1',8765));self.sock.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1);self.sock.setblocking(False)
            self.text('\r\nCONNECT\r\nOK\r\n')
        elif cmd=='AT+CIPCLOSE\r\n':
            if self.sock:self.sock.close();self.sock=None
            self.text('\r\nOK\r\nCLOSED\r\n')
        else:self.text('\r\nOK\r\n')

class BankZX(ZX):
    def __init__(self, real=False, uart=None):
        self.uart=uart or UART(real);self.pages=[];super().__init__()
        self.uart.now=self.now
        rom=self.read(0,0x4000);self.write(0,rom,rom_page=1);self.model=Spectrum128
        self.devices=Dispatcher([self,self.kb,Beeper(Spectrum128)])
        self.set_on_output_callback(self.output)
        self.write(24000,(D/'bomber').read_bytes(),ram_page=0)
        self.write(0xc000,(D/'esp_bank').read_bytes(),ram_page=6)
        # Execute the OUT instruction: hardware paging is handled by zx's C++ core.
        self.poke(0x5b00,bytes.fromhex('3e1001fd7fed79c30f5b'))
        self.pc=0x5b00;self.run_until(0x5b0f)
        self.poke(0x5b5c,[16]);self.pc=24000
    def now(self): return self.frame_count*70908+self.ticks_since_int
    def _input(self,port):
        value=self.uart.input(port)
        if value is not None: return value
        # No physical tape is playing in these peripheral tests.
        return 0xff if port&1 else self.kb.read_port(port)&0xbf
    def output(self,port,value):
        if port==0x7ffd:self.pages.append(value)
        self.uart.output(port,value)
    def call(self,name,*args):
        words=[0x5b0f,*reversed(args)]
        self.sp=0xfff0-len(words)*2
        self.poke(self.sp,b''.join(struct.pack('<H',x) for x in words))
        self.pc=S(name);before_sp=self.sp
        self.run_until(0x5b0f,40)
        assert self.sp==before_sp+2, (name,'stack',hex(self.sp),hex(before_sp))
        assert self.read8(0x5b5c)==16 and self.pages[-1]==16,(name,'page')
        return self.hl

def test_api(modern=False, early_closed=False):
    uart=UART()
    if modern:
        original=uart.text
        import re
        uart.text=lambda text:original(re.sub(r"\+CIPRECVDATA,(\d+):",r"+CIPRECVDATA:\1,",text))
    z=BankZX(uart=uart);assert z.call('_tcp_present')==1
    z.poke(0xc100,b'api.mzpico.com\0')
    assert z.call('_tcp_open',0xc100,80)==0
    msg=bytes([0,1,62,13,10,255]);z.poke(0xc100,msg)
    assert z.call('_tcp_send',0xc100,len(msg))==0
    assert z.uart.sent==msg
    if early_closed and not STREAM:z.uart.text('CLOSED\r\n')
    got=bytearray()
    for _ in range(10000):
        n=z.call('_tcp_recv',0xc100,17)
        if n==65535:break
        assert n<=17
        got.extend(z.read(0xc100,n))
        if STREAM and len(got)==len(z.uart.payload):break
    else:raise AssertionError('receive did not end')
    assert got==z.uart.payload,(len(got),len(z.uart.payload))
    assert z.call('_tcp_error')==0
    if not STREAM:assert z.call('_tcp_peer_closed')==1
    assert z.call('_tcp_read_commands')==(0 if STREAM else 4)
    assert len(z.pages)>20
    z.call('_tcp_close')
    print('PASS: real Z80 bank calls, bank-0 caller pointers/stack, binary TCP, fragmented UART, clean CLOSED')
    return z

def test_loader():
    z=BankZX()
    loader=(D/'loader128.bin').read_bytes()
    z.poke(0x8000,loader);z.pc=0x8000;z.sp=32760
    z.set_breakpoint(0x0556);z.set_breakpoint(24000)
    data=iter([(6,0xc000,(D/'esp_bank').read_bytes()),(0,24000,(D/'bomber').read_bytes())])
    loads=0
    while True:
        pc=z.step()
        if pc==0x0556:
            bank,addr,body=next(data)
            assert z.read8(0x5b5c)&7==bank and z.ix==addr and z.de==len(body)
            z.write(addr,body,ram_page=bank);loads+=1
            z.af|=1;z.pc=z.read16(z.sp);z.sp+=2
        elif pc==24000:break
        assert z.now()<30*3500000
    assert loads==2 and z.read8(0x5b5c)==16
    print('PASS: 128K loader bank probe, raw tape block addresses/lengths, game entry')

def test_boot():
    z=BankZX();z.pc=24000
    z.run_until(S('_flush_screen'),20)
    assert z.read8(S('_net_device'))==3
    assert z.read8(0x5b5c)==16
    for _ in range(5): z.run_until(S('_flush_screen'),20)
    z.screenshot(D/'title.png')
    print('PASS: game boots and detects ESP network interface')
    return z

if __name__=='__main__':
    test_api();test_api(modern=True,early_closed=True);test_loader();test_boot()
