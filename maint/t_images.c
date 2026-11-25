/*
 * Check images. Read only, behind the write guard. Every file of /ROBCO
 * with an image name, .SYS, .BAK or .IMG, is checked with the loader's
 * own steps by dg_sd_check_image(), the check DIAG.SYS's SD test uses.
 * One result per file.
 */

#include <stdint.h>

#include <robco/fs.h>
#include <robco/klibc.h>

#include "mt.h"
#include "../diag/dg_sd.h"
#include "../diag/dg_sdimg.h"

#define DIR_PATH "/ROBCO"

typedef struct {
    fs_dir_t d;
    fs_stat_t st;
    unsigned row;       /* next work row of the file list */
    char name[DG_SDIMG_NAME + 1];
    unsigned subtype;
    int stop;
} scan_ctx_t;

static scan_ctx_t sc;

static int op_opendir(void *ctx)
{
    scan_ctx_t *c = ctx;

    return fs_opendir(&c->d, DIR_PATH);
}

static int op_readdir(void *ctx, dg_sd_entry_t *e)
{
    scan_ctx_t *c = ctx;
    int rc = fs_readdir(&c->d, &c->st);

    if (rc == 1) {
        e->name = c->st.name;
        e->size = c->st.size;
        e->is_dir = c->st.is_dir;
    }
    return rc;
}

static int op_closedir(void *ctx)
{
    scan_ctx_t *c = ctx;

    return fs_closedir(&c->d);
}

static int op_check(void *ctx, dg_sd_entry_t *e)
{
    char path[FS_PATH_MAX + 1];
    scan_ctx_t *c = ctx;

    k_snprintf(path, sizeof(path), DIR_PATH "/%s", e->name);
    return dg_sd_check_image(path, e->size, c->name, &c->subtype, &e->fs_rc);
}

static void op_report(void *ctx, const dg_sd_entry_t *e, int out)
{
    scan_ctx_t *c = ctx;
    char line[MT_LINE_MAX];

    if (out == DG_SDF_OTHER) {
        return;
    }
    dg_sd_image_keys(line, sizeof(line), DIR_PATH, e, out);
    mt_mark("image %s", line);
    dg_sd_row(line, sizeof(line), e, out, c->name, c->subtype);
    mt_work_line(c->row++, line, 1);
}

static int op_abort(void *ctx)
{
    scan_ctx_t *c = ctx;

    if (!c->stop && mt_poll_abort()) {
        c->stop = 1;
    }
    return c->stop;
}

void mt_tool_images(mt_run_t *r)
{
    const dg_sd_ops_t ops = { &sc, op_opendir, op_readdir, op_closedir, op_check, op_report,
                              op_abort };
    static dg_sd_scan_t s;
    char keys[MT_KEYS_MAX];
    char name[DG_SD_NAME_COLS + 1];
    int rc;

    if (mt_need_card(r) != 0) {
        return;
    }
    k_memset(&sc, 0, sizeof(sc));
    sc.row = 1;
    mt_work_line(0, "File          Size     Image", 1);
    rc = dg_sd_scan(&ops, FS_ERR_NOENT, &s);
    if (rc == DG_SD_SCAN_NODIR) {
        r->result = MT_FAIL;
        mt_keys(r, "images=0 dir=missing");
        mt_text(r, "No " DIR_PATH " folder.");
        mt_work_line(1, "No " DIR_PATH " folder", 1);
        return;
    }
    mt_keys(r, "images=%u bad=%u", s.images, s.bad);
    if (s.bad != 0) {
        dg_sd_bad_keys(keys, sizeof(keys), DIR_PATH, &s);
        mt_keys(r, "%s", keys);
    }
    if (rc == DG_SD_SCAN_ABORT) {
        r->result = MT_FAIL;
        mt_keys(r, "why=stopped");
        mt_text(r, "Stopped.");
    } else if (rc == DG_SD_SCAN_DIRERR) {
        r->result = MT_FAIL;
        mt_keys(r, "dir_rc=%d", s.dir_rc);
        mt_text(r, "Folder read error %d.", s.dir_rc);
    } else if (s.bad != 0) {
        r->result = MT_FAIL;
        mt_text(r, "%s: %s", dg_sname(name, sizeof(name), s.bad_name, DG_SD_NAME_COLS),
                dg_sd_outcome_name(s.first_bad));
    } else if (s.images == 0) {
        r->result = MT_ABSENT;
        mt_text(r, "No image files.");
    } else {
        r->result = MT_OK;
        mt_text(r, "%u images good.", s.images);
    }
}
