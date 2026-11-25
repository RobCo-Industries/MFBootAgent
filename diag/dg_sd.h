/*
 * The SD test's read plan: which blocks of the boot volume it reads.
 * Pure: the host tests link it.
 *
 * The rule: a volume of DG_SD_FULL_MAX blocks or fewer is read whole, in
 * chunks of DG_SD_CHUNK blocks. A larger volume is read as DG_SD_SAMPLES
 * chunks spread evenly from its first block to its last, both included.
 * So a read never covers more than DG_SD_FULL_MAX blocks, 32 MiB.
 */
#ifndef DG_SD_H
#define DG_SD_H

#include <stddef.h>
#include <stdint.h>

#include <robco/blk.h>

#define DG_SD_CHUNK     64u         /* blocks per read, 32 KiB */
#define DG_SD_FULL_MAX  65536u      /* blocks: 32 MiB */
#define DG_SD_SAMPLES   1024u       /* chunks when the volume is larger */

typedef struct {
    uint64_t start;     /* first block of the volume on the card */
    uint64_t blocks;    /* blocks in the volume */
    uint32_t chunks;    /* reads to make */
    int sampled;        /* 1 when the plan reads a sample, 0 when it reads all */
} dg_sd_plan_t;

/* Builds the plan for a volume. A volume of 0 blocks gives 0 chunks. */
void dg_sd_plan(dg_sd_plan_t *p, uint64_t start, uint64_t blocks);

/*
 * Read number i of the plan, 0 to chunks - 1: its first block on the card
 * in *lba and its block count in *count. Returns 0, or -1 when i is out of
 * range. Every read lies inside the volume.
 */
int dg_sd_chunk(const dg_sd_plan_t *p, uint32_t i, uint64_t *lba, uint32_t *count);

/* Blocks the whole plan reads. */
uint64_t dg_sd_plan_blocks(const dg_sd_plan_t *p);

/*
 * The block read of one chunk. read() reads count blocks at lba and
 * returns 0 or a negative error. A failed chunk read counts in
 * chunk_errors, then each block of the chunk is read alone, and each
 * block that fails again counts in errors. A failed chunk read is a fault
 * even when every block read alone: some cards and controllers fail only
 * on multi-block reads, and the boot loader reads that way.
 */
typedef int (*dg_sd_read_fn)(void *ctx, uint64_t lba, uint32_t count);

typedef struct {
    uint32_t chunk_errors;  /* chunk reads that failed */
    uint32_t errors;        /* blocks that failed when read alone */
    uint64_t first_bad;     /* first block that failed alone, if errors != 0 */
} dg_sd_reads_t;

/* Sets every count to 0. */
void dg_sd_reads_init(dg_sd_reads_t *s);

/* Reads one chunk by the rule above and counts the result in s. */
void dg_sd_read_chunk(dg_sd_reads_t *s, dg_sd_read_fn read, void *ctx, uint64_t lba,
                      uint32_t count);

/* 1 when the reads saw no fault: no failed chunk read and no bad block. */
int dg_sd_reads_ok(const dg_sd_reads_t *s);

/*
 * The file check of /ROBCO. The rule that names images: an entry whose
 * extension is .SYS, .BAK or .IMG, in any case, must be an image. Its
 * outcome is DG_SDF_OK, an rimg_err_t code of rimg_validate(), or one of
 * the codes below. Any other file is DG_SDF_OTHER: listed, never read, and
 * never a failure. Any other directory is ignored.
 */
#define DG_SDF_OK       0       /* RIMG_OK */
#define DG_SDF_EMPTY    16      /* an image file of 0 bytes */
#define DG_SDF_SHORT    17      /* 1 to RIMG_META_OFFSET - 1 bytes: no whole header */
#define DG_SDF_ISDIR    18      /* a directory with an image name */
#define DG_SDF_OPEN     19      /* the file does not open */
#define DG_SDF_OTHER    20      /* not an image name */

/* 1 when name ends in .SYS, .BAK or .IMG, in any case. */
int dg_sd_image_name(const char *name);

/*
 * The outcome of the first bytes of an image file of `size` bytes. `n` is
 * what the read of its first RIMG_META_OFFSET bytes returned: the count,
 * or a negative error. Gives DG_SDF_EMPTY, DG_SDF_SHORT, RIMG_E_IO for a
 * failed or short read, the code of rimg_check_header(), or DG_SDF_OK
 * when the whole file must now be read and validated.
 */
int dg_sd_head(uint32_t size, const void *head, int n);

/* "ok", "empty", "short", "isdir", "open", "other", or the rimg_strerror() name. */
const char *dg_sd_outcome_name(int outcome);

/* One entry of the directory. */
typedef struct {
    const char *name;
    uint32_t size;
    int is_dir;
    int fs_rc;          /* check sets it with DG_SDF_OPEN: the fs_open() code */
} dg_sd_entry_t;

/*
 * The scan of /ROBCO, with the file system behind function pointers so
 * the host tests can fake it. opendir and closedir return 0 or a negative
 * fs error. readdir returns 1 with an entry, 0 at the end, or a negative
 * error, as fs_readdir() does. check runs only for an image file and
 * returns its outcome. report gets every entry the scan lists, with its
 * outcome. abort returns 1 to stop.
 */
typedef struct {
    void *ctx;
    int (*opendir)(void *ctx);
    int (*readdir)(void *ctx, dg_sd_entry_t *e);
    int (*closedir)(void *ctx);
    int (*check)(void *ctx, dg_sd_entry_t *e);
    void (*report)(void *ctx, const dg_sd_entry_t *e, int outcome);
    int (*abort)(void *ctx);
} dg_sd_ops_t;

/* Results of dg_sd_scan(). */
#define DG_SD_SCAN_OK       0   /* the whole directory was read */
#define DG_SD_SCAN_NODIR    1   /* opendir gave noent_rc: no /ROBCO */
#define DG_SD_SCAN_DIRERR   2   /* opendir or readdir failed: dir_rc */
#define DG_SD_SCAN_ABORT    3   /* abort asked to stop */

/* A whole long name, FS_NAME_MAX bytes, and its end. */
#define DG_SD_NAME_MAX 256u

typedef struct {
    unsigned files;     /* regular files */
    unsigned images;    /* entries with an image name, directories included */
    unsigned bad;       /* images whose outcome is not DG_SDF_OK */
    unsigned others;    /* files with another name */
    int dir_rc;         /* the failed opendir or readdir result, else 0 */
    int first_bad;      /* outcome of the first bad image */
    int first_fs_rc;    /* its fs code with DG_SDF_OPEN, else 0 */
    char bad_name[DG_SD_NAME_MAX]; /* name of the first bad image, "" if none */
} dg_sd_scan_t;

/*
 * Reads the directory to its end, checks each image and reports every
 * listed entry. Any readdir error ends the scan with DG_SD_SCAN_DIRERR.
 * The directory is closed on every path after a good opendir. noent_rc
 * is the opendir code for a missing directory. Returns DG_SD_SCAN_*.
 */
int dg_sd_scan(const dg_sd_ops_t *ops, int noent_rc, dg_sd_scan_t *s);

/*
 * The marker keys of one image of the scan: "path=<dir>/<name> size=<n>
 * rc=<outcome>", then "fs_rc=<code>" for DG_SDF_OPEN. The path and the outcome go through dg_mval(). Both
 * images print them: MAINT.SYS as `image`, DIAG.SYS as `sd_image`.
 */
void dg_sd_image_keys(char *out, size_t size, const char *dir, const dg_sd_entry_t *e,
                      int outcome);

/*
 * The keys of the first bad image: "bad_path=<dir>/<name> bad_rc=<outcome>",
 * encoded, then "bad_fs_rc=<code>" for DG_SDF_OPEN.
 */
void dg_sd_bad_keys(char *out, size_t size, const char *dir, const dg_sd_scan_t *s);

/* The file column of a screen row: names longer than this end in "...". */
#define DG_SD_NAME_COLS 12u

/*
 * One screen row of the scan, the same in both images: the file name in
 * DG_SD_NAME_COLS columns, its size, then "OK" with the header name and
 * subtype, "FAILED" with the outcome and, for "open", the fs code, or
 * "-" for another file. Names
 * from the card go through dg_sname(), so the row is one row of at most
 * 79 columns. hdr_name is the header name of a good image, else unused.
 */
void dg_sd_row(char *out, size_t size, const dg_sd_entry_t *e, int outcome,
               const char *hdr_name, unsigned subtype);

/*
 * The write guard in front of the card. blk_write() refuses a device
 * flagged BLK_FLAG_READONLY before it calls the write function, so such
 * a device can never count an attempt. The guard device carries no
 * read-only flag: blk_write() reaches its write function, which counts
 * the attempt in `writes` and returns BLK_ERR_RO. It never calls the
 * card's write function. Reads go to the card.
 *
 * With `pass` set, writes and syncs go to the card and nothing counts.
 * The maintenance image sets it only while a tool that writes runs. The
 * diagnostics image never sets it.
 */
typedef struct {
    blk_dev_t dev;      /* mount this one */
    blk_dev_t *card;    /* the real device, written only with pass set */
    uint32_t writes;    /* write attempts refused */
    int pass;           /* 1: writes reach the card */
    int fail_multi;     /* test hook: reads of more than one block fail with BLK_ERR_IO */
} dg_sd_guard_t;

/* Builds the guard over card with writes, pass and fail_multi at 0. */
void dg_sd_guard_init(dg_sd_guard_t *g, blk_dev_t *card);

/*
 * The card identity: the raw CID as sd_info() keeps it, bits 127:8, all
 * 0 or all 1. A card that answered CMD2 sends its own ID; a blank one
 * means the bus read no data. Returns 1 for a blank CID.
 */
int dg_sd_cid_blank(const uint32_t cid[4]);

#endif
