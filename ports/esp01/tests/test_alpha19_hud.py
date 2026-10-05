"""Original HUD information/layout with score-safe three-player colors."""
import random
from pathlib import Path
import test_alpha16_128 as base
from test_bank128 import BankZX,S
previous=base.expected
def expected(z,count):
 if count<3:return previous(z,count)
 paper=z.read8(S('_zx_bar_attr'))&0xf8;out=[paper]*30
 for i,(pos,n,life,m) in enumerate([(0,1,6,1),(7,2,13,2),(15,1,21,1)]+([(22,2,28,2)] if count==4 else [])):
  ink=paper|(z.read8(S('_player_attrs')+i)&7);out[pos:pos+n]=[ink]*n;out[life:life+m]=[ink]*m
 return out
base.expected=expected
def original():
 r=random.Random(18)
 for count in [1,2,3,4]:
  a,b=BankZX(),BankZX();b.write(24000,Path('build/alpha16-reference/bomber').read_bytes(),ram_page=0)
  for case in range(64):
   records=r.randbytes(64);field=r.randbytes(1000);time=r.randrange(65536);hi=r.randrange(65536)
   for z in [a,b]:
    z.poke(S('_player_count'),[count]);z.poke(S('_players'),records);z.poke(S('_draw_buf'),field);z.poke(S('_game_mode'),[case%2]);z.poke(S('_time_left'),time.to_bytes(2,'little'));z.poke(S('_hi_score'),hi.to_bytes(2,'little'));z.call('_draw_hud')
   assert a.read(S('_draw_buf'),1000)==b.read(S('_draw_buf'),1000),(count,case)
   assert a.read16(S('_hi_score'))==b.read16(S('_hi_score'))
 for count in [3,4]:
  z=BankZX();z.poke(S('_player_count'),[count]);z.poke(S('_title_mode'),[0]);z.call('_clear_buffers');z.call('_draw_hud');z.call('_flush_screen')
  paper=z.read8(S('_zx_bar_attr'))&0xf8
  for i in range(count):
   for cell in range(10*i+2,10*i+7):
    for x in range(cell*6,cell*6+6):assert z.read8(0x5ae1+x//8)==paper,(i,x)
  z.screenshot('build/esp01-128/hud19-%d.png'%count)
 print('PASS: 256 original HUD layouts byte-for-byte; every three/four-player score glyph column black')
if __name__=='__main__':base.hud();base.scene();original()
