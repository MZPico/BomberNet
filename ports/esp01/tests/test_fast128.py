"""Bounded UART burst checks against the compiled fast 128K driver."""
from test_bank128 import UART, BankZX

class BusyUART(UART):
    def __init__(self):super().__init__();self.tx_wait=0
    def input(self,port):
        value=super().input(port)
        if port==0x133b and self.tx_wait:
            self.tx_wait-=1;value|=2
        return value
    def output(self,port,byte):
        if port==0x133b:
            assert not self.tx_wait,'driver wrote while TX busy'
            super().output(port,byte);self.tx_wait=2
        else:super().output(port,byte)

class FaultUART(UART):
    mode='normal'
    def input(self,port):
        if self.mode=='fifo' and port==0x133b:return 4
        return super().input(port)
    def output(self,port,byte):
        if port==0x133b and byte==10 and not self.left:
            cmd=bytes(self.cmd)+bytes([byte])
            if self.mode=='timeout':self.cmd.clear();return
            if self.mode in ('query_error','missing_length') and cmd==b'AT+CIPRECVLEN?\r\n':
                self.cmd.clear();self.text('ERROR\r\n' if self.mode=='query_error' else 'OK\r\n');return
        super().output(port,byte)

def busy_send():
    uart=BusyUART();z=BankZX(uart=uart);z.poke(0xc100,b'host\0')
    assert z.call('_tcp_open',0xc100,80)==0
    payload=bytes(range(256))*2+bytes(range(88));z.poke(0xc100,payload)
    assert z.call('_tcp_send',0xc100,len(payload))==0
    assert uart.sent==payload
    assert z.call('_tcp_error')==0
    print('PASS: bounded command/payload bursts preserve every byte under TX backpressure')

def faults():
    for mode,error in [('fifo',5),('timeout',2)]:
        uart=FaultUART();uart.mode=mode;z=BankZX(uart=uart)
        assert z.call('_tcp_present')==0
        assert z.call('_tcp_error')==error,(mode,z.call('_tcp_error'))
    for mode,error in [('query_error',1),('missing_length',4)]:
        uart=FaultUART();z=BankZX(uart=uart)
        z.poke(0xc100,b'host\0');assert z.call('_tcp_open',0xc100,65535)==0
        assert 'AT+CIPSTART="TCP","host",65535' in uart.commands
        z.poke(0xc100,b'x');assert z.call('_tcp_send',0xc100,1)==0
        uart.mode=mode
        # An empty/unknown notification requires the defensive length query.
        # Known +IPD lengths now bypass it.
        from zxemu import sym_from_map
        from pathlib import Path
        uart.wire.clear()
        addr=sym_from_map(Path('build/esp01-128/esp_bank.map'),'es_busy')
        z._SpectrumState__memory[z._SpectrumState__RAM_PAGE_IMAGE_OFFSETS[6]+addr-0xc000]=0
        addr=sym_from_map(Path('build/esp01-128/esp_bank.map'),'es_promptwait')
        z._SpectrumState__memory[z._SpectrumState__RAM_PAGE_IMAGE_OFFSETS[6]+addr-0xc000]=0
        addr=sym_from_map(Path('build/esp01-128/esp_bank.map'),'es_ready')
        z._SpectrumState__memory[z._SpectrumState__RAM_PAGE_IMAGE_OFFSETS[6]+addr-0xc000]=0
        addr=sym_from_map(Path('build/esp01-128/esp_bank.map'),'es_pending')
        z._SpectrumState__memory[z._SpectrumState__RAM_PAGE_IMAGE_OFFSETS[6]+addr-0xc000]=1
        for _ in range(1000):
            if z.call('_tcp_recv',0xc100,17)==65535:break
        else:raise AssertionError(mode)
        assert z.call('_tcp_error')==error,(mode,z.call('_tcp_error'))
    print('PASS: fast bank UART full, bounded timeout, query errors, framing checks')
if __name__=='__main__':busy_send();faults()

class StuckUART(UART):
    def __init__(self):
        super().__init__();self.stuck=True;self.power_cycles=0
    def output(self,port,byte):
        if port==0x713b and not byte&1:
            self.power_cycles+=1;self.stuck=False
            self.cmd.clear();self.left=0;self.wire.clear()
        if self.stuck and port==0x133b:return
        super().output(port,byte)

def recovery():
    uart=FaultUART();z=BankZX(uart=uart)
    z.poke(0xc100,b'host\0');assert z.call('_tcp_open',0xc100,80)==0
    uart.mode='fifo';assert z.call('_tcp_recv',0xc100,17)==65535
    uart.mode='normal';z.call('_tcp_close')
    assert z.call('_tcp_present')==1
    assert z.call('_tcp_open',0xc100,80)==0
    z.call('_tcp_close');assert z.call('_tcp_open',0xc100,80)==0
    uart=StuckUART();z=BankZX(uart=uart)
    assert z.call('_tcp_present')==1 and uart.power_cycles==1
    print('PASS: poisoned driver reconnects, repeat open/close, unknown module state power recovery')

if __name__=='__main__':recovery()


class DelayedAckUART(UART):
    def __init__(self):
        super().__init__();self.ack_pending=None;self.ack_delivered=False
    def text(self,s):
        if s=='\r\nSEND OK\r\n':
            self.ack_pending=s;return
        super().text(s)
    def release(self):
        assert self.ack_pending
        UART.text(self,self.ack_pending);self.ack_pending=None;self.ack_delivered=True

def async_send():
    uart=DelayedAckUART();z=BankZX(uart=uart)
    z.poke(0xc100,b'host\0');assert z.call('_tcp_open',0xc100,80)==0
    z.poke(0xc100,b'hello');assert z.call('_tcp_send',0xc100,5)==0
    assert uart.sent==b'hello' and uart.ack_pending and not uart.ack_delivered
    # Do actual game work before the ESP's completion arrives.
    z.call('_composite_frame');uart.release()
    z.poke(0xc100,b'x');assert z.call('_tcp_send',0xc100,1)==0
    uart.release();z.call('_tcp_close')
    assert z.call('_tcp_error')==0
    uart=DelayedAckUART();z=BankZX(uart=uart)
    z.poke(0xc100,b'host\0');assert z.call('_tcp_open',0xc100,80)==0
    z.poke(0xc100,b'x');assert z.call('_tcp_send',0xc100,1)==0
    uart.ack_pending='\r\nSEND FAIL\r\n';uart.release()
    for _ in range(20):
        if z.call('_tcp_recv',0xc100,17)==65535:break
    else:raise AssertionError('late SEND FAIL was lost')
    assert z.call('_tcp_error')==1
    z.call('_tcp_close');assert z.call('_tcp_open',0xc100,80)==0
    print('PASS: pending SEND OK overlaps game, serialized sends/close, late SEND FAIL recovery')

if __name__=='__main__':async_send()


class TailUART(UART):
    def __init__(self):super().__init__();self.injected=False
    def output(self,port,byte):
        if port==0x133b and byte==10 and not self.left and bytes(self.cmd).startswith(b'AT+CIPRECVDATA=') and not self.injected:
            self.payload+=bytes(range(128))*3;self.injected=True
        super().output(port,byte)

def receive_tail():
    uart=TailUART();z=BankZX(uart=uart)
    z.poke(0xc100,b'host\0');assert z.call('_tcp_open',0xc100,80)==0
    z.poke(0xc100,b'x');assert z.call('_tcp_send',0xc100,1)==0
    got=bytearray()
    for _ in range(10000):
        n=z.call('_tcp_recv',0xc100,192)
        if n==65535:break
        got.extend(z.read(0xc100,n))
    else:raise AssertionError('tail receive did not end')
    assert got==uart.payload and z.call('_tcp_error')==0
    queries=uart.commands.count('AT+CIPRECVLEN?')
    assert queries<z.call('_tcp_read_commands'),uart.commands
    print('PASS: bytes arriving after +IPD count are retained, fewer length queries than reads')

if __name__=='__main__':receive_tail()


class OldAppUART(UART):
    def __init__(self):super().__init__();self.mux=True;self.old_socket=True;self.full=True;self.reg=0
    def input(self,p):
        if p==0x133b and self.full:return 5
        return super().input(p)
    def output(self,p,v):
        if p==0x7c3b:self.reg=v
        elif p==0x7d3b and self.reg==0x30 and v==1:self.full=False;self.wire.clear()
        if p==0x133b and v==10 and not self.left:
            cmd=bytes(self.cmd)+bytes([v])
            if cmd==b'AT+CIPCLOSE=5\r\n':
                self.cmd.clear();self.commands.append(cmd.decode().strip());self.old_socket=False;self.text('OK\r\n');return
            if cmd==b'AT+CIPMUX=0\r\n' and self.old_socket:
                self.cmd.clear();self.text('ERROR\r\n');return
        super().output(p,v)

def old_application():
    uart=OldAppUART();z=BankZX(uart=uart)
    assert z.call('_tcp_present')==1 and not uart.full
    z.poke(0xc100,b'host\0');assert z.call('_tcp_open',0xc100,80)==0
    assert not uart.old_socket
    assert uart.commands.index('AT+CIPCLOSE=5')<uart.commands.index('AT+CIPMUX=0')
    print('PASS: hardware FIFO overflow recovery and old multiplexed sockets closed before setup')

if __name__=='__main__':old_application()
