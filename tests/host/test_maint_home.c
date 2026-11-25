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

/* Only FS_ERR_NOENT is "not there". */
TEST(fs_kind)
{
    CHECK_EQ(mt_fs_kind(0), MT_FS_THERE);
    CHECK_EQ(mt_fs_kind(FS_ERR_NOENT), MT_FS_NONE);
    CHECK_EQ(mt_fs_kind(FS_ERR_IO), MT_FS_UNKNOWN);
    CHECK_EQ(mt_fs_kind(FS_ERR_CORRUPT), MT_FS_UNKNOWN);
    CHECK_EQ(mt_fs_kind(FS_ERR_GONE), MT_FS_UNKNOWN);
    CHECK_EQ(mt_fs_kind(FS_ERR_TIMEOUT), MT_FS_UNKNOWN);
}

/* A /UOS/HOME that does not read is no missing folder. */
TEST(home_listing_states)
{
    char note[160];
    mt_home_list_t l;

    mt_home_list_init(&l);
    CHECK(!mt_home_opened(&l, FS_ERR_NOENT));
    CHECK_EQ(l.dir, MT_HOME_MISSING);
    CHECK(!mt_home_fault(&l));
    mt_home_note(note, sizeof(note), &l);
    CHECK_STR(note, "No user TERMINAL.CFG: no /UOS/HOME folder.");

    mt_home_list_init(&l);
    CHECK(!mt_home_opened(&l, FS_ERR_IO));
    CHECK_EQ(l.dir, MT_HOME_ERROR);
    CHECK(mt_home_fault(&l));
    CHECK_EQ(mt_home_fault_rc(&l), FS_ERR_IO);
    mt_home_note(note, sizeof(note), &l);
    CHECK_STR(note, "/UOS/HOME does not read: error -1. Users may be missing.");
    CHECK(strlen(note) < 79);

    mt_home_list_init(&l);
    CHECK(mt_home_opened(&l, 0));
    mt_home_note(note, sizeof(note), &l);
    CHECK_STR(note, "No user TERMINAL.CFG: none found in /UOS/HOME.");
    CHECK(mt_home_offer(&l, 0, 0));
    mt_home_note(note, sizeof(note), &l);
    CHECK_STR(note, "");
    mt_home_read_failed(&l, FS_ERR_CORRUPT);
    CHECK(mt_home_fault(&l));
    CHECK_EQ(mt_home_fault_rc(&l), FS_ERR_CORRUPT);
    mt_home_note(note, sizeof(note), &l);
    CHECK_STR(note, "/UOS/HOME does not read: error -17. Users may be missing.");
}

/* A missing TERMINAL.CFG is not offered; a stat that could not tell is offered and counted. */
TEST(home_offer)
{
    char note[160];
    mt_home_list_t l;

    mt_home_list_init(&l);
    CHECK(mt_home_opened(&l, 0));
    CHECK(!mt_home_offer(&l, FS_ERR_NOENT, 0));
    CHECK(!mt_home_offer(&l, 0, 1));
    CHECK(!mt_home_fault(&l));
    CHECK(mt_home_offer(&l, FS_ERR_IO, 0));
    CHECK(mt_home_offer(&l, FS_ERR_CORRUPT, 0));
    CHECK_EQ(l.offered, 2);
    CHECK_EQ(l.stat_errors, 2);
    CHECK(mt_home_fault(&l));
    CHECK_EQ(mt_home_fault_rc(&l), FS_ERR_IO);
    mt_home_note(note, sizeof(note), &l);
    CHECK_STR(note, "2 user TERMINAL.CFG do not read: error -1.");
    mt_home_list_init(&l);
    l.long_paths = 1;
    mt_home_note(note, sizeof(note), &l);
    CHECK_STR(note, "1 user folders have a path too long to open.");
}
