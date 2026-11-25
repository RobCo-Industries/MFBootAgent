/* MAINT.SYS: which /UOS/HOME entries view file lists, and their TERMINAL.CFG paths. */

#include <string.h>

#include <robco/fs.h>

#include "mt_home.h"
#include "rc_test.h"

TEST(user_folders)
{
    CHECK(mt_home_user("ALICE", 0, 1, 0));
    CHECK(mt_home_user("bob", FS_ATTR_ARCHIVE | FS_ATTR_DIR, 1, 0));
    /* Files, dot names, hidden and system entries, work names are no users. */
    CHECK(!mt_home_user("NOTES.TXT", 0, 0, 0));
    CHECK(!mt_home_user(".trash", 0, 1, 0));
    CHECK(!mt_home_user("GHOST", FS_ATTR_HIDDEN, 1, 0));
    CHECK(!mt_home_user("SYS", FS_ATTR_SYSTEM, 1, 0));
    CHECK(!mt_home_user("~ALICE", 0, 1, 1));
    CHECK(!mt_home_user("", 0, 1, 0));
    CHECK(!mt_home_user(NULL, 0, 1, 0));
}

TEST(paths)
{
    char out[300];
    char user[300];

    CHECK_EQ(mt_home_path(out, sizeof(out), "ALICE", FS_PATH_MAX), 0);
    CHECK_STR(out, "/UOS/HOME/ALICE/TERMINAL.CFG");
    /* "/UOS/HOME/" + user + "/TERMINAL.CFG" is 23 characters plus the name. */
    memset(user, 'u', 232);
    user[232] = '\0';
    CHECK_EQ(mt_home_path(out, sizeof(out), user, FS_PATH_MAX), 0);
    CHECK_EQ(strlen(out), 255u);
    user[232] = 'u';
    user[233] = '\0';
    CHECK_EQ(mt_home_path(out, sizeof(out), user, FS_PATH_MAX), -1);
    CHECK_EQ(mt_home_path(out, 10, "ALICE", FS_PATH_MAX), -1);
}
