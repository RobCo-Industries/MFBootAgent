/* The menu lines: exact text and the 79-column limit. */

#include <string.h>

#include "boot.h"
#include "menulines.h"
#include "rc_test.h"

static mfb_entry_t entry(const char *name, const char *path)
{
    mfb_entry_t e;

    memset(&e, 0, sizeof(e));
    memcpy(e.name, name, strlen(name) < sizeof(e.name) ? strlen(name) : sizeof(e.name) - 1);
    memcpy(e.path, path, strlen(path) < sizeof(e.path) ? strlen(path) : sizeof(e.path) - 1);
    return e;
}

TEST(header)
{
    char b[MFB_LINE_MAX];
    unsigned i;

    mfb_line_title(b, sizeof(b), "0.1.0");
    CHECK_STR(b, "ROBCO MFBOOT AGENT V0.1.0");
    for (i = 0; i < MFB_LORE_LINES; i++) {
        CHECK(mfb_line_lore(i) != NULL);
        CHECK(strlen(mfb_line_lore(i)) < MFB_LINE_MAX);
    }
    CHECK(mfb_line_lore(MFB_LORE_LINES) == NULL);
    CHECK(strlen(mfb_line_rule()) < MFB_LINE_MAX);
    CHECK(strlen(mfb_line_help()) < MFB_LINE_MAX);
}

TEST(entry_lines)
{
    char b[MFB_LINE_MAX];
    mfb_entry_t e = entry("UOS", "/ROBCO/UOS.SYS");

    mfb_line_entry(b, sizeof(b), 0, &e, 1, 1);
    CHECK_STR(b, "> 1  UOS             DEFAULT  /ROBCO/UOS.SYS");
    e = entry("MAINT", "/ROBCO/MAINT.SYS");
    mfb_line_entry(b, sizeof(b), 1, &e, 0, 0);
    CHECK_STR(b, "  2  MAINT                    /ROBCO/MAINT.SYS");
}

TEST(longest_entry_is_cut_at_79)
{
    char b[MFB_LINE_MAX];
    char path[MFB_PATH_MAX + 1];
    mfb_entry_t e;

    memset(path, 'P', MFB_PATH_MAX);
    path[0] = '/';
    path[MFB_PATH_MAX] = '\0';
    e = entry("ABCDEFGHIJKLMNO", path);
    mfb_line_entry(b, sizeof(b), 7, &e, 1, 1);
    CHECK_EQ(strlen(b), MFB_LINE_MAX - 1);
    CHECK(strncmp(b, "> 8  ABCDEFGHIJKLMNO DEFAULT  /PPP", 34) == 0);
}

TEST(status_lines)
{
    char b[MFB_LINE_MAX];

    mfb_line_status(b, sizeof(b), 1, 0, 3, "UOS");
    CHECK_STR(b, "Booting UOS in 3 s. Press any key to stop.");
    mfb_line_status(b, sizeof(b), 1, 0, 99, "ABCDEFGHIJKLMNO");
    CHECK_STR(b, "Booting ABCDEFGHIJKLMNO in 99 s. Press any key to stop.");
    mfb_line_status(b, sizeof(b), 0, 1, 0, "UOS");
    CHECK_STR(b, "Countdown stopped. Press ENTER to boot.");
    mfb_line_status(b, sizeof(b), 0, 0, 0, "UOS");
    CHECK_STR(b, "");
}

TEST(loading_line)
{
    char b[MFB_LINE_MAX];
    mfb_entry_t e = entry("DIAG", "/ROBCO/DIAG.SYS");

    mfb_line_loading(b, sizeof(b), &e);
    CHECK_STR(b, "Loading DIAG from /ROBCO/DIAG.SYS");
}

TEST(menu_fits_the_screen)
{
    /* 30 rows on a 640x480 console. */
    CHECK(MFB_MENU_ROWS(MFB_MAX_ENTRIES) <= 30);
    CHECK_EQ(MFB_MENU_ROWS(3), 13);
}

TEST(minimum_grid_comes_from_the_lines)
{
    char b[MFB_LINE_MAX];
    char path[MFB_PATH_MAX + 1];
    mfb_entry_t e;

    /* The longest entry line fills every column but the last one. */
    memset(path, 'P', MFB_PATH_MAX);
    path[0] = '/';
    path[MFB_PATH_MAX] = '\0';
    e = entry("ABCDEFGHIJKLMNO", path);
    mfb_line_entry(b, sizeof(b), 7, &e, 1, 1);
    CHECK_EQ(strlen(b) + 1, MFB_MENU_MIN_COLS);
    /* Header, rule and blank, 8 entries, blank, help, countdown, error, start. */
    CHECK_EQ(MFB_MENU_MIN_ROWS, 5 + MFB_MAX_ENTRIES + 5);
    CHECK_EQ(MFB_MENU_MIN_COLS, 80);
    CHECK_EQ(MFB_MENU_MIN_ROWS, 18);
}

TEST(grid_check)
{
    CHECK_EQ(mfb_menu_grid(MFB_MENU_MIN_COLS, MFB_MENU_MIN_ROWS), MFB_GRID_OK);
    CHECK_EQ(mfb_menu_grid(80, 30), MFB_GRID_OK);
    CHECK_EQ(mfb_menu_grid(MFB_MENU_MIN_COLS - 1, MFB_MENU_MIN_ROWS), MFB_GRID_COLS);
    CHECK_EQ(mfb_menu_grid(MFB_MENU_MIN_COLS, MFB_MENU_MIN_ROWS - 1), MFB_GRID_ROWS);
    CHECK_EQ(mfb_menu_grid(MFB_MENU_MIN_COLS - 1, MFB_MENU_MIN_ROWS - 1), MFB_GRID_COLS);
    CHECK_EQ(mfb_menu_grid(0, 0), MFB_GRID_COLS);
    CHECK_EQ(mfb_menu_grid(1, 1), MFB_GRID_COLS);
    CHECK_STR(mfb_grid_name(MFB_GRID_OK), "ok");
    CHECK_STR(mfb_grid_name(MFB_GRID_COLS), "cols");
    CHECK_STR(mfb_grid_name(MFB_GRID_ROWS), "rows");
}

/*
 * Every placement on a grid that passes the check: the whole menu, its
 * error, countdown and start rows included, lands inside the grid; the
 * earlier text stays above it with a blank row; the screen scrolls only
 * as far as it must.
 */
static int place_ok(unsigned rows, unsigned count, unsigned cx, unsigned cy)
{
    unsigned menu_rows = MFB_MENU_ROWS(count);
    unsigned last_row = MFB_ROW_ENTRY0 + count + MFB_ROW_START_AFTER;
    unsigned text;
    unsigned first_free;
    mfb_place_t p;

    mfb_menu_place(rows, menu_rows, cx, cy, &p);
    if (MFB_ROW_ENTRY0 + count + MFB_ROW_ERROR_AFTER >= menu_rows ||
        MFB_ROW_ENTRY0 + count + MFB_ROW_STATUS_AFTER >= menu_rows || last_row + 1 != menu_rows) {
        return 0;
    }
    if (p.top > rows || menu_rows > rows - p.top) {
        return 0;
    }
    if (cy > rows - 1) {
        cy = rows - 1;
    }
    /* Rows of earlier text: those above the cursor, and the cursor row when it holds text. */
    text = cy + (cx != 0);
    first_free = text == 0 ? 0 : text + 1;
    if (p.scroll > first_free || p.top != first_free - p.scroll) {
        return 0;
    }
    if (p.scroll != 0 && p.top + menu_rows != rows) {
        return 0;
    }
    return 1;
}

TEST(place_on_every_grid)
{
    static const unsigned grids[] = { MFB_MENU_MIN_ROWS, 30, 60 };
    static const unsigned counts[] = { 1, 3, MFB_MAX_ENTRIES };
    unsigned g, c, cy, cx;

    for (g = 0; g < sizeof(grids) / sizeof(grids[0]); g++) {
        for (c = 0; c < sizeof(counts) / sizeof(counts[0]); c++) {
            for (cy = 0; cy <= grids[g]; cy++) {
                for (cx = 0; cx < 3; cx++) {
                    CHECK(place_ok(grids[g], counts[c], cx * 40, cy));
                }
            }
        }
    }
}

TEST(place_examples)
{
    mfb_place_t p;

    /* Fresh screen: the menu starts on row 0. */
    mfb_menu_place(30, MFB_MENU_ROWS(3), 0, 0, &p);
    CHECK_EQ(p.top, 0);
    CHECK_EQ(p.scroll, 0);
    /* One line of earlier text: a blank row, then the menu. */
    mfb_menu_place(30, MFB_MENU_ROWS(3), 0, 1, &p);
    CHECK_EQ(p.top, 2);
    CHECK_EQ(p.scroll, 0);
    /* Cursor on the last row of a minimum grid, mid line, eight entries. */
    mfb_menu_place(MFB_MENU_MIN_ROWS, MFB_MENU_ROWS(8), 5, MFB_MENU_MIN_ROWS - 1, &p);
    CHECK_EQ(p.top, 0);
    CHECK_EQ(p.scroll, MFB_MENU_MIN_ROWS + 1);
    /* Cursor on the last row of a tall grid, one entry. */
    mfb_menu_place(60, MFB_MENU_ROWS(1), 0, 59, &p);
    CHECK_EQ(p.top, 60 - MFB_MENU_ROWS(1));
    CHECK_EQ(p.scroll, 60 - (60 - MFB_MENU_ROWS(1)));
    /* A cursor below the grid counts as on the last row. */
    mfb_menu_place(30, MFB_MENU_ROWS(8), 0, 1000, &p);
    CHECK_EQ(p.top, 30 - MFB_MENU_ROWS(8));
}

TEST(place_never_underflows)
{
    mfb_place_t p;

    /* One row short, and an empty grid: top 0, no scroll, never a wrapped value. */
    mfb_menu_place(MFB_MENU_MIN_ROWS - 1, MFB_MENU_ROWS(8), 0, 3, &p);
    CHECK_EQ(p.top, 0);
    CHECK_EQ(p.scroll, 0);
    mfb_menu_place(0, MFB_MENU_ROWS(1), 0, 0, &p);
    CHECK_EQ(p.top, 0);
    CHECK_EQ(p.scroll, 0);
    /* The old formula with rows 17 and a menu of 18 gave a top near UINT_MAX. */
    mfb_menu_place(17, 18, 0, 16, &p);
    CHECK(p.top < 17);
}
