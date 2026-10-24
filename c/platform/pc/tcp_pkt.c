/* tcp.h (through tcp_pc.c) over Ethernet: a packet driver (the FTP Software specification,
 * the one every DOS network card has: Crynwr's NE2000.COM and the rest,
 * loaded before the game, on a software interrupt from 60h to 80h) and a
 * TCP/IP of our own, as small as the game allows (docs/port-pc.md):
 *
 *   ARP       one entry: the next hop (the relay, or the gateway); requests
 *             for our address answered
 *   IPv4      no options, no fragments
 *   ICMP      echo answered (ping works)
 *   UDP       DHCP (the address, the mask, the gateway, the DNS server; else
 *             /IP= /MASK= /GW= /DNS=) and DNS (one A record)
 *   TCP       one connection at a time, a client: data beyond a gap kept
 *             (one island of it, in the receive ring) till the gap is sent again;
 *             in-order data acknowledged by our next segment (the keys, every
 *             60 ms), else by a bare ACK after 70 ms; anything odd at once;
 *             go-back-N retransmission, on a timeout from the measured
 *             round trip (Karn; 100 ms at least) or on the second duplicate
 *             ACK
 *
 * The driver gives us each frame by calling a receiver twice, in its
 * interrupt: once for a buffer (a free one of 16 far slots), once when it
 * is filled. tcp_recv and the waits take them out in order (poll). Times:
 * the game's 1 kHz timer (pc_ms). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
#include <malloc.h>

uint32_t pc_ms(void);                     /* plat_pc.c: milliseconds */
extern void (*pc_net_cleanup)(void);      /* plat_pc.c calls it at the end */
extern uint8_t pc_ip[4], pc_mask[4], pc_gw[4], pc_dns[4];   /* plat_pc.c, the command line: 0.0.0.0 = DHCP's */
extern uint32_t pc_modem_tx, pc_modem_rx; /* tcp_modem.c: the network test's counters, these bytes too */

/* ---- the packet driver ---- */
#define NSLOT 16                          /* 24 KB of far memory: a second's frames while the game is busy */
#define FRAME 1514
static uint8_t __far *slot;              /* NSLOT frames, allocated when a driver is found (not in the .EXE) */
static volatile uint16_t slot_len[NSLOT]; /* 0: free (or being filled); the receiver sets it, poll clears it */
static uint16_t slot_off[NSLOT];          /* each slot's offset in slot (set when it is allocated) */
static volatile uint8_t slot_wr;          /* the receiver's next slot */
static uint8_t slot_rd;                   /* poll's next */
static uint8_t pkt_int;                   /* the driver's interrupt; 0: none found */
static uint16_t handle[2];                /* IP's, ARP's */
static uint8_t my_mac[6];

/* the driver's upcall, in its interrupt: AX 0, CX the length -> ES:DI a
 * buffer (0:0 to drop the frame); AX 1 -> the buffer is filled. Every
 * register but ES:DI kept. */
static void __declspec(naked) __far receiver(void) {
  _asm {
    push ds
    push ax
    mov ax, seg slot_len
    mov ds, ax
    pop ax
    push bx
    mov bl, slot_wr
    xor bh, bh
    shl bx, 1
    or ax, ax
    jnz filled
    cmp cx, FRAME
    ja drop
    cmp word ptr slot_len[bx], 0
    jne drop
    mov di, slot_off[bx]
    add di, word ptr slot
    mov es, word ptr slot+2
    pop bx
    pop ds
    retf
  drop:
    xor di, di
    mov es, di
    pop bx
    pop ds
    retf
  filled:
    mov slot_len[bx], cx
    shr bx, 1
    inc bx
    cmp bx, NSLOT
    jb next
    xor bx, bx
  next:
    mov slot_wr, bl
    pop bx
    pop ds
    retf
  }
}

static uint8_t driver(union REGS *r, struct SREGS *s) {   /* a driver call: 0, else its error code */
  int86x(pkt_int, r, r, s);
  return r->x.cflag ? (uint8_t)(r->h.dh ? r->h.dh : 1) : 0;
}

static uint8_t tx[FRAME];                 /* the frame being sent */
#ifdef PC_NETTEST
uint32_t pc_time_now(void);
uint32_t pkt_send_counts, pkt_rx_counts;  /* the network test: counts in the driver's send call, in handling frames received */
uint16_t pkt_sent, pkt_rcvd;
#endif
static uint32_t drv_vec;                  /* the driver's entry (its interrupt's vector): a frame sent by a direct call */
static void send_frame(uint16_t n) {
#ifdef PC_NETTEST
  uint32_t t0 = pc_time_now();
#endif
  if (n < 60) { memset(tx + n, 0, 60 - n); n = 60; }   /* Ethernet's shortest */
  _asm {                                  /* send_pkt (AH 4, DS:SI, CX) as INT does it, flags pushed, a far call: */
    push bp                               /* without int86x's register structures, once a frame on an XT */
    push es
    push di
    push dx
    push bx
    mov ah, 4
    mov si, offset tx
    mov cx, n
    pushf
    call dword ptr drv_vec
    pop bx
    pop dx
    pop di
    pop es
    pop bp
  }
#ifdef PC_NETTEST
  pkt_send_counts += pc_time_now() - t0; pkt_sent++;
#endif
}

/* ---- checksums: summed in the machine's byte order, kept in it (the
 * one's complement sum does not care), so no swapping ---- */
/* the one's complement sum of n bytes added to s, the carry folded back at
 * every word (ADD, ADC 0): an 8088 does in 5 instructions a word what the C
 * did in a 32-bit library-free but slow loop. A trailing odd byte counts as
 * the low byte of a word, as the machine's order wants. fold: the checksum. */
static uint16_t sum16(const uint8_t *p, uint16_t n, uint16_t s) {
  _asm {
    mov si, p
    mov bx, n
    mov ax, s
    mov cx, bx
    shr cx, 1
    jcxz last
  next:
    add ax, [si]
    adc ax, 0
    add si, 2
    dec cx
    jnz next
  last:
    test bl, 1
    jz done
    mov bl, [si]
    xor bh, bh
    add ax, bx
    adc ax, 0
  done:
    mov s, ax
  }
  return s;
}
#define fold(s) ((uint16_t)~(s))

/* network order, inline: a load and a byte swap */
static uint16_t be16(const uint8_t *p);
#pragma aux be16 = "mov ax,[bx]" "xchg al,ah" parm [bx] value [ax] modify exact [ax];
static uint32_t be32(const uint8_t *p);
#pragma aux be32 = "mov dx,[bx]" "xchg dl,dh" "mov ax,[bx+2]" "xchg al,ah" parm [bx] value [dx ax] modify exact [dx ax];
static void put16(uint8_t *p, uint16_t v);
#pragma aux put16 = "xchg al,ah" "mov [bx],ax" parm [bx] [ax] modify exact [ax];
static void put32(uint8_t *p, uint32_t v);
#pragma aux put32 = "xchg dl,dh" "mov [bx],dx" "xchg al,ah" "mov [bx+2],ax" parm [bx] [dx ax] modify exact [dx ax];

/* the rings: n bytes in or out at a position, in at most two block copies */
static void ring_in(uint8_t *ring, uint16_t size, uint16_t at, const uint8_t *src, uint16_t n) {
  uint16_t first = (uint16_t)(size - at);
  if (first > n) first = n;
  memcpy(ring + at, src, first);
  if (n > first) memcpy(ring, src + first, n - first);
}
static void ring_out(uint8_t *dst, const uint8_t *ring, uint16_t size, uint16_t at, uint16_t n) {
  uint16_t first = (uint16_t)(size - at);
  if (first > n) first = n;
  memcpy(dst, ring + at, first);
  if (n > first) memcpy(dst + first, ring, n - first);
}

/* ---- addresses ---- */
static uint8_t ip[4], mask[4], gw[4], dns[4];
static uint8_t configured;                /* ip known (DHCP's or the command line's) */
static const uint8_t bcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static const uint8_t zero4[4];
static uint8_t hop_ip[4], hop_mac[6], hop_ok;   /* the ARP entry */

static void eth(const uint8_t *dst_mac, uint16_t type) {
  memcpy(tx, dst_mac, 6); memcpy(tx + 6, my_mac, 6); put16(tx + 12, type);
}

static void arp(uint16_t op, const uint8_t *tha, const uint8_t *tpa) {
  static const uint8_t head[8] = {0, 1, 8, 0, 6, 4, 0, 0};
  eth(op == 1 ? bcast_mac : tha, 0x0806);
  memcpy(tx + 14, head, 8); tx[21] = (uint8_t)op;
  memcpy(tx + 22, my_mac, 6); memcpy(tx + 28, ip, 4);
  memcpy(tx + 32, op == 1 ? bcast_mac : tha, 6); memcpy(tx + 38, tpa, 4);
  if (op == 1) memset(tx + 32, 0, 6);
  send_frame(42);
}

static uint16_t ip_id;
static void ip_head(const uint8_t *dst_mac, const uint8_t *dst, uint8_t proto, uint16_t len) {   /* len: IP's payload */
  uint8_t *h = tx + 14;
  eth(dst_mac, 0x0800);
  h[0] = 0x45; h[1] = 0; put16(h + 2, (uint16_t)(20 + len)); put16(h + 4, ++ip_id);
  h[6] = 0x40; h[7] = 0; h[8] = 64; h[9] = proto; h[10] = h[11] = 0;   /* don't fragment, TTL 64 */
  memcpy(h + 12, ip, 4); memcpy(h + 16, dst, 4);
  *(uint16_t *)(h + 10) = fold(sum16(h, 20, 0));
}

/* UDP from tx+42 (len bytes there already), no checksum (IPv4 allows it) */
static void udp(const uint8_t *dst_mac, const uint8_t *dst, uint16_t sport, uint16_t dport, uint16_t len) {
  uint8_t *u = tx + 34;
  ip_head(dst_mac, dst, 17, (uint16_t)(8 + len));
  put16(u, sport); put16(u + 2, dport); put16(u + 4, (uint16_t)(8 + len)); put16(u + 6, 0);
  send_frame((uint16_t)(42 + len));
}

/* ---- TCP's state ---- */
#define RXQ 4096u                         /* powers of two */
#define TXQ 2048u
enum { T_CLOSED, T_SYN, T_EST };
static uint8_t state, peer_fin, reset;
static uint8_t rip[4];                    /* the far end */
static uint16_t lport, rport, mss = 536, snd_wnd;
static uint32_t iss, snd_una, rcv_nxt;
static uint8_t rxq[RXQ], txq[TXQ];
static uint16_t rx_head, rx_tail;         /* rxq: head the next to write */
static uint16_t tq_start, tq_len, tq_sent;   /* txq: snd_una's byte, the bytes unacknowledged or unsent, of them sent */
static uint32_t rtx_at;                   /* when the oldest unacknowledged byte went (the timer runs when tq_sent) */
static uint16_t rto = 1000, srtt;         /* ms */
static uint8_t retried;                   /* sent again since rtx_at (no round trip measured from it: Karn) */
static uint8_t dup_acks;                  /* bare ACKs of nothing new in a row: the far end got a later segment (fast retransmit) */
static uint16_t adv_wnd;                  /* the window last told the far end */
static uint16_t last_lport;               /* the connection closed: its stray segments answered with RST */
static uint8_t ooo;                       /* an island of data received beyond a gap, already in rxq past rx_head: */
static uint32_t ooo_seq, ooo_end;         /* its sequence numbers [ooo_seq, ooo_end) */

static uint16_t rx_free(void) { return (uint16_t)(RXQ - 1 - ((rx_head - rx_tail) & (RXQ - 1))); }

/* a segment: flags, the payload from txq at off (n bytes), the MSS option on a SYN */
static uint8_t ack_due;                   /* in-order data not yet acknowledged: the next segment out carries it */
static uint32_t ack_since;
#define ACK_DELAY 70                      /* ms: over a frame: the keys, sent every 60 ms, carry the ACK (40 ms sent it alone, before them) */

static void segment(uint8_t flags, uint32_t seq, uint16_t off, uint16_t n) {
  uint8_t *t = tx + 34, pseudo[12];
  uint16_t hl = (uint16_t)(flags & 0x02 ? 24 : 20), k;
  if (!hop_ok) return;
  ip_head(hop_mac, rip, 6, (uint16_t)(hl + n));
  put16(t, lport); put16(t + 2, rport); put32(t + 4, seq); put32(t + 8, rcv_nxt);
  t[12] = (uint8_t)(hl << 2); t[13] = flags;
  adv_wnd = rx_free(); put16(t + 14, adv_wnd);
  if (flags & 0x10) ack_due = 0;
  t[16] = t[17] = t[18] = t[19] = 0;
  if (hl == 24) { t[20] = 2; t[21] = 4; put16(t + 22, 1460); }
  ring_out(t + hl, txq, TXQ, (uint16_t)(off & (TXQ - 1)), n);
  memcpy(pseudo, ip, 4); memcpy(pseudo + 4, rip, 4); pseudo[8] = 0; pseudo[9] = 6; put16(pseudo + 10, (uint16_t)(hl + n));
  *(uint16_t *)(t + 16) = fold(sum16(t, (uint16_t)(hl + n), sum16(pseudo, 12, 0)));
#ifdef PKT_DROP                           /* the retransmission's test: every PKT_DROP-th segment lost going out */
  { static uint16_t c; if (++c % PKT_DROP == 0) { pc_modem_tx += n; return; } }
#endif
  send_frame((uint16_t)(34 + hl + n));
  pc_modem_tx += n;
}

static void ack(void) { segment(0x10, snd_una + tq_sent, 0, 0); }

static void output(void) {                /* what the window allows of the unsent, in segments */
  while (state == T_EST && tq_sent < tq_len && tq_sent < snd_wnd) {
    uint16_t n = (uint16_t)(tq_len - tq_sent);
    if (n > mss) n = mss;
    if (n > snd_wnd - tq_sent) n = (uint16_t)(snd_wnd - tq_sent);
    if (!tq_sent) { rtx_at = pc_ms(); retried = 0; }
    segment(0x18, snd_una + tq_sent, (uint16_t)(tq_start + tq_sent), n);   /* ACK PSH */
    tq_sent = (uint16_t)(tq_sent + n);
  }
}

static void tcp_in(const uint8_t *h, const uint8_t *t, uint16_t len) {   /* len: TCP header and data */
  uint16_t hl = (uint16_t)((t[12] >> 4) * 4), n, skip = 0;
  uint8_t flags = t[13];
  uint32_t seq = be32(t + 4), ackn = be32(t + 8);
  const uint8_t *data = t + hl;
  if (hl < 20 || hl > len || be16(t + 2) != lport || memcmp(h + 12, rip, 4) || be16(t) != rport || state == T_CLOSED) {
    if (be16(t + 2) == last_lport && last_lport && !(flags & 0x04)) {   /* the old connection's: RST */
      uint32_t r = rcv_nxt;                 /* the live connection's addresses kept */
      uint16_t lp = lport, rp = rport;
      uint8_t ri[4];
      memcpy(ri, rip, 4);
      rcv_nxt = seq + (len - hl) + (flags & 0x03 ? 1 : 0);
      memcpy(rip, h + 12, 4); lport = last_lport; rport = be16(t);
      segment(0x14, ackn, 0, 0);
      rcv_nxt = r; lport = lp; rport = rp; memcpy(rip, ri, 4);
    }
    return;
  }
  n = (uint16_t)(len - hl);
  if (flags & 0x04) { state = T_CLOSED; reset = 1; return; }   /* RST */
  if (state == T_SYN) {
    uint16_t k;
    if ((flags & 0x12) != 0x12 || ackn != iss + 1) return;
    rcv_nxt = seq + 1; snd_una = iss + 1; snd_wnd = be16(t + 14);
    for (k = 20; k + 3 < hl; ) {             /* the MSS option */
      if (t[k] == 0) break;
      if (t[k] == 1) { k++; continue; }
      if (t[k] == 2 && t[k + 1] == 4) { mss = be16(t + k + 2); if (mss > 1460) mss = 1460; }
      if (t[k + 1] < 2) break;
      k = (uint16_t)(k + t[k + 1]);
    }
    if (mss > TXQ / 2) mss = TXQ / 2;
    state = T_EST; tq_start = tq_len = tq_sent = 0;
    { uint16_t r = (uint16_t)(pc_ms() - rtx_at); if (!retried) { srtt = r; rto = (uint16_t)(r * 2 < 100 ? 100 : r * 2); } }
    ack();
    return;
  }
  if (flags & 0x10) {                       /* ACK: what it covers leaves the queue */
    uint32_t a = ackn - snd_una;
    if (a && a <= tq_sent) {
      if (!retried && a == tq_sent) {       /* a round trip measured */
        uint16_t r = (uint16_t)(pc_ms() - rtx_at);
        srtt = srtt ? (uint16_t)((srtt * 7u + r) / 8) : r;
        rto = (uint16_t)(srtt * 2 < 100 ? 100 : srtt * 2 > 3000 ? 3000 : srtt * 2);
      }
      tq_start = (uint16_t)((tq_start + a) & (TXQ - 1)); tq_len = (uint16_t)(tq_len - a); tq_sent = (uint16_t)(tq_sent - a);
      snd_una = ackn;
      if (tq_sent) { rtx_at = pc_ms(); retried = 0; }
    }
    else if (!a && !n && tq_sent && !(flags & 0x03) && ++dup_acks == 2) {   /* the second duplicate: the oldest lost */
      tq_sent = 0; output(); retried = 1;
    }
    if (a) dup_acks = 0;
    if (ackn == snd_una) snd_wnd = be16(t + 14);
  }
  if (n || (flags & 0x01)) {                /* data, FIN: in order only */
    uint32_t d = rcv_nxt - seq;
    uint8_t later = 0;                      /* in order, nothing odd: the ACK may wait for our next segment */
    if ((int32_t)d >= 0 && d < (uint32_t)n + 1) {
      uint16_t f = rx_free(), k;
      later = d == 0 && !(flags & 0x01) && !ooo;
      skip = (uint16_t)d;
      n = (uint16_t)(n - (skip < n ? skip : n));
      if (n > f) n = f;
      ring_in(rxq, RXQ, rx_head, data + skip, n); rx_head = (uint16_t)((rx_head + n) & (RXQ - 1));
      rcv_nxt += n;
      pc_modem_rx += n;
      if (ooo && (int32_t)(rcv_nxt - ooo_seq) >= 0) {   /* the gap filled: the island joins */
        if ((int32_t)(ooo_end - rcv_nxt) > 0) {
          uint16_t m = (uint16_t)(ooo_end - rcv_nxt);
          rx_head = (uint16_t)((rx_head + m) & (RXQ - 1)); rcv_nxt = ooo_end; pc_modem_rx += m;
        }
        ooo = 0;
      }
      if ((flags & 0x01) && seq + (len - hl) == rcv_nxt) { rcv_nxt++; peer_fin = 1; }
    } else if ((int32_t)d < 0 && n) {      /* beyond a gap: kept (one island), the gap to come again */
      uint32_t off = seq - rcv_nxt;
      if (off + n < rx_free() && (!ooo || ((int32_t)(seq - ooo_end) <= 0 && (int32_t)(seq + n - ooo_seq) >= 0))) {
        uint16_t k, at = (uint16_t)((rx_head + (uint16_t)off) & (RXQ - 1));
        ring_in(rxq, RXQ, at, data, n);
        if (!ooo) { ooo = 1; ooo_seq = seq; ooo_end = seq + n; }
        else {
          if ((int32_t)(seq - ooo_seq) < 0) ooo_seq = seq;
          if ((int32_t)(seq + n - ooo_end) > 0) ooo_end = seq + n;
        }
      }
    }
    if (later) { if (!ack_due) { ack_due = 1; ack_since = pc_ms(); } }
    else ack();                             /* a gap, old data, a FIN: at once (the sender's fast retransmit counts on it) */
  }
  output();
}

/* ---- DHCP and DNS replies, by the waits ---- */
static uint32_t dhcp_xid;
static uint8_t dhcp_got;                  /* 2 an offer, 5 an ACK, 6 a NAK */
static uint8_t offer_ip[4], offer_server[4];
static uint16_t dns_id, dns_port;
static uint8_t dns_got, dns_answer[4];    /* dns_got: 1 an address, 2 none */

static void dhcp_in(const uint8_t *d, uint16_t n) {
  uint16_t k = 240;
  uint8_t type = 0, m[4] = {255, 255, 255, 0}, r[4] = {0}, s[4] = {0}, srv[4] = {0};
  if (n < 240 || d[0] != 2 || be32(d + 4) != dhcp_xid || memcmp(d + 28, my_mac, 6)) return;
  while (k + 1 < n && d[k] != 255) {
    uint8_t o = d[k], l;
    if (o == 0) { k++; continue; }
    l = d[k + 1];
    if (k + 2 + l > n) break;
    if (o == 53) type = d[k + 2];
    else if (o == 1 && l >= 4) memcpy(m, d + k + 2, 4);
    else if (o == 3 && l >= 4) memcpy(r, d + k + 2, 4);
    else if (o == 6 && l >= 4) memcpy(s, d + k + 2, 4);
    else if (o == 54 && l >= 4) memcpy(srv, d + k + 2, 4);
    k = (uint16_t)(k + 2 + l);
  }
  if (type == 2 && !dhcp_got) { memcpy(offer_ip, d + 16, 4); memcpy(offer_server, srv, 4); dhcp_got = 2; }
  else if (type == 5 && dhcp_got == 2) {
    memcpy(ip, d + 16, 4);
    if (!memcmp(pc_mask, zero4, 4)) memcpy(mask, m, 4);
    if (!memcmp(pc_gw, zero4, 4)) memcpy(gw, r, 4);
    if (!memcmp(pc_dns, zero4, 4)) memcpy(dns, s, 4);
    dhcp_got = 5;
  }
  else if (type == 6 && dhcp_got == 2) dhcp_got = 6;
}

static void dns_in(const uint8_t *d, uint16_t n) {
  uint16_t k = 12, an, q;
  if (n < 12 || be16(d) != dns_id || !(d[2] & 0x80)) return;
  if (d[3] & 0x0f) { dns_got = 2; return; }   /* an error: no such name */
  q = be16(d + 4); an = be16(d + 6);
  for (; q; q--) {                          /* the question(s): a name, type, class */
    while (k < n && d[k] && (d[k] & 0xc0) != 0xc0) k = (uint16_t)(k + d[k] + 1);
    k = (uint16_t)(k + ((k < n && d[k]) ? 2 : 1) + 4);
  }
  for (; an && k + 10 <= n; an--) {
    uint16_t l;
    while (k < n && d[k] && (d[k] & 0xc0) != 0xc0) k = (uint16_t)(k + d[k] + 1);
    k = (uint16_t)(k + ((k < n && d[k]) ? 2 : 1));
    if (k + 10 > n) break;
    l = be16(d + k + 8);
    if (be16(d + k) == 1 && be16(d + k + 2) == 1 && l == 4 && k + 14 <= n) {
      memcpy(dns_answer, d + k + 10, 4); dns_got = 1; return;
    }
    k = (uint16_t)(k + 10 + l);             /* a CNAME and the like: on to the next */
  }
  dns_got = 2;
}

/* ---- the frames received ---- */
static uint8_t rx[FRAME];
static void poll(void) {
#ifdef PC_NETTEST
  uint32_t t0 = pc_time_now(), ts = pkt_send_counts;   /* (the ACKs sent meanwhile counted as sends, not here) */
#endif
  while (slot_len[slot_rd]) {
    uint16_t n = slot_len[slot_rd];
    uint8_t *h = rx + 14;
#ifdef PC_NETTEST
    pkt_rcvd++;
#endif
    _fmemcpy(rx, slot + slot_off[slot_rd], n);
    slot_len[slot_rd] = 0;
    if (++slot_rd == NSLOT) slot_rd = 0;
    if (n < 42) continue;
    if (be16(rx + 12) == 0x0806) {          /* ARP */
      if (be16(h) != 1 || be16(h + 2) != 0x0800) continue;
      if (configured && memcmp(hop_ip, zero4, 4) && !memcmp(h + 14, hop_ip, 4)) { memcpy(hop_mac, h + 8, 6); hop_ok = 1; }   /* the next hop's, a reply or not */
      if (be16(h + 6) == 1 && configured && !memcmp(h + 24, ip, 4)) arp(2, h + 8, h + 14);
      continue;
    }
    if (be16(rx + 12) != 0x0800 || (h[0] & 0xf0) != 0x40) continue;
    {
      uint16_t hl = (uint16_t)((h[0] & 15) * 4), tl = be16(h + 2);
      uint8_t *p = h + hl;
      if (hl < 20 || tl < hl || tl > n - 14 || (be16(h + 6) & 0x3fff)) continue;   /* short, or a fragment */
      if (fold(sum16(h, hl, 0))) continue;
      if (configured ? memcmp(h + 16, ip, 4) != 0 : 0) continue;   /* not ours (before DHCP: anything) */
      tl = (uint16_t)(tl - hl);
      if (h[9] == 6 && configured) {
        uint8_t pseudo[12];
        memcpy(pseudo, h + 12, 8); pseudo[8] = 0; pseudo[9] = 6; put16(pseudo + 10, tl);
#ifdef PKT_DROP                           /* and every (PKT_DROP+2)-th coming in */
        { static uint16_t c; if (++c % (PKT_DROP + 2) == 0) continue; }
#endif
        if (!fold(sum16(p, tl, sum16(pseudo, 12, 0)))) tcp_in(h, p, tl);
      } else if (h[9] == 17 && tl >= 8) {
        uint16_t dport = be16(p + 2);
        if (dport == 68) dhcp_in(p + 8, (uint16_t)(tl - 8));
        else if (dport == dns_port && configured) dns_in(p + 8, (uint16_t)(tl - 8));
      } else if (h[9] == 1 && configured && tl >= 8 && p[0] == 8 && tl <= FRAME - 34) {   /* ICMP echo: answered */
        memcpy(tx + 34, p, tl);
        tx[34] = 0; tx[36] = tx[37] = 0;
        *(uint16_t *)(tx + 36) = fold(sum16(tx + 34, tl, 0));
        ip_head(rx + 6, h + 12, 1, tl);
        send_frame((uint16_t)(34 + tl));
      }
    }
  }
#ifdef PC_NETTEST
  pkt_rx_counts += (pc_time_now() - t0) - (pkt_send_counts - ts);
#endif
}

/* ---- the next hop's MAC, DHCP, DNS ---- */
static uint8_t resolve_hop(const uint8_t *dst) {   /* 1: hop_mac known */
  uint8_t k, on_link = 1, try;
  for (k = 0; k < 4; k++) if ((dst[k] ^ ip[k]) & mask[k]) on_link = 0;
  if (!on_link && !memcmp(gw, zero4, 4)) return 0;
  if (hop_ok && !memcmp(hop_ip, on_link ? dst : gw, 4)) return 1;
  memcpy(hop_ip, on_link ? dst : gw, 4); hop_ok = 0;
  for (try = 0; try < 4 && !hop_ok; try++) {
    uint32_t t = pc_ms();
    arp(1, bcast_mac, hop_ip);
    while (!hop_ok && pc_ms() - t < 700) poll();
  }
  return hop_ok;
}

static void dhcp_send(uint8_t type) {
  uint8_t *d = tx + 42, *o = d + 240;
  memset(d, 0, 300);
  d[0] = 1; d[1] = 1; d[2] = 6; put32(d + 4, dhcp_xid); d[10] = 0x80;   /* the reply broadcast: we have no address yet */
  memcpy(d + 28, my_mac, 6);
  put32(d + 236, 0x63825363UL);
  *o++ = 53; *o++ = 1; *o++ = type;
  if (type == 3) {
    *o++ = 50; *o++ = 4; memcpy(o, offer_ip, 4); o += 4;
    *o++ = 54; *o++ = 4; memcpy(o, offer_server, 4); o += 4;
  }
  *o++ = 55; *o++ = 3; *o++ = 1; *o++ = 3; *o++ = 6;
  *o = 255;
  { static const uint8_t all[4] = {255, 255, 255, 255}; udp(bcast_mac, all, 68, 67, 300); }
}

static uint8_t dhcp(void) {               /* 1: configured */
  uint8_t try;
  memset(ip, 0, 4);
  for (try = 0; try < 3; try++) {          /* 4.5 s at most where nothing answers (a server answers in milliseconds) */
    uint32_t t;
    dhcp_xid = pc_ms() ^ ((uint32_t)my_mac[4] << 24 | (uint32_t)my_mac[5] << 16) ^ 0x42e7UL;
    dhcp_got = 0;
    dhcp_send(1);
    for (t = pc_ms(); dhcp_got != 2 && pc_ms() - t < 1500; ) poll();
    if (dhcp_got != 2) continue;
    dhcp_send(3);
    for (t = pc_ms(); dhcp_got == 2 && pc_ms() - t < 1500; ) poll();
    if (dhcp_got == 5) return 1;
  }
  return 0;
}

uint8_t pc_parse_ip(const char *s, uint8_t *out) {   /* plat_pc.c's command line too */   /* a dotted quad: 1 */
  uint8_t k;
  for (k = 0; k < 4; k++) {
    uint16_t v = 0, digits = 0;
    while (*s >= '0' && *s <= '9' && digits < 4) { v = (uint16_t)(v * 10 + (*s++ - '0')); digits++; }
    if (!digits || v > 255) return 0;
    out[k] = (uint8_t)v;
    if (k < 3 && *s++ != '.') return 0;
  }
  return *s == 0;
}

static uint8_t dns_lookup(const char *name, uint8_t *out) {   /* 1: the address */
  uint8_t try;
  if (pc_parse_ip(name, out)) return 1;
  if (!memcmp(dns, zero4, 4) || strlen(name) > 200 || !resolve_hop(dns)) return 0;
  for (try = 0; try < 3; try++) {
    uint8_t *d = tx + 42, *l, *o = d + 13;
    const char *s;
    uint32_t t;
    dns_id = (uint16_t)(pc_ms() * 31 + 0x1d5);
    dns_port = (uint16_t)(0xc000 | (dns_id & 0x3fff));
    memset(d, 0, 12); put16(d, dns_id); d[2] = 1; d[5] = 1;   /* recursion wanted, one question */
    l = d + 12; *l = 0;
    for (s = name; *s; s++) {
      if (*s == '.') { l = o++; *l = 0; }
      else { *o++ = (uint8_t)*s; (*l)++; }
    }
    *o++ = 0; put16(o, 1); put16(o + 2, 1); o += 4;
    dns_got = 0;
    udp(hop_mac, dns, dns_port, 53, (uint16_t)(o - d));
    for (t = pc_ms(); !dns_got && pc_ms() - t < 2000; ) poll();
    if (dns_got == 1) { memcpy(out, dns_answer, 4); return 1; }
    if (dns_got == 2) return 0;
  }
  return 0;
}

/* ---- tcp.h ---- */
static void cleanup(void) {
  union REGS r;
  struct SREGS s;
  uint8_t k;
  if (state == T_EST) { segment(0x14, snd_una + tq_sent, 0, 0); state = T_CLOSED; }   /* RST: the relay sees us gone */
  for (k = 0; k < 2; k++) if (handle[k]) { segread(&s); r.h.ah = 3; r.x.bx = handle[k]; driver(&r, &s); handle[k] = 0; }
}

uint8_t pkt_present(void) {               /* a packet driver: found, our two types taken, the MAC read */
  static const uint8_t types[2][2] = {{0x08, 0x00}, {0x08, 0x06}};
  union REGS r;
  struct SREGS s;
  uint8_t v, k;
  if (pkt_int) return 1;
  for (v = 0x60; v <= 0x80 && !pkt_int; v++) {
    const char __far *p = (const char __far *)_dos_getvect(v);
    if (p && !_fmemcmp(p + 3, "PKT DRVR", 8)) { pkt_int = v; drv_vec = (uint32_t)FP_SEG(p) << 16 | FP_OFF(p); }
  }
  if (!pkt_int || (!slot && !(slot = (uint8_t __far *)_fmalloc((uint16_t)NSLOT * FRAME)))) { pkt_int = 0; return 0; }
  for (k = 0; k < NSLOT; k++) slot_off[k] = (uint16_t)(k * FRAME);
  for (k = 0; k < 2; k++) {
    segread(&s);
    r.h.ah = 2; r.h.al = 1; r.x.bx = 0xffff; r.h.dl = 0;     /* class 1 (Ethernet), any type of card, the first */
    r.x.si = (uint16_t)types[k]; r.x.cx = 2;
    s.es = FP_SEG(receiver); r.x.di = FP_OFF(receiver);
    if (driver(&r, &s)) { cleanup(); pkt_int = 0; return 0; }
    handle[k] = r.x.ax;
  }
  pc_net_cleanup = cleanup;
  segread(&s);
  r.h.ah = 6; r.x.bx = handle[0]; s.es = s.ds; r.x.di = (uint16_t)my_mac; r.x.cx = 6;
  if (driver(&r, &s)) { cleanup(); pkt_int = 0; return 0; }
  return 1;
}

uint8_t pkt_open(const char *host, uint16_t port) {
  uint32_t t;
  if (!pkt_int) return 1;
  if (state != T_CLOSED) { segment(0x14, snd_una + tq_sent, 0, 0); state = T_CLOSED; }
  if (!configured) {
    memcpy(ip, pc_ip, 4); memcpy(mask, pc_mask, 4); memcpy(gw, pc_gw, 4); memcpy(dns, pc_dns, 4);
    if (!memcmp(mask, zero4, 4)) { mask[0] = mask[1] = mask[2] = 255; }
    if (memcmp(ip, zero4, 4) == 0 && !dhcp()) return 2;
    configured = 1;
  }
  if (!dns_lookup(host, rip)) return 3;
  if (!resolve_hop(rip)) return 4;
  rport = port;
  lport = (uint16_t)(1024 + ((pc_ms() + lport * 7u) & 0x3fff));
  if (lport == last_lport) lport++;
  iss = pc_ms() * 4093UL + ((uint32_t)my_mac[5] << 24);
  snd_una = iss; rcv_nxt = 0; snd_wnd = 0; mss = 536;
  rx_head = rx_tail = 0; tq_start = tq_len = tq_sent = 0;
  peer_fin = reset = 0; retried = 0; srtt = 0; rto = 1000; ooo = 0; dup_acks = 0;
  state = T_SYN;
  for (t = pc_ms(); state == T_SYN && pc_ms() - t < 15000UL; ) {
    uint32_t w;
    rtx_at = pc_ms();
    segment(0x02, iss, 0, 0);               /* SYN */
    for (w = pc_ms(); state == T_SYN && pc_ms() - w < rto; ) poll();
    if (state == T_SYN) { retried = 1; if (rto < 4000) rto = (uint16_t)(rto * 2); }
  }
  if (state != T_EST) { state = T_CLOSED; return 4; }
  return 0;
}

static void retransmit(void) {            /* the oldest unacknowledged too old: all of it again; an ACK held too long out */
  if (ack_due && state == T_EST && pc_ms() - ack_since >= ACK_DELAY) ack();
  if (state == T_EST && tq_sent && pc_ms() - rtx_at > rto) {
    tq_sent = 0; retried = 1;
    if (rto < 3000) rto = (uint16_t)(rto * 2);
    output();
    rtx_at = pc_ms(); retried = 1;
  }
}

uint8_t pkt_send(const uint8_t *buf, uint16_t n) {
  uint32_t t = pc_ms();
  while (n) {
    uint16_t f, k;
    poll(); retransmit();
    if (state != T_EST || peer_fin) return 1;
    f = (uint16_t)(TXQ - 1 - tq_len);
    if (!f) { if (pc_ms() - t > 5000) return 1; continue; }   /* the queue full: the far end is slow */
    if (f > n) f = n;
    ring_in(txq, TXQ, (uint16_t)((tq_start + tq_len) & (TXQ - 1)), buf, f);
    tq_len = (uint16_t)(tq_len + f); buf += f; n = (uint16_t)(n - f);
    output();
  }
  return 0;
}

int16_t pkt_recv(uint8_t *buf, uint16_t max) {
  int16_t n = 0;
  poll(); retransmit();
  n = (int16_t)((rx_head - rx_tail) & (RXQ - 1));
  if (n > (int16_t)max) n = (int16_t)max;
  if (n) { ring_out(buf, rxq, RXQ, rx_tail, (uint16_t)n); rx_tail = (uint16_t)((rx_tail + n) & (RXQ - 1)); }
  if (n && state == T_EST && adv_wnd < RXQ / 4 && rx_free() >= RXQ / 2) ack();   /* the window opened again: told */
  if (!n && (reset || peer_fin || state != T_EST)) return -1;
  return n;
}

void pkt_close(void) {
  if (state == T_EST) {
    uint32_t t = pc_ms();
    while (tq_len && state == T_EST && pc_ms() - t < 1000) { poll(); retransmit(); }   /* the last bytes out */
    if (state == T_EST) segment(0x11, snd_una + tq_sent, 0, 0);   /* FIN ACK */
  }
  last_lport = lport;
  state = T_CLOSED;
}

#ifdef PC_NETTEST
void pkt_debug(char *out) {               /* plat_pc.c's network test, when the game stands still */
  uint8_t k, used = 0;
  for (k = 0; k < NSLOT; k++) if (slot_len[k]) used++;
  sprintf(out, "st %u fin %u rst %u una %lu q %u sent %u wnd %u rto %u nxt %lu ooo %u %lu-%lu rxq %u slots %u wr %u rd %u hop %u age %lu",
          state, peer_fin, reset, (unsigned long)(snd_una - iss), tq_len, tq_sent, snd_wnd, rto, (unsigned long)rcv_nxt, ooo,
          (unsigned long)ooo_seq, (unsigned long)ooo_end, (uint16_t)((rx_head - rx_tail) & (RXQ - 1)), used, slot_wr, slot_rd, hop_ok,
          (unsigned long)(pc_ms() - rtx_at));
}
#endif
