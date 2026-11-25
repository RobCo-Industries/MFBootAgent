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
