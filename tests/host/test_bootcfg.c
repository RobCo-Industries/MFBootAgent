/* The BOOT.CFG parser: the spec's example, every limit and every bad form. */

#include <string.h>

#include "bootcfg.h"
#include "rc_test.h"

static mfb_cfg_t cfg;
static mfb_cfg_report_t rep;
static unsigned line;

static int parse(const char *text)
{
    return mfb_cfg_parse(text, strlen(text), &cfg, &rep, &line);
}

static void check_default_menu(void)
{
    CHECK_EQ(cfg.count, 1);
    CHECK_STR(cfg.entry[0].name, "UOS");
    CHECK_STR(cfg.entry[0].path, "/ROBCO/UOS.SYS");
    CHECK_STR(cfg.entry[0].cmdline, "");
    CHECK_EQ(cfg.def, 0);
    CHECK(cfg.timeout == MFB_TIMEOUT_NONE);
}

/* The example from BOOT_PROTOCOL.md at the pinned commit, verbatim. */
static const char spec_example[] =
    "[boot]\n"
    "default = UOS\n"
    "timeout = 3\n"
    "\n"
    "[entry UOS]\n"
    "path = /ROBCO/UOS.SYS\n"
    "cmdline =\n"
    "\n"
    "[entry MAINT]\n"
    "path = /ROBCO/MAINT.SYS\n"
    "\n"
    "[entry DIAG]\n"
    "path = /ROBCO/DIAG.SYS\n";

TEST(spec_example)
{
    CHECK_EQ(parse(spec_example), MFB_CFG_OK);
    CHECK_EQ(cfg.count, 3);
    CHECK_STR(cfg.entry[0].name, "UOS");
    CHECK_STR(cfg.entry[0].path, "/ROBCO/UOS.SYS");
    CHECK_STR(cfg.entry[0].cmdline, "");
    CHECK_STR(cfg.entry[1].name, "MAINT");
    CHECK_STR(cfg.entry[1].path, "/ROBCO/MAINT.SYS");
    CHECK_STR(cfg.entry[2].name, "DIAG");
    CHECK_STR(cfg.entry[2].path, "/ROBCO/DIAG.SYS");
    CHECK_EQ(cfg.def, 0);
    CHECK_EQ(cfg.timeout, 3);
    CHECK_EQ(rep.ignored + rep.dup + rep.unknown + rep.bad + rep.clamped, 0);
    CHECK_EQ(rep.default_found, 1);
}

TEST(default_points_at_a_later_entry)
{
    CHECK_EQ(parse("[boot]\ndefault = diag\n[entry UOS]\npath = /U\n[entry DIAG]\npath = /D\n"),
             MFB_CFG_OK);
    CHECK_EQ(cfg.def, 1);
    CHECK_EQ(rep.default_found, 1);
}

TEST(missing_or_unknown_default_is_the_first)
{
    CHECK_EQ(parse("[entry A]\npath = /A\n[entry B]\npath = /B\n"), MFB_CFG_OK);
    CHECK_EQ(cfg.def, 0);
    CHECK_EQ(rep.default_found, 0);
    CHECK_EQ(parse("[boot]\ndefault = ZZZ\n[entry A]\npath = /A\n[entry B]\npath = /B\n"),
             MFB_CFG_OK);
    CHECK_EQ(cfg.def, 0);
    CHECK_EQ(rep.default_found, 0);
    /* The default of a dropped entry is unknown too. */
    CHECK_EQ(parse("[boot]\ndefault = B\n[entry A]\npath = /A\n[entry B]\npath = bad\n"),
             MFB_CFG_OK);
    CHECK_EQ(cfg.count, 1);
    CHECK_EQ(cfg.def, 0);
}

TEST(timeout_rules)
{
    CHECK_EQ(parse("[entry A]\npath = /A\n"), MFB_CFG_OK);
    CHECK(cfg.timeout == MFB_TIMEOUT_NONE);
    CHECK_EQ(parse("[boot]\ntimeout = 0\n[entry A]\npath = /A\n"), MFB_CFG_OK);
    CHECK_EQ(cfg.timeout, 0);
    CHECK_EQ(parse("[boot]\ntimeout = 007\n[entry A]\npath = /A\n"), MFB_CFG_OK);
    CHECK_EQ(cfg.timeout, 7);
    CHECK_EQ(parse("[boot]\ntimeout = 99\n[entry A]\npath = /A\n"), MFB_CFG_OK);
    CHECK_EQ(cfg.timeout, 99);
    CHECK_EQ(rep.clamped, 0);
    CHECK_EQ(parse("[boot]\ntimeout = 100\n[entry A]\npath = /A\n"), MFB_CFG_OK);
    CHECK_EQ(cfg.timeout, 99);
    CHECK_EQ(rep.clamped, 1);
    CHECK_EQ(parse("[boot]\ntimeout = 99999999999999\n[entry A]\npath = /A\n"), MFB_CFG_OK);
    CHECK_EQ(cfg.timeout, 99);
    CHECK_EQ(rep.clamped, 1);
}

TEST(bad_timeouts_keep_waiting)
{
    static const char *const bad[] = { "-1", "+3", "3s", "3 s", "0x10", "1.5", "three" };
    char text[96];
    unsigned i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        snprintf(text, sizeof(text), "[boot]\ntimeout = %s\n[entry A]\npath = /A\n", bad[i]);
        CHECK_EQ(parse(text), MFB_CFG_OK);
        CHECK(cfg.timeout == MFB_TIMEOUT_NONE);
        CHECK_EQ(rep.bad, 1);
    }
    /* An empty value is bad too. */
    CHECK_EQ(parse("[boot]\ntimeout =\n[entry A]\npath = /A\n"), MFB_CFG_OK);
    CHECK(cfg.timeout == MFB_TIMEOUT_NONE);
    CHECK_EQ(rep.bad, 1);
}

TEST(number_rule)
{
    uint32_t v;

    CHECK_EQ(mfb_cfg_number("0", &v), 0);
    CHECK_EQ(v, 0);
    CHECK_EQ(mfb_cfg_number("4294967295", &v), 0);
    CHECK_EQ(v, 4294967295u);
    CHECK_EQ(mfb_cfg_number("4294967296", &v), 0);
    CHECK_EQ(v, 4294967295u);
    CHECK(mfb_cfg_number("", &v) == -1);
    CHECK(mfb_cfg_number("1a", &v) == -1);
}

/* Appends n entry sections E<first>..: with a path when with_path. */
static void add_entries(char *text, size_t size, unsigned first, unsigned n, int with_path)
{
    char one[64];
    unsigned i;

    for (i = first; i < first + n; i++) {
        if (with_path) {
            snprintf(one, sizeof(one), "[entry E%u]\npath = /E%u\n", i, i);
        } else {
            snprintf(one, sizeof(one), "[entry E%u]\n", i);
        }
        strncat(text, one, size - strlen(text) - 1);
    }
}

/* The first example of BOOT.CFG in BOOT_PROTOCOL.md: an empty section takes its name first. */
TEST(spec_example_empty_section_takes_its_name)
{
    CHECK_EQ(parse("[entry UOS]\n"
                   "\n"
                   "[entry UOS]\n"
                   "path = /ROBCO/UOS.SYS\n"
                   "\n"
                   "[entry MAINT]\n"
                   "path = /ROBCO/MAINT.SYS\n"),
             MFB_CFG_OK);
    CHECK_EQ(cfg.count, 1);
    CHECK_STR(cfg.entry[0].name, "MAINT");
    CHECK_EQ(rep.dup, 1);
    CHECK_EQ(rep.bad, 1);
    CHECK_EQ(rep.nopath, 1);
    CHECK_EQ(rep.ignored, 0);
}

/* The second example: dropped sections take no slot. This was the review finding. */
TEST(spec_example_dropped_sections_take_no_slots)
{
    CHECK_EQ(parse("[entry E1]\n[entry E2]\n[entry E3]\n[entry E4]\n"
                   "[entry E5]\n[entry E6]\n[entry E7]\n[entry E8]\n"
                   "[entry E9]\npath = /ROBCO/UOS.SYS\n"),
             MFB_CFG_OK);
    CHECK_EQ(cfg.count, 1);
    CHECK_STR(cfg.entry[0].name, "E9");
    CHECK_STR(cfg.entry[0].path, "/ROBCO/UOS.SYS");
    CHECK_EQ(rep.ignored, 0);
    CHECK_EQ(rep.bad, 8);
    CHECK_EQ(rep.nopath, 8);
}

TEST(unusable_sections_of_every_kind_take_no_slots)
{
    char text[2048] =
        "[entry]\npath = /A\n"                  /* no name: bad */
        "[entry a.b]\npath = /A\n"              /* bad name: bad */
        "[entry P]\npath = /BAD/\n"             /* bad path: bad */
        "[entry p]\npath = /P\n"                /* dup of P */
        "[entry C]\npath = /C\ncmdline = \"x\\ny\"\n"   /* bad cmdline: bad */
        "[entry N]\n";                          /* no path: bad */

    add_entries(text, sizeof(text), 1, 9, 1);
    CHECK_EQ(parse(text), MFB_CFG_OK);
    CHECK_EQ(cfg.count, MFB_MAX_ENTRIES);
    CHECK_STR(cfg.entry[0].name, "E1");
    CHECK_STR(cfg.entry[7].name, "E8");
    CHECK_EQ(rep.bad, 5);
    CHECK_EQ(rep.nopath, 1);
    CHECK_EQ(rep.dup, 1);
    CHECK_EQ(rep.ignored, 1);
}

TEST(usable_entries_past_the_eighth_are_ignored)
{
    char text[1024] = "";

    add_entries(text, sizeof(text), 1, 11, 1);
    /* The keys of an ignored section count nowhere, and its name still makes a dup. */
    strcat(text, "icon = x\n[entry E10]\npath = /X\n");
    CHECK_EQ(parse(text), MFB_CFG_OK);
    CHECK_EQ(cfg.count, MFB_MAX_ENTRIES);
    CHECK_STR(cfg.entry[7].name, "E8");
    CHECK_EQ(rep.ignored, 3);
    CHECK_EQ(rep.dup, 1);
    CHECK_EQ(rep.unknown, 0);
    CHECK_EQ(rep.bad, 0);
}

TEST(names_up_to_fifteen_as_written)
{
    CHECK_EQ(parse("[entry Abc-1_xYZ123456]\npath = /A\n[entry Abc-1_xYZ1234567]\npath = /B\n"),
             MFB_CFG_OK);
    CHECK_EQ(cfg.count, 1);
    CHECK_STR(cfg.entry[0].name, "Abc-1_xYZ123456");
    CHECK_EQ(rep.bad, 1);
}

TEST(name_characters)
{
    static const char *const bad[] = { "a.b", "A.SYS", "x.y-z" };
    char text[96];
    unsigned i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        snprintf(text, sizeof(text), "[entry %s]\npath = /A\n[entry B]\npath = /B\n", bad[i]);
        CHECK_EQ(parse(text), MFB_CFG_OK);
        CHECK_EQ(cfg.count, 1);
        CHECK_STR(cfg.entry[0].name, "B");
        CHECK_EQ(rep.bad, 1);
    }
    /* A space inside the name is a bad name too. */
    CHECK_EQ(parse("[entry A B]\npath = /A\n[entry B]\npath = /B\n"), MFB_CFG_OK);
    CHECK_EQ(cfg.count, 1);
    CHECK_EQ(rep.bad, 1);
}

TEST(header_takes_spaces_or_tabs)
{
    CHECK_EQ(parse("[entry\tTAB]\npath = /T\n[entry   SP]\npath = /S\n[ entry X ]\npath = /X\n"),
             MFB_CFG_OK);
    CHECK_EQ(cfg.count, 3);
    CHECK_STR(cfg.entry[0].name, "TAB");
    CHECK_STR(cfg.entry[1].name, "SP");
    CHECK_STR(cfg.entry[2].name, "X");
    /* "entryX" is not an entry header: an unknown section. */
    CHECK_EQ(parse("[entryX]\npath = /X\n[entry B]\npath = /B\n"), MFB_CFG_OK);
    CHECK_EQ(cfg.count, 1);
    CHECK_EQ(rep.unknown, 1);
    CHECK_EQ(rep.bad, 0);
}

TEST(entry_without_name)
{
    CHECK_EQ(parse("[entry]\npath = /A\n[entry ]\npath = /A\n[entry B]\npath = /B\n"),
             MFB_CFG_OK);
    CHECK_EQ(cfg.count, 1);
    CHECK_STR(cfg.entry[0].name, "B");
    CHECK_EQ(rep.bad, 2);
}

TEST(duplicates_first_wins)
{
    CHECK_EQ(parse("[entry UOS]\npath = /FIRST\n[entry uos]\npath = /SECOND\n"
                   "[entry UOS]\npath = /THIRD\ncmdline = x\n"),
             MFB_CFG_OK);
    CHECK_EQ(cfg.count, 1);
    CHECK_STR(cfg.entry[0].path, "/FIRST");
    CHECK_STR(cfg.entry[0].cmdline, "");
    CHECK_EQ(rep.dup, 2);
    /* A first section dropped for a bad path keeps the name. */
    CHECK_EQ(parse("[entry A]\npath = nope\n[entry A]\npath = /A\n[entry B]\npath = /B\n"),
             MFB_CFG_OK);
    CHECK_EQ(cfg.count, 1);
    CHECK_STR(cfg.entry[0].name, "B");
    CHECK_EQ(rep.bad, 1);
    CHECK_EQ(rep.dup, 1);
    /* The keys of a dup section count nowhere. */
    CHECK_EQ(parse("[entry A]\npath = /A\n[entry A]\nicon = x\n"), MFB_CFG_OK);
    CHECK_EQ(rep.unknown, 0);
}

TEST(a_repeated_key_takes_its_last_value)
{
    CHECK_EQ(parse("[entry A]\npath = /ONE\npath = /TWO\ncmdline = a\ncmdline = b\n"),
             MFB_CFG_OK);
    CHECK_STR(cfg.entry[0].path, "/TWO");
    CHECK_STR(cfg.entry[0].cmdline, "b");
    /* A second, bad path drops the entry. */
    CHECK_EQ(parse("[entry A]\npath = /ONE\npath = two\n[entry B]\npath = /B\n"), MFB_CFG_OK);
    CHECK_EQ(cfg.count, 1);
    CHECK_STR(cfg.entry[0].name, "B");
    CHECK_EQ(rep.bad, 1);
    /* A bad path then a good one: the good one counts, nothing is bad. */
    CHECK_EQ(parse("[entry A]\npath = two\npath = /ONE\n"), MFB_CFG_OK);
    CHECK_STR(cfg.entry[0].path, "/ONE");
    CHECK_EQ(rep.bad, 0);
}

TEST(boot_given_twice_acts_as_one)
{
    CHECK_EQ(parse("[boot]\ntimeout = x\ndefault = A\n[entry A]\npath = /A\n[entry B]\n"
                   "path = /B\n[boot]\ntimeout = 4\n"),
             MFB_CFG_OK);
    CHECK_EQ(cfg.timeout, 4);
    CHECK_EQ(cfg.def, 0);
    CHECK_EQ(rep.default_found, 1);
    CHECK_EQ(rep.bad, 0);
    CHECK_EQ(parse("[boot]\ndefault = A\n[entry A]\npath = /A\n[entry B]\npath = /B\n"
                   "[BOOT]\ndefault = b\n"),
             MFB_CFG_OK);
    CHECK_EQ(cfg.def, 1);
}

TEST(default_of_a_dropped_or_ignored_section_is_the_first)
{
    char text[1024] = "[boot]\ndefault = E9\n";

    add_entries(text, sizeof(text), 1, 9, 1);
    CHECK_EQ(parse(text), MFB_CFG_OK);
    CHECK_EQ(rep.ignored, 1);
    CHECK_EQ(cfg.def, 0);
    CHECK_EQ(rep.default_found, 0);
    CHECK_EQ(parse("[boot]\ndefault = X\n[entry A]\npath = /A\n[entry X]\n"), MFB_CFG_OK);
    CHECK_EQ(cfg.def, 0);
    CHECK_EQ(rep.default_found, 0);
}

TEST(path_limits)
{
    static const char at63[] = "/ABCDEFGH/ABCDEFGH/ABCDEFGH/ABCDEFGH/ABCDEFGH/ABCDEFGH/A/AB.SYS";
    static const char at64[] = "/ABCDEFGH/ABCDEFGH/ABCDEFGH/ABCDEFGH/ABCDEFGH/ABCDEFGH/A/ABC.SYS";
    char text[256];

    CHECK_EQ(strlen(at63), MFB_PATH_MAX);
    CHECK_EQ(strlen(at64), MFB_PATH_MAX + 1);
    snprintf(text, sizeof(text), "[entry A]\npath = %s\n", at63);
    CHECK_EQ(parse(text), MFB_CFG_OK);
    CHECK_STR(cfg.entry[0].path, at63);
    snprintf(text, sizeof(text), "[entry A]\npath = %s\n[entry B]\npath = /B\n", at64);
    CHECK_EQ(parse(text), MFB_CFG_OK);
    CHECK_EQ(cfg.count, 1);
    CHECK_STR(cfg.entry[0].name, "B");
    CHECK_EQ(rep.bad, 1);
    /* Eight components, then nine. */
    CHECK_EQ(parse("[entry A]\npath = /A/B/C/D/E/F/G/H\n"), MFB_CFG_OK);
    CHECK_STR(cfg.entry[0].path, "/A/B/C/D/E/F/G/H");
    CHECK_EQ(parse("[entry A]\npath = /A/B/C/D/E/F/G/H/I\n[entry B]\npath = /B\n"), MFB_CFG_OK);
    CHECK_EQ(cfg.count, 1);
    CHECK_STR(cfg.entry[0].name, "B");
    CHECK_EQ(rep.bad, 1);
}

TEST(path_rule)
{
    static const char *const good[] = {
        "/A", "/ROBCO/UOS.SYS", "/ABCDEFGH.ABC", "/A~1/B_C-D.X", "/0/9.Z",
    };
    static const char *const bad[] = {
        "ROBCO/UOS.SYS", "/", "//A", "/A//B", "/A/", "/.", "/..", "/A/./B", "/A/../B",
        "/ABCDEFGHI", "/A.ABCD", "/A.", "/.SYS", "/A.B.C", "/A$", "/A!", "/A B", "\"/A B\"",
        "/A\\B",
    };
    char text[128];
    unsigned i;

    for (i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
        snprintf(text, sizeof(text), "[entry A]\npath = %s\n", good[i]);
        CHECK_EQ(parse(text), MFB_CFG_OK);
        CHECK_STR(cfg.entry[0].path, good[i]);
    }
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        snprintf(text, sizeof(text), "[entry A]\npath = %s\n[entry B]\npath = /B\n", bad[i]);
        CHECK_EQ(parse(text), MFB_CFG_OK);
        CHECK_EQ(cfg.count, 1);
        CHECK_STR(cfg.entry[0].name, "B");
        CHECK_EQ(rep.bad, 1);
    }
}

TEST(path_is_stored_in_upper_case)
{
    CHECK_EQ(parse("[entry A]\npath = /robco/uos.sys\n[entry B]\npath = /Robco/Maint~1.Sys\n"),
             MFB_CFG_OK);
    CHECK_STR(cfg.entry[0].path, "/ROBCO/UOS.SYS");
    CHECK_STR(cfg.entry[1].path, "/ROBCO/MAINT~1.SYS");
}

TEST(no_path_drops_the_entry)
{
    CHECK_EQ(parse("[entry A]\ncmdline = x\nicon = y\n[entry B]\npath = /B\n"), MFB_CFG_OK);
    CHECK_EQ(cfg.count, 1);
    CHECK_STR(cfg.entry[0].name, "B");
    CHECK_EQ(rep.bad, 1);
    CHECK_EQ(rep.nopath, 1);
    /* The unknown key of the dropped section counts nowhere. */
    CHECK_EQ(rep.unknown, 0);
}

TEST(cmdline_limits)
{
    char text[400];
    char cl[200];

    memset(cl, 'c', sizeof(cl));
    cl[MFB_CMDLINE_MAX] = '\0';
    snprintf(text, sizeof(text), "[entry A]\npath = /A\ncmdline = %s\n", cl);
    CHECK_EQ(parse(text), MFB_CFG_OK);
    CHECK_EQ(strlen(cfg.entry[0].cmdline), MFB_CMDLINE_MAX);
    /* One more is past the INI reader's value limit: the whole file is bad. */
    cl[MFB_CMDLINE_MAX] = 'c';
    cl[MFB_CMDLINE_MAX + 1] = '\0';
    snprintf(text, sizeof(text), "[entry A]\npath = /A\ncmdline = %s\n", cl);
    CHECK_EQ(parse(text), MFB_CFG_SYNTAX);
    CHECK_EQ(line, 3);
    check_default_menu();
    /* Spaces inside a quoted cmdline stay, and so do the decoded escapes \" and \\. */
    CHECK_EQ(parse("[entry A]\npath = /A\ncmdline = \"a b  c \\\"q\\\" \\\\ ~\"\n"),
             MFB_CFG_OK);
    CHECK_STR(cfg.entry[0].cmdline, "a b  c \"q\" \\ ~");
}

TEST(cmdline_with_a_byte_outside_printable_ascii_drops_the_entry)
{
    /* A \n escape, a tab, bytes above 0x7E. ini.h itself rejects 0x7F and other raw controls. */
    static const char *const bad[] = { "\"a\\nb\"", "\"a\tb\"", "a\tb", "caf\xc3\xa9", "\x80" };
    char text[128];
    unsigned i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        snprintf(text, sizeof(text), "[entry A]\npath = /A\ncmdline = %s\n[entry B]\n"
                 "path = /B\n", bad[i]);
        CHECK_EQ(parse(text), MFB_CFG_OK);
        CHECK_EQ(cfg.count, 1);
        CHECK_STR(cfg.entry[0].name, "B");
        CHECK_EQ(rep.bad, 1);
    }
    CHECK_EQ(parse("[entry A]\npath = /A\ncmdline = a\x7f\n[entry B]\npath = /B\n"),
             MFB_CFG_SYNTAX);
    check_default_menu();
}

TEST(unknown_keys_and_sections)
{
    CHECK_EQ(parse("top = 1\n[boot]\ncolour = red\n[entry A]\npath = /A\nicon = x\n"
                   "[other]\nk = v\n[bios]\ndelay_ms = 0\nbeep = off\n"),
             MFB_CFG_OK);
    CHECK_EQ(cfg.count, 1);
    /* top, colour, icon and k. [bios] keys are not counted: the BIOS owns them. */
    CHECK_EQ(rep.unknown, 4);
    CHECK_EQ(rep.bad, 0);
}

TEST(maint_section_is_skipped_without_a_count)
{
    /* BOOT_PROTOCOL.md: MFBootAgent skips [maint] without a count, as [bios]. */
    CHECK_EQ(parse("[maint]\nrestore_users = on\nother = 1\n[entry A]\npath = /A\n"
                   "[MAINT]\nk = v\n"),
             MFB_CFG_OK);
    CHECK_EQ(cfg.count, 1);
    CHECK_EQ(rep.unknown, 0);
    CHECK_EQ(rep.bad, 0);
    CHECK_EQ(rep.dup, 0);
    CHECK_EQ(rep.ignored, 0);
    /* [maint] keys never reach an entry. */
    CHECK_STR(cfg.entry[0].cmdline, "");
    CHECK_EQ(parse("[entry A]\npath = /A\n[maint]\npath = /B\n"), MFB_CFG_OK);
    CHECK_STR(cfg.entry[0].path, "/A");
    CHECK_EQ(rep.unknown, 0);
}

TEST(names_and_keys_ignore_case)
{
    CHECK_EQ(parse("[BOOT]\nDEFAULT = b\nTimeout = 5\n[Entry b]\nPATH = /B\nCmdLine = q\n"),
             MFB_CFG_OK);
    CHECK_EQ(cfg.count, 1);
    CHECK_STR(cfg.entry[0].name, "b");
    CHECK_STR(cfg.entry[0].cmdline, "q");
    CHECK_EQ(cfg.timeout, 5);
    CHECK_EQ(rep.default_found, 1);
}

TEST(syntax_errors_give_the_default_menu)
{
    static const char *const bad[] = {
        "[entry A]\npath = /A\nnot a line\n",
        "[entry A\npath = /A\n",
        "[entry A]\npath = /A\x01\n",
        "[boot]\ntimeout = 3\n[entry A]\npath = /A\rB\n",
        /* Sections before the bad line count nowhere. */
        "[entry]\n[entry A]\npath = /A\n[entry A]\nnot a line\n",
    };
    unsigned i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK_EQ(parse(bad[i]), MFB_CFG_SYNTAX);
        CHECK(line > 0);
        check_default_menu();
        CHECK_EQ(rep.bad + rep.dup + rep.ignored + rep.unknown + rep.nopath, 0);
    }
}

TEST(empty_files)
{
    CHECK_EQ(parse(""), MFB_CFG_EMPTY);
    check_default_menu();
    CHECK_EQ(parse("[boot]\ntimeout = 3\n"), MFB_CFG_EMPTY);
    check_default_menu();
    CHECK_EQ(parse("[entry A]\ncmdline = x\n"), MFB_CFG_EMPTY);
    check_default_menu();
    CHECK_EQ(rep.bad, 1);
    CHECK_EQ(rep.nopath, 1);
}

TEST(crlf_and_comments)
{
    CHECK_EQ(parse("# menu\r\n[boot]\r\ntimeout = 2 ; seconds\r\n[entry A]\r\npath = /A\r\n"),
             MFB_CFG_OK);
    CHECK_EQ(cfg.timeout, 2);
    CHECK_STR(cfg.entry[0].path, "/A");
}
