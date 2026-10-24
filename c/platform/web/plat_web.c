/* The browser (docs/port-web.md): the game itself compiled to WebAssembly,
 * no emulated MZ-800 under it.
 *
 * The core's main loop never returns and waits by calling plat_frame_sync;
 * Emscripten's Asyncify lets those waits yield to the browser
 * (emscripten_sleep), so the page keeps drawing, the keys come in and the
 * WebSocket delivers. A frame is FRAME_MS of the browser's clock.
 *
 * Screen: the same 40 x 25 cells of 8 x 8 as the Amiga, the ST and the PC,
 * from the Amiga's tables (c/platform/amiga/tables.c: the MZ's glyphs, its
 * 16 colours); each changed cell is written into a 320 x 200 RGBA buffer,
 * which the page puts on its canvas once a frame (web_present).
 * Keys: the page sends KeyboardEvent codes as the small numbers below
 * (web_key); gamepads (the Gamepad API) as key masks (web_pad).
 * Sound: a square wave of 1108400 / ratio Hz (the MZ's 8253) through Web
 * Audio, for len * 0.93 ms. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <emscripten.h>
#include "game.h"
#include "tables.h"

/* ---- keys: the page's numbers ---- */
#define K_UP 1
#define K_DOWN 2
#define K_LEFT 3
#define K_RIGHT 4
#define K_SPACE 5
#define K_RETURN 6
#define K_ESC 7
#define K_BACKSPACE 8
#define K_DELETE 9                        /* 'A'..'Z': the letters */
static uint8_t keydown[128];
static uint8_t pad[2];                    /* the gamepads' key masks */

EMSCRIPTEN_KEEPALIVE void web_key(int code, int down) { if (code > 0 && code < 128) keydown[code] = (uint8_t)(down != 0); }
EMSCRIPTEN_KEEPALIVE void web_pad(int n, int mask) {
  if (n < 0 || n > 1) return;
  pad[n] = (uint8_t)mask;
  if (joy_type == JOY_NONE) joy_type = 1;   /* a gamepad: the JOYSTICK row offers it */
}

/* ---- the screen ---- */
#define W 320
#define H 200
static uint32_t rgba[W * H];              /* what the page draws (web_present) */
static uint32_t colours[16];
static uint8_t mode = 0xff;

static void cell(uint8_t x, uint8_t y, uint8_t g, uint8_t col) {
  const uint8_t *s = amiga_glyphs + ((uint16_t)g << 3);
  uint32_t *d = rgba + (uint32_t)y * 8 * W + (uint32_t)x * 8, fg = colours[col & 15], bg = colours[0];
  uint8_t l, b;
  for (l = 0; l < 8; l++, d += W - 8)
    for (b = 0x80; b; b >>= 1) *d++ = (s[l] & b) ? fg : bg;
}

static uint8_t fire_held;                 /* a fire key down when a game started: ignored until released */
static uint8_t fire_down(void) { return keydown[K_SPACE] | keydown[K_RETURN] | keydown['E'] | ((pad[0] | pad[1]) & KEY_SPACE); }

static void set_mode(uint8_t title) {
  mode = title;
  memset(shadow_vram, 0xff, SCREEN_CELLS);
  /* the press that started the game (the title reads the keys itself, the
   * game's input only from its first frame) would drop a bomb at once: the
   * switch is instant here, so the key is still down */
  if (!title) fire_held = fire_down() != 0;
}

EM_JS(int, js_runahead_cap, (void), { return Module.bombernetRunahead === undefined ? 8 : Module.bombernetRunahead | 0; });
EM_JS(void, js_present, (const uint32_t *p), {
  if (typeof Module.bombernetPresent === 'function') Module.bombernetPresent(p);
});
EM_JS(void, js_hash, (int frame, int hash, const uint8_t *map), {   /* the tests' (?trace=1): every 16th frame's hash and map */
  if (!Module.bombernetTrace) return;
  (Module.bombernetHashes = Module.bombernetHashes || []).push([frame, hash]);
  (Module.bombernetMaps = Module.bombernetMaps || {})[frame] = Array.from(HEAPU8.subarray(map, map + 1000));
});

#ifdef WEB_TEST
#include "rec_data.h"                     /* a recording to replay (tools/build_web.py --test) */
static uint16_t rec_pos = 16, frames;
static uint16_t test_bad, test_n;
#endif

void flush_screen(void) {
  uint16_t i;
  uint8_t a;
#ifdef WEB_TEST
  frames++;
  if (!title_mode && rec_pos + 6 <= REC_LEN) {   /* the hash checked */
    if (hash_period && (rec[rec_pos + 4] | (uint16_t)(rec[rec_pos + 5] << 8)) != state_hash && !test_bad)
      test_bad = rec_pos;
    memcpy(replay_keys, rec + rec_pos, 4); rec_pos += 6; test_n++;
  }
#endif
  if (mode != title_mode) set_mode(title_mode);
  if (title_mode) {
    for (i = 0; i < SCREEN_CELLS; i++) {
      a = draw_buf[i]; draw_buf[i] = C_SPACE;
      if (a == shadow_vram[i]) continue;
      shadow_vram[i] = a;
      cell((uint8_t)(i % SCREEN_W), (uint8_t)(i / SCREEN_W), amiga_title_glyph[a], amiga_title_colour[a]);
    }
  } else {
    for (i = 0; i < HUD_ROW * SCREEN_W; i++) {   /* rows 0-23 (composite_frame writes them all), then the status line */
      a = draw_buf[i];
      if (a == shadow_vram[i]) continue;
      shadow_vram[i] = a;
      cell((uint8_t)(i % SCREEN_W), (uint8_t)(i / SCREEN_W), amiga_game_glyph[a], amiga_game_colour[a]);
    }
    for (i = HUD_ROW * SCREEN_W; i < SCREEN_CELLS; i++) {
      a = draw_buf[i]; draw_buf[i] = C_SPACE;
      if (a == shadow_vram[i]) continue;
      shadow_vram[i] = a;
      cell((uint8_t)(i - HUD_ROW * SCREEN_W), HUD_ROW, amiga_game_glyph[a], amiga_game_colour[a]);
    }
  }
#ifndef WEB_TEST
  if (!title_mode && net_active && frame_no % 16 == 1) js_hash(frame_no, state_hash, snap_map());   /* the agreed frame's map, not a guess */   /* (frame - 1's hash, computed at its end) */
#endif
#ifdef WEB_TEST
  if (!title_mode && rec_pos + 6 > REC_LEN) {   /* the recording replayed: the result, done */
    printf("frames %u bad %u\n", test_n, test_bad);
    emscripten_force_exit(test_bad ? 1 : 0);
  }
#endif
}

/* ---- for the tests (tools/web_match.py, tools/crossplay.py): the state as
 * the other machines' test logs give it, and the hashed frames' hashes ---- */
EMSCRIPTEN_KEEPALIVE const char *web_state(void) {
  static char s[192];
  uint8_t i, nb = 0;
  for (i = 0; i < BOMB_SLOTS; i++) if (bombs[i].state) nb++;
  snprintf(s, sizeof(s), "frame %u title %u code %.4s active %u abort %u slot %u delay %u table %02x%02x%02x%02x hash %04x"
           " p1x %u p1y %u p1s %u bombs %u alt %u keys %02x lead %u",
           frame_no, title_mode, net_code[0] ? net_code : "    ", net_active, net_abort, net_slot, net_delay,
           net_table[0], net_table[1], net_table[2], net_table[3], state_hash,
           players[0].x, players[0].y, players[0].state, nb, kbd_alt_fire, plat_keys_a(), net_active ? net_runahead_lead() : 0);
  return s;
}

EMSCRIPTEN_KEEPALIVE void web_poke(int id, int v) {   /* the tests set the menu as the other machines' are set in memory */
  switch (id) {
  case 0: menu_mode = (uint8_t)v; break;
  case 1: menu_players = (uint8_t)v; break;
  case 2: menu_net = (uint8_t)v; break;
  case 3: menu_local = (uint8_t)v; break;
  default: if (id >= 4 && id < 8) net_code[id - 4] = (char)v; break;
  }
}

void plat_player_colour(uint8_t x, uint8_t y, uint8_t player) {
  player = (player & PLAYER_FLASH) ? 1 : (uint8_t)(8 + (player & 3));   /* 1: the bombs' blue */
  cell(x, y, amiga_game_glyph[shadow_vram[(uint16_t)y * SCREEN_W + x]], player);
}

void plat_hud_colour(uint8_t x, uint8_t n, uint8_t player, uint8_t blank) {
  (void)blank;
  for (; n; n--, x++) cell(x, HUD_ROW, amiga_game_glyph[shadow_vram[HUD_ROW * SCREEN_W + x]], (uint8_t)(8 + player));
}

/* ---- menus and input ---- */
const char *const plat_joy_names[PLAT_JOY_TYPES] = {"NONE    ", "GAMEPAD "};
const char *const plat_kbd_a_alt_name = "CURSOR AND RET  ";
const char *plat_input_name(uint8_t input) {
  switch (input) {
  case INPUT_KBD_A: return "CURSOR AND SPACE";
  case INPUT_KBD_B: return "WASD AND E      ";
  case INPUT_JOY1:  return "GAMEPAD 1       ";
  case INPUT_JOY2:  return "GAMEPAD 2       ";
  }
  return "";
}
uint8_t plat_input_allowed(uint8_t input) {
  if (input == INPUT_JOY1 || input == INPUT_JOY2) return joy_type != JOY_NONE;
  return 1;
}

uint8_t plat_keys_a(void) {
  uint8_t k = 0;
#ifdef WEB_TEST
  return title_mode && (frames & 1) ? KEY_SPACE : 0;
#endif
  if (keydown[K_UP]) k |= KEY_UP;
  if (keydown[K_DOWN]) k |= KEY_DOWN;
  if (keydown[K_LEFT]) k |= KEY_LEFT;
  if (keydown[K_RIGHT]) k |= KEY_RIGHT;
  if (kbd_alt_fire ? keydown[K_RETURN] : keydown[K_SPACE]) k |= KEY_SPACE;
  if (fire_held) { if (fire_down()) k &= (uint8_t)~KEY_SPACE; else fire_held = 0; }
  return k;
}

uint8_t plat_keys_b(void) {
  uint8_t k = 0;
  if (keydown['W']) k |= KEY_UP;
  if (keydown['S']) k |= KEY_DOWN;
  if (keydown['A']) k |= KEY_LEFT;
  if (keydown['D']) k |= KEY_RIGHT;
  if (keydown['E']) k |= KEY_SPACE;
  if (fire_held) { if (fire_down()) k &= (uint8_t)~KEY_SPACE; else fire_held = 0; }
  return k;
}

uint8_t plat_joy(uint8_t n) {
  uint8_t k = n < 2 ? pad[n] : 0;
  if (fire_held) { if (fire_down()) k &= (uint8_t)~KEY_SPACE; else fire_held = 0; }
  return k;
}

uint8_t plat_key_char(void) {             /* text entry: letters, BACKSPACE or DELETE deletes, ESC cancels */
  uint8_t c;
  if (keydown[K_ESC]) return 0x1b;
  if (keydown[K_BACKSPACE] || keydown[K_DELETE]) return 8;
  for (c = 'A'; c <= 'Z'; c++) if (keydown[c]) return c;
  return 0;
}

uint8_t plat_break(void) { return keydown[K_ESC]; }

/* ---- time ---- */
static double next_frame;
uint8_t plat_frame_idle;

void plat_frame_sync(void) {              /* FRAME_MS of the browser's clock; the page is drawn meanwhile */
  double now;
#ifdef WEB_TEST
  emscripten_sleep(0);                    /* as fast as it goes, but the runtime gets its turn */
  return;
#endif
  js_present(rgba);
  now = emscripten_get_now();
  if (next_frame == 0 || now - next_frame > FRAME_MS) next_frame = now;   /* the start, or far behind (a hidden tab): no rush */
  next_frame += FRAME_MS;
  plat_frame_idle = next_frame - now < 1 ? 0 : next_frame - now > 255 ? 255 : (uint8_t)(next_frame - now);   /* the run-ahead's lead */
  if (next_frame - now >= 1) emscripten_sleep((unsigned)(next_frame - now));
}

void plat_delay(void) {                   /* a TV frame's worth: 20 ms */
#ifdef WEB_TEST
  emscripten_sleep(0);
  return;
#endif
  js_present(rgba);
  emscripten_sleep(20);
}

/* ---- sound ---- */
EM_JS(void, js_tone, (double hz, double ms), {
  if (typeof Module.bombernetTone === 'function') Module.bombernetTone(hz, ms);
});

void plat_tone(uint16_t ratio, uint8_t len) {
#ifdef WEB_TEST
  (void)ratio; (void)len;
  return;
#endif
  if (!ratio) return;
  js_tone(1108400.0 / ratio, len * 238.0 / 256.0 + 1);
}

/* ---- start ---- */
void plat_init(void) {
  uint8_t i;
  net_runahead_cap = (uint8_t)js_runahead_cap();   /* ?runahead=0: plain lockstep (the tests compare both) */
  for (i = 0; i < 16; i++) {              /* the palette, 4 bits a gun -> RGBA in memory order (R, G, B, A) */
    uint16_t c = amiga_palette[i];
    uint32_t r = ((c >> 8) & 15) * 17, g = ((c >> 4) & 15) * 17, b = (c & 15) * 17;
    colours[i] = r | g << 8 | b << 16 | 0xff000000u;
  }
  joy_type = JOY_NONE;
  set_mode(1);
#ifdef WEB_TEST
  match_seed = rec[4] | (uint16_t)(rec[5] << 8);
  menu_mode = rec[6]; menu_players = rec[7];
  memcpy(menu_inputs, rec + 8, 4);
  hash_period = rec[12];
  replay_active = 1;
#endif
}

void bomber_main(void);
int main(void) {                          /* the core's main is compiled as bomber_main (tools/build_web.py) */
  bomber_main();
  return 0;
}
