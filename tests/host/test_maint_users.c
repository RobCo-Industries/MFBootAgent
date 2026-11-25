/* MAINT.SYS: the decision table of restore users. */

#include <string.h>

#include <robco/fs.h>

#include "mt_ui.h"
#include "mt_users.h"
#include "rc_test.h"

#define NOENT (-6)

TEST(check_before_the_question)
{
    CHECK_EQ(mt_users_check(0, 1, 0, NOENT, 0, 100), MT_US_OFF);
    /* Off wins over everything: nothing is even looked at. */
    CHECK_EQ(mt_users_check(0, 1, NOENT, NOENT, 0, 0), MT_US_OFF);
    CHECK_EQ(mt_users_check(1, 1, NOENT, NOENT, 0, 0), MT_US_MISSING);
    CHECK_EQ(mt_users_check(1, 1, 0, NOENT, 1, 0), MT_US_MISSING);
    CHECK_EQ(mt_users_check(1, 1, -1, NOENT, 0, 0), MT_US_STAT);
    CHECK_EQ(mt_users_check(1, 1, 0, NOENT, 0, 0), MT_US_EMPTY);
    CHECK_EQ(mt_users_check(1, 1, 0, NOENT, 0, MT_USERS_MAX + 1u), MT_US_LARGE);
    CHECK_EQ(mt_users_check(1, 1, 0, NOENT, 0, MT_USERS_MAX), MT_US_ASK);
    CHECK_EQ(mt_users_check(1, 1, 0, NOENT, 0, 1), MT_US_ASK);
}

/* A BOOT.CFG that did not read or parse: off, but the row says the setting is not known. */
TEST(config_not_known)
{
    CHECK_EQ(mt_users_check(0, 0, 0, NOENT, 0, 100), MT_US_CONFIG);
    CHECK_EQ(mt_users_check(0, 1, 0, NOENT, 0, 100), MT_US_OFF);
    CHECK_EQ(mt_users_may_write(MT_US_CONFIG, MT_ANS_YES), 0);
}

/* Only a missing file is "no default". Every other stat error refuses the copy. */
TEST(stat_errors_refuse)
{
    static const int could_not_tell[] = {
        FS_ERR_IO, FS_ERR_TIMEOUT, FS_ERR_NOTREADY, FS_ERR_INVAL, FS_ERR_NAME,
        FS_ERR_NAMETOOLONG, FS_ERR_NOVOL, FS_ERR_NOFS, FS_ERR_CORRUPT, FS_ERR_NOMEM,
        FS_ERR_GONE, FS_ERR_BUSY, FS_ERR_DENIED, -1000,
    };
    const char *why = NULL;
    unsigned i;

    CHECK_EQ(mt_users_check(1, 1, FS_ERR_NOENT, FS_ERR_NOENT, 0, 0), MT_US_MISSING);
    for (i = 0; i < sizeof(could_not_tell) / sizeof(could_not_tell[0]); i++) {
        int c = mt_users_check(1, 1, could_not_tell[i], FS_ERR_NOENT, 0, 100);

        CHECK_EQ(c, MT_US_STAT);
        CHECK_EQ(mt_users_may_write(c, MT_ANS_YES), 0);
        CHECK_EQ(mt_users_result(c, MT_US_ANS_NONE, 0, 0, &why), MT_FAIL);
        CHECK_STR(why, "stat");
    }
}

TEST(only_a_yes_after_the_question_writes)
{
    int c;

    for (c = MT_US_OFF; c <= MT_US_CONFIG; c++) {
        CHECK_EQ(mt_users_may_write(c, MT_ANS_NO), 0);
        CHECK_EQ(mt_users_may_write(c, MT_US_ANS_NONE), 0);
        CHECK_EQ(mt_users_may_write(c, MT_ANS_YES), c == MT_US_ASK);
    }
}

TEST(results)
{
    static const struct {
        int check, answer, read_rc, write_rc, result;
        const char *why;
    } rows[] = {
        { MT_US_OFF, MT_US_ANS_NONE, 0, 0, MT_OFF, "setting" },
        { MT_US_CONFIG, MT_US_ANS_NONE, 0, 0, MT_FAIL, "config" },
        { MT_US_MISSING, MT_US_ANS_NONE, 0, 0, MT_ABSENT, "missing" },
        { MT_US_EMPTY, MT_US_ANS_NONE, 0, 0, MT_FAIL, "empty" },
        { MT_US_LARGE, MT_US_ANS_NONE, 0, 0, MT_FAIL, "large" },
        { MT_US_STAT, MT_US_ANS_NONE, 0, 0, MT_FAIL, "stat" },
        { MT_US_ASK, MT_ANS_NO, 0, 0, MT_REFUSED, "no" },
        { MT_US_ASK, MT_US_ANS_NONE, 0, 0, MT_REFUSED, "no" },
        { MT_US_ASK, MT_ANS_YES, -1, 0, MT_FAIL, "read" },
        { MT_US_ASK, MT_ANS_YES, 0, -12, MT_FAIL, "write" },
        { MT_US_ASK, MT_ANS_YES, 0, 0, MT_OK, "" },
    };
    const char *why = NULL;
    unsigned i;

    for (i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        CHECK_EQ(mt_users_result(rows[i].check, rows[i].answer, rows[i].read_rc,
                                 rows[i].write_rc, &why), rows[i].result);
        CHECK_STR(why, rows[i].why);
    }
}
