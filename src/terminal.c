// src/terminal.c - Terminal I/O implementation

#include "terminal.h"
#include "hardware.h"
#include "mfboot.h"

static uint8_t current_color = COLOR_GREEN;

void terminal_init(void) {
    // UART already initialized by RETROS-BIOS
    // Just set default color
    current_color = COLOR_GREEN;
}

void term_clear(void) {
    // Send ANSI clear screen sequence
    uart_putc(27);  // ESC
    uart_putc('[');
    uart_putc('2');
    uart_putc('J');
    uart_putc(27);  // ESC
    uart_putc('[');
    uart_putc('H');
}

void term_print(const char* str) {
    while (*str) {
        uart_putc(*str++);
    }
}

void term_printf(const char* fmt, ...) {
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    
    // Simple printf implementation
    int i = 0;
    const char* p = fmt;
    
    while (*p && i < 255) {
        if (*p == '%') {
            p++;
            if (*p == 's') {
                // String
                const char* s = va_arg(args, const char*);
                while (*s && i < 255) {
                    buffer[i++] = *s++;
                }
            } else if (*p == 'd') {
                // Decimal integer
                int val = va_arg(args, int);
                if (val < 0) {
                    buffer[i++] = '-';
                    val = -val;
                }
                char temp[16];
                int j = 0;
                do {
                    temp[j++] = '0' + (val % 10);
                    val /= 10;
                } while (val > 0);
                while (j > 0 && i < 255) {
                    buffer[i++] = temp[--j];
                }
            } else if (*p == 'x' || *p == 'X') {
                // Hexadecimal
                uint32_t val = va_arg(args, uint32_t);
                char temp[16];
                int j = 0;
                do {
                    int digit = val & 0xF;
                    temp[j++] = digit < 10 ? '0' + digit : 'A' + digit - 10;
                    val >>= 4;
                } while (val > 0);
                while (j > 0 && i < 255) {
                    buffer[i++] = temp[--j];
                }
            } else if (*p == '0') {
                // Padded hex (e.g., %08X)
                p++;
                int width = *p - '0';
                p++;
                if (*p == 'x' || *p == 'X') {
                    uint32_t val = va_arg(args, uint32_t);
                    char temp[16];
                    int j = 0;
                    for (int k = 0; k < width; k++) {
                        int digit = (val >> ((width - 1 - k) * 4)) & 0xF;
                        temp[j++] = digit < 10 ? '0' + digit : 'A' + digit - 10;
                    }
                    while (j > 0 && i < 255) {
                        buffer[i++] = temp[--j];
                    }
                }
            } else if (*p == '%') {
                buffer[i++] = '%';
            }
            p++;
        } else {
            buffer[i++] = *p++;
        }
    }
    
    buffer[i] = '\0';
    va_end(args);
    term_print(buffer);
}

void term_set_color(uint8_t color) {
    current_color = color;
    // For now, we don't implement color codes
    // Would need ANSI escape sequences for framebuffer
}

char wait_for_key(void) {
    // Wait for and return a key press
    while (!uart_readable()) {
        // Busy wait
    }
    
    char c = uart_getc();
    
    // Handle escape sequences for arrow keys
    if (c == 27) {  // ESC
        if (uart_readable()) {
            char c2 = uart_getc();
            if (c2 == '[') {
                if (uart_readable()) {
                    char c3 = uart_getc();
                    switch (c3) {
                        case 'A': return KEY_UP;
                        case 'B': return KEY_DOWN;
                        case 'C': return KEY_RIGHT;
                        case 'D': return KEY_LEFT;
                    }
                }
            }
        }
    }
    
    return c;
}
