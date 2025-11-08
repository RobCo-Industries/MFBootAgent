#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdint.h>
#include <stdarg.h>

// Terminal colors (VGA-style)
#define COLOR_BLACK     0
#define COLOR_GREEN     2
#define COLOR_AMBER     3
#define COLOR_WHITE     7

// Function declarations
void terminal_init(void);
void term_clear(void);
void term_print(const char* str);
void term_printf(const char* fmt, ...);
void term_set_color(uint8_t color);
char wait_for_key(void);

// Key codes
#define KEY_UP      0x80
#define KEY_DOWN    0x81
#define KEY_LEFT    0x82
#define KEY_RIGHT   0x83
#define KEY_ENTER   0x0D

#endif // TERMINAL_H
