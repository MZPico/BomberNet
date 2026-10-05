"""Stable HUD attribute overlay on exact alpha16; no simulation/timing changes."""
from pathlib import Path
import hashlib,re,struct,sys,subprocess,shutil
sys.path.insert(0,'ports/esp01');from bank_layout import symbols
D=Path('build/esp01-128');R=Path('build/alpha18-reference')
M=symbols(R/'bomber.map');g=bytearray((R/'bomber').read_bytes())
assert hashlib.sha256(g).hexdigest()=='2f8fc4cfae6e9601034cbf346e61620402c10b6bf2358861ffb874ca209438c0'
M.update(fz_bar=0xc1b3,h16_two=0xb172,h16_compact=0xb190,h17_three=0xb1ae)
eq=''.join(f'{k} EQU {v}\n' for k,v in M.items() if re.fullmatch(r'\w+',k) and v<=65535)
def asm(body,at):
 Path('/tmp/a16.a80').write_text(f' ORG {at}\n OUTPUT "/tmp/a16.bin"\n'+eq+body+'\n OUTEND\n')
 r=subprocess.run([sys.argv[1],'/tmp/a16.a80'],capture_output=True,text=True);assert not r.returncode,r.stdout+r.stderr
 return Path('/tmp/a16.bin').read_bytes()
def put(at,b):g[at-24000:at-24000+len(b)]=b
# Four-player cells shared with score stay black, matching three-player policy.
four=[0]*30
for index,start,n in [(1,0,1),(1,6,1),(2,7,2),(2,13,2),(3,15,1),(3,21,1),(4,22,2),(4,28,2)]:four[start:start+n]=[index]*n
put(0xb190,bytes(four))
# New BREAK guard occupies only the free tail after the status-row guard.
b=asm(Path('ports/esp01/alpha19_break.a80').read_text(),0xb2d7)
assert 0xb2d7+len(b)<=M['_check_pickups'];put(0xb2d7,b)
assert g[M['_frame']-24000:M['_frame']-23997]==bytes.fromhex('c3e364')
put(M['_frame'],bytes.fromhex('c3d7b2'))
# BREAK uses the ordinary net_match_end cleanup, then skips PRESS FIRE box.
assert g[0x8c6a-24000:0x8c70-24000]==bytes.fromhex('21eddec3738c')
put(0x8c6a,bytes.fromhex('c37d8c000000'))
bank=bytearray((R/'esp_bank').read_bytes());B=symbols(R/'esp_bank.map');at=B['_command_close']-0xc000
assert bank[at:at+4]==b'\x3a'+struct.pack('<H',B['es_opened'])+b'\xb7'
assert bank[at+4]==0x28
bank[at:at+6]=bytes(6) # always attempt CIPCLOSE, even if parser cleared opened
(D/'esp_bank').write_bytes(bank)
(D/'bomber').write_bytes(g)
for n in ['bomber.map','esp_bank.map']:shutil.copy(R/n,D/n)
print('four-player color-only table and BREAK guard',len(b),'bytes')
