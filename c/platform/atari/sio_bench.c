/* The cost of a network frame's SIO on the Atari, measured in emulated time
 * (tools/build_atari.py --bench; Altirra with tools/atari/fakefn.atdevice,
 * a FujiNet stand-in with FujiNet's own delays). The game's screen is on,
 * with its DLIs, so the CPU sees the DMA and interrupts of a match. Each
 * frame does what netdev_soft.c does: our input line out (12 bytes with its
 * WebSocket header), then reads until nothing waits (a status, a read, the
 * drained call). The status line shows, in 128 us units, the average and
 * the longest of 64 frames: 2 players (9 bytes in a frame), 4 players (27),
 * then the same at 19,200 baud. BENCH_OUT, BENCH_IN2 and BENCH_IN4 set
 * other sizes (docs/net-protocol.md: JSON lines). */
#include <stdint.h>
#include <string.h>
#include "game.h"
#include "tcp.h"

extern uint8_t atari_screen[];
extern const uint8_t atari_text_code[256];
uint16_t atari_now(void);
uint16_t atari_since(uint16_t t);
void tcp_bench_in(uint16_t n);
void tcp_bench_arrive(uint16_t ms);
extern uint16_t atari_sio_calls;

static void show(uint8_t at, uint16_t v) {
  atari_screen[960 + at] = atari_text_code[v / 100 % 10];
  atari_screen[961 + at] = atari_text_code[v / 10 % 10];
  atari_screen[962 + at] = atari_text_code[v % 10];
}

#ifndef BENCH_OUT                                 /* bytes a frame: ours out, waiting for 2 and 4 players */
#define BENCH_OUT 12
#define BENCH_IN2 9
#define BENCH_IN4 27
#endif

static uint8_t out[64], in[64];

static void bench(uint8_t at, uint16_t in_len) {
  uint16_t t, d, max = 0;
  uint32_t sum = 0, sw = 0;
  uint8_t f;
  tcp_bench_in(in_len);
  atari_sio_calls = 0;
  for (f = 0; f < 64; f++) {
    plat_frame_sync();                            /* a frame's start, as in the game */
    t = atari_now();
    tcp_send(out, BENCH_OUT);
    sw += atari_since(t);
    while (tcp_recv(in, sizeof(in)) > 0) ;
    d = atari_since(t);
    sum += d;
    if (d > max) max = d;
  }
  show(at, (uint16_t)(sum / 64)); show((uint8_t)(at + 4), max);
  show((uint8_t)(at + 20), (uint16_t)(sw / 64));  /* the write alone */
  show((uint8_t)(at + 24), (uint16_t)(atari_sio_calls * 10 / 64));   /* commands a frame, x10 */
}

/* How many frames a message waits. 9 bytes arrive (the fake FujiNet's 'J':
 * at once, whatever the delay asked; Altirra's device threads do not
 * sleep), in even frames before the frame's poll, in odd frames after it;
 * each frame polls as the game does (our line out, then reads until
 * nothing waits). On row 23: arrivals taken at the first poll after them
 * with 0 frames waited (the "before" ones), 1 frame (the "after" ones,
 * taken at the next frame's poll), 2 or more (a poll that missed pending
 * data: a fault), the frames waited in all, and the 'J' command's longest
 * time (128 us units). */
static void show23(uint8_t at, uint16_t v) {
  atari_screen[920 + at] = atari_text_code[v / 100 % 10];
  atari_screen[921 + at] = atari_text_code[v / 10 % 10];
  atari_screen[922 + at] = atari_text_code[v % 10];
}
static uint8_t poll(void) {                       /* as the game: 1 when bytes came */
  uint8_t got = 0;
  tcp_send(out, 12);
  while (tcp_recv(in, sizeof(in)) > 0) got = 1;
  return got;
}
static void latency(void) {
  uint16_t sum = 0, jmax = 0, t;
  uint8_t f, n0 = 0, n1 = 0, n2 = 0, waiting = 0, waited = 0, after = 0;
  tcp_bench_in(9);
  for (f = 0; f < 90 && (f < 64 || waiting); f++) {
    plat_frame_sync();
    if (!waiting && f < 64 && !after) {           /* an arrival before this frame's poll */
      t = atari_now(); tcp_bench_arrive(0); t = atari_since(t);
      if (t > jmax) jmax = t;
      waiting = 1; waited = 0;
    }
    if (poll()) {
      if (waited == 0) n0++; else if (waited == 1) n1++; else n2++;
      sum += waited; waiting = 0;
    } else if (waiting) waited++;
    if (!waiting && f < 64 && after) {            /* an arrival after this frame's poll: the next frame's is */
      tcp_bench_arrive(0);                        /* the first that can take it, one frame of waiting */
      waiting = 1; waited = 1;
    }
    after ^= 1;
  }
  show23(0, n0); show23(4, n1); show23(8, n2); show23(12, sum); show23(20, jmax);
}

void main(void) {
  plat_init();
  clear_buffers();
  title_mode = 0;
  flush_screen();                                 /* the game's screen: its DMA and DLIs */
  memset(out, 0x42, sizeof(out));
  if (!tcp_present()) { show(0, 999); for (;;) ; }
  if (tcp_open("bench", 1)) { show(0, 998); for (;;) ; }
  bench(0, BENCH_IN2);                                    /* 2 players */
  bench(8, BENCH_IN4);                                   /* 4 players */
  {                                               /* a status alone (nothing waiting) */
    uint16_t t; uint32_t sum = 0; uint8_t f;
    tcp_bench_in(0);
    for (f = 0; f < 64; f++) { plat_frame_sync(); t = atari_now(); tcp_recv(in, sizeof(in)); sum += atari_since(t); }
    show(32, (uint16_t)(sum * 10 / 64));          /* x10: in 12.8 us units */
  }
  latency();
  for (;;) ;
}
