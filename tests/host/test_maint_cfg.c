/* MAINT.SYS: the [maint] section of BOOT.CFG, by the key table of BOOT_PROTOCOL.md. */

#include <string.h>

#include "mt_cfg.h"
#include "rc_test.h"

static int parse(const char *text, mt_cfg_t *c, unsigned *line)
{
    return mt_cfg_parse(text, strlen(text), c, line);
}

TEST(off_by_default)
{
    mt_cfg_t c;
    unsigned line = 99;

    CHECK_EQ(parse("[boot]\ndefault = UOS\n", &c, &line), MT_CFG_OK);
    CHECK_EQ(c.restore_users, 0);
    CHECK_EQ(c.bad, 0u);
    CHECK_EQ(c.unknown, 0u);
    CHECK_EQ(line, 0u);
    CHECK_EQ(parse("", &c, &line), MT_CFG_OK);
    CHECK_EQ(c.restore_users, 0);
}

TEST(on_off_and_case)
{
    mt_cfg_t c;

    CHECK_EQ(parse("[maint]\nrestore_users = on\n", &c, NULL), MT_CFG_OK);
    CHECK_EQ(c.restore_users, 1);
    CHECK_EQ(parse("[Maint]\nRESTORE_USERS = On\n", &c, NULL), MT_CFG_OK);
    CHECK_EQ(c.restore_users, 1);
    CHECK_EQ(parse("[maint]\nrestore_users = OFF\n", &c, NULL), MT_CFG_OK);
    CHECK_EQ(c.restore_users, 0);
    CHECK_EQ(c.bad, 0u);
}

TEST(bad_values_keep_it_off)
{
    static const char *const bad[] = { "yes", "1", "true", "onn", "o", "" };
    char text[64];
    mt_cfg_t c;
    unsigned i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        snprintf(text, sizeof(text), "[maint]\nrestore_users = %s\n", bad[i]);
        CHECK_EQ(parse(text, &c, NULL), MT_CFG_OK);
        CHECK_EQ(c.restore_users, 0);
        CHECK_EQ(c.bad, 1u);
    }
    /* The last value wins: a bad value after on turns it off. */
    CHECK_EQ(parse("[maint]\nrestore_users = on\nrestore_users = maybe\n", &c, NULL), MT_CFG_OK);
    CHECK_EQ(c.restore_users, 0);
    CHECK_EQ(c.bad, 1u);
    CHECK_EQ(parse("[maint]\nrestore_users = maybe\nrestore_users = on\n", &c, NULL), MT_CFG_OK);
    CHECK_EQ(c.restore_users, 1);
    CHECK_EQ(c.bad, 1u);
}

TEST(sections)
{
    mt_cfg_t c;

    /* Unknown keys count; other sections are skipped without a count. */
    CHECK_EQ(parse("[maint]\ncolour = red\n[boot]\nrestore_users = on\n", &c, NULL), MT_CFG_OK);
    CHECK_EQ(c.restore_users, 0);
    CHECK_EQ(c.unknown, 1u);
    /* Two [maint] headers act as one section. */
    CHECK_EQ(parse("[maint]\nrestore_users = on\n[boot]\ntimeout = 3\n[maint]\nx = 1\n", &c, NULL),
             MT_CFG_OK);
    CHECK_EQ(c.restore_users, 1);
    CHECK_EQ(c.unknown, 1u);
    /* Keys before any section are not [maint]. */
    CHECK_EQ(parse("restore_users = on\n", &c, NULL), MT_CFG_OK);
    CHECK_EQ(c.restore_users, 0);
}

TEST(a_bad_line_leaves_it_off)
{
    mt_cfg_t c;
    unsigned line = 0;

    CHECK_EQ(parse("[maint]\nrestore_users = on\nnot a line\n", &c, &line), MT_CFG_SYNTAX);
    CHECK_EQ(c.restore_users, 0);
    CHECK_EQ(line, 3u);
    CHECK_EQ(parse("[maint]\nrestore_users = on\n[broken\n", &c, &line), MT_CFG_SYNTAX);
    CHECK_EQ(c.restore_users, 0);
    CHECK_EQ(line, 3u);
}

/* The start says when it could not read [maint]; a missing file is a known off. */
TEST(not_known_says_so)
{
    static const char bad[] = "[maint]\nrestore_users = on\nthis line has no equals\n";
    char out[160];
    mt_cfg_t c;
    unsigned line = 0;

    mt_cfg_default(&c);
    CHECK(mt_cfg_known(&c));
    mt_cfg_status(out, sizeof(out), &c);
    CHECK_STR(out, "");
    c.read_rc = -17;
    CHECK(!mt_cfg_known(&c));
    mt_cfg_status(out, sizeof(out), &c);
    CHECK_STR(out, "BOOT.CFG does not read: error -17. Restore users is off.");
    CHECK(strlen(out) < 79);
    CHECK_EQ(mt_cfg_parse(bad, sizeof(bad) - 1, &c, &line), MT_CFG_SYNTAX);
    CHECK_EQ(c.restore_users, 0);
    CHECK_EQ(c.syntax_line, line);
    CHECK(!mt_cfg_known(&c));
    mt_cfg_status(out, sizeof(out), &c);
    CHECK_STR(out, "BOOT.CFG line 3 does not parse. Restore users is off.");
}
