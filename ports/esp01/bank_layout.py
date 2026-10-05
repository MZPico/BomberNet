"""Export linked bank entry points and validate initialized image and BSS."""
import re
from pathlib import Path
ROOT=Path('build/esp01-128')
def symbols(path):
    return {n:int(v,16) for n,v in re.findall(r'^(\S+)\s+= \$([0-9A-Fa-f]+)',Path(path).read_text(),re.M)}
if __name__=='__main__':
    b=symbols(ROOT/'esp_bank.map')
    assert b['__head']==0xc000
    assert b['__BSS_END_tail']<=0xfdff, 'driver collides with its 512-byte stack reserve'
    entries={'RESET':'_esp_bank_reset','PRESENT':'_tcp_present','OPEN':'_tcp_open','SEND':'_tcp_send','RECV':'_tcp_recv','CLOSE':'_tcp_close','ERROR':'_tcp_error','CLOSED':'_tcp_peer_closed','READS':'_tcp_read_commands'}
    (ROOT/'bank_entries.h').write_text('/* Generated from esp_bank.map */\n'+''.join('#define ESP_%s 0x%04x\n'%(n,b[s]) for n,s in entries.items()))
    print('ESP bank: %d allocated bytes, %d free below SP' % (b['__BSS_END_tail']-0xc000,0xffff-b['__BSS_END_tail']))
