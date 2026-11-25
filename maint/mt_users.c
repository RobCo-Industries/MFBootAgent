/* The decision table of the restore users tool: see mt_users.h. */

#include <stddef.h>

#include "mt_ui.h"
#include "mt_users.h"

int mt_users_check(int on, int cfg_known, int stat_rc, int missing_rc, int is_dir,
                   uint32_t size)
{
    if (!on) {
        return cfg_known ? MT_US_OFF : MT_US_CONFIG;
    }
    if (stat_rc == missing_rc || (stat_rc == 0 && is_dir)) {
        return MT_US_MISSING;
    }
    if (stat_rc != 0) {
        return MT_US_STAT;
    }
    if (size == 0) {
        return MT_US_EMPTY;
    }
    if (size > MT_USERS_MAX) {
        return MT_US_LARGE;
    }
    return MT_US_ASK;
}

int mt_users_may_write(int check, int answer)
{
    return check == MT_US_ASK && answer == MT_ANS_YES;
}

int mt_users_result(int check, int answer, int read_rc, int write_rc, const char **why)
{
    const char *w = "";
    int r = MT_FAIL;

    switch (check) {
    case MT_US_OFF:
        r = MT_OFF;
        w = "setting";
        break;
    case MT_US_CONFIG:
        /* The setting is not known: never a pass, nothing changes. */
        w = "config";
        break;
    case MT_US_MISSING:
        r = MT_ABSENT;
        w = "missing";
        break;
    case MT_US_EMPTY:
        w = "empty";
        break;
    case MT_US_LARGE:
        w = "large";
        break;
    case MT_US_STAT:
        w = "stat";
        break;
    case MT_US_ASK:
        if (answer != MT_ANS_YES) {
            r = MT_REFUSED;
            w = "no";
        } else if (read_rc != 0) {
            w = "read";
        } else if (write_rc != 0) {
            w = "write";
        } else {
            r = MT_OK;
        }
        break;
    default:
        w = "unknown";
        break;
    }
    if (why != NULL) {
        *why = w;
    }
    return r;
}
