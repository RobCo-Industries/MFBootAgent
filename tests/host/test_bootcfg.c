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

TEST(at_most_eight_entries)
{
    char text[1024] = "";
    char one[64];
    unsigned i;

    for (i = 0; i < 11; i++) {
        snprintf(one, sizeof(one), "[entry E%u]\npath = /E%u\n", i + 1, i + 1);
        strcat(text, one);
    }
    CHECK_EQ(parse(text), MFB_CFG_OK);
    CHECK_EQ(cfg.count, MFB_MAX_ENTRIES);
    CHECK_STR(cfg.entry[7].name, "E8");
    CHECK_EQ(rep.ignored, 3);
    CHECK_EQ(rep.bad, 0);
}

TEST(names_up_to_fifteen_as_written)
{
    CHECK_EQ(parse("[entry Abc-1.x_YZ12345]\npath = /A\n[entry Abc-1.x_YZ123456]\npath = /B\n"),
             MFB_CFG_OK);
    CHECK_EQ(cfg.count, 1);
    CHECK_STR(cfg.entry[0].name, "Abc-1.x_YZ12345");
    CHECK_EQ(rep.bad, 1);
}

TEST(entry_without_name)
{
    CHECK_EQ(parse("[entry]\npath = /A\n[entry B]\npath = /B\n"), MFB_CFG_OK);
    CHECK_EQ(cfg.count, 1);
    CHECK_STR(cfg.entry[0].name, "B");
    CHECK_EQ(rep.bad, 1);
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
}

TEST(path_limits)
{
    char text[256];
    char path[80];

    memset(path, 'P', sizeof(path));
    path[0] = '/';
    path[MFB_PATH_MAX] = '\0';
    snprintf(text, sizeof(text), "[entry A]\npath = %s\n", path);
    CHECK_EQ(parse(text), MFB_CFG_OK);
    CHECK_EQ(strlen(cfg.entry[0].path), MFB_PATH_MAX);
    path[MFB_PATH_MAX] = 'P';
    path[MFB_PATH_MAX + 1] = '\0';
    snprintf(text, sizeof(text), "[entry A]\npath = %s\n[entry B]\npath = /B\n", path);
    CHECK_EQ(parse(text), MFB_CFG_OK);
    CHECK_EQ(cfg.count, 1);
    CHECK_STR(cfg.entry[0].name, "B");
    /* The bad value, then the entry with no path. */
    CHECK_EQ(rep.bad, 2);
}

TEST(bad_paths)
{
    static const char *const bad[] = { "ROBCO/UOS.SYS", "/", "\"/A B\"" };
    char text[96];
    unsigned i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        snprintf(text, sizeof(text), "[entry A]\npath = %s\n[entry B]\npath = /B\n", bad[i]);
        CHECK_EQ(parse(text), MFB_CFG_OK);
        CHECK_EQ(cfg.count, 1);
        CHECK_STR(cfg.entry[0].name, "B");
    }
    /* No path key at all. */
    CHECK_EQ(parse("[entry A]\ncmdline = x\n[entry B]\npath = /B\n"), MFB_CFG_OK);
    CHECK_EQ(cfg.count, 1);
    CHECK_EQ(rep.bad, 1);
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
    /* Spaces inside a quoted cmdline stay. */
    CHECK_EQ(parse("[entry A]\npath = /A\ncmdline = \"a b  c\"\n"), MFB_CFG_OK);
    CHECK_STR(cfg.entry[0].cmdline, "a b  c");
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
    };
    unsigned i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK_EQ(parse(bad[i]), MFB_CFG_SYNTAX);
        CHECK(line > 0);
        check_default_menu();
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
}

TEST(crlf_and_comments)
{
    CHECK_EQ(parse("# menu\r\n[boot]\r\ntimeout = 2 ; seconds\r\n[entry A]\r\npath = /A\r\n"),
             MFB_CFG_OK);
    CHECK_EQ(cfg.timeout, 2);
    CHECK_STR(cfg.entry[0].path, "/A");
}
