/*
 * The user folders of /UOS/HOME that view file lists, each for its
 * TERMINAL.CFG. Pure: the host tests link it.
 */
#ifndef MT_HOME_H
#define MT_HOME_H

#include <stddef.h>

#define MT_HOME_DIR     "/UOS/HOME"
#define MT_HOME_FILE    "TERMINAL.CFG"
/* User folders listed, in directory order. A later one is not listed. */
#define MT_HOME_MAX     16u

/*
 * 1 when a /UOS/HOME entry counts as a user folder: a folder whose name
 * does not start with '.', with neither the hidden nor the system
 * attribute, and that is no work name of fs_write_file_atomic().
 * fs_readdir() already skips "." and "..".
 */
int mt_home_user(const char *name, unsigned attr, int is_dir, int leftover);

/*
 * Writes "/UOS/HOME/<user>/TERMINAL.CFG" into out. Returns 0, or -1 when
 * it does not fit `size` or the path would pass max_path characters.
 */
int mt_home_path(char *out, size_t size, const char *user, size_t max_path);

#endif
