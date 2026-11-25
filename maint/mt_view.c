/* The pager of the view file tool: see mt_view.h. */

#include <stddef.h>
#include <stdint.h>

#include <robco/klibc.h>

#include "mt_view.h"

int mt_view_printable(uint8_t b)
{
    return b >= 0x20 && b <= 0x7e;
}

static unsigned tab_width(unsigned col)
{
    return 8u - col % 8u;
}

void mt_view_init(mt_view_t *v, const uint8_t *buf, uint32_t len, unsigned cols)
{
    uint32_t i = 0;
    unsigned col = 0;
    int open = 0;      /* a display line has started */

    if (len > MT_VIEW_MAX) {
        len = MT_VIEW_MAX;
    }
    if (cols < 8) {
        cols = 8;
    }
    if (cols > 255) {
        cols = 255;
    }
    v->buf = buf;
    v->len = len;
    v->cols = cols;
    v->lines = 0;
    v->binary = 0;
    while (i < len) {
        uint8_t b = buf[i];
        unsigned w;

        if (!open) {
            v->start[v->lines++] = (uint16_t)i;
            open = 1;
            col = 0;
        }
        if (b == '\n' || b == '\r') {
            i += b == '\r' && i + 1 < len && buf[i + 1] == '\n' ? 2u : 1u;
            open = 0;
            continue;
        }
        w = b == '\t' ? tab_width(col) : 1u;
        if (col + w > cols && col != 0) {
            /* Wrap: this byte starts the next display line. */
            open = 0;
            continue;
        }
        if (b != '\t' && !mt_view_printable(b)) {
            v->binary++;
        }
        col += w;
        i++;
    }
    v->start[v->lines] = (uint16_t)len;
}

void mt_view_line(const mt_view_t *v, unsigned i, char *out, size_t size)
{
    uint32_t p;
    uint32_t end;
    size_t n = 0;

    if (size == 0) {
        return;
    }
    out[0] = '\0';
    if (i >= v->lines) {
        return;
    }
    end = v->start[i + 1];
    for (p = v->start[i]; p < end && n + 1 < size; p++) {
        uint8_t b = v->buf[p];

        if (b == '\n' || b == '\r') {
            break;
        }
        if (b == '\t') {
            unsigned w = tab_width((unsigned)n);

            while (w-- > 0 && n + 1 < size && n < v->cols) {
                out[n++] = ' ';
            }
            continue;
        }
        out[n++] = mt_view_printable(b) ? (char)b : MT_VIEW_DOT;
    }
    out[n] = '\0';
}
