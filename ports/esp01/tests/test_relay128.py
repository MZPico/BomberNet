"""Compiled game's WebSocket handshake/create/close through UART + local relay."""
import os, socket, subprocess, sys, time
from pathlib import Path
from test_bank128 import BankZX, D, S

def main():
    log=open(D/'relay-test.log','w')
    env=dict(os.environ,TCP_PORT='8766')
    module='relay_reordered13:app' if os.environ.get('ESP_RELAY_REORDER')=='1' else 'relay:app'
    appdir='ports/esp01/tests' if os.environ.get('ESP_RELAY_REORDER')=='1' else 'relay'
    server=subprocess.Popen([sys.executable,'-m','uvicorn',module,'--app-dir',appdir,'--host','127.0.0.1','--port','8765','--log-level','warning'],stdout=log,stderr=log,env=env)
    try:
        for _ in range(200):
            try:
                s=socket.create_connection(('127.0.0.1',8765),timeout=.1);s.close();break
            except OSError: time.sleep(.025)
        else:raise AssertionError('relay did not start')
        z=BankZX(real=True);z.pc=24000;z.run_until(S('_flush_screen'),20)
        assert z.read8(S('_net_device'))==3
        # Pointers above C000 exercise actual game variables in the caller bank.
        settings=S('_outbuf');code=S('_net_code');slot=S('_net_slot')
        z.poke(settings,b'\x01\x02')
        # Use the documented build id in this exact build.
        build=S('BUILD_ID') if 'BUILD_ID' in (D/'bomber.map').read_text() else 0x0604
        result=z.call('_net_create',build,2,settings,2,code,slot)
        assert result==0, ('create',result,z.uart.commands[-8:])
        room=z.read(code,4).decode();assert len(room)==4 and room.isalpha()
        assert z.read8(slot)==0
        assert z.call('_tcp_error')==0
        z.call('_net_leave')
        assert z.call('_net_create',build,2,settings,2,code,slot)==0, 'reconnect after leave'
        # Complete a new HOST room with a peer, including link ping/pong and READY.
        peer=BankZX(real=True);peer.pc=24000;peer.run_until(S('_flush_screen'),20)
        peer.poke(code,z.read(code,5));buf=settings+112
        assert peer.call('_net_join',build,code,slot,S('_net_slots'),settings,buf)==0
        z.poke(buf,bytes([1,42]));assert z.call('_net_msg_send',255,buf,2)==0
        for _ in range(1000):
            if peer.call('_net_msg_recv',buf+16,buf+20)>=2:break
        else:raise AssertionError('new HOST ping not received')
        assert peer.read(buf+20,2)==bytes([1,42])
        peer.poke(buf,bytes([2,42]));peer.call('_net_msg_send',0,buf,2)
        for _ in range(1000):
            if z.call('_net_msg_recv',buf+16,buf+20)>=2:break
        else:raise AssertionError('new HOST pong not received')
        assert z.read(buf+20,2)==bytes([2,42])
        peer.call('_net_ready',1,buf,buf+2)
        for _ in range(1000):
            z.call('_net_ready',1,buf,buf+2)
            if z.read16(buf)!=65535:break
        else:raise AssertionError('new HOST READY did not start')
        z.call('_net_leave');peer.call('_net_leave')
        print('PASS: re-HOST with peer link ping/pong and READY start')
        subprocess.run([sys.executable,os.environ.get('ESP_MATCH','ports/esp01/tests/match128.py'),'60'],check=True,timeout=180)
        print('PASS: real game WebSocket handshake, relay room creation %s, bank-0 pointers and leave'%room)
    finally:
        server.terminate()
        try:server.wait(timeout=3)
        except subprocess.TimeoutExpired:server.kill();server.wait()
        log.close()

if __name__=='__main__':main()
