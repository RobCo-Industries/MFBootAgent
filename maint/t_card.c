/*
 * Check card. Read only, behind the write guard. Walks every folder of
 * the boot volume by the rule of mt_walk.h and reads every file whole, in
 * chunks. Between chunks it runs the input queue, so the screen, the keys
 * and the maintenance port keep working on a full card. Reports each
 * failure with its path, then the free space.
 */

#include <stdint.h>

#include <robco/fs.h>
#include <robco/klibc.h>
#include <robco/timer.h>

#include "mt.h"
#include "mt_walk.h"

#define CHUNK (32u * 1024u)
/* Work rows: 0 progress, 1 the summary, from 2 the failures. */
#define ROW_ERR0 2u

typedef struct {
    mt_run_t *r;
    fs_dir_t dirs[MT_WALK_DEPTH];
    fs_stat_t st;
    dg_rate_t rate;
    uint64_t done;      /* bytes read so far */
    uint64_t used;      /* bytes in use on the volume, for the progress */
    unsigned row;       /* next work row for a failure */
    int stop;           /* ESC or Ctrl-C came */
} walk_ctx_t;

static walk_ctx_t wc;
static uint8_t buf[CHUNK] __attribute__((aligned(64)));

static int op_opendir(void *ctx, unsigned level, const char *path)
{
    walk_ctx_t *c = ctx;

    return fs_opendir(&c->dirs[level], path);
}

static int op_readdir(void *ctx, unsigned level, mt_walk_entry_t *e)
{
    walk_ctx_t *c = ctx;
    int rc = fs_readdir(&c->dirs[level], &c->st);

    if (rc == 1) {
        e->name = c->st.name;
        e->size = c->st.size;
        e->is_dir = c->st.is_dir;
    }
    return rc;
}

static int op_closedir(void *ctx, unsigned level)
{
    walk_ctx_t *c = ctx;

    return fs_closedir(&c->dirs[level]);
}

static int op_file(void *ctx, const char *path, uint32_t size)
{
    static fs_file_t f;
    walk_ctx_t *c = ctx;
    uint32_t got = 0;
    int rc;

    rc = fs_open(&f, path, FS_READ);
    if (rc != 0) {
        return rc;
    }
    for (;;) {
        int n = fs_read(&f, buf, CHUNK);

        if (n < 0) {
            rc = n;
            break;
        }
        if (n == 0) {
            rc = got == size ? 0 : MT_WALK_SHORT;
            break;
        }
        got += (uint32_t)n;
        c->done += (uint64_t)n;
        if (got > size) {
            rc = MT_WALK_SHORT;
            break;
        }
        /* One step: the keys and the port run between chunks. */
        if (mt_poll_abort()) {
            c->stop = 1;
            break;
        }
        mt_progress(&c->rate, 0, "check_card", c->done, c->used);
    }
    (void)fs_close(&f);
    return c->stop ? 0 : rc;
}

/* A path for a marker value: no spaces. */
static void marker_path(char *out, size_t size, const char *path)
{
    size_t i;

    for (i = 0; i + 1 < size && path[i] != '\0'; i++) {
        out[i] = path[i] == ' ' ? '_' : path[i];
    }
    out[i] = '\0';
}

static const char *rc_name(int what, int rc)
{
    if (what == MT_WE_FILE && rc == MT_WALK_SHORT) {
        return "short";
    }
    if (what == MT_WE_DEEP || what == MT_WE_LONG) {
        return "skipped";
    }
    return fs_strerror(rc);
}

static void op_error(void *ctx, const char *path, int what, int rc)
{
    walk_ctx_t *c = ctx;
    char mpath[MT_WALK_PATH + 1];
    char line[MT_LINE_MAX];

    marker_path(mpath, sizeof(mpath), path);
    mt_mark("card_error path=%s what=%s rc=%d", mpath, mt_walk_what(what),
            what == MT_WE_FILE && rc == MT_WALK_SHORT ? 0 : rc);
    k_snprintf(line, sizeof(line), "ERROR %s: %s %s", path, mt_walk_what(what),
               rc_name(what, rc));
    mt_work_line(c->row++, line, 1);
}

static int op_abort(void *ctx)
{
    walk_ctx_t *c = ctx;

    if (!c->stop && mt_poll_abort()) {
        c->stop = 1;
    }
    return c->stop;
}

void mt_tool_card(mt_run_t *r)
{
    const mt_walk_ops_t ops = { &wc, op_opendir, op_readdir, op_closedir, op_file, op_error,
                                op_abort };
    static mt_walk_t w;
    fs_statvfs_t vfs;
    char line[MT_LINE_MAX];
    uint32_t t0 = timer_now_ms();
    int vrc;

    if (mt_need_card(r) != 0) {
        return;
    }
    k_memset(&wc, 0, sizeof(wc));
    wc.r = r;
    wc.row = ROW_ERR0;
    dg_rate_init(&wc.rate, MT_PROGRESS_MS);
    k_memset(&vfs, 0, sizeof(vfs));
    vrc = fs_statvfs(MT_VOL, &vfs);
    wc.used = vrc == 0 ? vfs.total_bytes - vfs.free_bytes : 0;
    mt_work_line(0, "Reading every file of the card.", 0);
    (void)mt_walk(&ops, &w);
    mt_keys(r, "dirs=%u files=%u bytes=%u errors=%u", w.dirs, w.files, (unsigned)w.bytes,
            w.errors);
    if (vrc == 0) {
        mt_keys(r, "free_bytes=%u total_bytes=%u fat=%u", (unsigned)vfs.free_bytes,
                (unsigned)vfs.total_bytes, (unsigned)vfs.fat_type);
    } else {
        mt_keys(r, "statvfs_rc=%d", vrc);
    }
    if (w.errors != 0) {
        char mpath[MT_WALK_PATH + 1];

        marker_path(mpath, sizeof(mpath), w.first_path);
        mt_keys(r, "first_path=%s first_what=%s", mpath, mt_walk_what(w.first_what));
    }
    mt_keys(r, "ms=%u", (unsigned)(timer_now_ms() - t0));
    k_snprintf(line, sizeof(line), "%u folders, %u files, %u errors. Free %u KiB of %u KiB.",
               w.dirs, w.files, w.errors, (unsigned)(vfs.free_bytes >> 10),
               (unsigned)(vfs.total_bytes >> 10));
    mt_work_line(1, line, 1);
    if (wc.stop) {
        r->result = MT_FAIL;
        mt_keys(r, "why=stopped");
        mt_text(r, "Stopped.");
    } else if (w.errors != 0) {
        r->result = MT_FAIL;
        mt_text(r, "Errors: %u.", w.errors);
    } else if (vrc != 0) {
        r->result = MT_FAIL;
        mt_text(r, "No free space count.");
    } else {
        r->result = MT_OK;
        mt_text(r, "%u files, %u KiB free.", w.files, (unsigned)(vfs.free_bytes >> 10));
    }
}
