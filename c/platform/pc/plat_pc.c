/* IBM PC (DOS): the platform layer (core/platform.h), docs/port-pc.md. A
 * real-mode DOS program (OpenWatcom, small model) for any PC with CGA,
 * EGA or VGA.
 *
 * Screen: mode 0Dh, 320 x 200 in 16 colours, 40 x 25 cells of 8 x 8; the
 * four bitplanes in parallel at A000:0000, 40 bytes a line. A cell is
 * written plane by plane through the Map Mask: the planes of its colour
 * get the glyph's 8 rows, the others zeros (16 memory writes, 2 port
 * writes). An earlier way, through Set/Reset and the latches (8 Bit Mask
 * writes), striped the black on a user's DOSBox-X: no latches now. The glyphs, colours and palette are the Amiga's
 * tables (the same 16-colour cells). A VGA shows the palette as it is
 * (its DAC); an EGA in a 200-line mode has only the CGA's 16 colours,
 * chosen for each of the game's by hand (ega_colours).
 *
 * A CGA: mode 4, 320 x 200 in 4 colours, the even lines at B800:0000 and
 * the odd ones at B800:2000, 80 bytes a line; a glyph's row is one word,
 * each bit two (cga_expand), masked to the colour. The title in palette 1
 * (cyan, magenta, white: the logo keeps its magenta), the game in palette
 * 0 (green, red, yellow: the field's red). Solid is a player, textured an
 * enemy: the players 1 green, 2 yellow, 3 and 4 the same colours hollow
 * (each block of the figure with its centre cut out), their status-line
 * marks inverse; the enemies' four colours as checkerboards of two
 * (red solid, olive, lime, dim yellow), the bombs red and yellow (the
 * game's blue would be green: player 1's); the text always solid. On the
 * title the legend's figures (mosaic glyphs) textured the same way.
 *
 * Time: the timer chip's channel 0 at about 1 kHz (the BIOS's own 18.2 Hz
 * tick still called on time), a game frame being 60 of its ticks; finer
 * times from its counter (1.19 MHz).
 *
 * Input: the keyboard's interrupt (INT 9) is the game's while it runs: the
 * make and break codes from port 60h (set 1, the same numbers as the Atari
 * ST's) keep the state of every key; the E0 prefix of an enhanced
 * keyboard's cursor keys is skipped. The game port (201h): two joysticks,
 * their axes timed against the centre found at the start.
 *
 * Sound: the speaker, which is the timer chip's channel 2: the MZ-800 has
 * the same chip, so a tone's divider is the MZ's scaled from its 1.1084 MHz
 * to the PC's 1.19318; the 1 kHz interrupt ends it on time. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include "game.h"
#include "tables.h"

#define PIT_HZ 1193182UL
#define TICK_DIV 1193u                    /* the timer at 1000.15 Hz */
#define GC_INDEX 0x3ce                    /* the EGA's graphics controller */
#define GC_DATA 0x3cf
#define SEQ_INDEX 0x3c4
#define SEQ_DATA 0x3c5

static volatile uint8_t far *vram;
static uint8_t col_map[16];               /* the game's colour -> the colour register drawn with (the nearest CGA colour on an EGA) */
static uint8_t old_mode;
static void (__interrupt __far *old_int8)(void);
static volatile uint32_t ticks;           /* timer interrupts since the start, ~1 ms each */
static volatile uint16_t bios_acc;        /* the BIOS's tick owed: 65536 counts of the timer's input */
static volatile uint16_t tone_ms;         /* the tone's time left */
static uint16_t opl;                      /* an AdLib / Sound Blaster FM chip's port (388h); 0: the speaker */
static uint8_t opl_b0;                    /* the tone's B0h register (its octave, the frequency's top bits) */
static void opl_write(uint8_t reg, uint8_t v);
static uint8_t started;
static void (__interrupt __far *old_int9)(void);
static volatile uint8_t keydown[128];

static void __interrupt __far timer_isr(void) {
  ticks++;
  if (tone_ms && !--tone_ms) {
    if (opl) opl_write(0xb0, opl_b0);     /* the key off */
    else outp(0x61, inp(0x61) & 0xfc);    /* the speaker off */
  }
  bios_acc += TICK_DIV;
  if (bios_acc < TICK_DIV) _chain_intr(old_int8);   /* carried past 65536: the BIOS's 18.2 Hz tick is due (it ends the interrupt) */
  outp(0x20, 0x20);
}

static uint32_t now(void) {               /* ticks: 32 bits read whole (a 16-bit CPU reads them in two) */
  uint32_t t;
  _disable(); t = ticks; _enable();
  return t;
}

/* the keyboard: a byte from port 60h, acknowledged (the XT's way, port 61h
 * bit 7, harmless on an AT), the interrupt ended; the BIOS never sees it */
static void __interrupt __far key_isr(void) {
  static uint8_t prefix;
  uint8_t b = inp(0x60), k = inp(0x61);
  outp(0x61, k | 0x80); outp(0x61, k);
  if (b == 0xe0 || b == 0xe1) prefix = 1;
  else {
    if (!(prefix && (b & 0x7f) == 0x2a)) keydown[b & 0x7f] = !(b & 0x80);   /* (an E0 2A: the enhanced keyboard's fake shift) */
    prefix = 0;
  }
  outp(0x20, 0x20);
}
#define K_ESC 0x01
#define K_BACKSPACE 0x0e
#define K_RETURN 0x1c
#define K_SPACE 0x39
#define K_UP 0x48
#define K_LEFT 0x4b
#define K_RIGHT 0x4d
#define K_DOWN 0x50
#define K_DELETE 0x53

#ifdef PC_PLAYTEST
static FILE *playlog;                     /* tools/pc_play.py reads it: the players' places every frame */
#endif
uint32_t pc_ms(void) { return now(); }    /* the network driver's clock */

/* ---- the command line ----
 *   /RELAY=host[:port]   the relay (port 8765 if not given; the built-in one is api.mzpico.com:80)
 *   /CGA /EGA /VGA       the adapter's look, else the one found (a CGA's on any EGA or VGA, an EGA's on a VGA)
 *   /COM1 /COM2          the modem's port, else both looked at
 *   /BAUD=n              the modem's speed (19200, 115200 ...), else both tried
 *   /NOSOUND             no sound
 *   /SPEAKER             the PC speaker even where an AdLib or Sound Blaster is found
 *   /NET=PKT, /NET=MODEM Ethernet (a packet driver) or the modem only, else Ethernet where a driver is loaded
 *   /IP= /MASK= /GW= /DNS= Ethernet's addresses, else DHCP's
 * (a dash serves as well as the slash; upper or lower case) */
uint8_t pc_modem_port;                    /* 0: either; 1, 2: COM1, COM2 (tcp_modem.c) */
uint32_t pc_modem_baud;                   /* 0: 19200 then 115200 */
uint8_t pc_net;                           /* 0: either; 'P', 'M' (tcp_pc.c) */
uint8_t pc_ip[4], pc_mask[4], pc_gw[4], pc_dns[4];   /* 0.0.0.0: DHCP's (tcp_pkt.c) */
static uint8_t force_adapter;             /* 0: the one found; 'C', 'E', 'V' */
static uint8_t no_sound, no_fm;

#ifndef PC_TEST
uint8_t pc_parse_ip(const char *s, uint8_t *out);   /* tcp_pkt.c: a dotted quad, 1 */
#endif
static uint8_t opt(const char *arg, const char *name) {   /* arg is /NAME or -NAME (any case): 1 */
  if (*arg != '/' && *arg != '-') return 0;
  arg++;
  while (*name) { if ((*arg & 0xdf) != *name && *arg != *name) return 0; arg++; name++; }
  return *arg == 0 || *arg == '=';
}

static void options(void) {
  int k;
  for (k = 1; k < __argc; k++) {
    const char *a = __argv[k], *v = strchr(a, '=');
    if (opt(a, "CGA")) force_adapter = 'C';
    else if (opt(a, "EGA")) force_adapter = 'E';
    else if (opt(a, "VGA")) force_adapter = 'V';
    else if (opt(a, "COM1")) pc_modem_port = 1;
    else if (opt(a, "COM2")) pc_modem_port = 2;
    else if (opt(a, "BAUD") && v) pc_modem_baud = (uint32_t)atol(v + 1);
    else if (opt(a, "NOSOUND")) no_sound = 1;
    else if (opt(a, "SPEAKER")) no_fm = 1;
#ifndef PC_TEST
    else if (opt(a, "RELAY") && v) {        /* host or host:port, the host kept where it is (argv lives on) */
      extern const char *net_relay_host;
      extern uint16_t net_relay_port;
      char *colon = strrchr((char *)v + 1, ':');
      net_relay_port = 8765;
      if (colon) { *colon = 0; net_relay_port = (uint16_t)atoi(colon + 1); }
      net_relay_host = v + 1;
    }
    else if (opt(a, "NET") && v && (v[1] & 0xdf) == 'P') pc_net = 'P';
    else if (opt(a, "NET") && v && (v[1] & 0xdf) == 'M') pc_net = 'M';
    else if (opt(a, "IP") && v && pc_parse_ip(v + 1, pc_ip)) ;
    else if (opt(a, "MASK") && v && pc_parse_ip(v + 1, pc_mask)) ;
    else if (opt(a, "GW") && v && pc_parse_ip(v + 1, pc_gw)) ;
    else if (opt(a, "DNS") && v && pc_parse_ip(v + 1, pc_dns)) ;
#endif
    else {
      printf("BomberNet for DOS\n"
             "  /RELAY=host[:port]   the relay (port 8765 if not given)\n"
             "  /CGA /EGA /VGA       the adapter's look (else the one found)\n"
             "  /COM1 /COM2          the WiFi modem's port (else both are tried)\n"
             "  /BAUD=n              the modem's speed (else 19200, then 115200)\n"
             "  /NOSOUND             no sound\n"
             "  /SPEAKER             the PC speaker (else an AdLib or Sound Blaster's FM)\n"
             "  /NET=PKT /NET=MODEM  Ethernet or the modem only (else Ethernet\n"
             "                       where a packet driver is loaded)\n"
             "  /IP= /MASK= /GW= /DNS=  the addresses (else DHCP's)\n");
      exit(opt(a, "?") || opt(a, "H") ? 0 : 1);
    }
  }
}
void (*pc_net_cleanup)(void);             /* the network driver's: hung up, its UART's interrupt given back */
#ifdef PC_NETTEST
#include "netdev.h"
static FILE *netlog;                      /* C:\NET.LOG: tools/pc_match.py, tools/crossplay.py read it, the room's state every frame */
static char __far log_buf[16384];         /* formatted by hand, written every 32 frames (far: the data segment is 64 KB) */
static uint16_t log_len;
static uint32_t stall_at;                 /* when the game frame last moved on */
static uint16_t logged_frame = 0xffff;    /* the game frame last logged */
static void log_out(void) {
  static char chunk[512];
  uint16_t i = 0, n;
  while (i < log_len) {                   /* through a near buffer: fwrite takes near pointers in the small model */
    n = (uint16_t)(log_len - i > sizeof(chunk) ? sizeof(chunk) : log_len - i);
    _fmemcpy(chunk, log_buf + i, n);
    fwrite(chunk, 1, n, netlog);
    i += n;
  }
  fflush(netlog); _dos_commit(fileno(netlog));
  log_len = 0;
}
static void log_s(const char *t) { while (*t) log_buf[log_len++] = *t++; }
static void log_dec(uint32_t v) {         /* 16-bit arithmetic where it fits: a 32-bit division is a library call on the 8086 */
  char d[10]; uint8_t n = 0;
  if (v <= 0xffffu) { uint16_t w = (uint16_t)v; do { d[n++] = (char)('0' + w % 10); w /= 10; } while (w); }
  else do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v);
  while (n) log_buf[log_len++] = d[--n];
}
static void log_hex(uint8_t v);
static void log_hex32(uint32_t v) {
  log_hex((uint8_t)(v >> 24)); log_hex((uint8_t)(v >> 16)); log_hex((uint8_t)(v >> 8)); log_hex((uint8_t)v);
}
static void log_hex(uint8_t v) {
  static const char digit[] = "0123456789abcdef";
  log_buf[log_len++] = digit[v >> 4]; log_buf[log_len++] = digit[v & 15];
}
#endif

#if defined(PC_TEST) || defined(PC_NETTEST)
static uint32_t pc_time(void) {           /* in the timer's counts (1.19 MHz, 0.84 us) */
  uint32_t t;
  uint16_t c;
  _disable();
  outp(0x43, 0x00);                       /* channel 0's count latched */
  c = inp(0x40); c |= (uint16_t)inp(0x40) << 8;
  t = ticks;
  outp(0x20, 0x0a);                       /* the interrupt controller's request register: */
  if ((inp(0x20) & 1) && c > TICK_DIV / 2) t++;   /* the count wrapped, its interrupt not taken yet */
  _enable();
  return t * TICK_DIV + (TICK_DIV - c);
}
#endif

#ifdef PC_TEST
#include "rec_data.h"                     /* a recording to replay (tools/build_pc.py --test) */
#define TEST_FRAMES ((REC_LEN - 16) / 6)
static uint16_t rec_pos = 16, frames;
static uint32_t t_start, t_flush;
static struct {
  uint16_t bad, late, n;                  /* the first record whose hash differs (its offset), late frames, frames timed */
  uint32_t flush;                         /* timer counts in the game flush, all frames */
  uint32_t work[TEST_FRAMES];             /* each game frame's work (flush .. next frame sync) */
  uint32_t fl[TEST_FRAMES];               /* of it the flush */
} __far test;                             /* far, as the recording: 24 KB for the longest */
#endif

/* ---- menus and input ---- */
const char *const plat_joy_names[PLAT_JOY_TYPES] = {"NONE    ", "GAMEPORT"};
const char *const plat_kbd_a_alt_name = "CURSOR AND RET  ";
const char *plat_input_name(uint8_t input) {
  switch (input) {
  case INPUT_KBD_A: return "CURSOR AND SPACE";
  case INPUT_KBD_B: return "WASD AND E      ";
  case INPUT_JOY1:  return "JOYSTICK A      ";
  case INPUT_JOY2:  return "JOYSTICK B      ";
  }
  return "";
}
uint8_t plat_input_allowed(uint8_t input) {
  if (input == INPUT_JOY1 || input == INPUT_JOY2) return joy_type != JOY_NONE;
  return 1;
}

uint8_t plat_keys_a(void) {
  uint8_t k = 0;
#ifdef PC_TEST
  return title_mode && (frames & 1) ? KEY_SPACE : 0;
#endif
#if defined(PC_NETTEST) && PC_NETTEST == 2
  {                                       /* joining: the room code from C:\CODE.TXT, once the test wrote it */
    static uint8_t poll, got;
    if (title_mode && !got && !(++poll & 15)) {
      FILE *f = fopen("C:\\CODE.TXT", "r");
      if (f) {                            /* (made before the start with a placeholder: DOSBox caches the folder's files) */
        char c[4];
        if (fread(c, 1, 4, f) == 4 && c[0] >= 'A' && c[0] <= 'Z' && c[1] >= 'A' && c[1] <= 'Z' && c[2] >= 'A' && c[2] <= 'Z'
            && c[3] >= 'A' && c[3] <= 'Z') { memcpy(net_code, c, 4); net_code[4] = 0; got = 1; }
        fclose(f);
      }
    }
  }
#endif
  if (keydown[K_UP]) k |= KEY_UP;
  if (keydown[K_DOWN]) k |= KEY_DOWN;
  if (keydown[K_LEFT]) k |= KEY_LEFT;
  if (keydown[K_RIGHT]) k |= KEY_RIGHT;
  if (kbd_alt_fire ? keydown[K_RETURN] : keydown[K_SPACE]) k |= KEY_SPACE;
#ifdef PC_NETTEST
  if (!title_mode) k &= (uint8_t)~KEY_SPACE;   /* the test's SPACE presses to ready up must not drop a bomb in the game */
#endif
  return k;
}

uint8_t plat_keys_b(void) {
  uint8_t k = 0;
  if (keydown[0x11]) k |= KEY_UP;           /* W */
  if (keydown[0x1f]) k |= KEY_DOWN;         /* S */
  if (keydown[0x1e]) k |= KEY_LEFT;         /* A */
  if (keydown[0x20]) k |= KEY_RIGHT;        /* D */
  if (keydown[0x12]) k |= KEY_SPACE;        /* E */
  return k;
}

/* the game port: a write starts the four timers, each axis's bit stays 1
 * for a time that grows with the stick's position; counted in loops of
 * this code (the CPU's speed cancels out: the centre is counted the same
 * way at the start). Joystick A: axes bits 0-1, buttons 4-5; B: 2-3, 6-7. */
#define JOY_PORT 0x201
#define JOY_MAX 4000                      /* loops: an axis not back by then has no stick */
static uint16_t joy_centre[4];
static uint8_t joy_present;               /* bit n: joystick n answers */

static void joy_read(uint16_t *t) {       /* the four axes' counts (JOY_MAX for an axis that never ends) */
  uint16_t n;
  uint8_t pending = 0x0f, b, k;
  for (k = 0; k < 4; k++) t[k] = JOY_MAX;
  _disable();
  outp(JOY_PORT, 0);
  for (n = 0; n < JOY_MAX && pending; n++) {
    b = (uint8_t)(inp(JOY_PORT) & pending);
    if (b != pending) {
      for (k = 0; k < 4; k++) if ((pending & (1 << k)) && !(b & (1 << k))) t[k] = n;
      pending = b;
    }
  }
  _enable();
}

uint8_t plat_joy(uint8_t n) {             /* n 0: joystick A, 1: B */
  uint16_t t[4];
  uint8_t k = 0, b, ax = (uint8_t)(n * 2);
  if (!(joy_present & (1 << n))) return 0;
  joy_read(t);
  if (t[ax] < joy_centre[ax] / 2) k |= KEY_LEFT;
  else if (t[ax] > joy_centre[ax] + joy_centre[ax] / 2) k |= KEY_RIGHT;
  if (t[ax + 1] < joy_centre[ax + 1] / 2) k |= KEY_UP;
  else if (t[ax + 1] > joy_centre[ax + 1] + joy_centre[ax + 1] / 2) k |= KEY_DOWN;
  b = (uint8_t)~inp(JOY_PORT);            /* the buttons, low while pressed */
  if (b & (n ? 0xc0 : 0x30)) k |= KEY_SPACE;
  return k;
}

/* text entry: letters, BACKSPACE or DELETE deletes, ESC cancels */
static const char letters[3][11] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
static uint8_t typing;                    /* plat_key_char was asked this frame: a room code or the lobby, where ESC cancels */
uint8_t plat_key_char(void) {
  uint8_t r, c;
  typing = 1;
  if (keydown[K_ESC]) return 0x1b;
  if (keydown[K_BACKSPACE] || keydown[K_DELETE]) return 8;
  for (r = 0; r < 3; r++)
    for (c = 0; letters[r][c]; c++)
      if (keydown[(r == 0 ? 0x10 : r == 1 ? 0x1e : 0x2c) + c]) return (uint8_t)letters[r][c];
  return 0;
}

uint8_t plat_break(void) { return keydown[K_ESC]; }

/* ---- time ---- */
static uint32_t next_frame;

#ifdef PC_NETTEST
static uint32_t sync_out, frame_busy;     /* the network test: the last frame's work, in timer counts (0.84 us) */
uint32_t pc_net_counts;                   /* tcp_pc.c: counts in the network calls since the last frame */
uint16_t pc_net_calls;                    /* and receive calls */
static uint32_t frame_net;
static uint16_t frame_calls, frame_polls;
static uint32_t frame_nd;
uint32_t pc_time_now(void) { return pc_time(); }
#endif
void plat_frame_sync(void) {                    /* 60 ms */
#ifdef PC_NETTEST
  frame_busy = pc_time() - sync_out; frame_net = pc_net_counts; pc_net_counts = 0; frame_calls = pc_net_calls; pc_net_calls = 0;
  { extern uint32_t pc_nd_counts; extern uint16_t pc_nd_polls; frame_nd = pc_nd_counts; pc_nd_counts = 0; frame_polls = pc_nd_polls; pc_nd_polls = 0; }
#endif
#ifdef PC_TEST
  if (t_start && test.n < TEST_FRAMES) {
    test.work[test.n++] = pc_time() - t_start;
    if ((int32_t)(now() - next_frame) > 0) test.late++;
  }
  t_start = 0;
#endif
  while ((int32_t)(now() - next_frame) < 0) ;
#ifdef PC_NETTEST
  sync_out = pc_time();
#endif
  next_frame += FRAME_MS;
  if ((int32_t)(now() - next_frame) > FRAME_MS) next_frame = now();   /* far behind (a pause): no rush to catch up */
  {                                       /* ESC on the title screen ends the program: a new press (not one held from */
    static uint8_t esc_was;               /* cancelling a room code), on a frame nothing asked for typed keys */
    uint8_t esc = keydown[K_ESC];
    if (title_mode && esc && !esc_was && !typing) exit(0);
    esc_was = esc;
    typing = 0;
  }
}

void plat_delay(void) {                         /* a TV frame's worth: 20 ms */
  uint32_t t = now();
  while (now() - t < 20) ;
}

/* frequency = 1108400 / ratio Hz on the MZ, whose 8253 counts 1108400 Hz:
 * the PC's counts 1193182, so the divider is ratio * 1193182 / 1108400.
 * The length: len units of about 0.93 ms (len * 238 / 256 ms). */
/* ---- an AdLib's or a Sound Blaster's FM chip (a YM3812, OPL2; an OPL3
 * the same here) at 388h, else at the BLASTER variable's port + 8: one
 * channel, two operators, a tone near the MZ's square wave (OPL_MOD
 * below); held while the key is on, the key off from the timer's
 * interrupt. A register wants 6 status reads after
 * its index, 35 after its value (3.3 and 23 microseconds on an AdLib). */
static void opl_write(uint8_t reg, uint8_t v) {
  uint8_t k;
  outp(opl, reg);
  for (k = 0; k < 6; k++) (void)inp(opl);
  outp(opl + 1, v);
  for (k = 0; k < 35; k++) (void)inp(opl);
}

static uint8_t opl_found(uint16_t port) { /* the timer test: timer 1 started, its flag up within a millisecond */
  uint8_t a, b;
  uint32_t t;
  opl = port;
  opl_write(4, 0x60); opl_write(4, 0x80);   /* the timers off, their flags cleared */
  a = (uint8_t)inp(port);
  opl_write(2, 0xff); opl_write(4, 0x21);   /* timer 1: 80 microseconds */
  t = now(); while (now() - t < 3) ;
  b = (uint8_t)inp(port);
  opl_write(4, 0x60); opl_write(4, 0x80);
  if ((a & 0xe0) == 0 && (b & 0xe0) == 0xc0) return 1;
  opl = 0;
  return 0;
}

#ifndef OPL_MOD                           /* the voice, measured (tools/pc_play.py --sound fm): the modulator */
#define OPL_MOD 0x22                      /* at twice the carrier's frequency puts the sidebands on the odd    */
#define OPL_TL 0x20                       /* harmonics only, as a square wave's; 24 dB down, the harmonics up  */
#define OPL_FB 0x00                       /* to the 9th within 2 dB of DOSBox-X's speaker (3rd -9 dB, 5th -12, */
#endif                                    /* 7th -28, 9th -41); feedback changed nothing here */
static void opl_init(void) {
  static const uint8_t regs[] = {
    0x01, 0x20,                           /* the waveforms selectable */
    0x20, OPL_MOD, 0x23, 0x21,               /* modulator, carrier: held (sustained); twice, once the frequency */
    0x40, OPL_TL, 0x43, 0x00,               /* the modulator down (how bright), the carrier loud */
    0x60, 0xf0, 0x63, 0xf0,               /* attack at once, no decay */
    0x80, 0x0f, 0x83, 0x0f,               /* sustained at full, released at once */
    0xe0, 0x00, 0xe3, 0x00,               /* sines */
    0xc0, OPL_FB,                           /* the feedback, FM */
    0xb0, 0x00 };
  const char *b = getenv("BLASTER");
  uint8_t k;
  if (no_fm || no_sound) return;
  if (!opl_found(0x388) && b) {
    for (; *b; b++) if ((*b & 0xdf) == 'A') { if (!opl_found((uint16_t)(strtoul(b + 1, NULL, 16) + 8))) opl = 0; break; }
  }
  if (!opl) return;
  for (k = 1; k < 0xf6; k++) opl_write(k, 0);   /* everything silent */
  for (k = 0; k < sizeof(regs); k += 2) opl_write(regs[k], regs[k + 1]);
}

static void opl_off(void) {
  uint8_t k;
  if (!opl) return;
  for (k = 0xb0; k < 0xb9; k++) opl_write(k, 0);   /* the keys off */
  for (k = 0x40; k < 0x56; k++) opl_write(k, 0x3f); /* every operator quiet */
}

void plat_tone(uint16_t ratio, uint8_t len) {
  uint32_t d;
#ifdef PC_NO_TONES
  return;
#endif
  if (!ratio || no_sound) return;
  if (opl) {                              /* f = 1108400 / ratio Hz; the F-number f * 2^(20 - block) / 49716 under 1024 */
    uint32_t f = (1108400UL << 4) / (ratio < 16 ? 16 : ratio);   /* in sixteenths of a Hz: under 2^21 */
    uint8_t block = 7;                    /* from the top octave down while the F-number has room (more precision) */
    uint32_t fn = (f << 9) / 49716UL;     /* f * 2^(16 - block) / 49716, f in sixteenths */
    while (block && fn < 512) { block--; fn = (f << (16 - block)) / 49716UL; }
    if (fn > 1023) fn = 1023;
    _disable();
    opl_b0 = (uint8_t)(block << 2 | (uint8_t)(fn >> 8));
    opl_write(0xb0, opl_b0);              /* the key off first: the envelope starts again */
    opl_write(0xa0, (uint8_t)fn);
    opl_write(0xb0, (uint8_t)(opl_b0 | 0x20));
    tone_ms = (uint16_t)(((uint16_t)len * 238) >> 8) + 1;
    _enable();
    return;
  }
  d = (uint32_t)ratio * 1193182UL / 1108400UL;
  if (d > 0xffff) d = 0xffff;
  _disable();
  outp(0x43, 0xb6);                             /* channel 2, both bytes, square wave */
  outp(0x42, (uint8_t)d); outp(0x42, (uint8_t)(d >> 8));
  outp(0x61, inp(0x61) | 3);                     /* its gate and the speaker on */
  tone_ms = (uint16_t)(((uint16_t)len * 238) >> 8) + 1;
  _enable();
}

/* ---- screen ---- */
static uint8_t mode = 0xff;

static uint8_t cga;                       /* the adapter is a CGA: mode 4 */
static uint16_t cga_expand[256];          /* a glyph row: each bit as a 2-bit pixel, the two bytes in memory order */
static const uint8_t cga_game_map[16] = {0, 1, 2, 2, 1, 1, 3, 3,   1, 3, 1, 3,   3, 3, 1, 1};   /* palette 0: green, red, yellow */
static const uint8_t cga_title_map[16] = {0, 1, 2, 2, 1, 1, 3, 3,  3, 1, 2, 3,   3, 3, 1, 1};   /* palette 1: cyan, magenta, white */
/* the figures' colours, by the game's colour: two palette colours in a checkerboard (both the same: solid) */
static const uint8_t cga_game_fig[16][2] = {   /* palette 0: the bombs (blue) red+yellow; the enemies red, olive, lime, dim yellow */
  {0, 0}, {2, 3}, {2, 2}, {1, 2}, {1, 1}, {1, 3}, {3, 0}, {3, 3}, {1, 1}, {3, 3}, {1, 1}, {3, 3}, {3, 3}, {3, 3}, {1, 1}, {1, 1}};
static const uint8_t cga_title_fig[16][2] = {  /* palette 1: the legend: red dim magenta, magenta, cyan dim cyan, yellow pink */
  {0, 0}, {2, 3}, {2, 0}, {2, 2}, {1, 1}, {1, 0}, {3, 2}, {3, 3}, {3, 3}, {1, 1}, {2, 2}, {3, 3}, {3, 3}, {3, 3}, {1, 1}, {1, 1}};
static uint16_t cga_pair[4][4][2];        /* the masks of a checkerboard of colours a, b: [a][b][row parity] */
static uint8_t glyph_mosaic[256 / 8];     /* a bit a glyph: a mosaic figure (four 3 x 3 blocks, each full or empty) */
#define CGA_FIGURE 0x80                   /* in cga_cell's colour: drawn with the figures' colours */

static void cga_cell(uint8_t x, uint8_t y, uint8_t g, uint8_t col) {
  const uint8_t *s = amiga_glyphs + ((uint16_t)g << 3);
  volatile uint16_t far *even = (volatile uint16_t far *)(vram + (uint16_t)y * 320 + x * 2);
  volatile uint16_t far *odd = (volatile uint16_t far *)(vram + 0x2000 + (uint16_t)y * 320 + x * 2);
  uint16_t m[2];
  uint8_t marked, hollow, invert, l, b;
  if (title_mode && (glyph_mosaic[g >> 3] & (1 << (g & 7)))) col |= CGA_FIGURE;   /* the legend's figures */
  if (col & CGA_FIGURE) {
    const uint8_t *f = (title_mode ? cga_title_fig : cga_game_fig)[col & 15];
    m[0] = cga_pair[f[0]][f[1]][0]; m[1] = cga_pair[f[0]][f[1]][1];
  } else m[0] = m[1] = (uint16_t)(0x5555u * (title_mode ? cga_title_map : cga_game_map)[col & 15]);
  col &= 15;
  marked = (uint8_t)(!title_mode && (col == 10 || col == 11));   /* players 3 and 4 */
  hollow = (uint8_t)(marked && y < HUD_ROW); invert = (uint8_t)(marked && y == HUD_ROW);
  for (l = 0; l < 8; l++) {
    b = s[l];
    if (hollow && (l == 2 || l == 6)) b &= (uint8_t)~0x22;   /* the blocks' centres: rows 2 and 6, columns 2 and 6 */
    if (invert) b = (uint8_t)~b;
    if (l & 1) { *odd = cga_expand[b] & m[1]; odd += 40; }
    else { *even = cga_expand[b] & m[0]; even += 40; }
  }
}

static void cell(uint8_t x, uint8_t y, uint8_t g, uint8_t col) {
  volatile uint8_t far *d = vram + (uint16_t)y * 320 + x;
  const uint8_t *s = amiga_glyphs + ((uint16_t)g << 3);
  uint8_t l;
  if (cga) { cga_cell(x, y, g, col); return; }
  col = col_map[col & 15];
  outp(SEQ_DATA, col);                          /* the colour's planes: the glyph (the Map Mask is selected: ega_ready) */
  for (l = 0; l < 8; l++) d[l * 40] = s[l];
  if (col != 15) {                              /* the other planes: zeros */
    outp(SEQ_DATA, (uint8_t)(~col & 15));
    for (l = 0; l < 8; l++) d[l * 40] = 0;
  }
}

/* The CGA's fast path (an XT drew a frame in 37 ms in C: two divisions
 * and a call a cell, a colour decided per cell): every code's 8 screen
 * words worked out when the screen switches (title or game: its glyph,
 * players 3 and 4 hollow, its colour's row masks), so drawing a cell is 8
 * word copies; the cells' offsets in a table; the changed cells found by
 * REPE CMPSW. The status line (its inverse marks) and the players'
 * recolouring keep cga_cell. */
static uint16_t cga_words[256][8];        /* by code: the cell's 8 rows as they go to the screen (4 KB) */
static uint16_t cga_off[SCREEN_CELLS];    /* by cell: its offset on the screen (a CGA: its even line's in B800; an EGA: in A000) */
static uint8_t hollow_glyphs[632];        /* the glyphs with their blocks' centres cut out (players 3 and 4) */

static void cga_tables(uint8_t title) {
  uint16_t c, m0, m1;
  uint8_t l;
  for (c = 0; c < 256; c++) {
    uint8_t g = title ? amiga_title_glyph[c] : amiga_game_glyph[c];
    uint8_t col = (uint8_t)((title ? amiga_title_colour[c] : amiga_game_colour[c]) & 15);
    uint8_t fig = title ? (uint8_t)(glyph_mosaic[g >> 3] & (1 << (g & 7)))
                        : (uint8_t)((c >= C_ENEMY_BASE && c < C_FIRE) || (c & 0xe0) == C_BOMB_TILE);
    const uint8_t *gl = amiga_glyphs + ((uint16_t)g << 3);
    if (fig) {
      const uint8_t *f = (title ? cga_title_fig : cga_game_fig)[col];
      m0 = cga_pair[f[0]][f[1]][0]; m1 = cga_pair[f[0]][f[1]][1];
    } else m0 = m1 = (uint16_t)(0x5555u * (title ? cga_title_map : cga_game_map)[col]);
    if (!title && (col == 10 || col == 11)) gl = hollow_glyphs + ((uint16_t)g << 3);   /* players 3 and 4, in the field */
    for (l = 0; l < 8; l++) cga_words[c][l] = cga_expand[gl[l]] & ((l & 1) ? m1 : m0);
  }
}

/* one cell: its code's 8 words copied, the even rows to the even lines'
 * bank, the odd ones to the odd lines' (2000h on), the rows 80 bytes apart */
static void cga_draw(uint16_t off, uint16_t code) {
  _asm {
    push si
    push di
    push es
    mov di, off
    mov si, code
    mov cl, 4
    shl si, cl
    add si, offset cga_words
    mov ax, 0b800h
    mov es, ax
    movsw
    add di, 1ffeh
    movsw
    sub di, 1fb2h
    movsw
    add di, 1ffeh
    movsw
    sub di, 1fb2h
    movsw
    add di, 1ffeh
    movsw
    sub di, 1fb2h
    movsw
    add di, 1ffeh
    movsw
    pop es
    pop di
    pop si
  }
}

/* The EGA's fast path, as the CGA's (an XT's flush 33 ms in C): every
 * code's colour (the adapter's register) and glyph worked out when the
 * screen switches; a cell in assembly: the colour's planes (the Map Mask,
 * selected by ega_ready) get its 8 rows, the other planes zeros */
static uint8_t ega_col[256];
static uint16_t ega_glyph[256];

static void ega_tables(uint8_t title) {
  uint16_t c;
  for (c = 0; c < 256; c++) {
    ega_col[c] = col_map[(title ? amiga_title_colour[c] : amiga_game_colour[c]) & 15];
    ega_glyph[c] = (uint16_t)(amiga_glyphs + ((uint16_t)(title ? amiga_title_glyph[c] : amiga_game_glyph[c]) << 3));
  }
}

static void ega_draw(uint16_t off, uint16_t code) {
  _asm {
    push si
    push di
    push es
    mov di, off
    mov bx, code
    mov al, ega_col[bx]
    shl bx, 1
    mov si, ega_glyph[bx]
    mov dx, 3c5h
    out dx, al
    mov ah, al
    mov cx, 0a000h
    mov es, cx
    lodsb
    mov es:[di], al
    lodsb
    mov es:[di + 40], al
    lodsb
    mov es:[di + 80], al
    lodsb
    mov es:[di + 120], al
    lodsb
    mov es:[di + 160], al
    lodsb
    mov es:[di + 200], al
    lodsb
    mov es:[di + 240], al
    lodsb
    mov es:[di + 280], al
    mov al, ah
    not al
    and al, 0fh
    jz done
    out dx, al
    xor al, al
    mov es:[di], al
    mov es:[di + 40], al
    mov es:[di + 80], al
    mov es:[di + 120], al
    mov es:[di + 160], al
    mov es:[di + 200], al
    mov es:[di + 240], al
    mov es:[di + 280], al
  done:
    pop es
    pop di
    pop si
  }
}

#define fast_draw(off, code) (cga ? cga_draw((off), (code)) : ega_draw((off), (code)))

/* the first word of the field rows (draw_buf against shadow_vram) from
 * word w on that differs, or end: REPE CMPSW */
static uint16_t cga_next(uint16_t w, uint16_t end) {
  uint16_t r = end;
  _asm {
    push si
    push di
    push es
    push ds
    pop es
    mov cx, end
    sub cx, w
    jbe done
    mov si, w
    shl si, 1
    mov di, si
    add si, offset draw_buf
    add di, offset shadow_vram
    cld
    repe cmpsw
    je done
    sub si, offset draw_buf
    shr si, 1
    dec si
    mov r, si
  done:
    pop es
    pop di
    pop si
  }
  return r;
}

static void set_mode(uint8_t title) {           /* everything is drawn again */
  mode = title;
  memset(shadow_vram, 0xff, SCREEN_CELLS);
  if (cga) cga_tables(title); else ega_tables(title);
  if (cga) {                                    /* the CGA's palette: 1 for the title, 0 for the game; bright, black behind */
    union REGS r;
    r.h.ah = 0x0b; r.h.bh = 0; r.h.bl = 0x10; int86(0x10, &r, &r);
    r.h.ah = 0x0b; r.h.bh = 1; r.h.bl = title ? 1 : 0; int86(0x10, &r, &r);
  }
}

static void ega_ready(void) {             /* the EGA's writes as cell() needs them, the Map Mask selected (nothing on a CGA): */
  if (cga) return;                        /* the CPU's byte as it is (no Set/Reset, every bit, replace, write mode 0) */
  outp(GC_INDEX, 1); outp(GC_DATA, 0);
  outp(GC_INDEX, 3); outp(GC_DATA, 0);
  outp(GC_INDEX, 5); outp(GC_DATA, 0);
  outp(GC_INDEX, 8); outp(GC_DATA, 0xff);
  outp(SEQ_INDEX, 2);
}

void flush_screen(void) {
  uint16_t i;
  uint8_t a;
#ifdef PC_TEST
  frames++;
  if (!title_mode && rec_pos + 6 <= REC_LEN) {   /* the hash checked */
    if (hash_period && (rec[rec_pos + 4] | (uint16_t)(rec[rec_pos + 5] << 8)) != state_hash && !test.bad)
      test.bad = rec_pos;
    _fmemcpy(replay_keys, rec + rec_pos, 4); rec_pos += 6;   /* rec is far: the near memcpy would read DS:offset */
  }
  if (!title_mode) t_start = t_flush = pc_time();
#endif
  if (mode != title_mode) set_mode(title_mode);
  ega_ready();                                  /* the latches: zeros */
  if (title_mode) {
    for (i = 0; i < SCREEN_CELLS; i++) {
      a = draw_buf[i]; draw_buf[i] = C_SPACE;
      if (a == shadow_vram[i]) continue;
      shadow_vram[i] = a;
      fast_draw(cga_off[i], a);
    }
  } else {
    /* rows 0-23, two cells a compare (every game frame begins with
     * composite_frame, which overwrites them: no clearing); then the
     * status line, cleared */
    const uint16_t *d2 = (const uint16_t *)draw_buf, *s2 = (const uint16_t *)shadow_vram;
    uint16_t w;
    for (w = 0; (w = cga_next(w, (HUD_ROW * SCREEN_W) / 2)) < (HUD_ROW * SCREEN_W) / 2; w++) {   /* the changed words by */
      i = (uint16_t)(w * 2);                                                                   /* REPE CMPSW, their cells */
      if ((a = draw_buf[i]) != shadow_vram[i]) { shadow_vram[i] = a; fast_draw(cga_off[i], a); }
      if ((a = draw_buf[i + 1]) != shadow_vram[i + 1]) { shadow_vram[i + 1] = a; fast_draw(cga_off[i + 1], a); }
    }
    for (i = HUD_ROW * SCREEN_W; i < SCREEN_CELLS; i++) {
      a = draw_buf[i]; draw_buf[i] = C_SPACE;
      if (a == shadow_vram[i]) continue;
      shadow_vram[i] = a;
      cell((uint8_t)(i - HUD_ROW * SCREEN_W), HUD_ROW, amiga_game_glyph[a], amiga_game_colour[a]);
    }
  }
#ifdef PC_PLAYTEST
  if (playlog) {
    static uint8_t every;
    fprintf(playlog, "%u %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n", frame_no, title_mode, players[0].x, players[0].y, players[0].state,
            players[1].x, players[1].y, players[1].state, players[2].x, players[2].y, players[2].state,
            players[3].x, players[3].y, players[3].state, (unsigned)(kbd_alt_fire | joy_present << 1));
    if (!(++every & 7)) { fflush(playlog); _dos_commit(fileno(playlog)); }
  }
#endif
#ifdef PC_NETTEST
#ifdef NETLOG_SPARSE                      /* the XT's pace measured: a line only on the hashed frames (the tools' hashes) */
  if (netlog && (title_mode || (frame_no != logged_frame && frame_no % 16 == 1))) {
#else
  if (netlog && (title_mode || frame_no != logged_frame)) {   /* the state the tools read from the other machines' memory: */
#endif
    static uint8_t every;                 /* in a game once a frame (a line a loop slowed an XT to a crawl) */
    extern uint32_t pc_modem_tx, pc_modem_rx;
    logged_frame = frame_no; stall_at = now();
    if (log_len > sizeof(log_buf) - 2300) log_out();
    log_s("frame "); log_dec(frame_no); log_s(" title "); log_dec(title_mode);
    log_s(" code "); { uint8_t k; for (k = 0; k < 4; k++) log_buf[log_len++] = net_code[k] ? net_code[k] : ' '; }
    log_s(" active "); log_dec(net_active); log_s(" abort "); log_dec(net_abort); log_s(" slot "); log_dec(net_slot);
    log_s(" delay "); log_dec(net_delay); log_s(" table ");
    { uint8_t k; for (k = 0; k < 4; k++) log_hex(net_table[k]); }
    log_s(" hash "); log_hex((uint8_t)(state_hash >> 8)); log_hex((uint8_t)state_hash); log_s(" map ");
#ifdef NETLOG_SPARSE
    if (0)                                /* the pace measured: no map (2000 hex digits cost an XT 5 ms a frame) */
#else
    if (title_mode || frame_no % 16 == 1)  /* the map: on the hashed frames' lines (what the tools compare) */
#endif
      for (i = 0; i < SCREEN_CELLS; i++) log_hex(map_layer[i]);
    else log_s("none");
    /* the rest in hex (cheap on an 8088; the profile of an XT measures the game, not this) */
    log_s(" msx "); log_hex32(now()); log_s(" txx "); log_hex32(pc_modem_tx); log_s(" rxx "); log_hex32(pc_modem_rx);
    { extern uint8_t net_adapt_seen[]; log_s(" seen "); log_hex(net_adapt_seen[0]); log_hex(net_adapt_seen[1]); }
    log_s(" busyc "); log_hex32(frame_busy); log_s(" netc "); log_hex32(frame_net); log_s(" calls "); log_dec(frame_calls); log_s(" ndc "); log_hex32(frame_nd); log_s(" polls "); log_dec(frame_polls);
    { extern uint32_t pkt_send_counts, pkt_rx_counts; extern uint16_t pkt_sent, pkt_rcvd;
      log_s(" sendc "); log_hex32(pkt_send_counts); log_s(" rxc "); log_hex32(pkt_rx_counts); log_s(" sent "); log_dec(pkt_sent); log_s(" rcvd "); log_dec(pkt_rcvd);
      pkt_send_counts = pkt_rx_counts = 0; pkt_sent = pkt_rcvd = 0; }
    log_s("\n");
    if (!(++every & 31)) log_out();
  } else if (netlog && now() - stall_at > 2000) {   /* the game stood still 2 s: the network's state, written at once */
    char d[240];
    void pkt_debug(char *out);
    stall_at = now();
    pkt_debug(d);
    log_s("stall frame "); log_dec(frame_no); log_s(" delay "); log_dec(net_delay); log_s(" ms "); log_dec(now());
    { extern uint8_t net_adapt_seen[]; log_s(" seen "); log_hex(net_adapt_seen[0]); log_hex(net_adapt_seen[1]); }
    log_s(" "); log_s(d); log_s("\n");
    log_out();
  }
#endif
#ifdef PC_TEST
  if (!title_mode) {
    if (rec_pos + 6 <= REC_LEN) { uint32_t d = pc_time() - t_flush; test.flush += d; if (test.n < TEST_FRAMES) test.fl[test.n] = d; }
    else {                                      /* the recording replayed: the figures out, done */
      FILE *f = fopen(PC_RESULT, "w");
      if (f) {
        fprintf(f, "frames %u bad %u late %u hz %lu flush %lu\n", test.n, test.bad, test.late, (unsigned long)PIT_HZ,
                (unsigned long)test.flush);
        for (i = 0; i < test.n; i++) fprintf(f, "%lu %lu\n", (unsigned long)test.work[i], (unsigned long)test.fl[i]);
        fclose(f);
      }
      exit(0);
    }
  }
#endif
}

void plat_player_colour(uint8_t x, uint8_t y, uint8_t player) {
  ega_ready();
  cell(x, y, amiga_game_glyph[shadow_vram[(uint16_t)y * SCREEN_W + x]], (uint8_t)(8 + player));
}

void plat_hud_colour(uint8_t x, uint8_t n, uint8_t player, uint8_t blank) {
  (void)blank;
  ega_ready();
  for (; n; n--, x++) cell(x, HUD_ROW, amiga_game_glyph[shadow_vram[HUD_ROW * SCREEN_W + x]], (uint8_t)(8 + player));
}

/* ---- start and end ---- */
static void cleanup(void) {
  union REGS r;
  if (!started) return;
  if (pc_net_cleanup) pc_net_cleanup();          /* hung up, the UART's interrupt given back */
  _disable();
  outp(0x43, 0x36); outp(0x40, 0); outp(0x40, 0);   /* the timer back to 18.2 Hz */
  _dos_setvect(0x08, old_int8);
  _dos_setvect(0x09, old_int9);
  outp(0x61, inp(0x61) & 0xfc);                  /* the speaker off */
  _enable();
  opl_off();
#ifdef PC_PLAYTEST
  if (playlog) fclose(playlog);
#endif
#ifdef PC_NETTEST
  if (netlog) { log_out(); fclose(netlog); netlog = NULL; }
#endif
  if (!cga) { outp(SEQ_INDEX, 2); outp(SEQ_DATA, 0x0f); }   /* all four planes written again, as the BIOS left them */
  r.h.ah = 0; r.h.al = old_mode;                 /* the screen mode back */
  int86(0x10, &r, &r);
  started = 0;
}

/* an EGA in a 200-line mode has the CGA's 16 colours; the game's are
 * chosen by hand, not by nearness (which made players 3 and 4 both grey):
 * the field in the dark shades (the bricks dark red, the border dark
 * magenta), the players in bright ones of their own (green, brown for
 * orange, light blue, light red) */
static const uint8_t ega_colours[16] = {
  0,  1,  4,  5,  2,  3, 14, 15,          /* the MZ's black, blue, red, magenta, green, cyan, yellow, white */
  10, 6,  9, 12,                          /* players 1-4: light green, orange, light blue, light red */
  7,  7,  6,  2                           /* the spares: greys, brown, dark green */
};

void plat_init(void) {
  union REGS r;
  uint8_t i;
  options();
  r.h.ah = 0x0f; int86(0x10, &r, &r); old_mode = r.h.al;
  r.h.ah = 0x12; r.h.bl = 0x10; r.h.bh = 0xff; int86(0x10, &r, &r);   /* EGA or better? (BH 0 or 1 if so) */
  if (r.h.bh > 1) {                              /* no: a CGA, unless the BIOS's equipment word says mono (MDA, Hercules) */
    int86(0x11, &r, &r);
    if ((r.x.ax & 0x30) == 0x30) { printf("BomberNet needs a CGA, EGA or VGA card.\n"); exit(1); }
    cga = 1;
  }
  if (force_adapter == 'C') cga = 1;             /* /CGA: any EGA or VGA shows the CGA's mode */
  if (cga) {
    uint16_t b;
    r.x.ax = 0x0004; int86(0x10, &r, &r);       /* 320 x 200, 4 colours */
    vram = (volatile uint8_t far *)MK_FP(0xb800, 0);
    for (b = 0; b < 256; b++) {
      uint16_t w = 0;
      uint8_t k;
      for (k = 0; k < 8; k++)                    /* pixel k: the first four in the first byte, the leftmost highest */
        if (b & (0x80 >> k)) w |= (uint16_t)(3u << ((k < 4 ? 0 : 8) + 6 - 2 * (k & 3)));
      cga_expand[b] = w;
    }
    {                                            /* the checkerboards: pixel k of an even row a's colour when k is even */
      uint8_t a, c, par, k;
      for (a = 0; a < 4; a++)
        for (c = 0; c < 4; c++)
          for (par = 0; par < 2; par++) {
            uint16_t w = 0;
            for (k = 0; k < 8; k++)
              w |= (uint16_t)((uint16_t)(((k + par) & 1) ? c : a) << ((k < 4 ? 0 : 8) + 6 - 2 * (k & 3)));
            cga_pair[a][c][par] = w;
          }
    }
    for (b = 0; b < 79 && b < 256; b++) {         /* the mosaic glyphs: every pixel in the four blocks, each full or empty */
      const uint8_t *gl = amiga_glyphs + (b << 3);
      uint8_t y, ok = 1, any = 0, qy, qx;
      for (y = 0; y < 8; y++) if (gl[y] & (uint8_t)~((y & 3) ? 0x77 : 0x00)) ok = 0;
      for (qy = 1; qy < 8 && ok; qy += 4)
        for (qx = 1; qx < 8 && ok; qx += 4) {
          uint8_t n = 0, yy, xx;
          for (yy = qy; yy < qy + 3; yy++) for (xx = qx; xx < qx + 3; xx++) if (gl[yy] & (0x80 >> xx)) n++;
          if (n != 0 && n != 9) ok = 0;
          if (n) any = 1;
        }
      if (ok && any) glyph_mosaic[b >> 3] |= (uint8_t)(1 << (b & 7));
    }
    for (b = 0; b < sizeof(hollow_glyphs); b++)    /* rows 2 and 6 without columns 2 and 6 */
      hollow_glyphs[b] = (uint8_t)(((b & 7) == 2 || (b & 7) == 6) ? amiga_glyphs[b] & ~0x22 : amiga_glyphs[b]);
    for (b = 0; b < SCREEN_CELLS; b++) cga_off[b] = (uint16_t)((b / SCREEN_W) * 320 + (b % SCREEN_W) * 2);
  } else {
  uint16_t b;
  r.h.ah = 0; r.h.al = 0x0d; int86(0x10, &r, &r);
  vram = (volatile uint8_t far *)MK_FP(0xa000, 0);
  r.x.ax = 0x1a00; int86(0x10, &r, &r);
  if (r.h.al == 0x1a && force_adapter != 'E') {  /* (/EGA: a VGA shows the EGA's 16 colours) */                          /* a VGA: the game's colours exactly, in the DAC entries mode 0Dh shows */
    for (i = 0; i < 16; i++) {                   /* (0-7 and 16-23), 6 bits a gun */
      uint16_t c = amiga_palette[i];
      r.x.ax = 0x1010; r.x.bx = (uint16_t)(i < 8 ? i : i + 8);
      r.h.dh = (uint8_t)(((c >> 8) & 15) * 63 / 15); r.h.ch = (uint8_t)(((c >> 4) & 15) * 63 / 15); r.h.cl = (uint8_t)((c & 15) * 63 / 15);
      int86(0x10, &r, &r);
      col_map[i] = i;
    }
  } else for (i = 0; i < 16; i++) col_map[i] = ega_colours[i];   /* an EGA: its 16, chosen by hand */
  for (b = 0; b < SCREEN_CELLS; b++) cga_off[b] = (uint16_t)((b / SCREEN_W) * 320 + b % SCREEN_W);   /* a byte a cell in each plane */
  ega_ready();
  }
  {                                              /* the game port: which sticks answer, and their centres */
    uint16_t t[4];
    uint8_t k;
    joy_read(t);
    for (k = 0; k < 4; k++) joy_centre[k] = t[k];
    if (t[0] < JOY_MAX && t[1] < JOY_MAX) joy_present |= 1;
    if (t[2] < JOY_MAX && t[3] < JOY_MAX) joy_present |= 2;
  }
  old_int8 = _dos_getvect(0x08);
  old_int9 = _dos_getvect(0x09);
  _disable();
  _dos_setvect(0x08, timer_isr);
  _dos_setvect(0x09, key_isr);
  outp(0x43, 0x36); outp(0x40, TICK_DIV & 0xff); outp(0x40, TICK_DIV >> 8);
  _enable();
  started = 1;
  atexit(cleanup);
  opl_init();                                    /* after the timer: its test waits a few milliseconds */
  joy_type = joy_present ? 1 : JOY_NONE;
  set_mode(1);
#ifdef PC_TEST
  match_seed = rec[4] | (uint16_t)(rec[5] << 8);
  menu_mode = rec[6]; menu_players = rec[7];
  _fmemcpy(menu_inputs, rec + 8, 4);
#ifdef TEST_HASH_OFF
  hash_period = 0;
#else
  hash_period = rec[12];
#endif
  replay_active = 1;
#endif
#ifdef PC_NETTEST                         /* 1: host a room of PC_NETTEST_PLAYERS on SPACE; 2: join */
  netlog = fopen("C:\\NET.LOG", "a");
  if (netlog) { log_s("start\n"); log_out(); }
  menu_mode = 1; menu_players = PC_NETTEST_PLAYERS; menu_net = PC_NETTEST; menu_local = PC_NETTEST_LOCAL;
#endif
#ifdef PC_PLAYTEST
  playlog = fopen("C:\\PLAY.LOG", "w");
  menu_mode = 1; menu_players = 4;              /* a deathmatch of 4: key set A, key set B, the two joysticks */
  menu_inputs[0] = INPUT_KBD_A; menu_inputs[1] = INPUT_KBD_B; menu_inputs[2] = INPUT_JOY1; menu_inputs[3] = INPUT_JOY2;
#endif
  next_frame = now() + FRAME_MS;
}
