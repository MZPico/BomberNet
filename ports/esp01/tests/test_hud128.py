"""Stable HUD colors and no intermediate attribute writes on native Z80."""
from pathlib import Path
from test_bank128 import BankZX,S
class TraceZX(BankZX):
 def __init__(self):
  self.expected=None;self.attr_writes=[];self.trapped=set();super().__init__()
  data=Path('build/esp01-128/bomber').read_bytes()
  # Trap all LD (HL),A instructions in the renderer plus new HUD helper.
  for lo,hi in [(S('_flush_screen'),S('h16_two'))]:
   for addr in range(lo,hi):
    if data[addr-24000]==0x77:self.set_breakpoint(addr);self.trapped.add(addr)
 def on_breakpoint(self):
  if self.pc in self.trapped and self.expected is not None and 0x5ae1<=self.hl<0x5aff:
   value=self.af>>8;want=self.expected[self.hl-0x5ae1]
   assert value==want,('temporary HUD attribute',hex(self.pc),hex(self.hl),value,want)
   self.attr_writes.append((self.hl,value))
  super().on_breakpoint()
def expected(z,count):
 base=z.read8(S('_zx_bar_attr'))&0xf8;out=[base]*30
 segments=[] if count==1 else [(0,2,7,2),(9,3,17,2)] if count==2 else [(0,1,6,1),(7,2,13,2),(15,1,21,1)]+([(22,2,28,2)] if count==4 else [])
 for i,(label,n,life,m) in enumerate(segments):
  color=base|(z.read8(S('_player_attrs')+i)&7)
  out[label:label+n]=[color]*n;out[life:life+m]=[color]*m
 return out
def hud():
 for online in [0,1]:
  for count in [1,2,3,4]:
   z=TraceZX();z.poke(S('_player_count'),[count]);z.poke(S('_title_mode'),[0]);z.poke(S('_net_active'),[online]);z.call('_clear_buffers')
   for i in range(4):z.poke(S('_players')+16*i+6,[3]);z.poke(S('_players')+16*i+10,(1234+i).to_bytes(2,'little'))
   z.call('_draw_hud');z.call('_flush_screen');want=expected(z,count)
   assert list(z.read(0x5ae1,30))==want,(online,count,list(z.read(0x5ae1,30)),want)
   if online:z.screenshot('build/esp01-128/hud16-%d.png'%count)
   z.expected=want
   # Changed scores, lives/wins and time force actual HUD bitmap redraws.
   for mode in [0,1]:
    for score in [0,9,10,99,100,999,1000,65535]:
     z.poke(S('_game_mode'),[mode]);z.poke(S('_time_left'),score.to_bytes(2,'little'))
     for i in range(4):z.poke(S('_players')+16*i+6,[score%10]);z.poke(S('_players')+16*i+12,[score%7]);z.poke(S('_players')+16*i+10,score.to_bytes(2,'little'))
     z.call('_draw_hud');z.call('_flush_screen');assert list(z.read(0x5ae1,30))==want
   assert not z.attr_writes,('unchanged attributes should never be rewritten',online,count,z.attr_writes)
 print('PASS: 1-4 player HUD in online/offline modes; player labels and life/win icons+counts colored, scores/time black, purple paper')
 print('PASS: changed HUD glyphs never write temporary attributes; stable attributes are not rewritten')

if __name__=="__main__":hud()
