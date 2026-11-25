/*
 * The walk of the check card tool: every folder and every file of the
 * boot volume, depth first, with the file system behind function pointers
 * so the host tests can fake it. Pure: the host tests link it.
 */
#ifndef MT_WALK_H
#define MT_WALK_H

#include <stdint.h>

/* Folders open at once: the root and seven levels below it. */
#define MT_WALK_DEPTH   8u
/* The longest path, as the file system takes it. */
#define MT_WALK_PATH    255u

/* What failed, for error(). */
#define MT_WE_OPENDIR   0   /* a folder does not open */
#define MT_WE_READDIR   1   /* reading a folder failed: the rest of it is not checked */
#define MT_WE_FILE      2   /* a file failed: rc from file() */
#define MT_WE_DEEP      3   /* a folder below MT_WALK_DEPTH levels: not entered */
#define MT_WE_LONG      4   /* a path longer than MT_WALK_PATH: not checked */

/* file() result for a file that read fewer bytes than its size. */
#define MT_WALK_SHORT   (-100)

typedef struct {
    const char *name;
    uint32_t size;
    int is_dir;
} mt_walk_entry_t;

/*
 * opendir(level, path) opens the folder at `level`, 0 for the root "/".
 * readdir(level) returns 1 with an entry, 0 at the end or a negative
 * error. closedir(level) closes it. file(path, size) reads the whole file
 * and returns 0, a negative error, or MT_WALK_SHORT. error() gets each
 * failure with its path. abort() returns 1 to stop.
 */
typedef struct {
    void *ctx;
    int (*opendir)(void *ctx, unsigned level, const char *path);
    int (*readdir)(void *ctx, unsigned level, mt_walk_entry_t *e);
    int (*closedir)(void *ctx, unsigned level);
    int (*file)(void *ctx, const char *path, uint32_t size);
    void (*error)(void *ctx, const char *path, int what, int rc);
    int (*abort)(void *ctx);
} mt_walk_ops_t;

typedef struct {
    unsigned dirs;      /* folders opened, the root included */
    unsigned files;     /* files checked */
    unsigned errors;    /* every failure error() got */
    uint64_t bytes;     /* sizes of the files that read whole */
    int first_what;     /* MT_WE_* of the first failure */
    int first_rc;       /* and its code */
    char first_path[MT_WALK_PATH + 1]; /* and its path, "" if none */
} mt_walk_t;

/* Results of mt_walk(). */
#define MT_WALK_OK      0   /* the whole volume was walked */
#define MT_WALK_ABORT   1   /* abort() asked to stop */

/*
 * Walks from the root. Skips "." and "..". Every open folder is closed on
 * every path. A failure is counted and the walk goes on with the next
 * entry. Returns MT_WALK_OK or MT_WALK_ABORT.
 */
int mt_walk(const mt_walk_ops_t *ops, mt_walk_t *w);

/* "opendir", "readdir", "file", "deep", "long". */
const char *mt_walk_what(int what);

/* 1 when the walk found no failure. */
int mt_walk_clean(const mt_walk_t *w);

#endif
