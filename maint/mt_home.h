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

/*
 * What a file system code says of a path: there, not there, or the call
 * could not tell. Only FS_ERR_NOENT means "not there". Every other error,
 * such as FS_ERR_IO, FS_ERR_CORRUPT or FS_ERR_GONE, means the card did
 * not answer the question, and the tool shows it as an error.
 */
#define MT_FS_THERE     0
#define MT_FS_NONE      1
#define MT_FS_UNKNOWN   2
int mt_fs_kind(int rc);

/* The state of the /UOS/HOME listing. */
#define MT_HOME_OK      0   /* read to its end */
#define MT_HOME_MISSING 1   /* fs_opendir() gave FS_ERR_NOENT */
#define MT_HOME_ERROR   2   /* fs_opendir() or fs_readdir() failed otherwise */

typedef struct {
    int dir;            /* MT_HOME_* */
    int dir_rc;         /* the failed fs_opendir() or fs_readdir() code, else 0 */
    unsigned users;     /* user folders listed */
    int cut;            /* a user folder past MT_HOME_MAX was not listed */
    unsigned offered;   /* TERMINAL.CFG files offered */
    unsigned stat_errors; /* TERMINAL.CFG files whose fs_stat() could not tell */
    int stat_rc;        /* the first such code */
    unsigned long_paths; /* user folders whose path passes FS_PATH_MAX */
} mt_home_list_t;

/* Sets every field to 0: an empty listing of a /UOS/HOME that read. */
void mt_home_list_init(mt_home_list_t *l);

/* The fs_opendir() result of /UOS/HOME. Returns 1 when the folder may be read. */
int mt_home_opened(mt_home_list_t *l, int rc);

/* An fs_readdir() result below 0: the rest of the folder is not known. */
void mt_home_read_failed(mt_home_list_t *l, int rc);

/*
 * The fs_stat() of one user's TERMINAL.CFG. Returns 1 when the file is
 * offered: it is a file, or the stat could not tell. A missing file or a
 * folder of that name is not offered. A stat that could not tell counts
 * in stat_errors; the view of that entry then shows the error.
 */
int mt_home_offer(mt_home_list_t *l, int stat_rc, int is_dir);

/* 1 when the listing saw a fault: a folder read error or a stat error. */
int mt_home_fault(const mt_home_list_t *l);

/* The first fault code of the listing, or 0. */
int mt_home_fault_rc(const mt_home_list_t *l);

/* The chooser's note row for the listing, "" when there is nothing to say. */
void mt_home_note(char *out, size_t size, const mt_home_list_t *l);

#endif
