/*
 * Restore users. Off unless [maint] restore_users = on. With it on, it
 * checks the default file, names both files in a question, and only on
 * a yes copies the default over the user file with fs_write_file_atomic().
 * The write guard lets writes through only for that call. Every refusal
 * and failure before the write leaves the card untouched.
 */

#include <stdint.h>

#include <robco/alloc.h>
#include <robco/fs.h>
#include <robco/klibc.h>

#include "mt.h"
#include "mt_users.h"

void mt_tool_users(mt_run_t *r)
{
    fs_stat_t st;
    const char *why = "";
    void *base = 0;
    size_t room = robco_heap_high(&base);
    size_t len = 0;
    int check;
    int answer = MT_US_ANS_NONE;
    int read_rc = 0;
    int write_rc = 0;
    int rc = 0;

    if (mt.cfg.restore_users && mt_need_card(r) != 0) {
        return;
    }
    k_memset(&st, 0, sizeof(st));
    if (mt.cfg.restore_users) {
        rc = fs_stat(MT_USERS_DEF, &st);
    }
    check = mt_users_check(mt.cfg.restore_users, rc, FS_ERR_NOENT, st.is_dir, st.size);
    switch (check) {
    case MT_US_OFF:
        mt_work_line(0, "Off. BOOT.CFG must have [maint] restore_users = on.", 1);
        mt_text(r, "Off in BOOT.CFG.");
        break;
    case MT_US_MISSING:
        mt_work_line(0, "No default user file " MT_USERS_DEF ". Nothing changed.", 1);
        mt_text(r, "No " MT_USERS_DEF ".");
        break;
    case MT_US_EMPTY:
        mt_work_line(0, "The default user file " MT_USERS_DEF " is empty. Nothing changed.", 1);
        mt_text(r, "Empty " MT_USERS_DEF ".");
        break;
    case MT_US_LARGE:
        mt_work_line(0, "The default user file is too large. Nothing changed.", 1);
        mt_text(r, "Default file too large.");
        break;
    case MT_US_STAT:
        mt_work_line(0, "The default user file does not read. Nothing changed.", 1);
        mt_text(r, "Stat error %d.", rc);
        break;
    default:
        mt_work_line(0, "Copy " MT_USERS_DEF " over " MT_USERS_DAT "?", 1);
        mt_work_line(1, "Every account goes back to the default. ENTER or Y: yes. "
                        "Other keys: no.", 1);
        mt_mark("ask what=restore_users from=" MT_USERS_DEF " to=" MT_USERS_DAT " size=%u",
                (unsigned)st.size);
        answer = mt_ask();
        break;
    }
    if (mt_users_may_write(check, answer)) {
        if (room > MT_USERS_MAX) {
            room = MT_USERS_MAX;
        }
        read_rc = fs_read_file(MT_USERS_DEF, base, room, &len);
        if (read_rc == 0 && len != st.size) {
            read_rc = FS_ERR_IO;
        }
        if (read_rc == 0) {
            mt.guard.pass = 1;
            write_rc = fs_write_file_atomic(MT_USERS_DAT, base, len);
            mt.guard.pass = 0;
        }
    }
    r->result = mt_users_result(check, answer, read_rc, write_rc, &why);
    if (why[0] != '\0') {
        mt_keys(r, "why=%s", why);
    }
    if (check == MT_US_STAT) {
        mt_keys(r, "rc=%d", rc);
    }
    if (check == MT_US_ASK) {
        mt_keys(r, "from=" MT_USERS_DEF " to=" MT_USERS_DAT " size=%u", (unsigned)st.size);
    }
    if (read_rc != 0) {
        mt_keys(r, "rc=%d", read_rc);
        mt_text(r, "Read error %d. Nothing changed.", read_rc);
    } else if (write_rc != 0) {
        mt_keys(r, "rc=%d", write_rc);
        mt_text(r, "Write error %d.", write_rc);
    } else if (r->result == MT_REFUSED) {
        mt_text(r, "No. Nothing changed.");
        mt_work_line(2, "No. Nothing changed.", 1);
    } else if (r->result == MT_OK) {
        mt_text(r, "Default users restored.");
        mt_work_line(2, "Done. " MT_USERS_DAT " now holds the default users.", 1);
    }
}
