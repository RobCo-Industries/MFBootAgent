/* Reading the previous stage's text back from the framebuffer pixels. */

#include <stdint.h>
#include <string.h>

#include <robco/console.h>
#include <robco/fb.h>
#include <robco/font.h>

#include "rc_test.h"
#include "screen.h"

#define COLS 10
#define ROWS 3
#define PITCH (COLS * FONT_W * 4 + 16) /* a pitch wider than the row, as on hardware */

static uint8_t fb[ROWS * FONT_H * PITCH];
static mfb_cell_t out[COLS * ROWS];

/* Draws one cell the way the console's draw_cell() does. */
static void draw(const mfb_palette_t *p, unsigned col, unsigned row, uint8_t ch, unsigned attr)
{
    const uint8_t *g = font8x16_glyph(ch);
    uint32_t fg = p->level[attr & 3u];
    uint32_t bg = p->bg;
    unsigned x, y;

    if (attr & CONSOLE_ATTR_INVERSE) {
        uint32_t t = fg;

        fg = bg;
        bg = t;
    }
    for (y = 0; y < FONT_H; y++) {
        uint32_t *px = (uint32_t *)(void *)(fb + (row * FONT_H + y) * PITCH + col * FONT_W * 4);

        for (x = 0; x < FONT_W; x++) {
            px[x] = (g[y] & (0x80u >> x)) ? fg : bg;
        }
    }
}

static void fill(const mfb_palette_t *p)
{
    unsigned r, c;

    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            draw(p, c, r, ' ', CONSOLE_ATTR_NORMAL);
        }
    }
}

TEST(pixel_words_follow_the_order)
{
    CHECK_EQ(mfb_pixel(FB_ORDER_BGR, 0x11, 0x22, 0x33), 0xff112233u);
    CHECK_EQ(mfb_pixel(FB_ORDER_RGB, 0x11, 0x22, 0x33), 0xff332211u);
}

TEST(unknown_scheme)
{
    mfb_palette_t p;

    CHECK(mfb_palette(CONSOLE_SCHEME_COUNT, FB_ORDER_BGR, &p) == -1);
    CHECK_EQ(mfb_palette(CONSOLE_SCHEME_AMBER, FB_ORDER_BGR, &p), 0);
}

TEST(reads_text_and_attributes)
{
    static const char text[] = "READY. ~|";
    unsigned scheme, order, i;

    for (scheme = 0; scheme < CONSOLE_SCHEME_COUNT; scheme++) {
        for (order = 0; order <= 1; order++) {
            mfb_palette_t p;

            CHECK_EQ(mfb_palette(scheme, order, &p), 0);
            fill(&p);
            for (i = 0; text[i] != '\0'; i++) {
                draw(&p, i, 0, (uint8_t)text[i], CONSOLE_ATTR_NORMAL);
            }
            draw(&p, 0, 1, 'D', CONSOLE_ATTR_DIM);
            draw(&p, 1, 1, 'B', CONSOLE_ATTR_BRIGHT);
            draw(&p, 2, 1, 'I', CONSOLE_ATTR_NORMAL | CONSOLE_ATTR_INVERSE);
            draw(&p, 9, 2, FONT_CH_HLINE, CONSOLE_ATTR_NORMAL);
            CHECK_EQ(mfb_screen_read(fb, PITCH, COLS, ROWS, &p, out), 0);
            for (i = 0; text[i] != '\0'; i++) {
                CHECK_EQ(out[i].ch, (uint8_t)text[i]);
                CHECK_EQ(out[i].attr, CONSOLE_ATTR_NORMAL);
            }
            CHECK_EQ(out[COLS].ch, 'D');
            CHECK_EQ(out[COLS].attr, CONSOLE_ATTR_DIM);
            CHECK_EQ(out[COLS + 1].ch, 'B');
            CHECK_EQ(out[COLS + 1].attr, CONSOLE_ATTR_BRIGHT);
            CHECK_EQ(out[COLS + 2].ch, 'I');
            CHECK_EQ(out[COLS + 2].attr, CONSOLE_ATTR_NORMAL | CONSOLE_ATTR_INVERSE);
            CHECK_EQ(out[2 * COLS + 9].ch, FONT_CH_HLINE);
            CHECK_EQ(out[2 * COLS].ch, ' ');
        }
    }
}

TEST(foreign_pixels_are_unknown)
{
    mfb_palette_t p;

    mfb_palette(CONSOLE_SCHEME_GREEN, FB_ORDER_BGR, &p);
    fill(&p);
    draw(&p, 3, 1, 'A', CONSOLE_ATTR_NORMAL);
    draw(&p, 4, 1, 'Z', CONSOLE_ATTR_NORMAL);
    /* A colour of no level, and a cell with two levels. */
    *(uint32_t *)(void *)(fb + (1 * FONT_H + 5) * PITCH + 3 * FONT_W * 4) = 0xff123456u;
    *(uint32_t *)(void *)(fb + (1 * FONT_H + 5) * PITCH + 4 * FONT_W * 4 + 4) =
        p.level[CONSOLE_ATTR_DIM];
    CHECK_EQ(mfb_screen_read(fb, PITCH, COLS, ROWS, &p, out), 2);
    CHECK_EQ(out[COLS + 3].ch, '?');
    CHECK_EQ(out[COLS + 4].ch, '?');
    CHECK_EQ(out[COLS + 5].ch, ' ');
}

TEST(other_scheme_is_unknown)
{
    mfb_palette_t green, amber;

    mfb_palette(CONSOLE_SCHEME_GREEN, FB_ORDER_BGR, &green);
    mfb_palette(CONSOLE_SCHEME_AMBER, FB_ORDER_BGR, &amber);
    fill(&green);
    draw(&green, 0, 0, 'A', CONSOLE_ATTR_NORMAL);
    /* Read with the wrong scheme: every cell is foreign. */
    CHECK_EQ(mfb_screen_read(fb, PITCH, COLS, ROWS, &amber, out), COLS * ROWS);
}
