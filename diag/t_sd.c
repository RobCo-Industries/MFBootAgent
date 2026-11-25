/*
 * The SD test. Read only: the card is reached through the write guard
 * of dg_sd.h, whose write function counts the attempt and refuses it,
 * so nothing on the card can change. It reads the
 * card identity, the blocks of the boot volume by the plan of dg_sd.h,
 * then checks every file of /ROBCO by the rule of dg_sd.h.
 */

#include <stdint.h>

#include <robco/blk.h>
#include <robco/fs.h>
#include <robco/klibc.h>
#include <robco/part.h>
#include <robco/rimg.h>
#include <robco/sd.h>
#include <robco/timer.h>

#include "dg.h"
#include "dg_sd.h"
#include "dg_sdimg.h"

#define DIR_PATH "/ROBCO"
static blk_dev_t *card;
static dg_sd_guard_t guard;
static uint64_t read_total;     /* blocks the last read_blocks() read */
static fs_boot_t vol;
static uint8_t buf[DG_SD_CHUNK * BLK_SECTOR_SIZE] __attribute__((aligned(64)));

static const char *const markers[] = { "/ROBCO/BOOT.CFG", "/ROBCO/UOS.SYS", "/ROBCO/DIAG.SYS" };

/* A printable copy of a short ID string: anything else becomes '_'. */
static void clean(char *out, size_t size, const char *in, size_t n)
{
    size_t i;

    for (i = 0; i + 1 < size && i < n && in[i] != '\0'; i++) {
        char c = in[i];

        out[i] = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ?
                 c : '_';
    }
    out[i] = '\0';
    if (i == 0 && size > 1) {
        out[0] = '-';
        out[1] = '\0';
    }
}

/* The blocks of the volume. Returns 1 when ESC stopped it. */
static int guard_read(void *ctx, uint64_t lba, uint32_t count)
{
    (void)ctx;
    return blk_read(&guard.dev, lba, count, buf);
}

static int read_blocks(dg_run_t *r, dg_sd_reads_t *rd)
{
    dg_sd_plan_t plan;
    dg_rate_t rate;
    uint64_t done = 0;
    uint64_t lba;
    uint32_t count;
    uint32_t i;
    uint32_t t0 = timer_now_ms();
    uint32_t ms;
    char line[DG_LINE_MAX];

    dg_sd_reads_init(rd);
    dg_sd_plan(&plan, vol.start_lba, vol.block_count);
    dg_rate_init(&rate, DG_PROGRESS_MS);
    for (i = 0; i < plan.chunks; i++) {
        (void)dg_sd_chunk(&plan, i, &lba, &count);
        dg_sd_read_chunk(rd, guard_read, 0, lba, count);
        done += count;
        if (dg_poll_abort()) {
            read_total = done;
            return 1;
        }
        dg_progress(&rate, 2, "sd", "blocks", done, dg_sd_plan_blocks(&plan));
    }
    ms = timer_now_ms() - t0;
    read_total = done;
    dg_keys(r, "vol_lba=%u vol_blocks=%u read=%u sampled=%d chunk_errors=%u errors=%u",
            (unsigned)vol.start_lba, (unsigned)vol.block_count, (unsigned)done, plan.sampled,
            (unsigned)rd->chunk_errors, (unsigned)rd->errors);
    if (rd->errors != 0) {
        dg_keys(r, "first_bad=%u", (unsigned)rd->first_bad);
    }
    dg_keys(r, "ms=%u kib_s=%u", (unsigned)ms,
            (unsigned)(ms != 0 ? done * BLK_SECTOR_SIZE / 1024u * 1000u / ms : 0));
    k_snprintf(line, sizeof(line), "Read %u of %u blocks%s, %u errors, %u failed chunks, %u ms",
               (unsigned)done, (unsigned)vol.block_count, plan.sampled ? " (sample)" : "",
               (unsigned)rd->errors, (unsigned)rd->chunk_errors, (unsigned)ms);
    dg_work_line(2, line, 1);
    return 0;
}

/* The scan of /ROBCO on the mounted card: the fs side of dg_sd_scan(). */
typedef struct {
    dg_run_t *r;
    fs_dir_t *d;
    fs_stat_t st;
    unsigned row;       /* next work row of the file list */
    char name[DG_SDIMG_NAME + 1]; /* header name of the last good image */
    unsigned subtype;   /* and its subtype */
} scan_ctx_t;

static int op_opendir(void *ctx)
{
    scan_ctx_t *c = ctx;

    return fs_opendir(c->d, DIR_PATH);
}

static int op_readdir(void *ctx, dg_sd_entry_t *e)
{
    scan_ctx_t *c = ctx;
    int rc = fs_readdir(c->d, &c->st);

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

    return fs_closedir(c->d);
}

/* One image file: dg_sd_check_image(), shared with the maintenance image. */
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
    char line[DG_LINE_MAX];

    if (out != DG_SDF_OTHER) {
        dg_sd_image_keys(line, sizeof(line), DIR_PATH, e, out);
        dg_mark("sd_image %s", line);
    }
    dg_sd_row(line, sizeof(line), e, out, c->name, c->subtype);
    dg_work_line(c->row++, line, 1);
}

static int op_abort(void *ctx)
{
    (void)ctx;
    return dg_poll_abort();
}

/* Returns DG_SD_SCAN_*. */
static int check_files(dg_run_t *r, dg_sd_scan_t *s)
{
    static fs_dir_t d;
    scan_ctx_t c;
    const dg_sd_ops_t ops = { &c, op_opendir, op_readdir, op_closedir, op_check, op_report,
                              op_abort };
    int rc;

    k_memset(&c, 0, sizeof(c));
    c.r = r;
    c.d = &d;
    c.row = 4;
    rc = dg_sd_scan(&ops, FS_ERR_NOENT, s);
    if (rc == DG_SD_SCAN_NODIR) {
        dg_keys(r, "files=0 dir=missing");
        dg_work_line(4, "No " DIR_PATH " folder", 1);
        return rc;
    }
    dg_keys(r, "files=%u images=%u bad_images=%u others=%u", s->files, s->images, s->bad,
            s->others);
    if (s->bad != 0) {
        char keys[DG_KEYS_MAX];

        dg_sd_bad_keys(keys, sizeof(keys), DIR_PATH, s);
        dg_keys(r, "%s", keys);
    }
    if (rc == DG_SD_SCAN_DIRERR) {
        dg_keys(r, "dir_rc=%d", s->dir_rc);
    }
    return rc;
}

void dg_test_sd(dg_run_t *r)
{
    const sd_info_t *info;
    char line[DG_LINE_MAX];
    char oid[4];
    char pnm[8];
    dg_sd_scan_t sc;
    dg_sd_reads_t rd;
    int scan = DG_SD_SCAN_OK;
    int rc;
#ifdef DG_TEST
    /* Test builds: sdwrite arms one probe; this run consumes it. */
    int probe = dg.sd_probe_write;
    int fail_multi = dg.sd_fail_multi;

    dg.sd_probe_write = 0;
    dg.sd_fail_multi = 0;
#endif

    rc = sd_init();
    guard.writes = 0;
    if (rc != 0) {
        r->result = rc == SD_ERR_NOCARD ? DG_ABSENT : DG_FAIL;
        dg_keys(r, "why=init rc=%d", rc);
        dg_text(r, rc == SD_ERR_NOCARD ? DG_TX_SD_NOCARD : DG_TX_SD_NOSTART);
        return;
    }
    card = sd_blkdev();
    dg_sd_guard_init(&guard, card);
    dg_sd_reads_init(&rd);
#ifdef DG_TEST
    /* Test builds: sdmulti makes every multi-block read of this run fail. */
    guard.fail_multi = fail_multi;
    /* Test builds: prove the counter is reachable through blk_write(). */
    if (probe) {
        dg_keys(r, "probe_rc=%d", blk_write(&guard.dev, 0, 1, buf));
    }
#endif
    info = sd_info();
    clean(oid, sizeof(oid), info->oid, sizeof(info->oid));
    clean(pnm, sizeof(pnm), info->pnm, sizeof(info->pnm));
    dg_keys(r, "card=%s mid=0x%02x oid=%s pnm=%s psn=0x%08x blocks=%u",
            info->type == SD_TYPE_SDHC ? "sdhc" : "sdsc", info->mid, oid, pnm,
            (unsigned)info->psn, (unsigned)info->capacity_blocks);
    k_snprintf(line, sizeof(line), "Card %s %s, %u MiB, serial 0x%08x",
               info->type == SD_TYPE_SDHC ? "SDHC" : "SDSC", pnm,
               (unsigned)(info->capacity_blocks >> 11), (unsigned)info->psn);
    dg_work_line(0, line, 1);
    if (dg_sd_cid_blank(info->cid)) {
        r->result = DG_FAIL;
        dg_keys(r, "why=cid cid0=0x%08x", (unsigned)info->cid[0]);
        dg_text(r, DG_TX_SD_CID);
        return;
    }
    rc = fs_mount_boot("sd", &guard.dev, markers, sizeof(markers) / sizeof(markers[0]), &vol);
    if (rc != PART_BOOT_OK) {
        r->result = DG_FAIL;
        dg_keys(r, "why=mount rc=%d writes=%u", rc, (unsigned)guard.writes);
        dg_text(r, DG_TX_SD_NOVOL);
        return;
    }
    dg_work_line(3, "File          Size     Image", 1);
    if (read_blocks(r, &rd) || (scan = check_files(r, &sc)) == DG_SD_SCAN_ABORT) {
        (void)fs_unmount("sd");
        r->result = DG_SKIPPED;
        dg_keys(r, "why=abort writes=%u", (unsigned)guard.writes);
        dg_text(r, DG_TX_STOPPED);
        return;
    }
    (void)fs_unmount("sd");
    dg_keys(r, "writes=%u", (unsigned)guard.writes);
    r->result = dg_sd_reads_ok(&rd) && sc.bad == 0 && guard.writes == 0 && scan == DG_SD_SCAN_OK ?
                DG_OK : DG_FAIL;
    if (rd.errors != 0) {
        dg_text(r, DG_TX_SD_BLOCKS, (unsigned)rd.errors, (unsigned)rd.chunk_errors);
    } else if (rd.chunk_errors != 0) {
        dg_text(r, DG_TX_SD_CHUNKS, (unsigned)rd.chunk_errors);
    } else if (scan == DG_SD_SCAN_NODIR) {
        dg_text(r, DG_TX_SD_NODIR);
    } else if (scan == DG_SD_SCAN_DIRERR) {
        dg_text(r, DG_TX_SD_DIRERR, sc.dir_rc);
    } else if (sc.bad != 0) {
        char name[DG_SD_NAME_COLS + 1];

        dg_text(r, DG_TX_SD_BAD, dg_sname(name, sizeof(name), sc.bad_name, DG_SD_NAME_COLS),
                dg_sd_outcome_name(sc.first_bad));
    } else if (guard.writes != 0) {
        dg_text(r, DG_TX_SD_WRITES, (unsigned)guard.writes);
    } else {
        dg_text(r, DG_TX_SD_OK, (unsigned)read_total, sc.images);
    }
}
