/*
 * The SD test. Read only: the card is reached through a copy of its
 * block device that is flagged read-only and whose write function only
 * counts the attempt, so nothing on the card can change. It reads the
 * card identity, the blocks of the boot volume by the plan of dg_sd.h,
 * then checks every file of /ROBCO that carries an image header.
 */

#include <stdint.h>

#include <robco/alloc.h>
#include <robco/blk.h>
#include <robco/fs.h>
#include <robco/klibc.h>
#include <robco/part.h>
#include <robco/rimg.h>
#include <robco/sd.h>
#include <robco/timer.h>

#include "dg.h"
#include "dg_sd.h"

#define DIR_PATH "/ROBCO"
/* Largest image file the check reads: the OS slot is 12 MiB. */
#define FILE_MAX (16u << 20)

static blk_dev_t *card;
static blk_dev_t ro;
static unsigned writes;
static fs_boot_t vol;
static uint8_t buf[DG_SD_CHUNK * BLK_SECTOR_SIZE] __attribute__((aligned(64)));

static const char *const markers[] = { "/ROBCO/BOOT.CFG", "/ROBCO/UOS.SYS", "/ROBCO/DIAG.SYS" };

static int ro_read(blk_dev_t *dev, uint64_t lba, uint32_t count, void *out)
{
    (void)dev;
    return card->read(card, lba, count, out);
}

static int ro_write(blk_dev_t *dev, uint64_t lba, uint32_t count, const void *in)
{
    (void)dev;
    (void)lba;
    (void)count;
    (void)in;
    writes++;
    return BLK_ERR_RO;
}

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
static int read_blocks(dg_run_t *r, uint32_t *errors)
{
    dg_sd_plan_t plan;
    dg_rate_t rate;
    uint64_t first_bad = 0;
    uint64_t done = 0;
    uint64_t lba;
    uint32_t count;
    uint32_t i;
    uint32_t t0 = timer_now_ms();
    uint32_t ms;
    char line[DG_LINE_MAX];

    *errors = 0;
    dg_sd_plan(&plan, vol.start_lba, vol.block_count);
    dg_rate_init(&rate, DG_PROGRESS_MS);
    for (i = 0; i < plan.chunks; i++) {
        (void)dg_sd_chunk(&plan, i, &lba, &count);
        if (blk_read(&ro, lba, count, buf) != 0) {
            uint32_t k;

            /* Find the bad blocks of the chunk one by one. */
            for (k = 0; k < count; k++) {
                if (blk_read(&ro, lba + k, 1, buf) != 0) {
                    if (*errors == 0) {
                        first_bad = lba + k;
                    }
                    (*errors)++;
                }
            }
        }
        done += count;
        if (dg_poll_abort()) {
            return 1;
        }
        dg_progress(&rate, 2, "sd", "blocks", done, dg_sd_plan_blocks(&plan));
    }
    ms = timer_now_ms() - t0;
    dg_keys(r, "vol_lba=%u vol_blocks=%u read=%u sampled=%d errors=%u", (unsigned)vol.start_lba,
            (unsigned)vol.block_count, (unsigned)done, plan.sampled, (unsigned)*errors);
    if (*errors != 0) {
        dg_keys(r, "first_bad=%u", (unsigned)first_bad);
    }
    dg_keys(r, "ms=%u kib_s=%u", (unsigned)ms,
            (unsigned)(ms != 0 ? done * BLK_SECTOR_SIZE / 1024u * 1000u / ms : 0));
    k_snprintf(line, sizeof(line), "Read %u of %u blocks%s, %u errors, %u ms", (unsigned)done,
               (unsigned)vol.block_count, plan.sampled ? " (sample)" : "", (unsigned)*errors,
               (unsigned)ms);
    dg_work_line(2, line, 1);
    return 0;
}

/*
 * One file: 1 when it carries an image header. *bad gets 1 when that
 * image fails its checks. The whole file goes to RAM above the OS slot,
 * then through rimg_validate(), the loader's own steps.
 */
static int check_file(const fs_stat_t *st, unsigned row, int *bad, char *bad_path, size_t bp_size)
{
    static fs_file_t f;
    char path[FS_PATH_MAX + 1];
    char line[DG_LINE_MAX];
    const rimg_header_t *h = 0;
    uint8_t head[RIMG_META_OFFSET];
    void *base = 0;
    size_t room = robco_heap_high(&base);
    int n;
    int rc;

    *bad = 0;
    k_snprintf(path, sizeof(path), DIR_PATH "/%s", st->name);
    if (fs_open(&f, path, FS_READ) != 0) {
        return 0;
    }
    n = fs_read(&f, head, sizeof(head));
    if (n < (int)sizeof(head) || rimg_check_header(head, (size_t)n, 0) == RIMG_E_MAGIC) {
        (void)fs_close(&f);
        return 0;
    }
    if (room > FILE_MAX) {
        room = FILE_MAX;
    }
    if (st->size > room) {
        rc = RIMG_E_SIZE;
    } else if (fs_seek(&f, 0, FS_SEEK_SET) != 0 || fs_read(&f, base, st->size) != (int)st->size) {
        rc = RIMG_E_IO;
    } else {
        rc = rimg_validate(base, st->size, 0, &h);
    }
    (void)fs_close(&f);
    dg_mark("sd_image path=%s size=%u rc=%s", path, (unsigned)st->size, rimg_strerror(rc));
    if (rc == RIMG_OK) {
        char name[sizeof(h->name) + 1];

        k_memcpy(name, h->name, sizeof(h->name));
        name[sizeof(h->name)] = '\0';
        k_snprintf(line, sizeof(line), "%-12s %8u  OK      %s subtype %u", st->name,
                   (unsigned)st->size, name, (unsigned)h->subtype);
    } else {
        *bad = 1;
        if (bad_path[0] == '\0') {
            k_strncpy(bad_path, path, bp_size);
            bad_path[bp_size - 1] = '\0';
        }
        k_snprintf(line, sizeof(line), "%-12s %8u  FAILED  %s", st->name, (unsigned)st->size,
                   rimg_strerror(rc));
    }
    dg_work_line(row, line, 1);
    return 1;
}

static int check_files(dg_run_t *r, unsigned *bad_images)
{
    static fs_dir_t d;
    fs_stat_t st;
    char bad_path[FS_PATH_MAX + 1];
    unsigned files = 0;
    unsigned images = 0;
    int rc;

    *bad_images = 0;
    bad_path[0] = '\0';
    rc = fs_opendir(&d, DIR_PATH);
    if (rc != 0) {
        dg_keys(r, "files=0 dir_rc=%d", rc);
        return -1;
    }
    while (fs_readdir(&d, &st) == 1) {
        int bad;

        if (st.is_dir) {
            continue;
        }
        files++;
        if (check_file(&st, 4 + images, &bad, bad_path, sizeof(bad_path))) {
            images++;
            *bad_images += (unsigned)bad;
        }
        if (dg_poll_abort()) {
            (void)fs_closedir(&d);
            return 1;
        }
    }
    (void)fs_closedir(&d);
    dg_keys(r, "files=%u images=%u bad_images=%u", files, images, *bad_images);
    if (bad_path[0] != '\0') {
        dg_keys(r, "bad_path=%s", bad_path);
    }
    return 0;
}

void dg_test_sd(dg_run_t *r)
{
    const sd_info_t *info;
    char line[DG_LINE_MAX];
    char oid[4];
    char pnm[8];
    uint32_t errors = 0;
    unsigned bad = 0;
    int files = 0;
    int rc = sd_init();

    writes = 0;
    if (rc != 0) {
        r->result = rc == SD_ERR_NOCARD ? DG_ABSENT : DG_FAIL;
        dg_keys(r, "why=init rc=%d", rc);
        dg_text(r, rc == SD_ERR_NOCARD ? "No card." : "The card does not start.");
        return;
    }
    card = sd_blkdev();
    ro = *card;
    ro.flags |= BLK_FLAG_READONLY;
    ro.read = ro_read;
    ro.write = ro_write;
    ro.sync = 0;
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
    rc = fs_mount_boot("sd", &ro, markers, sizeof(markers) / sizeof(markers[0]), &vol);
    if (rc != PART_BOOT_OK) {
        r->result = DG_FAIL;
        dg_keys(r, "why=mount rc=%d writes=%u", rc, writes);
        dg_text(r, "No boot volume.");
        return;
    }
    dg_work_line(3, "File          Size     Image", 1);
    if (read_blocks(r, &errors) || (files = check_files(r, &bad)) == 1) {
        (void)fs_unmount("sd");
        r->result = DG_SKIPPED;
        dg_keys(r, "why=abort writes=%u", writes);
        dg_text(r, "Stopped.");
        return;
    }
    (void)fs_unmount("sd");
    dg_keys(r, "writes=%u", writes);
    r->result = errors == 0 && bad == 0 && writes == 0 && files == 0 ? DG_OK : DG_FAIL;
    dg_text(r, "%u read errors, %u bad images", (unsigned)errors, bad);
}
