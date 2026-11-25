/* The SD test's read plan. Pure: see dg_sd.h. */

#include <stddef.h>

#include <robco/klibc.h>
#include <robco/rimg.h>

#include "dg_fmt.h"
#include "dg_sd.h"

void dg_sd_plan(dg_sd_plan_t *p, uint64_t start, uint64_t blocks)
{
    if (p == NULL) {
        return;
    }
    p->start = start;
    p->blocks = blocks;
    p->sampled = blocks > DG_SD_FULL_MAX;
    if (p->sampled) {
        p->chunks = DG_SD_SAMPLES;
    } else {
        p->chunks = (uint32_t)((blocks + DG_SD_CHUNK - 1u) / DG_SD_CHUNK);
    }
}

int dg_sd_chunk(const dg_sd_plan_t *p, uint32_t i, uint64_t *lba, uint32_t *count)
{
    uint64_t off;
    uint64_t left;

    if (p == NULL || lba == NULL || count == NULL || i >= p->chunks) {
        return -1;
    }
    if (p->sampled) {
        /* Spread from block 0 to the last chunk that fits: the first and last are read. */
        uint64_t span = p->blocks - DG_SD_CHUNK;

        off = span * i / (DG_SD_SAMPLES - 1u);
    } else {
        off = (uint64_t)i * DG_SD_CHUNK;
    }
    left = p->blocks - off;
    *lba = p->start + off;
    *count = left < DG_SD_CHUNK ? (uint32_t)left : DG_SD_CHUNK;
    return 0;
}

uint64_t dg_sd_plan_blocks(const dg_sd_plan_t *p)
{
    if (p == NULL) {
        return 0;
    }
    return p->sampled ? (uint64_t)p->chunks * DG_SD_CHUNK : p->blocks;
}

void dg_sd_reads_init(dg_sd_reads_t *s)
{
    if (s != NULL) {
        k_memset(s, 0, sizeof(*s));
    }
}

void dg_sd_read_chunk(dg_sd_reads_t *s, dg_sd_read_fn read, void *ctx, uint64_t lba,
                      uint32_t count)
{
    uint32_t k;

    if (s == NULL || read == NULL || read(ctx, lba, count) == 0) {
        return;
    }
    s->chunk_errors++;
    /* Find the bad blocks of the chunk one by one. */
    for (k = 0; k < count; k++) {
        if (read(ctx, lba + k, 1) != 0) {
            if (s->errors == 0) {
                s->first_bad = lba + k;
            }
            s->errors++;
        }
    }
}

int dg_sd_reads_ok(const dg_sd_reads_t *s)
{
    return s != NULL && s->chunk_errors == 0 && s->errors == 0;
}

static char upper(char c)
{
    return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
}

int dg_sd_image_name(const char *name)
{
    static const char *const exts[] = { ".SYS", ".BAK", ".IMG" };
    size_t n;
    size_t i;
    size_t k;

    if (name == NULL) {
        return 0;
    }
    n = k_strlen(name);
    if (n < 5) {
        /* A name needs at least one character before its extension. */
        return 0;
    }
    for (i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
        for (k = 0; k < 4 && upper(name[n - 4 + k]) == exts[i][k]; k++) {
        }
        if (k == 4) {
            return 1;
        }
    }
    return 0;
}

int dg_sd_head(uint32_t size, const void *head, int n)
{
    int rc;

    if (size == 0) {
        return DG_SDF_EMPTY;
    }
    if (size < RIMG_META_OFFSET) {
        return DG_SDF_SHORT;
    }
    if (head == NULL || n < (int)RIMG_META_OFFSET) {
        return RIMG_E_IO;
    }
    rc = rimg_check_header(head, size, NULL);
    return rc;
}

const char *dg_sd_outcome_name(int outcome)
{
    switch (outcome) {
    case DG_SDF_EMPTY: return "empty";
    case DG_SDF_SHORT: return "short";
    case DG_SDF_ISDIR: return "isdir";
    case DG_SDF_OPEN:  return "open";
    case DG_SDF_OTHER: return "other";
    default:           return rimg_strerror(outcome);
    }
}

int dg_sd_scan(const dg_sd_ops_t *ops, int noent_rc, dg_sd_scan_t *s)
{
    dg_sd_entry_t e;
    int result = DG_SD_SCAN_OK;
    int rc;

    if (s == NULL) {
        return DG_SD_SCAN_DIRERR;
    }
    k_memset(s, 0, sizeof(*s));
    if (ops == NULL) {
        return DG_SD_SCAN_DIRERR;
    }
    rc = ops->opendir(ops->ctx);
    if (rc != 0) {
        s->dir_rc = rc;
        return rc == noent_rc ? DG_SD_SCAN_NODIR : DG_SD_SCAN_DIRERR;
    }
    for (;;) {
        int image;
        int out;

        k_memset(&e, 0, sizeof(e));
        rc = ops->readdir(ops->ctx, &e);
        if (rc == 0) {
            break;
        }
        if (rc != 1) {
            /* An error is never the end of the directory. */
            s->dir_rc = rc < 0 ? rc : -1;
            result = DG_SD_SCAN_DIRERR;
            break;
        }
        image = dg_sd_image_name(e.name);
        if (e.is_dir && !image) {
            continue;
        }
        if (!e.is_dir) {
            s->files++;
        }
        if (!image) {
            s->others++;
            out = DG_SDF_OTHER;
        } else {
            s->images++;
            out = e.is_dir ? DG_SDF_ISDIR : ops->check(ops->ctx, &e);
            if (out != DG_SDF_OK) {
                if (s->bad == 0) {
                    s->first_bad = out;
                    s->first_fs_rc = out == DG_SDF_OPEN ? e.fs_rc : 0;
                    k_strncpy(s->bad_name, e.name, sizeof(s->bad_name));
                    s->bad_name[sizeof(s->bad_name) - 1] = '\0';
                }
                s->bad++;
            }
        }
        ops->report(ops->ctx, &e, out);
        if (ops->abort(ops->ctx)) {
            result = DG_SD_SCAN_ABORT;
            break;
        }
    }
    (void)ops->closedir(ops->ctx);
    return result;
}

static int guard_read(blk_dev_t *dev, uint64_t lba, uint32_t count, void *out)
{
    dg_sd_guard_t *g = (dg_sd_guard_t *)dev->priv;

    if (count > 1 && g->fail_multi) {
        return BLK_ERR_IO;
    }
    return blk_read(g->card, lba, count, out);
}

static int guard_write(blk_dev_t *dev, uint64_t lba, uint32_t count, const void *in)
{
    dg_sd_guard_t *g = (dg_sd_guard_t *)dev->priv;

    if (g->pass) {
        return blk_write(g->card, lba, count, in);
    }
    g->writes++;
    return BLK_ERR_RO;
}

static int guard_sync(blk_dev_t *dev)
{
    dg_sd_guard_t *g = (dg_sd_guard_t *)dev->priv;

    return g->pass ? blk_sync(g->card) : 0;
}

void dg_sd_guard_init(dg_sd_guard_t *g, blk_dev_t *card)
{
    k_memset(g, 0, sizeof(*g));
    g->card = card;
    g->dev = *card;
    g->dev.flags &= ~BLK_FLAG_READONLY;
    g->dev.read = guard_read;
    g->dev.write = guard_write;
    g->dev.sync = guard_sync;
    g->dev.priv = g;
}

int dg_sd_cid_blank(const uint32_t cid[4])
{
    if (cid[0] == 0 && cid[1] == 0 && cid[2] == 0 && cid[3] == 0) {
        return 1;
    }
    return cid[0] == 0xffffffffu && cid[1] == 0xffffffffu && cid[2] == 0xffffffffu &&
           cid[3] == 0xffffff00u;
}

const char *dg_sd_path(char out[DG_TEXT_RAW_SIZE], const char *dir, const char *name)
{
    k_snprintf(out, DG_TEXT_RAW_SIZE, "%s/%s", dir, name != NULL ? name : "");
    return out;
}

void dg_sd_image_keys(char *out, size_t size, const char *dir, const dg_sd_entry_t *e,
                      int outcome)
{
    char rc[DG_SD_RC_SIZE];
    char path[DG_TEXT_RAW_SIZE];

    if (size == 0) {
        return;
    }
    k_snprintf(out, size, "rc=%s size=%u", dg_mval(rc, sizeof(rc), dg_sd_outcome_name(outcome)),
               (unsigned)e->size);
    if (outcome == DG_SDF_OPEN) {
        size_t n = k_strlen(out);

        k_snprintf(out + n, size - n, " fs_rc=%d", e->fs_rc);
    }
    dg_keys_text(out, size, "path", dg_sd_path(path, dir, e->name));
}

void dg_sd_bad_keys(char *out, size_t size, const dg_sd_scan_t *s)
{
    char rc[DG_SD_RC_SIZE];
    int n = k_snprintf(out, size, "bad_rc=%s",
                       dg_mval(rc, sizeof(rc), dg_sd_outcome_name(s->first_bad)));

    if (s->first_bad == DG_SDF_OPEN && n >= 0 && (size_t)n < size) {
        k_snprintf(out + n, size - (size_t)n, " bad_fs_rc=%d", s->first_fs_rc);
    }
}

void dg_sd_row(char *out, size_t size, const dg_sd_entry_t *e, int outcome,
               const char *hdr_name, unsigned subtype)
{
    char name[DG_SD_NAME_COLS + 1];
    char hdr[DG_SD_NAME_COLS + 8];

    (void)dg_sname(name, sizeof(name), e->name, DG_SD_NAME_COLS);
    if (outcome == DG_SDF_OTHER) {
        k_snprintf(out, size, "%-12s %8u  -       not an image", name, (unsigned)e->size);
    } else if (outcome == DG_SDF_OK) {
        k_snprintf(out, size, "%-12s %8u  OK      %s subtype %u", name, (unsigned)e->size,
                   dg_sname(hdr, sizeof(hdr), hdr_name, 16), subtype);
    } else if (outcome == DG_SDF_OPEN) {
        k_snprintf(out, size, "%-12s %8u  FAILED  open, error %d", name, (unsigned)e->size,
                   e->fs_rc);
    } else {
        k_snprintf(out, size, "%-12s %8u  FAILED  %s", name, (unsigned)e->size,
                   dg_sd_outcome_name(outcome));
    }
}
