/* Platform interface: everything the core needs from the machine.
 * A port implements these in platform/<name>/ and describes itself in
 * platform/<name>/plat_config.h.
 *
 * Screen contract: the core draws logical codes into draw_buf, 40 x 25 cells.
 * Rows 0..23 are the field (row 23 is the bottom wall), row 24 is the status
 * line. flush_screen() shows draw_buf and clears it (in a game, title_mode 0,
 * clearing the status line is enough: every game frame starts with
 * composite_frame, which overwrites rows 0-23); a machine with 24 text
 * rows (PLAT_ROWS 24) shows row 24 on top of row 23, the wall, without
 * touching draw_buf, because the logic reads the wall cells there. */
#ifndef PLATFORM_H
#define PLATFORM_H
#include <stdint.h>
#include "plat_config.h"

/* ---- input: key masks ---- */
#define KEY_UP    0x01
#define KEY_DOWN  0x02
#define KEY_RIGHT 0x04
#define KEY_LEFT  0x08
#define KEY_SPACE 0x10          /* fire */
extern uint8_t kbd_alt_fire;    /* set by the core: key set A fires with its alternative key
                                   (two keyboard players share the keyboard) */
uint8_t plat_keys_a(void);      /* key set A (several keys at once) */
uint8_t plat_keys_b(void);      /* key set B */
uint8_t plat_key_char(void);    /* text entry: 'A'..'Z', 8 = delete, 0x1b = cancel, 0 = none */
uint8_t plat_break(void);       /* the cancel key of plat_key_char is down: a quick test, every game frame */
uint8_t plat_joy(uint8_t n);    /* joystick 0/1 as a key mask, kind from joy_type */

/* The four input sources of the menu (INPUT_KBD_A, INPUT_KBD_B, INPUT_JOY1,
 * INPUT_JOY2) and the JOYSTICK row, which picks the kind of stick. What the
 * sources are, which of them can be used with the chosen kind, and what they
 * are called is the machine's business. */
#define JOY_NONE 0              /* joy_type 0; 1..PLAT_JOY_TYPES-1 are the platform's kinds */
extern uint8_t joy_type;
extern const char *const plat_joy_names[PLAT_JOY_TYPES];
uint8_t plat_input_allowed(uint8_t input);         /* usable with the current joy_type */
const char *plat_input_name(uint8_t input);        /* 16 characters, with the current joy_type */
extern const char *const plat_kbd_a_alt_name;      /* key set A with the alternative fire key */

/* ---- time and sound ---- */
void plat_init(void);                          /* once at start: timers */
void plat_frame_sync(void);                    /* wait until FRAME_MS passed since the last call */
void plat_tone(uint16_t ratio, uint8_t len);   /* short tone; ratio as the MZ monitor's divider */
void plat_delay(void);

/* ---- screen ---- */
void flush_screen(void);                       /* draw_buf -> screen (changed cells), clears draw_buf */
#define PLAYER_FLASH 4                    /* with the player's number (0..3): the bomb's colour instead (the drop pose's frame;
                                             a machine whose bomb is that player's colour shows white) */
void plat_player_colour(uint8_t x, uint8_t y, uint8_t player);   /* after the flush: cell in the player's colour, or PLAYER_FLASH */
#ifdef PLAT_RUNAHEAD                      /* a machine that shows a frame ahead of the agreed one (docs/rollback-plan.md) */
extern uint8_t plat_frame_idle;           /* set by plat_frame_sync: the ms it waited for the tick, 0 when the frame ran late */
#endif
void plat_hud_colour(uint8_t x, uint8_t n, uint8_t player, uint8_t blank);
                         /* after the flush: status-line cells x..x+n-1 in the player's colour, where the
                          * platform can do it cleanly; blank: bit 0 cell x-1 is blank, bit 1 cell x+n */

/* ---- optional assembly versions of core loops (plat_config.h: PLAT_ASM_*) ---- */
void composite_map(void);                      /* non-space map cells over draw_buf */
void composite_frame(void);                    /* the same at the start of a frame: rows 0-23 of draw_buf are blank */
void hash_run(void);                           /* hash byte loop over hash_ptr/hash_n into hh */
#endif
