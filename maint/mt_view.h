/*
 * The pager of the view file tool: the display lines of a text file.
 * Pure: the host tests link it.
 *
 * The rules:
 *   - The tool reads at most MT_VIEW_MAX bytes. A larger file shows its
 *     first MT_VIEW_MAX bytes and says so.
 *   - LF, CR and CRLF end a line. A last line with no end still shows. A
 *     line end at the end of the file adds no empty line.
 *   - A tab moves to the next column that is a multiple of 8.
 *   - A line longer than the screen wraps onto the next display line.
 *   - Bytes 0x20 to 0x7E show as they are. Every other byte, NUL, a
 *     control byte or a byte of 0x7F and above, shows as '.' and counts
 *     as binary. The file is not parsed.
 */
#ifndef MT_VIEW_H
#define MT_VIEW_H

#include <stddef.h>
#include <stdint.h>

#define MT_VIEW_MAX     16384u
#define MT_VIEW_DOT     '.'

typedef struct {
    const uint8_t *buf;
    uint32_t len;               /* bytes in buf, at most MT_VIEW_MAX */
    unsigned cols;              /* display columns */
    unsigned lines;             /* display lines */
    uint32_t binary;            /* bytes shown as MT_VIEW_DOT */
    uint16_t start[MT_VIEW_MAX + 1]; /* first byte of each display line */
} mt_view_t;

/* Splits buf into display lines of at most cols columns, 8 to 255. */
void mt_view_init(mt_view_t *v, const uint8_t *buf, uint32_t len, unsigned cols);

/* Display line i, as text of at most cols characters. "" past the last line. */
void mt_view_line(const mt_view_t *v, unsigned i, char *out, size_t size);

/* 1 for a byte that shows as itself. */
int mt_view_printable(uint8_t b);

#endif
