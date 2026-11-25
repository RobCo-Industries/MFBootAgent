/* The SD test: the read plan and its bounds, the file rule and the scan of /ROBCO. */

#include <stdio.h>
#include <string.h>

#include <robco/fs.h>
#include <robco/rimg.h>

#include "dg_sd.h"
#include "rc_test.h"

/* Every read of the plan lies inside the volume and moves forward. */
static void check_inside(const dg_sd_plan_t *p)
{
    uint64_t prev_end = p->start;
    uint64_t lba = 0;
    uint32_t count = 0;
    uint32_t i;

    for (i = 0; i < p->chunks; i++) {
        CHECK_EQ(dg_sd_chunk(p, i, &lba, &count), 0);
        CHECK(count >= 1 && count <= DG_SD_CHUNK);
        CHECK(lba >= prev_end);
        CHECK(lba + count <= p->start + p->blocks);
        prev_end = lba + count;
    }
}

TEST(small_volumes_are_read_whole)
{
    dg_sd_plan_t p;
    uint64_t lba = 0;
    uint32_t count = 0;

    dg_sd_plan(&p, 2048, 0);
    CHECK_EQ(p.chunks, 0);
    CHECK_EQ(dg_sd_plan_blocks(&p), 0);
    CHECK_EQ(dg_sd_chunk(&p, 0, &lba, &count), -1);
    dg_sd_plan(&p, 2048, 1);
    CHECK_EQ(p.chunks, 1);
    CHECK_EQ(p.sampled, 0);
    CHECK_EQ(dg_sd_chunk(&p, 0, &lba, &count), 0);
    CHECK_EQ(lba, 2048);
    CHECK_EQ(count, 1);
    dg_sd_plan(&p, 2048, DG_SD_CHUNK);
    CHECK_EQ(p.chunks, 1);
    dg_sd_plan(&p, 2048, DG_SD_CHUNK + 1);
    CHECK_EQ(p.chunks, 2);
    CHECK_EQ(dg_sd_chunk(&p, 1, &lba, &count), 0);
    CHECK_EQ(lba, 2048 + DG_SD_CHUNK);
    CHECK_EQ(count, 1);
    CHECK_EQ(dg_sd_plan_blocks(&p), DG_SD_CHUNK + 1);
    /* The QEMU test card: 30720 blocks. */
    dg_sd_plan(&p, 2048, 30720);
    CHECK_EQ(p.chunks, 480);
    CHECK_EQ(dg_sd_plan_blocks(&p), 30720);
    check_inside(&p);
    /* The largest whole read. */
    dg_sd_plan(&p, 0, DG_SD_FULL_MAX);
    CHECK_EQ(p.sampled, 0);
    CHECK_EQ(p.chunks, DG_SD_FULL_MAX / DG_SD_CHUNK);
    check_inside(&p);
}

TEST(large_volumes_are_sampled)
{
    dg_sd_plan_t p;
    uint64_t lba = 0;
    uint32_t count = 0;

    dg_sd_plan(&p, 8192, DG_SD_FULL_MAX + 1);
    CHECK_EQ(p.sampled, 1);
    CHECK_EQ(p.chunks, DG_SD_SAMPLES);
    CHECK_EQ(dg_sd_plan_blocks(&p), (uint64_t)DG_SD_SAMPLES * DG_SD_CHUNK);
    CHECK(dg_sd_plan_blocks(&p) <= DG_SD_FULL_MAX);
    check_inside(&p);
    /* A 32 GiB card: the first and the last block are read. */
    dg_sd_plan(&p, 8192, 62521344);
    check_inside(&p);
    CHECK_EQ(dg_sd_chunk(&p, 0, &lba, &count), 0);
    CHECK_EQ(lba, 8192);
    CHECK_EQ(count, DG_SD_CHUNK);
    CHECK_EQ(dg_sd_chunk(&p, DG_SD_SAMPLES - 1, &lba, &count), 0);
    CHECK_EQ(lba + count, 8192 + 62521344ull);
    CHECK_EQ(dg_sd_chunk(&p, DG_SD_SAMPLES, &lba, &count), -1);
    /* Above 2 TiB of blocks: no overflow. */
    dg_sd_plan(&p, 0, 1ull << 40);
    check_inside(&p);
    CHECK_EQ(dg_sd_chunk(&p, DG_SD_SAMPLES - 1, &lba, &count), 0);
    CHECK_EQ(lba + count, 1ull << 40);
}

TEST(bad_arguments)
{
    dg_sd_plan_t p;
    uint64_t lba = 0;
    uint32_t count = 0;

    dg_sd_plan(NULL, 0, 10);
    dg_sd_plan(&p, 0, 10);
    CHECK_EQ(dg_sd_chunk(NULL, 0, &lba, &count), -1);
    CHECK_EQ(dg_sd_chunk(&p, 0, NULL, &count), -1);
    CHECK_EQ(dg_sd_chunk(&p, 0, &lba, NULL), -1);
    CHECK_EQ(dg_sd_plan_blocks(NULL), 0);
}

/* ---- The file rule and the scan of /ROBCO ---------------------------- */

/* A fake card that records every call that reaches it. */
static unsigned card_reads;
static unsigned card_writes;

static int card_read(blk_dev_t *dev, uint64_t lba, uint32_t count, void *out)
{
    (void)dev;
    (void)lba;
    memset(out, 0x5a, count * BLK_SECTOR_SIZE);
    card_reads++;
    return 0;
}

static int card_write(blk_dev_t *dev, uint64_t lba, uint32_t count, const void *in)
{
    (void)dev;
    (void)lba;
    (void)count;
    (void)in;
    card_writes++;
    return 0;
}

TEST(guard_counts_and_refuses_writes)
{
    blk_dev_t card = { "sd0", BLK_SECTOR_SIZE, 1000, BLK_FLAG_REMOVABLE, card_read, card_write,
                       0, 0 };
    dg_sd_guard_t g;
    uint8_t b[BLK_SECTOR_SIZE];

    card_reads = 0;
    card_writes = 0;
    dg_sd_guard_init(&g, &card);
    CHECK_EQ(g.writes, 0);
    CHECK_EQ(g.dev.flags & BLK_FLAG_READONLY, 0);
    CHECK_EQ(g.dev.flags & BLK_FLAG_REMOVABLE, BLK_FLAG_REMOVABLE);
    CHECK_EQ(g.dev.block_count, 1000);
    /* The attempt reaches the counter through blk_write() and is refused. */
    CHECK_EQ(blk_write(&g.dev, 5, 1, b), BLK_ERR_RO);
    CHECK_EQ(blk_write(&g.dev, 6, 2, b), BLK_ERR_RO);
    CHECK_EQ(g.writes, 2);
    CHECK_EQ(card_writes, 0);
    /* Out of range: blk_check() refuses it before the guard sees it. */
    CHECK_EQ(blk_write(&g.dev, 1000, 1, b), BLK_ERR_RANGE);
    CHECK_EQ(g.writes, 2);
    /* Reads reach the card. */
    CHECK_EQ(blk_read(&g.dev, 7, 1, b), 0);
    CHECK_EQ(card_reads, 1);
    CHECK_EQ(b[0], 0x5a);
    CHECK_EQ(blk_sync(&g.dev), 0);
    CHECK_EQ(card_writes, 0);
    /* A card flagged read-only still gives a guard the counter can be reached through. */
    card.flags |= BLK_FLAG_READONLY;
    dg_sd_guard_init(&g, &card);
    CHECK_EQ(blk_write(&g.dev, 5, 1, b), BLK_ERR_RO);
    CHECK_EQ(g.writes, 1);
    CHECK_EQ(card_writes, 0);    /* The fail_multi hook fails reads of more than one block, never a single block. */
    CHECK_EQ(g.fail_multi, 0);
    g.fail_multi = 1;
    card_reads = 0;
    CHECK_EQ(blk_read(&g.dev, 7, 2, b), BLK_ERR_IO);
    CHECK_EQ(card_reads, 0);
    CHECK_EQ(blk_read(&g.dev, 7, 1, b), 0);
    CHECK_EQ(card_reads, 1);
}

TEST(image_names)
{
    CHECK_EQ(dg_sd_image_name("UOS.SYS"), 1);
    CHECK_EQ(dg_sd_image_name("diag.sys"), 1);
    CHECK_EQ(dg_sd_image_name("Uos.Bak"), 1);
    CHECK_EQ(dg_sd_image_name("GAME.IMG"), 1);
    CHECK_EQ(dg_sd_image_name("a.img"), 1);
    CHECK_EQ(dg_sd_image_name("BOOT.CFG"), 0);
    CHECK_EQ(dg_sd_image_name("NOTES.TXT"), 0);
    CHECK_EQ(dg_sd_image_name("SYS"), 0);
    CHECK_EQ(dg_sd_image_name(".SYS"), 0);
    CHECK_EQ(dg_sd_image_name("UOS.SYS.TXT"), 0);
    CHECK_EQ(dg_sd_image_name("UOSSYS"), 0);
    CHECK_EQ(dg_sd_image_name(""), 0);
    CHECK_EQ(dg_sd_image_name(NULL), 0);
}

/* The first 0x60 bytes of a good UOS image of 0x1000 bytes. */
static uint8_t head[RIMG_META_OFFSET];

static rimg_header_t *good_head(void)
{
    rimg_header_t *h = (rimg_header_t *)(void *)(head + RIMG_HDR_OFFSET);

    memset(head, 0, sizeof(head));
    memcpy(h->magic, RIMG_MAGIC_BOOT, 8);
    h->hdr_version = RIMG_HDR_VERSION;
    h->hdr_size = RIMG_HDR_SIZE;
    h->load_addr = 0x00400000u;
    h->file_size = 0x1000u;
    h->mem_size = 0x1000u;
    h->abi_version = 2u << 16;
    h->subtype = RIMG_BOOT_UOS;
    return h;
}

TEST(file_outcomes)
{
    rimg_header_t *h;

    good_head();
    CHECK_EQ(dg_sd_head(0x1000u, head, sizeof(head)), DG_SDF_OK);
    CHECK_EQ(dg_sd_head(0, head, 0), DG_SDF_EMPTY);
    CHECK_EQ(dg_sd_head(1, head, 1), DG_SDF_SHORT);
    CHECK_EQ(dg_sd_head(16, head, 16), DG_SDF_SHORT);
    CHECK_EQ(dg_sd_head(RIMG_META_OFFSET - 1u, head, RIMG_META_OFFSET - 1), DG_SDF_SHORT);
    /* A read error or a short read of a long file. */
    CHECK_EQ(dg_sd_head(0x1000u, head, FS_ERR_IO), RIMG_E_IO);
    CHECK_EQ(dg_sd_head(0x1000u, head, 16), RIMG_E_IO);
    CHECK_EQ(dg_sd_head(0x1000u, NULL, sizeof(head)), RIMG_E_IO);
    h = good_head();
    h->magic[0] ^= 1;
    CHECK_EQ(dg_sd_head(0x1000u, head, sizeof(head)), RIMG_E_MAGIC);
    h = good_head();
    h->hdr_version = 9;
    CHECK_EQ(dg_sd_head(0x1000u, head, sizeof(head)), RIMG_E_VERSION);
    /* file_size above the size in the directory. */
    good_head();
    CHECK_EQ(dg_sd_head(0x800u, head, sizeof(head)), RIMG_E_SIZE);
    h = good_head();
    h->load_addr = 0x00008000u;
    CHECK_EQ(dg_sd_head(0x1000u, head, sizeof(head)), RIMG_E_BOUNDS);
    h = good_head();
    h->abi_version = 9u << 16;
    CHECK_EQ(dg_sd_head(0x1000u, head, sizeof(head)), RIMG_E_ABI);

    CHECK_STR(dg_sd_outcome_name(DG_SDF_OK), "ok");
    CHECK_STR(dg_sd_outcome_name(DG_SDF_EMPTY), "empty");
    CHECK_STR(dg_sd_outcome_name(DG_SDF_SHORT), "short");
    CHECK_STR(dg_sd_outcome_name(DG_SDF_ISDIR), "isdir");
    CHECK_STR(dg_sd_outcome_name(DG_SDF_OPEN), "open");
    CHECK_STR(dg_sd_outcome_name(DG_SDF_OTHER), "other");
    CHECK_STR(dg_sd_outcome_name(RIMG_E_IO), "io");
    CHECK_STR(dg_sd_outcome_name(RIMG_E_MAGIC), "magic");
    CHECK_STR(dg_sd_outcome_name(RIMG_E_VERSION), "version");
    CHECK_STR(dg_sd_outcome_name(RIMG_E_SIZE), "size");
    CHECK_STR(dg_sd_outcome_name(RIMG_E_BOUNDS), "bounds");
    CHECK_STR(dg_sd_outcome_name(RIMG_E_ABI), "abi");
    CHECK_STR(dg_sd_outcome_name(RIMG_E_CRC), "crc");
}

/* A fake /ROBCO: entries, then the end or an error, and a log of calls. */
typedef struct {
    const char *name;
    uint32_t size;
    int is_dir;
    int outcome;        /* what check gives for it */
} fake_ent_t;

typedef struct {
    const fake_ent_t *ents;
    unsigned n;
    unsigned pos;
    int open_rc;
    int end_rc;         /* readdir after the last entry: 0, or an error */
    int fail_at;        /* readdir at this index gives end_rc instead; -1 never */
    unsigned abort_at;  /* abort is 1 after this many reports; 0 never */
    int opened;
    int closed;
    unsigned checks;
    unsigned reports;
    char log[512];
} fake_t;

static int f_opendir(void *ctx)
{
    fake_t *f = ctx;

    f->opened++;
    return f->open_rc;
}

static int f_readdir(void *ctx, dg_sd_entry_t *e)
{
    fake_t *f = ctx;

    if (f->fail_at >= 0 && f->pos == (unsigned)f->fail_at) {
        return f->end_rc;
    }
    if (f->pos >= f->n) {
        return f->end_rc;
    }
    e->name = f->ents[f->pos].name;
    e->size = f->ents[f->pos].size;
    e->is_dir = f->ents[f->pos].is_dir;
    f->pos++;
    return 1;
}

static int f_closedir(void *ctx)
{
    fake_t *f = ctx;

    f->closed++;
    return 0;
}

static int f_check(void *ctx, dg_sd_entry_t *e)
{
    fake_t *f = ctx;
    unsigned i;

    f->checks++;
    for (i = 0; i < f->n; i++) {
        if (strcmp(f->ents[i].name, e->name) == 0) {
            if (f->ents[i].outcome == DG_SDF_OPEN) {
                e->fs_rc = FS_ERR_CORRUPT;
            }
            return f->ents[i].outcome;
        }
    }
    return -1;
}

static void f_report(void *ctx, const dg_sd_entry_t *e, int out)
{
    fake_t *f = ctx;
    size_t n = strlen(f->log);

    f->reports++;
    snprintf(f->log + n, sizeof(f->log) - n, "%s=%s ", e->name, dg_sd_outcome_name(out));
}

static int f_abort(void *ctx)
{
    fake_t *f = ctx;

    return f->abort_at != 0 && f->reports >= f->abort_at;
}

static int scan(fake_t *f, const fake_ent_t *ents, unsigned n, dg_sd_scan_t *s)
{
    const dg_sd_ops_t ops = { f, f_opendir, f_readdir, f_closedir, f_check, f_report, f_abort };

    f->ents = ents;
    f->n = n;
    return dg_sd_scan(&ops, FS_ERR_NOENT, s);
}

static const fake_ent_t card[] = {
    { "BOOT.CFG", 120, 0, -1 },
    { "UOS.SYS", 4096, 0, DG_SDF_OK },
    { "DIAG.SYS", 16, 0, DG_SDF_SHORT },
    { "SAVES", 0, 1, -1 },
    { "OLD.SYS", 0, 1, -1 },
    { "MAINT.bak", 0, 0, DG_SDF_EMPTY },
    { "NOTES.TXT", 10, 0, -1 },
    { "GAME.IMG", 4096, 0, RIMG_E_CRC },
};
#define CARD_N (sizeof(card) / sizeof(card[0]))

TEST(scan_reads_every_entry_by_the_rule)
{
    fake_t f;
    dg_sd_scan_t s;

    memset(&f, 0, sizeof(f));
    f.fail_at = -1;
    CHECK_EQ(scan(&f, card, CARD_N, &s), DG_SD_SCAN_OK);
    CHECK_STR(f.log, "BOOT.CFG=other UOS.SYS=ok DIAG.SYS=short OLD.SYS=isdir MAINT.bak=empty "
                     "NOTES.TXT=other GAME.IMG=crc ");
    /* Only image files are read; other files and directories never are. */
    CHECK_EQ(f.checks, 4);
    CHECK_EQ(s.files, 6);
    CHECK_EQ(s.images, 5);
    CHECK_EQ(s.bad, 4);
    CHECK_EQ(s.others, 2);
    CHECK_EQ(s.dir_rc, 0);
    CHECK_EQ(s.first_bad, DG_SDF_SHORT);
    CHECK_STR(s.bad_name, "DIAG.SYS");
    CHECK_EQ(f.closed, 1);
}

TEST(scan_good_card_passes_with_other_files)
{
    static const fake_ent_t good[] = {
        { "BOOT.CFG", 120, 0, -1 },
        { "UOS.SYS", 4096, 0, DG_SDF_OK },
        { "README.TXT", 5, 0, -1 },
    };
    fake_t f;
    dg_sd_scan_t s;

    memset(&f, 0, sizeof(f));
    f.fail_at = -1;
    CHECK_EQ(scan(&f, good, 3, &s), DG_SD_SCAN_OK);
    CHECK_EQ(s.bad, 0);
    CHECK_STR(s.bad_name, "");
    CHECK_EQ(s.others, 2);
    CHECK_EQ(f.closed, 1);
}

TEST(scan_directory_errors_fail)
{
    fake_t f;
    dg_sd_scan_t s;

    /* An error after the last entry is not the end of the directory. */
    memset(&f, 0, sizeof(f));
    f.fail_at = -1;
    f.end_rc = FS_ERR_CORRUPT;
    CHECK_EQ(scan(&f, card, 2, &s), DG_SD_SCAN_DIRERR);
    CHECK_EQ(s.dir_rc, FS_ERR_CORRUPT);
    CHECK_EQ(s.images, 1);
    CHECK_EQ(f.closed, 1);
    /* An error first: nothing listed. */
    memset(&f, 0, sizeof(f));
    f.fail_at = 0;
    f.end_rc = FS_ERR_IO;
    CHECK_EQ(scan(&f, card, CARD_N, &s), DG_SD_SCAN_DIRERR);
    CHECK_EQ(s.dir_rc, FS_ERR_IO);
    CHECK_EQ(f.reports, 0);
    CHECK_EQ(f.closed, 1);
    /* The card removed in the middle. */
    memset(&f, 0, sizeof(f));
    f.fail_at = 3;
    f.end_rc = FS_ERR_NOTREADY;
    CHECK_EQ(scan(&f, card, CARD_N, &s), DG_SD_SCAN_DIRERR);
    CHECK_EQ(s.dir_rc, FS_ERR_NOTREADY);
    CHECK_EQ(f.reports, 3);
    CHECK_EQ(f.closed, 1);
    /* A positive value other than 1 is an error too. */
    memset(&f, 0, sizeof(f));
    f.fail_at = 0;
    f.end_rc = 2;
    CHECK_EQ(scan(&f, card, CARD_N, &s), DG_SD_SCAN_DIRERR);
    CHECK(s.dir_rc < 0);
    CHECK_EQ(f.closed, 1);
}

TEST(scan_missing_or_unreadable_directory)
{
    fake_t f;
    dg_sd_scan_t s;

    memset(&f, 0, sizeof(f));
    f.fail_at = -1;
    f.open_rc = FS_ERR_NOENT;
    CHECK_EQ(scan(&f, card, CARD_N, &s), DG_SD_SCAN_NODIR);
    CHECK_EQ(s.dir_rc, FS_ERR_NOENT);
    CHECK_EQ(f.reports, 0);
    /* A failed open leaves nothing to close. */
    CHECK_EQ(f.closed, 0);
    memset(&f, 0, sizeof(f));
    f.fail_at = -1;
    f.open_rc = FS_ERR_IO;
    CHECK_EQ(scan(&f, card, CARD_N, &s), DG_SD_SCAN_DIRERR);
    CHECK_EQ(s.dir_rc, FS_ERR_IO);
    CHECK_EQ(f.closed, 0);
    CHECK_EQ(dg_sd_scan(NULL, FS_ERR_NOENT, &s), DG_SD_SCAN_DIRERR);
}

TEST(scan_abort_closes_the_directory)
{
    fake_t f;
    dg_sd_scan_t s;

    memset(&f, 0, sizeof(f));
    f.fail_at = -1;
    f.abort_at = 2;
    CHECK_EQ(scan(&f, card, CARD_N, &s), DG_SD_SCAN_ABORT);
    CHECK_EQ(f.reports, 2);
    CHECK_EQ(f.closed, 1);
}

/* A fake card for dg_sd_read_chunk(): multi-block reads and some blocks fail. */
typedef struct {
    int fail_multi;     /* reads of more than one block fail */
    uint64_t bad[4];    /* blocks that fail when read alone */
    unsigned n_bad;
    unsigned reads;
} reads_fake_t;

static int fake_read(void *ctx, uint64_t lba, uint32_t count)
{
    reads_fake_t *f = ctx;
    unsigned i;

    f->reads++;
    if (count > 1 && f->fail_multi) {
        return BLK_ERR_IO;
    }
    for (i = 0; i < f->n_bad; i++) {
        if (f->bad[i] >= lba && f->bad[i] < lba + count) {
            return BLK_ERR_IO;
        }
    }
    return 0;
}

TEST(chunk_read_fails_and_every_block_reads_alone)
{
    reads_fake_t f;
    dg_sd_reads_t s;

    memset(&f, 0, sizeof(f));
    f.fail_multi = 1;
    dg_sd_reads_init(&s);
    dg_sd_read_chunk(&s, fake_read, &f, 100, 4);
    dg_sd_read_chunk(&s, fake_read, &f, 104, 4);
    /* Each failed chunk is retried block by block: 2 x (1 + 4) reads. */
    CHECK_EQ(f.reads, 10);
    CHECK_EQ(s.errors, 0);
    CHECK_EQ(s.chunk_errors, 2);
    CHECK_EQ(dg_sd_reads_ok(&s), 0);
}

TEST(chunk_read_fails_and_some_blocks_fail_alone)
{
    reads_fake_t f;
    dg_sd_reads_t s;

    memset(&f, 0, sizeof(f));
    f.bad[0] = 102;
    f.bad[1] = 103;
    f.n_bad = 2;
    dg_sd_reads_init(&s);
    dg_sd_read_chunk(&s, fake_read, &f, 96, 4);
    dg_sd_read_chunk(&s, fake_read, &f, 100, 4);
    CHECK_EQ(s.chunk_errors, 1);
    CHECK_EQ(s.errors, 2);
    CHECK_EQ(s.first_bad, 102);
    CHECK_EQ(dg_sd_reads_ok(&s), 0);
}

TEST(every_chunk_reads_is_ok)
{
    reads_fake_t f;
    dg_sd_reads_t s;

    memset(&f, 0, sizeof(f));
    dg_sd_reads_init(&s);
    dg_sd_read_chunk(&s, fake_read, &f, 0, 64);
    dg_sd_read_chunk(&s, fake_read, &f, 64, 3);
    CHECK_EQ(f.reads, 2);
    CHECK_EQ(s.chunk_errors, 0);
    CHECK_EQ(s.errors, 0);
    CHECK_EQ(dg_sd_reads_ok(&s), 1);
    CHECK_EQ(dg_sd_reads_ok(NULL), 0);
}

TEST(blank_card_identity)
{
    uint32_t cid[4] = { 0x035344u, 0x53433332u, 0x47801234u, 0x56789a00u };

    CHECK_EQ(dg_sd_cid_blank(cid), 0);
    cid[0] = cid[1] = cid[2] = cid[3] = 0;
    CHECK_EQ(dg_sd_cid_blank(cid), 1);
    cid[3] = 0x100u;
    CHECK_EQ(dg_sd_cid_blank(cid), 0);
    cid[0] = cid[1] = cid[2] = 0xffffffffu;
    cid[3] = 0xffffff00u;
    CHECK_EQ(dg_sd_cid_blank(cid), 1);
    cid[3] = 0xfffffe00u;
    CHECK_EQ(dg_sd_cid_blank(cid), 0);
}

/* A file that does not open keeps its outcome and carries the fs code to the marker and row. */
TEST(open_failure_keeps_its_fs_code)
{
    static const fake_ent_t ents[] = {
        { "UOS.SYS", 4096, 0, DG_SDF_OK },
        { "BAD.SYS", 2048, 0, DG_SDF_OPEN },
    };
    char line[400];
    dg_sd_entry_t e;
    fake_t f;
    dg_sd_scan_t s;

    memset(&f, 0, sizeof(f));
    f.fail_at = -1;
    CHECK_EQ(scan(&f, ents, 2, &s), DG_SD_SCAN_OK);
    CHECK_EQ(s.first_bad, DG_SDF_OPEN);
    CHECK_EQ(s.first_fs_rc, FS_ERR_CORRUPT);
    dg_sd_bad_keys(line, sizeof(line), &s);
    CHECK_STR(line, "bad_rc=open bad_fs_rc=-17");
    memset(&e, 0, sizeof(e));
    e.name = "BAD.SYS";
    e.size = 2048;
    e.fs_rc = FS_ERR_IO;
    dg_sd_image_keys(line, sizeof(line), "/ROBCO", &e, DG_SDF_OPEN);
    CHECK_STR(line, "rc=open size=2048 fs_rc=-1 path=/ROBCO/BAD.SYS");
    dg_sd_row(line, sizeof(line), &e, DG_SDF_OPEN, "", 0);
    CHECK_STR(line, "BAD.SYS          2048  FAILED  open, error -1");
    /* Other outcomes carry no fs code. */
    dg_sd_image_keys(line, sizeof(line), "/ROBCO", &e, DG_SDF_SHORT);
    CHECK_STR(line, "rc=short size=2048 path=/ROBCO/BAD.SYS");
}
