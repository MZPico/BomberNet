"""Four independent ESP devices through the real relay; simultaneous first keys."""
import sys,time
from test_bank128 import BankZX,S
cs=[BankZX(real=True) for _ in range(4)]
F=S('_flush_screen');TM=S('_title_mode');NA=S('_net_active');NC=S('_net_code')
for c in cs:c.pc=24000;c.set_breakpoint(F);c.set_breakpoint(S('_net_match_start'))
def frames(n):
 counts=[0]*4;deadline=time.time()+100
 while min(counts)<n:
  for i,c in enumerate(cs):
   pc=c.step()
   if pc==S('_net_match_start'):c.press('P')
   if pc==F:counts[i]+=1
  assert time.time()<deadline,('timeout',[(c.pc,c.read8(TM),c.read8(S('_net_abort'))) for c in cs])
def tap(i):cs[i].press('SPACE');frames(4);cs[i].release('SPACE');frames(2)
frames(4)
for i,c in enumerate(cs):
 c.poke(S('_menu_net'),[1 if i==0 else 2]);c.poke(S('_menu_local'),[1])
cs[0].poke(S('_menu_players'),[4]);cs[0].poke(S('_menu_mode'),[1]);tap(0)
code=cs[0].read(NC,4);assert code!=b'AAAA';print('four-device room',code,flush=True)
for i in range(1,4):cs[i].poke(NC,code);tap(i);frames(4);tap(i);frames(12)
frames(60)
for i in range(1,4):tap(i)
tap(0)
for _ in range(300):
 if all(c.read8(TM)==0 and c.read8(NA) for c in cs):break
 frames(1)
assert all(c.read8(TM)==0 and c.read8(NA) for c in cs)
print('four started',[(c.read8(S('_net_slot')),c.read8(S('_esp_input_delay')),c.read(S('_net_table'),4).hex()) for c in cs],flush=True)
seen=[{} for _ in cs]
for _ in range(160):
 frames(1)
 for i,c in enumerate(cs):
  f=c.read16(S('_frame_no'))
  if f>=16:seen[i][f//16*16]=c.read16(S('_state_hash'))
 assert not any(c.read8(S('_net_abort')) for c in cs),[(c.read16(S('_frame_no')),c.read8(S('_net_abort'))) for c in cs]
common=set.intersection(*(set(s) for s in seen));assert common
for f in sorted(common):assert len({s[f] for s in seen})==1,(f,[s[f] for s in seen])
print('PASS: four independent devices, all first keys held before match start,',len(common),'matching hash frames; no DESYNC/abort',flush=True)

# Leaving must use the ordinary socket cleanup and return directly to menu.
cs[0].release('P');cs[0].press('CS','SPACE');frames(40);cs[0].release('CS','SPACE');frames(10)
assert cs[0].read8(TM)==1 and cs[0].read8(NA)==0
print('BREAK cleanup',cs[0].uart.sock,cs[0].uart.commands[-12:],flush=True)
assert cs[0].uart.sock is None and 'AT+CIPCLOSE' in cs[0].uart.commands
print('PASS: network BREAK returns directly to menu and closes ESP socket',flush=True)
old=cs[0].read(NC,4);tap(0);frames(30)
assert cs[0].read(NC,4)!=old and cs[0].uart.sock is not None
print('PASS: HOST creates a fresh room after BREAK without reset',flush=True)
