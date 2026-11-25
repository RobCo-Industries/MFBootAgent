/* The walk of the check card tool: see mt_walk.h. */

#include <stddef.h>

#include <robco/klibc.h>

#include "mt_walk.h"
#include "../diag/dg_fmt.h"

static void fail(const mt_walk_ops_t *ops, mt_walk_t *w, const char *path, int what, int rc)
{
    if (w->errors == 0) {
        w->first_what = what;
        w->first_rc = rc;
        k_strncpy(w->first_path, path, sizeof(w->first_path));
        w->first_path[sizeof(w->first_path) - 1] = '\0';
    }
    w->errors++;
    ops->error(ops->ctx, path, what, rc);
}

/* Appends "/name" to path at len. Returns the new length, or 0 when it does not fit. */
static size_t join(char *path, size_t len, const char *name)
{
    size_t n = k_strlen(name);
    size_t base = len == 1 ? 0 : len;   /* the root is "/" */

    if (base + 1 + n > MT_WALK_PATH) {
        return 0;
    }
    path[base] = '/';
    k_memcpy(path + base + 1, name, n);
    path[base + 1 + n] = '\0';
    return base + 1 + n;
}

int mt_walk(const mt_walk_ops_t *ops, mt_walk_t *w)
{
    char path[MT_WALK_PATH + 1];
    size_t len[MT_WALK_DEPTH];
    unsigned level = 0;
    int rc;

    k_memset(w, 0, sizeof(*w));
    path[0] = '/';
    path[1] = '\0';
    len[0] = 1;
    rc = ops->opendir(ops->ctx, 0, path);
    if (rc != 0) {
        fail(ops, w, path, MT_WE_OPENDIR, rc);
        return MT_WALK_OK;
    }
    w->dirs = 1;
    for (;;) {
        mt_walk_entry_t e;
        size_t n;

        if (ops->abort(ops->ctx)) {
            for (;;) {
                (void)ops->closedir(ops->ctx, level);
                if (level == 0) {
                    return MT_WALK_ABORT;
                }
                level--;
            }
        }
        k_memset(&e, 0, sizeof(e));
        path[len[level]] = '\0';
        rc = ops->readdir(ops->ctx, level, &e);
        if (rc != 1) {
            if (rc != 0) {
                fail(ops, w, path, MT_WE_READDIR, rc < 0 ? rc : -1);
            }
            (void)ops->closedir(ops->ctx, level);
            if (level == 0) {
                return MT_WALK_OK;
            }
            level--;
            continue;
        }
        if (e.name == NULL || k_strcmp(e.name, ".") == 0 || k_strcmp(e.name, "..") == 0) {
            continue;
        }
        n = join(path, len[level], e.name);
        if (n == 0) {
            /* The path names the folder that holds the entry. */
            fail(ops, w, path, MT_WE_LONG, 0);
            continue;
        }
        if (e.is_dir) {
            if (level + 1 >= MT_WALK_DEPTH) {
                fail(ops, w, path, MT_WE_DEEP, 0);
                continue;
            }
            rc = ops->opendir(ops->ctx, level + 1, path);
            if (rc != 0) {
                fail(ops, w, path, MT_WE_OPENDIR, rc);
                continue;
            }
            w->dirs++;
            level++;
            len[level] = n;
            continue;
        }
        w->files++;
        rc = ops->file(ops->ctx, path, e.size);
        if (rc != 0) {
            fail(ops, w, path, MT_WE_FILE, rc);
        } else {
            w->bytes += e.size;
        }
    }
}

const char *mt_walk_what(int what)
{
    switch (what) {
    case MT_WE_OPENDIR: return "opendir";
    case MT_WE_READDIR: return "readdir";
    case MT_WE_FILE:    return "file";
    case MT_WE_DEEP:    return "deep";
    case MT_WE_LONG:    return "long";
    default:            return "unknown";
    }
}

int mt_walk_clean(const mt_walk_t *w)
{
    return w != NULL && w->errors == 0;
}

void mt_card_keys(char *out, size_t size, const mt_walk_t *w, const mt_space_t *sp)
{
    char a[DG_U64_SIZE];
    char b[DG_U64_SIZE];
    int n;

    n = k_snprintf(out, size, "dirs=%u files=%u bytes=%s errors=%u", w->dirs, w->files,
                   dg_u64(a, w->bytes), w->errors);
    if (n < 0 || (size_t)n >= size) {
        return;
    }
    if (sp->rc == 0) {
        k_snprintf(out + n, size - (size_t)n, " free_bytes=%s total_bytes=%s fat=%u",
                   dg_u64(a, sp->free_bytes), dg_u64(b, sp->total_bytes), sp->fat);
    } else {
        k_snprintf(out + n, size - (size_t)n, " statvfs_rc=%d", sp->rc);
    }
}

void mt_card_counts(char *out, size_t size, const mt_walk_t *w)
{
    k_snprintf(out, size, "%u folders, %u files, %u errors.", w->dirs, w->files, w->errors);
}

void mt_card_free(char *out, size_t size, const mt_space_t *sp)
{
    char a[DG_U64_SIZE];
    char b[DG_U64_SIZE];

    if (sp->rc != 0) {
        k_snprintf(out, size, "No free space count: error %d.", sp->rc);
        return;
    }
    k_snprintf(out, size, "Free %s KiB of %s KiB.", dg_u64(a, sp->free_bytes >> 10),
               dg_u64(b, sp->total_bytes >> 10));
}

void mt_card_error_keys(char *out, size_t size, const char *path, int what, int rc)
{
    /* A short file has no fs code: rc 0. */
    k_snprintf(out, size, "what=%s rc=%d", mt_walk_what(what),
               what == MT_WE_FILE && rc == MT_WALK_SHORT ? 0 : rc);
    dg_keys_text(out, size, "path", path);
}

void mt_card_first_keys(char *out, size_t size, const mt_walk_t *w)
{
    k_snprintf(out, size, "first_what=%s", mt_walk_what(w->first_what));
}
