"""BREAK detection, actual offline menu return and subsequent game restart."""
from test_bank128 import BankZX,S
F=S('_flush_screen')
def frames(z,n):
 for _ in range(n):z.run_until(F,20)
def offline():
 for mode in [0,1]:
  z=BankZX();z.pc=24000;frames(z,6);z.poke(S('_menu_mode'),[mode]);z.poke(S('_menu_players'),[2]);z.poke(S('_menu_net'),[0])
  z.press('SPACE');frames(z,5);z.release('SPACE');frames(z,60)
  assert z.read8(S('_title_mode'))==0
  z.press('SPACE');frames(z,3);z.release('SPACE')
  assert z.read8(S('_net_abort'))==0,'SPACE alone must not exit'
  z.press('CS');frames(z,3);z.release('CS')
  assert z.read8(S('_net_abort'))==0,'CAPS SHIFT alone must not exit'
  z.press('CS','SPACE');frames(z,6);z.release('CS','SPACE');frames(z,6)
  assert z.read8(S('_title_mode'))==1 and not z.read8(S('_net_active'))
  z.press('SPACE');frames(z,5);z.release('SPACE');frames(z,60)
  assert z.read8(S('_title_mode'))==0 and z.read8(S('_net_abort'))==0
 print('PASS: offline COOP/DM BREAK returns to menu; SPACE/SHIFT alone do not exit; next game runs without reset')
def exact_guard():
 for online in [0,1]:
  z=BankZX();z.poke(S('_net_active'),[online]);z.poke(S('_net_abort'),[0]);z.poke(S('_frame_no'),b'\x34\x12');z.poke(S('_players'),bytes(range(64)));z.press('CS','SPACE')
  z.call('_frame');assert z.read8(S('_net_abort'))==7
  assert z.read16(S('_frame_no'))==0x1234 and z.read(S('_players'),64)==bytes(range(64))
 print('PASS: BREAK exits before simulation/input transmission; no extra frame or player-state changes')
if __name__=='__main__':exact_guard();offline()
