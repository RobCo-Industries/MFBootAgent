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
