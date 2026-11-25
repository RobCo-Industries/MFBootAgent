/* The SD test's read plan. Pure: see dg_sd.h. */

#include <stddef.h>

#include <robco/klibc.h>
#include <robco/rimg.h>

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

    (void)lba;
    (void)count;
    (void)in;
    g->writes++;
    return BLK_ERR_RO;
}

void dg_sd_guard_init(dg_sd_guard_t *g, blk_dev_t *card)
{
    k_memset(g, 0, sizeof(*g));
    g->card = card;
    g->dev = *card;
    g->dev.flags &= ~BLK_FLAG_READONLY;
    g->dev.read = guard_read;
    g->dev.write = guard_write;
    g->dev.sync = 0;
    g->dev.priv = g;
}
