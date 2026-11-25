/* The user folders of /UOS/HOME: see mt_home.h. */

#include <stddef.h>

#include <robco/fs.h>
#include <robco/klibc.h>

#include "mt_home.h"

int mt_home_user(const char *name, unsigned attr, int is_dir, int leftover)
{
    if (name == NULL || name[0] == '\0' || name[0] == '.' || !is_dir || leftover) {
        return 0;
    }
    return (attr & (FS_ATTR_HIDDEN | FS_ATTR_SYSTEM)) == 0;
}

int mt_home_path(char *out, size_t size, const char *user, size_t max_path)
{
    size_t n = sizeof(MT_HOME_DIR) - 1 + 1 + k_strlen(user) + 1 + sizeof(MT_HOME_FILE) - 1;

    if (n > max_path || n + 1 > size) {
        return -1;
    }
    k_snprintf(out, size, MT_HOME_DIR "/%s/" MT_HOME_FILE, user);
    return 0;
}

int mt_fs_kind(int rc)
{
    if (rc == 0) {
        return MT_FS_THERE;
    }
    return rc == FS_ERR_NOENT ? MT_FS_NONE : MT_FS_UNKNOWN;
}

void mt_home_list_init(mt_home_list_t *l)
{
    k_memset(l, 0, sizeof(*l));
}

int mt_home_opened(mt_home_list_t *l, int rc)
{
    switch (mt_fs_kind(rc)) {
    case MT_FS_THERE:
        return 1;
    case MT_FS_NONE:
        l->dir = MT_HOME_MISSING;
        return 0;
    default:
        l->dir = MT_HOME_ERROR;
        l->dir_rc = rc;
        return 0;
    }
}

void mt_home_read_failed(mt_home_list_t *l, int rc)
{
    l->dir = MT_HOME_ERROR;
    l->dir_rc = rc < 0 ? rc : -1;
}

int mt_home_offer(mt_home_list_t *l, int stat_rc, int is_dir)
{
    switch (mt_fs_kind(stat_rc)) {
    case MT_FS_THERE:
        if (is_dir) {
            return 0;
        }
        l->offered++;
        return 1;
    case MT_FS_NONE:
        return 0;
    default:
        if (l->stat_errors == 0) {
            l->stat_rc = stat_rc;
        }
        l->stat_errors++;
        l->offered++;
        return 1;
    }
}

int mt_home_fault(const mt_home_list_t *l)
{
    return l->dir == MT_HOME_ERROR || l->stat_errors != 0;
}

int mt_home_fault_rc(const mt_home_list_t *l)
{
    if (l->dir == MT_HOME_ERROR) {
        return l->dir_rc;
    }
    return l->stat_errors != 0 ? l->stat_rc : 0;
}

void mt_home_note(char *out, size_t size, const mt_home_list_t *l)
{
    if (l->dir == MT_HOME_ERROR) {
        k_snprintf(out, size, MT_HOME_DIR " does not read: error %d. Users may be missing.",
                   l->dir_rc);
    } else if (l->stat_errors != 0) {
        k_snprintf(out, size, "%u user " MT_HOME_FILE " do not read: error %d.",
                   l->stat_errors, l->stat_rc);
    } else if (l->cut) {
        k_snprintf(out, size, "The list is cut: only the first %u user folders of "
                   MT_HOME_DIR ".", MT_HOME_MAX);
    } else if (l->long_paths != 0) {
        k_snprintf(out, size, "%u user folders have a path too long to open.", l->long_paths);
    } else if (l->offered == 0) {
        k_snprintf(out, size, "No user " MT_HOME_FILE ": %s.",
                   l->dir == MT_HOME_MISSING ? "no " MT_HOME_DIR " folder" :
                   "none found in " MT_HOME_DIR);
    } else if (size != 0) {
        out[0] = '\0';
    }
}
