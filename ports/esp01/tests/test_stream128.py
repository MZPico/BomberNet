"""Raw UART WebSocket transport: bounded TX/RX, ring wrap and mode recovery."""
from test_bank128 import BankZX,UART,D,STREAM
from test_fast128 import busy_send,old_application

class RingUART(UART):
    def __init__(self):
        super().__init__();self.payload=b'';self.source=bytes(range(256))*40;self.offset=0
    def poll(self):
        if self.raw and self.raw_delivered and not self.wire and self.offset<len(self.source):
            self.wire.extend(self.source[self.offset:self.offset+256]);self.offset+=256

def ring():
    u=RingUART();z=BankZX(uart=u);z.poke(0xc100,b'host\0')
    assert z.call('_tcp_open',0xc100,80)==0
    z.poke(0xc100,b'x');assert z.call('_tcp_send',0xc100,1)==0
    got=bytearray()
    for _ in range(10000):
        n=z.call('_tcp_recv',0xc100,17)
        assert n!=65535
        got.extend(z.read(0xc100,n))
        if len(got)==len(u.source):break
    else:raise AssertionError('ring drain stalled')
    assert got==u.source
    assert not any(c.startswith('AT+CIPSEND=') or c.startswith('AT+CIPRECV') and c.endswith('?') for c in u.commands)
    assert z.call('_tcp_error')==0
    z.call('_tcp_close');assert not u.raw
    z.poke(0xc100,b'host\0')
    assert z.call('_tcp_open',0xc100,80)==0
    print('PASS: 10KB binary RX ring wraps, bounded FIFO batches, raw transfer and command-mode reconnect')

if __name__=='__main__':
    assert STREAM
    busy_send();old_application();ring()

def closes_and_timeout():
    for data in [b'\x88\0',b'\r\nCLOSED\r\n']:
        u=UART();u.payload=b'';z=BankZX(uart=u)
        z.poke(0xc100,b'host\0');assert z.call('_tcp_open',0xc100,80)==0
        u.wire.extend(data);z.poke(__import__('test_bank128').S('_ws_open_now'),[1])
        assert z.call('_ws_poll')==0
        assert z.read8(__import__('test_bank128').S('_ws_lost'))==1 and not u.raw
        assert z.call('_tcp_open',0xc100,80)==0
    class JamUART(UART):
        jam=False
        def input(self,p):
            if self.jam and p==0x133b:return 2
            return super().input(p)
    u=JamUART();z=BankZX(uart=u);z.poke(0xc100,b'host\0')
    assert z.call('_tcp_open',0xc100,80)==0
    u.jam=True;z.poke(0xc100,b'x')
    assert z.call('_tcp_send',0xc100,1)==1 and z.call('_tcp_error')==2
    u.jam=False;z.call('_tcp_close')
    print('PASS: WebSocket/ESP close detected, reconnect and bounded stuck TX deadline')

if __name__=='__main__':closes_and_timeout()


def inherited_stream():
    u=UART();u.raw=True;u.payload=b'';z=BankZX(uart=u)
    assert z.call('_tcp_present')==1 and not u.raw
    z.poke(0xc100,b'host\0');assert z.call('_tcp_open',0xc100,80)==0
    print('PASS: inherited transparent session escaped before AT setup without module reset')

if __name__=='__main__':inherited_stream()


def full_ring():
    class Flood(UART):
        def __init__(self): super().__init__(); self.payload=b''
    u=Flood();z=BankZX(uart=u);z.poke(0xc100,b'host\0')
    assert z.call('_tcp_open',0xc100,80)==0
    data=bytes(range(256))*16;u.wire.extend(data)
    for i in range(32): assert z.call('_tcp_send',0xc100,0)==0
    got=bytearray()
    for i in range(300):
        n=z.call('_tcp_recv',0xc100,17);assert n!=65535;got+=z.read(0xc100,n)
        if len(got)==len(data):break
    assert got==data
    z.call('_tcp_close')
    print('PASS: full 4096-byte RX ring preserves exact binary payload')

if __name__=='__main__':full_ring()
