/*
 * Reading the text the previous stage left on the framebuffer. Pure
 * code: the host tests link it.
 *
 * RobCo-Core has no call that takes over a live framebuffer and its
 * console. MFBootAgent calls fb_init() and console_init() again, which
 * clears the screen. Before that, this file turns the pixels of each
 * 8x16 cell back into a character and an attribute, by matching them
 * against the console font. main.c then writes the cells again, so the
 * text above the menu stays and the console knows it.
 */
#ifndef MFB_SCREEN_H
#define MFB_SCREEN_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t ch;
    uint8_t attr;   /* CONSOLE_ATTR_* brightness, maybe ORed with CONSOLE_ATTR_INVERSE */
} mfb_cell_t;

/* Pixel words of one colour scheme: background, then dim, normal and bright. */
typedef struct {
    uint32_t bg;
    uint32_t level[3];
} mfb_palette_t;

/* The pixel word fb_rgb() writes for this colour in pixel order `order` (FB_ORDER_*). */
uint32_t mfb_pixel(uint32_t order, uint8_t r, uint8_t g, uint8_t b);

/* The palette of console scheme `scheme` in pixel order `order`. Returns -1 for an unknown scheme. */
int mfb_palette(unsigned scheme, uint32_t order, mfb_palette_t *out);

/*
 * Reads cols x rows cells from a 32-bit framebuffer at `fb` with `pitch`
 * bytes per row, into out[row * cols + col]. A cell that matches no
 * glyph in one colour of the palette becomes '?' with the normal
 * attribute. Returns the number of such cells.
 */
unsigned mfb_screen_read(const uint8_t *fb, uint32_t pitch, unsigned cols, unsigned rows,
                         const mfb_palette_t *pal, mfb_cell_t *out);

#endif
