#ifndef __CONSOLE_H__
#define __CONSOLE_H__

#include <stdint.h>

#define C7_BLACK 0x00
#define C7_BLUE 0x01
#define C7_RED 0x02
#define C7_PURPLE 0x03
#define C7_GREEN 0x04
#define C7_LIGHT_BLUE 0x05
#define C7_YELLOW 0x06
#define C7_WHITE 0x07

#define C8_BLACK 0x00
#define C8_BLUE 0x01
#define C8_RED 0x02
#define C8_MAGENTA 0x03
#define C8_GREEN 0x04
#define C8_CYAN 0x05
#define C8_YELLOW 0x06
#define C8_WHITE 0x07
#define C8_GRAY 0x08
#define C8_LIGHT_BLUE 0x09
#define C8_LIGHT_RED 0x0a
#define C8_LIGHT_MAGENTA 0x0b
#define C8_LIGHT_GREEN 0x0c
#define C8_LIGHT_CYAN 0x0d
#define C8_LIGHT_YELLOW 0x0e
#define C8_LIGHT_WHITE 0x0f

#define ATT(front, back) ((front)*16+(back))
#define ATT_ALT(front, back) (0x8000+(front)*16+(back))

void console_init(void);
void put_str_xy(uint8_t x, uint8_t y, const char *s);
void put_str_attr_xy(uint8_t x, uint8_t y, const char *s, uint8_t attr);
void put_char_attr_xy(uint8_t x, uint8_t y, char c, uint8_t attr);
void put_multi_char_xy(uint8_t x, uint8_t y, uint8_t c, uint8_t cnt);
void put_multi_attr_xy(uint8_t x, uint8_t y, uint8_t attr, uint8_t cnt);
void border(uint8_t color);
void clrscr(void);
void scroll_down(uint8_t first, uint8_t number);
void scroll_up(uint8_t first, uint8_t number);
uint8_t inkey(void);
void beep(void);
void loading_screen(const char* name);
void get_uppercase_extension(const char* filename, char* extension);

#endif
