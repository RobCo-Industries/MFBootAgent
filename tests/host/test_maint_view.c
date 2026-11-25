/* MAINT.SYS: the pager of view file: line ends, wraps, tabs, binary bytes, the size cap. */

#include <stdlib.h>
#include <string.h>

#include "mt_view.h"
#include "rc_test.h"

static mt_view_t v;

static void view(const char *text, unsigned cols)
{
    mt_view_init(&v, (const uint8_t *)text, (uint32_t)strlen(text), cols);
}

static const char *line(unsigned i)
{
    static char out[300];

    mt_view_line(&v, i, out, sizeof(out));
    return out;
}

TEST(empty_file_has_no_lines)
{
    view("", 79);
    CHECK_EQ(v.lines, 0u);
    CHECK_EQ(v.binary, 0u);
    CHECK_STR(line(0), "");
}

TEST(line_ends)
{
    view("a\nb\r\nc\rd", 79);
    CHECK_EQ(v.lines, 4u);
    CHECK_STR(line(0), "a");
    CHECK_STR(line(1), "b");
    CHECK_STR(line(2), "c");
    CHECK_STR(line(3), "d");
    CHECK_STR(line(4), "");
    /* A last line end adds no empty line; an empty line in the middle shows. */
    view("a\n\nb\n", 79);
    CHECK_EQ(v.lines, 3u);
    CHECK_STR(line(1), "");
    CHECK_STR(line(2), "b");
    CHECK_EQ(v.binary, 0u);
}

TEST(long_lines_wrap)
{
    view("abcdefghijKLM\nx", 10);
    CHECK_EQ(v.lines, 3u);
    CHECK_STR(line(0), "abcdefghij");
    CHECK_STR(line(1), "KLM");
    CHECK_STR(line(2), "x");
    /* Exactly cols characters then a line end: no empty wrap line. */
    view("abcdefghij\nx", 10);
    CHECK_EQ(v.lines, 2u);
}

TEST(tabs_go_to_the_next_stop)
{
    view("a\tb\n\tc", 79);
    CHECK_STR(line(0), "a       b");
    CHECK_STR(line(1), "        c");
    /* A tab that does not fit wraps. */
    view("abcdefghi\tx", 12);
    CHECK_EQ(v.lines, 2u);
    CHECK_STR(line(0), "abcdefghi");
    CHECK_STR(line(1), "        x");
}

TEST(binary_bytes_show_as_dots)
{
    static const uint8_t b[] = { 'o', 'k', 0x00, 0x01, 0x7f, 0x80, 0xff, 0x1b, '\n', 'z' };
    char out[64];

    mt_view_init(&v, b, sizeof(b), 79);
    CHECK_EQ(v.lines, 2u);
    CHECK_EQ(v.binary, 6u);
    mt_view_line(&v, 0, out, sizeof(out));
    CHECK_STR(out, "ok......");
    mt_view_line(&v, 1, out, sizeof(out));
    CHECK_STR(out, "z");
    CHECK(mt_view_printable(' ') && mt_view_printable('~'));
    CHECK(!mt_view_printable('\t') && !mt_view_printable(0x7f));
}

TEST(the_buffer_is_capped)
{
    uint8_t *big = malloc(MT_VIEW_MAX + 100);
    char out[100];
    unsigned i;

    for (i = 0; i < MT_VIEW_MAX + 100; i++) {
        big[i] = '\n';
    }
    mt_view_init(&v, big, MT_VIEW_MAX + 100, 79);
    CHECK_EQ(v.len, MT_VIEW_MAX);
    CHECK_EQ(v.lines, MT_VIEW_MAX);
    mt_view_line(&v, MT_VIEW_MAX - 1, out, sizeof(out));
    CHECK_STR(out, "");
    /* One line of MT_VIEW_MAX bytes: every line is full but the last. */
    for (i = 0; i < MT_VIEW_MAX; i++) {
        big[i] = 'a' + i % 26;
    }
    mt_view_init(&v, big, MT_VIEW_MAX, 79);
    CHECK_EQ(v.lines, (MT_VIEW_MAX + 78) / 79);
    mt_view_line(&v, 0, out, sizeof(out));
    CHECK_EQ(strlen(out), 79u);
    free(big);
}
