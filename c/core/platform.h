/* Platform interface: everything the core needs from the machine.
 * A port implements these in platform/<name>/ and describes itself in
 * platform/<name>/plat_config.h.
 *
 * Screen contract: the core draws logical codes into draw_buf, 40 x 25 cells.
 * Rows 0..23 are the field (row 23 is the bottom wall), row 24 is the status
 * line. flush_screen() shows draw_buf and clears it; a machine with 24 text
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
void plat_player_colour(uint8_t x, uint8_t y, uint8_t player);   /* after the flush: cell in the player's colour */

/* ---- optional assembly versions of core loops (plat_config.h: PLAT_ASM_*) ---- */
void composite_map(void);                      /* non-space map cells over draw_buf */
void hash_run(void);                           /* hash byte loop over hash_ptr/hash_n into hh */
#endif
