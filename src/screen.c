#include "screen.h"

#include <robco/console.h>
#include <robco/fb.h>
#include <robco/font.h>

uint32_t mfb_pixel(uint32_t order, uint8_t r, uint8_t g, uint8_t b)
{
    /* The same words as fb_rgb() in RobCo-Core's src/drv/fb.c. */
    if (order == FB_ORDER_RGB) {
        return 0xff000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
    }
    return 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

int mfb_palette(unsigned scheme, uint32_t order, mfb_palette_t *out)
{
    const console_scheme_t *s = console_scheme_info(scheme);

    if (s == NULL) {
        return -1;
    }
    out->bg = mfb_pixel(order, s->bg.r, s->bg.g, s->bg.b);
    out->level[CONSOLE_ATTR_DIM] = mfb_pixel(order, s->dim.r, s->dim.g, s->dim.b);
    out->level[CONSOLE_ATTR_NORMAL] = mfb_pixel(order, s->normal.r, s->normal.g, s->normal.b);
    out->level[CONSOLE_ATTR_BRIGHT] = mfb_pixel(order, s->bright.r, s->bright.g, s->bright.b);
    return 0;
}

static int same_rows(const uint8_t *a, const uint8_t *b)
{
    unsigned i;

    for (i = 0; i < FONT_H; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

/* The first character whose glyph has exactly these rows, or 0. ASCII first. */
static uint8_t match_glyph(const uint8_t *rows)
{
    unsigned ch;

    for (ch = 0x20; ch <= 0x7e; ch++) {
        if (same_rows(rows, font8x16_glyph(ch))) {
            return (uint8_t)ch;
        }
    }
    for (ch = 0x80; ch <= 0xff; ch++) {
        if (same_rows(rows, font8x16_glyph(ch))) {
            return (uint8_t)ch;
        }
    }
    return 0;
}

/*
 * One cell. Every pixel must be the background or one level colour.
 * Set bits are the pixels that differ from the background; for an
 * inverse cell they are the background pixels.
 */
static int read_cell(const uint8_t *fb, uint32_t pitch, unsigned col, unsigned row,
                     const mfb_palette_t *pal, mfb_cell_t *out)
{
    uint8_t fg_rows[FONT_H];
    uint8_t bg_rows[FONT_H];
    int level = -1;
    unsigned x, y, l;
    uint8_t ch;

    for (y = 0; y < FONT_H; y++) {
        const uint32_t *px = (const uint32_t *)(const void *)
                             (fb + (size_t)(row * FONT_H + y) * pitch + (size_t)col * FONT_W * 4);
        uint8_t fg_bits = 0;
        uint8_t bg_bits = 0;

        for (x = 0; x < FONT_W; x++) {
            uint32_t p = px[x];

            if (p == pal->bg) {
                bg_bits |= (uint8_t)(0x80u >> x);
                continue;
            }
            if (level < 0) {
                for (l = 0; l < 3; l++) {
                    if (p == pal->level[l]) {
                        level = (int)l;
                        break;
                    }
                }
                if (level < 0) {
                    return -1;
                }
            } else if (p != pal->level[level]) {
                return -1;
            }
            fg_bits |= (uint8_t)(0x80u >> x);
        }
        fg_rows[y] = fg_bits;
        bg_rows[y] = bg_bits;
    }
    if (level < 0) {
        out->ch = ' ';
        out->attr = CONSOLE_ATTR_NORMAL;
        return 0;
    }
    ch = match_glyph(fg_rows);
    if (ch != 0) {
        out->ch = ch;
        out->attr = (uint8_t)level;
        return 0;
    }
    ch = match_glyph(bg_rows);
    if (ch != 0) {
        out->ch = ch;
        out->attr = (uint8_t)(level | CONSOLE_ATTR_INVERSE);
        return 0;
    }
    return -1;
}

unsigned mfb_screen_read(const uint8_t *fb, uint32_t pitch, unsigned cols, unsigned rows,
                         const mfb_palette_t *pal, mfb_cell_t *out)
{
    unsigned bad = 0;
    unsigned r, c;

    for (r = 0; r < rows; r++) {
        for (c = 0; c < cols; c++) {
            mfb_cell_t *cell = &out[r * cols + c];

            if (read_cell(fb, pitch, c, r, pal, cell) != 0) {
                cell->ch = '?';
                cell->attr = CONSOLE_ATTR_NORMAL;
                bad++;
            }
        }
    }
    return bad;
}
