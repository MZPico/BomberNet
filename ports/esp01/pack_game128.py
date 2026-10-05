"""Prepare loader constants, verify maps, then pack BASIC + banked TAP."""
import sys, struct
from pathlib import Path
from bank_layout import symbols
D=Path('build/esp01-128')
ALPHA_VERSION=19
game=(D/'bomber').read_bytes(); bank=(D/'esp_bank').read_bytes()
g=symbols(D/'bomber.map'); b=symbols(D/'esp_bank.map')
assert g['__head']==24000 and g['__BSS_END_tail']<=0xfdff
for name in ['_tcp_present','_tcp_open','_tcp_send','_tcp_recv','_tcp_close','eb_return','eb_arg1','eb_sp','eb_init','_esp_stage']:
    assert g[name]<0xc000, '%s is not in fixed RAM' % name
assert g['_esp_stage']==23808 and g['_esp_stage']+192<=24000
assert b['__head']==0xc000 and b['__BSS_END_tail']<=0xfdff
assert 24000+len(game)<=g['__BSS_END_tail']
assert 0xc000+len(bank)<=b['__BSS_END_tail']
if '--prepare' in sys.argv:
    (D/'loader_sizes.inc').write_text('BANK_SIZE EQU %d\nGAME_SIZE EQU %d\n'%(len(bank),len(game)))
    sys.exit(0)
loader=(D/'loader128.bin').read_bytes()
assert len(loader)<=192

def block(flag,data):
    body=bytes([flag])+data
    check=0
    for x in body: check^=x
    body+=bytes([check])
    return struct.pack('<H',len(body))+body

def header(kind,name,data,p1,p2):
    return block(0,bytes([kind])+name.encode('ascii').ljust(10,b' ')[:10]+struct.pack('<HHH',len(data),p1,p2))

def number(n):
    return str(n).encode()+b'\x0e\x00\x00'+struct.pack('<H',n)+b'\x00'

def line(n,text):
    text+=b'\x0d'
    return struct.pack('>H',n)+struct.pack('<H',len(text))+text
basic=(line(10,b'\xfd '+number(32767))+
       line(20,b'\xef "" \xaf')+
       line(30,b'\xf9 \xc0 '+number(32768)))
tape=(header(0,f'{ALPHA_VERSION}Bomberman',basic,10,len(basic))+block(255,basic)+
      header(3,'ESP loader',loader,32768,0x8000)+block(255,loader)+
      block(255,bank)+block(255,game))
(D/f'bombernet_esp01_128_alpha{ALPHA_VERSION}.tap').write_bytes(tape)
# Re-read every TAP block and its XOR checksum, including the raw data blocks.
p=0; blocks=[]
while p<len(tape):
    n=struct.unpack_from('<H',tape,p)[0]; p+=2
    data=tape[p:p+n]; p+=n
    assert len(data)==n
    check=0
    for x in data: check^=x
    assert check==0
    blocks.append(data)
assert len(blocks)==6 and p==len(tape)
assert blocks[4][1:-1]==bank and blocks[5][1:-1]==game
print('TAP: %d bytes; loader %d, ESP image %d, game image %d' % (len(tape),len(loader),len(bank),len(game)))
