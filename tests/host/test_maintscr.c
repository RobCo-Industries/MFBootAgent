/* The maintenance screen: every key on every action, and the lines of each page. */

#include <string.h>

#include "keymap.h"
#include "maintscr.h"
#include "menulines.h"
#include "rc_test.h"

static mfb_ms_t s;

static const int all_keys[] = {
    MFB_K_NONE, MFB_K_OTHER, MFB_K_UP, MFB_K_DOWN, MFB_K_SELECT, MFB_K_BACK, MFB_K_MAINT,
    MFB_K_QUIT, MFB_K_REBOOT, MFB_K_DIGIT1, MFB_K_DIGIT1 + 3, MFB_K_DIGIT8,
};

static mfb_entry_t entry(const char *name, const char *path)
{
    mfb_entry_t e;

    memset(&e, 0, sizeof(e));
    memcpy(e.name, name, strnlen(name, MFB_NAME_MAX));
    memcpy(e.path, path, strnlen(path, MFB_PATH_MAX));
    return e;
}

TEST(fits_the_minimum_grid)
{
    CHECK_EQ(MFB_MS_ROWS, 18);
    CHECK(MFB_MS_ROWS <= MFB_MENU_MIN_ROWS);
    CHECK_EQ(MFB_MS_OUT_ROWS, MFB_MAX_ENTRIES + 2);
}

TEST(every_key_on_every_action)
{
    unsigned sel, k;

    for (sel = 0; sel < MFB_MS_ITEMS; sel++) {
        for (k = 0; k < sizeof(all_keys) / sizeof(all_keys[0]); k++) {
            unsigned item = 99;
            unsigned old = 99;
            int key = all_keys[k];
            int act;

            mfb_ms_init(&s);
            s.sel = sel;
            s.drawn_sel = sel;
            act = mfb_ms_key(&s, key, &item);
            switch (key) {
            case MFB_K_UP:
                CHECK_EQ(act, MFB_MS_ACT_NONE);
                CHECK_EQ(s.sel, sel > 0 ? sel - 1 : 0);
                CHECK_EQ(mfb_ms_take(&s, &old), sel > 0);
                break;
            case MFB_K_DOWN:
                CHECK_EQ(act, MFB_MS_ACT_NONE);
                CHECK_EQ(s.sel, sel + 1 < MFB_MS_ITEMS ? sel + 1 : sel);
                CHECK_EQ(mfb_ms_take(&s, &old), sel + 1 < MFB_MS_ITEMS);
                break;
            case MFB_K_SELECT:
                if (sel == MFB_MS_BACK) {
                    CHECK_EQ(act, MFB_MS_ACT_CLOSE);
                    CHECK_EQ(item, 99);
                } else {
                    CHECK_EQ(act, MFB_MS_ACT_RUN);
                    CHECK_EQ(item, sel);
                }
                CHECK_EQ(s.sel, sel);
                break;
            case MFB_K_BACK:
            case MFB_K_QUIT:
                CHECK_EQ(act, MFB_MS_ACT_CLOSE);
                CHECK_EQ(s.sel, sel);
                break;
            default:
                CHECK_EQ(act, MFB_MS_ACT_NONE);
                CHECK_EQ(s.sel, sel);
                CHECK_EQ(mfb_ms_take(&s, &old), 0);
                CHECK_EQ(old, 99);
                break;
            }
        }
    }
}

TEST(take_reports_the_drawn_action)
{
    unsigned item = 0;
    unsigned old = 99;

    mfb_ms_init(&s);
    CHECK_EQ(mfb_ms_take(&s, &old), 0);
    mfb_ms_key(&s, MFB_K_DOWN, &item);
    mfb_ms_key(&s, MFB_K_DOWN, &item);
    CHECK_EQ(mfb_ms_take(&s, &old), 1);
    CHECK_EQ(old, 0);
    CHECK_EQ(s.sel, 2);
    mfb_ms_key(&s, MFB_K_UP, &item);
    CHECK_EQ(mfb_ms_take(&s, &old), 1);
    CHECK_EQ(old, 2);
    /* Down and up again before a redraw: the drawn action is the selected one. */
    mfb_ms_key(&s, MFB_K_DOWN, &item);
    mfb_ms_key(&s, MFB_K_UP, &item);
    CHECK_EQ(mfb_ms_take(&s, &old), 1);
    CHECK_EQ(old, s.sel);
}

TEST(names_and_fixed_rows)
{
    char b[MFB_LINE_MAX];
    unsigned i;

    CHECK_STR(mfb_ms_name(MFB_MS_VERIFY), "verify");
    CHECK_STR(mfb_ms_name(MFB_MS_SYSTEM), "system");
    CHECK_STR(mfb_ms_name(MFB_MS_CONFIG), "config");
    CHECK_STR(mfb_ms_name(MFB_MS_REBOOT), "reboot");
    CHECK_STR(mfb_ms_name(MFB_MS_BACK), "back");
    CHECK_STR(mfb_ms_name(MFB_MS_ITEMS), "unknown");
    mfb_ms_title(b, sizeof(b), "0.1.0");
    CHECK_STR(b, "MFBOOT AGENT V0.1.0 MAINTENANCE");
    CHECK(strlen(mfb_ms_help()) < MFB_LINE_MAX);
    CHECK_STR(mfb_ms_help(), "UP/DOWN: move   ENTER: run   ESC or Q: back to the boot menu");
    mfb_ms_item(b, sizeof(b), MFB_MS_VERIFY, 1);
    CHECK_STR(b, "> Verify entries");
    mfb_ms_item(b, sizeof(b), MFB_MS_SYSTEM, 0);
    CHECK_STR(b, "  System");
    mfb_ms_item(b, sizeof(b), MFB_MS_CONFIG, 0);
    CHECK_STR(b, "  Configuration");
    mfb_ms_item(b, sizeof(b), MFB_MS_REBOOT, 0);
    CHECK_STR(b, "  Reboot");
    mfb_ms_item(b, sizeof(b), MFB_MS_BACK, 1);
    CHECK_STR(b, "> Back to the boot menu");
    mfb_ms_item(b, sizeof(b), MFB_MS_ITEMS, 1);
    CHECK_STR(b, "");
    for (i = 0; i < MFB_MS_ITEMS; i++) {
        mfb_ms_item(b, sizeof(b), i, 1);
        CHECK(strlen(b) < MFB_LINE_MAX);
    }
}

TEST(verify_table)
{
    char b[MFB_LINE_MAX];
    mfb_entry_t e = entry("UOS", "/ROBCO/UOS.SYS");

    mfb_ms_verify_head(b, sizeof(b));
    CHECK_STR(b, "#  ENTRY           RESULT      PATH");
    mfb_ms_verify_row(b, sizeof(b), 0, &e, 0);
    CHECK_STR(b, "1  UOS             OK          /ROBCO/UOS.SYS");
    mfb_ms_verify_row(b, sizeof(b), 1, &e, MFB_ERR_CRC);
    CHECK_STR(b, "2  UOS             E08 CRC     /ROBCO/UOS.SYS");
    mfb_ms_verify_row(b, sizeof(b), 2, &e, MFB_ERR_SUBTYPE);
    CHECK_STR(b, "3  UOS             E06 BOUNDS  /ROBCO/UOS.SYS");
    mfb_ms_verify_row(b, sizeof(b), 7, &e, MFB_ERR_VERSION);
    CHECK_STR(b, "8  UOS             E04 VERSION /ROBCO/UOS.SYS");
    mfb_ms_verify_sum(b, sizeof(b), 1, 2);
    CHECK_STR(b, "3 entries checked: 1 good, 2 failed.");
    mfb_ms_verify_sum(b, sizeof(b), 1, 0);
    CHECK_STR(b, "1 entry checked: 1 good, 0 failed.");
}

TEST(verify_row_cuts_long_names_and_paths_as_the_menu_does)
{
    char b[MFB_LINE_MAX];
    char m[MFB_LINE_MAX];
    char path[MFB_PATH_MAX + 1];
    mfb_entry_t e;

    memset(path, 'P', MFB_PATH_MAX);
    path[0] = '/';
    path[MFB_PATH_MAX] = '\0';
    e = entry("ABCDEFGHIJKLMNO", path);
    mfb_ms_verify_row(b, sizeof(b), 7, &e, MFB_ERR_NOFILE);
    CHECK_EQ(strlen(b), MFB_LINE_MAX - 1);
    CHECK(strncmp(b, "8  ABCDEFGHIJKLMNO E01 NOFILE  /PPP", 35) == 0);
    /* The menu cuts the same entry at the same column. */
    mfb_line_entry(m, sizeof(m), 7, &e, 1, 1);
    CHECK_EQ(strlen(m), strlen(b));
    /* A small buffer is cut, never overrun. */
    mfb_ms_verify_row(b, 8, 7, &e, MFB_ERR_NOFILE);
    CHECK_STR(b, "8  ABCD");
}

static void check_page(mfb_ms_page_t p, unsigned n)
{
    unsigned i;

    CHECK(n <= MFB_MS_OUT_ROWS);
    for (i = 0; i < n; i++) {
        CHECK(strlen(p[i]) < MFB_LINE_MAX);
    }
}

TEST(system_page)
{
    mfb_ms_page_t p;
    mfb_ms_sys_t sys = {
        .board = "Raspberry Pi 2 Model B", .soc = "BCM2836", .mem_bytes = 1024u << 20,
        .vol_lba = 2048, .vol_blocks = 30720, .trusted = 1, .flags_in = 0x2,
        .version = "0.1.0", .core_pin = "796607fc0197", .screen = MFB_MS_SCREEN_ADOPT,
    };
    unsigned n = mfb_ms_system(p, &sys);

    CHECK_EQ(n, 9);
    check_page(p, n);
    CHECK_STR(p[0], "SYSTEM");
    CHECK_STR(p[1], "Board:        Raspberry Pi 2 Model B, BCM2836");
    CHECK_STR(p[2], "Memory:       1024 MiB");
    CHECK_STR(p[3], "Boot volume:  SD card, block 2048, 30720 blocks");
    CHECK_STR(p[4], "Boot info:    trusted, from the previous stage");
    CHECK_STR(p[5], "Boot flags:   0x00000002 received");
    CHECK_STR(p[6], "Version:      0.1.0");
    CHECK_STR(p[7], "RobCo-Core:   796607fc0197");
    CHECK_STR(p[8], "Screen:       adopted from the previous stage");
    sys.trusted = 0;
    sys.screen = MFB_MS_SCREEN_FRESH;
    sys.mem_bytes = 0xFFFFFFFFu;
    sys.vol_lba = 0xFFFFFFFFu;
    sys.vol_blocks = 0xFFFFFFFFu;
    n = mfb_ms_system(p, &sys);
    check_page(p, n);
    CHECK_STR(p[2], "Memory:       4095 MiB");
    CHECK_STR(p[3], "Boot volume:  SD card, block 4294967295, 4294967295 blocks");
    CHECK_STR(p[4], "Boot info:    none trusted, this stage cold-booted");
    CHECK_STR(p[5], "Boot flags:   none received");
    CHECK_STR(p[8], "Screen:       started by this stage");
    sys.screen = MFB_MS_SCREEN_NONE;
    sys.board = "A board name far longer than any real one, made to run past the end";
    n = mfb_ms_system(p, &sys);
    check_page(p, n);
    CHECK_STR(p[8], "Screen:       none, serial only");
    CHECK_EQ(strlen(p[1]), MFB_LINE_MAX - 1);
}

TEST(config_page)
{
    mfb_ms_page_t p;
    mfb_ms_cfg_t c = {
        .result = MFB_MS_CFG_OK, .path = "/ROBCO/BOOT.CFG", .def = "UOS", .timeout = 3,
        .entries = 3, .ignored = 1, .dup = 2, .bad = 3, .clamped = 4,
    };
    unsigned n = mfb_ms_config(p, &c);

    CHECK_EQ(n, 9);
    check_page(p, n);
    CHECK_STR(p[0], "CONFIGURATION");
    CHECK_STR(p[1], "File:         /ROBCO/BOOT.CFG, in use");
    CHECK_STR(p[2], "Default:      UOS");
    CHECK_STR(p[3], "Timeout:      3 s");
    CHECK_STR(p[4], "Entries:      3");
    CHECK_STR(p[5], "Ignored:      1");
    CHECK_STR(p[6], "Duplicate:    2");
    CHECK_STR(p[7], "Bad:          3");
    CHECK_STR(p[8], "Clamped:      4");
    c.timeout = MFB_TIMEOUT_NONE;
    c.result = MFB_MS_CFG_MISSING;
    mfb_ms_config(p, &c);
    CHECK_STR(p[1], "File:         /ROBCO/BOOT.CFG missing, default menu");
    CHECK_STR(p[3], "Timeout:      none, the menu waits for a key");
    c.result = MFB_MS_CFG_READ;
    mfb_ms_config(p, &c);
    CHECK_STR(p[1], "File:         /ROBCO/BOOT.CFG does not read, default menu");
    c.result = MFB_MS_CFG_SYNTAX;
    c.line = 0xFFFFFFFFu;
    n = mfb_ms_config(p, &c);
    check_page(p, n);
    CHECK_STR(p[1], "File:         /ROBCO/BOOT.CFG, line 4294967295 does not parse, default menu");
    c.result = MFB_MS_CFG_EMPTY;
    c.timeout = 99;
    c.entries = 0xFFFFFFFFu;
    c.def = "ABCDEFGHIJKLMNO";
    n = mfb_ms_config(p, &c);
    check_page(p, n);
    CHECK_STR(p[1], "File:         /ROBCO/BOOT.CFG has no usable entry, default menu");
    CHECK_STR(p[2], "Default:      ABCDEFGHIJKLMNO");
    CHECK_STR(p[3], "Timeout:      99 s");
    CHECK_STR(p[4], "Entries:      4294967295");
}

/* The screen placed on an 80 by 18 and an 80 by 30 grid, from any cursor row. */
TEST(placement_on_small_and_large_grids)
{
    static const unsigned grids[] = { 18, 30 };
    mfb_place_t pl;
    unsigned g, cy;

    for (g = 0; g < 2; g++) {
        for (cy = 0; cy < grids[g] + 2; cy++) {
            mfb_menu_place(grids[g], MFB_MS_ROWS, 0, cy, &pl);
            CHECK(pl.top + MFB_MS_ROWS <= grids[g]);
        }
    }
    mfb_menu_place(18, MFB_MS_ROWS, 5, 17, &pl);
    CHECK_EQ(pl.top, 0);
    CHECK_EQ(pl.scroll, 19);
    mfb_menu_place(30, MFB_MS_ROWS, 0, 3, &pl);
    CHECK_EQ(pl.top, 4);
    CHECK_EQ(pl.scroll, 0);
    /* A grid smaller than the screen: top 0, the caller never asks for it. */
    mfb_menu_place(10, MFB_MS_ROWS, 0, 3, &pl);
    CHECK_EQ(pl.top, 0);
    CHECK_EQ(pl.scroll, 0);
}
