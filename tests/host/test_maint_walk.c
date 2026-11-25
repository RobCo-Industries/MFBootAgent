/* MAINT.SYS: the walk of check card over a fake tree. */

#include <stdio.h>
#include <string.h>

#include "mt_walk.h"
#include "rc_test.h"

/* A fake tree: each node has a parent index, -1 for the root's children. */
typedef struct {
    const char *name;
    int parent;
    int is_dir;
    uint32_t size;
    int file_rc;        /* what file() returns */
    int open_rc;        /* what opendir() returns for this folder */
    int read_rc_at;     /* readdir fails with -1 after this many entries, 0: never */
} node_t;

typedef struct {
    const node_t *nodes;
    int count;
    int dir[MT_WALK_DEPTH];     /* the open folder at each level, -1 for the root */
    int pos[MT_WALK_DEPTH];
    int open[MT_WALK_DEPTH];
    int opens;
    int closes;
    int root_rc;                /* what opendir() returns for the root */
    int abort_after;            /* abort() returns 1 from this call on, 0: never */
    int abort_calls;
    char errors[16][300];
    int nerr;
    char files[32][300];
    int nfiles;
} fake_t;

static int find(const fake_t *f, const char *path)
{
    char buf[300];
    int i;

    for (i = 0; i < f->count; i++) {
        int chain[MT_WALK_DEPTH + 2];
        int n = 0;
        int p;

        for (p = i; p >= 0 && n < (int)MT_WALK_DEPTH + 2; p = f->nodes[p].parent) {
            chain[n++] = p;
        }
        buf[0] = '\0';
        while (n > 0) {
            n--;
            if (strlen(buf) + 1 + strlen(f->nodes[chain[n]].name) < sizeof(buf)) {
                strcat(buf, "/");
                strcat(buf, f->nodes[chain[n]].name);
            }
        }
        if (strcmp(buf, path) == 0) {
            return i;
        }
    }
    return -2;
}

static int f_opendir(void *ctx, unsigned level, const char *path)
{
    fake_t *f = ctx;
    int d = strcmp(path, "/") == 0 ? -1 : find(f, path);

    if (d == -1 && f->root_rc != 0) {
        return f->root_rc;
    }
    if (d >= 0 && f->nodes[d].open_rc != 0) {
        return f->nodes[d].open_rc;
    }
    CHECK(!f->open[level]);
    f->dir[level] = d;
    f->pos[level] = 0;
    f->open[level] = 1;
    f->opens++;
    return 0;
}

static int f_readdir(void *ctx, unsigned level, mt_walk_entry_t *e)
{
    fake_t *f = ctx;
    int seen = 0;
    int i;

    CHECK(f->open[level]);
    if (f->dir[level] >= 0 && f->nodes[f->dir[level]].read_rc_at != 0 &&
        f->pos[level] >= f->nodes[f->dir[level]].read_rc_at) {
        return -1;
    }
    for (i = 0; i < f->count; i++) {
        if (f->nodes[i].parent == f->dir[level]) {
            if (seen == f->pos[level]) {
                f->pos[level]++;
                e->name = f->nodes[i].name;
                e->size = f->nodes[i].size;
                e->is_dir = f->nodes[i].is_dir;
                return 1;
            }
            seen++;
        }
    }
    return 0;
}

static int f_closedir(void *ctx, unsigned level)
{
    fake_t *f = ctx;

    CHECK(f->open[level]);
    f->open[level] = 0;
    f->closes++;
    return 0;
}

static int f_file(void *ctx, const char *path, uint32_t size)
{
    fake_t *f = ctx;
    int i = find(f, path);

    CHECK(i >= 0);
    CHECK_EQ(size, f->nodes[i].size);
    snprintf(f->files[f->nfiles++], 300, "%s", path);
    return f->nodes[i].file_rc;
}

static void f_error(void *ctx, const char *path, int what, int rc)
{
    fake_t *f = ctx;

    snprintf(f->errors[f->nerr++], 300, "%s %s %d", path, mt_walk_what(what), rc);
}

static int f_abort(void *ctx)
{
    fake_t *f = ctx;

    f->abort_calls++;
    return f->abort_after != 0 && f->abort_calls >= f->abort_after;
}

static int walk(fake_t *f, mt_walk_t *w)
{
    const mt_walk_ops_t ops = { f, f_opendir, f_readdir, f_closedir, f_file, f_error, f_abort };

    return mt_walk(&ops, w);
}

static const node_t tree[] = {
    { "ROBCO", -1, 1, 0, 0, 0, 0 },          /* 0 */
    { "BOOT.CFG", 0, 0, 100, 0, 0, 0 },
    { "UOS.SYS", 0, 0, 5000, 0, 0, 0 },
    { "UOS", -1, 1, 0, 0, 0, 0 },            /* 3 */
    { "HOLO", 3, 1, 0, 0, 0, 0 },            /* 4 */
    { "A.HTP", 4, 0, 7, 0, 0, 0 },
    { ".", 4, 1, 0, 0, 0, 0 },
    { "..", 4, 1, 0, 0, 0, 0 },
    { "README.TXT", -1, 0, 3, 0, 0, 0 },
    { "EMPTY", -1, 1, 0, 0, 0, 0 },
};

TEST(a_good_tree)
{
    fake_t f;
    mt_walk_t w;

    memset(&f, 0, sizeof(f));
    f.nodes = tree;
    f.count = (int)(sizeof(tree) / sizeof(tree[0]));
    CHECK_EQ(walk(&f, &w), MT_WALK_OK);
    CHECK_EQ(w.dirs, 5u);
    CHECK_EQ(w.files, 4u);
    CHECK_EQ(w.bytes, 5110u);
    CHECK_EQ(w.errors, 0u);
    CHECK(mt_walk_clean(&w));
    CHECK_EQ(f.opens, f.closes);
    CHECK_STR(f.files[0], "/ROBCO/BOOT.CFG");
    CHECK_STR(f.files[2], "/UOS/HOLO/A.HTP");
    CHECK_STR(f.files[3], "/README.TXT");
}

TEST(failures_carry_their_path_and_the_walk_goes_on)
{
    node_t t[sizeof(tree) / sizeof(tree[0])];
    fake_t f;
    mt_walk_t w;

    memcpy(t, tree, sizeof(t));
    t[2].file_rc = MT_WALK_SHORT;
    t[4].open_rc = -6;
    t[8].file_rc = -1;
    memset(&f, 0, sizeof(f));
    f.nodes = t;
    f.count = (int)(sizeof(t) / sizeof(t[0]));
    CHECK_EQ(walk(&f, &w), MT_WALK_OK);
    CHECK_EQ(w.errors, 3u);
    CHECK(!mt_walk_clean(&w));
    CHECK_STR(f.errors[0], "/ROBCO/UOS.SYS file -100");
    CHECK_STR(f.errors[1], "/UOS/HOLO opendir -6");
    CHECK_STR(f.errors[2], "/README.TXT file -1");
    CHECK_STR(w.first_path, "/ROBCO/UOS.SYS");
    CHECK_EQ(w.first_what, MT_WE_FILE);
    CHECK_EQ(w.first_rc, MT_WALK_SHORT);
    /* A failed file is checked but its bytes do not count. */
    CHECK_EQ(w.files, 3u);
    CHECK_EQ(w.bytes, 100u + 0u);
    CHECK_EQ(f.opens, f.closes);
}

TEST(a_readdir_error_ends_that_folder_only)
{
    node_t t[sizeof(tree) / sizeof(tree[0])];
    fake_t f;
    mt_walk_t w;

    memcpy(t, tree, sizeof(t));
    t[0].read_rc_at = 1;
    memset(&f, 0, sizeof(f));
    f.nodes = t;
    f.count = (int)(sizeof(t) / sizeof(t[0]));
    CHECK_EQ(walk(&f, &w), MT_WALK_OK);
    CHECK_EQ(w.errors, 1u);
    CHECK_STR(f.errors[0], "/ROBCO readdir -1");
    CHECK_EQ(w.files, 3u);
    CHECK_EQ(f.opens, f.closes);
}

TEST(depth_and_length_limits)
{
    static const char *const names[] = { "A", "B", "C", "D", "E", "F", "G", "H", "I" };
    node_t t[10];
    fake_t f;
    mt_walk_t w;
    char longname[300];
    int i;

    /* /A/B/C/D/E/F/G is the deepest folder entered; /A/B/C/D/E/F/G/H is too deep. */
    for (i = 0; i < 8; i++) {
        t[i] = (node_t){ names[i], i - 1, 1, 0, 0, 0, 0 };
    }
    memset(longname, 'L', 254);
    longname[254] = '\0';
    t[8] = (node_t){ longname, 0, 0, 1, 0, 0, 0 };
    t[9] = (node_t){ "OK.TXT", 6, 0, 2, 0, 0, 0 };
    memset(&f, 0, sizeof(f));
    f.nodes = t;
    f.count = 10;
    CHECK_EQ(walk(&f, &w), MT_WALK_OK);
    CHECK_EQ(w.dirs, 8u);
    CHECK_EQ(w.errors, 2u);
    CHECK_STR(f.errors[0], "/A/B/C/D/E/F/G/H deep 0");
    /* The long name is not checked; its error names the folder that holds it. */
    CHECK_EQ(w.files, 1u);
    CHECK_EQ(f.opens, f.closes);
    CHECK_EQ(f.nfiles, 1);
    CHECK_STR(f.errors[1], "/A long 0");
}

TEST(abort_closes_every_folder)
{
    fake_t f;
    mt_walk_t w;

    memset(&f, 0, sizeof(f));
    f.nodes = tree;
    f.count = (int)(sizeof(tree) / sizeof(tree[0]));
    f.abort_after = 7;
    CHECK_EQ(walk(&f, &w), MT_WALK_ABORT);
    CHECK_EQ(f.opens, f.closes);
    CHECK(f.nfiles < 4);
}

TEST(a_root_that_does_not_open)
{
    fake_t f;
    mt_walk_t w;

    memset(&f, 0, sizeof(f));
    f.nodes = tree;
    f.count = (int)(sizeof(tree) / sizeof(tree[0]));
    f.root_rc = -1;
    CHECK_EQ(walk(&f, &w), MT_WALK_OK);
    CHECK_EQ(w.errors, 1u);
    CHECK_EQ(w.dirs, 0u);
    CHECK_STR(f.errors[0], "/ opendir -1");
    CHECK_EQ(f.closes, 0);
}

/* Sizes past 32 bits come out whole: just under and over 4 GiB, and 2 TiB. */
TEST(card_sizes_whole)
{
    const uint64_t g4 = (uint64_t)1 << 32;
    const uint64_t t2 = (uint64_t)2 << 40;
    static const struct {
        uint64_t bytes, free_bytes, total_bytes;
        const char *keys;
        const char *free_line;
    } rows[] = {
        { 4294967295u, 4294967295u, 4294967295u,
          "dirs=3 files=7 bytes=4294967295 errors=0 free_bytes=4294967295 "
          "total_bytes=4294967295 fat=32", "Free 4194303 KiB of 4194303 KiB." },
        { 4294967297u, 4294967296u, 8589934592u,
          "dirs=3 files=7 bytes=4294967297 errors=0 free_bytes=4294967296 "
          "total_bytes=8589934592 fat=32", "Free 4194304 KiB of 8388608 KiB." },
        { 0, 0, 0, NULL, NULL },
    };
    char out[400];
    mt_walk_t w;
    mt_space_t sp;
    unsigned i;

    memset(&w, 0, sizeof(w));
    w.dirs = 3;
    w.files = 7;
    for (i = 0; rows[i].keys != NULL; i++) {
        w.bytes = rows[i].bytes;
        sp.rc = 0;
        sp.free_bytes = rows[i].free_bytes;
        sp.total_bytes = rows[i].total_bytes;
        sp.fat = 32;
        mt_card_keys(out, sizeof(out), &w, &sp);
        CHECK_STR(out, rows[i].keys);
        mt_card_free(out, sizeof(out), &sp);
        CHECK_STR(out, rows[i].free_line);
    }
    w.bytes = t2 - 1;
    sp.free_bytes = t2 - g4;
    sp.total_bytes = t2;
    mt_card_keys(out, sizeof(out), &w, &sp);
    CHECK_STR(out, "dirs=3 files=7 bytes=2199023255551 errors=0 free_bytes=2194728288256 "
                   "total_bytes=2199023255552 fat=32");
    mt_card_free(out, sizeof(out), &sp);
    CHECK_STR(out, "Free 2143289344 KiB of 2147483648 KiB.");
    /* The free line fits a work row even for the largest volume. */
    sp.free_bytes = UINT64_MAX;
    sp.total_bytes = UINT64_MAX;
    mt_card_free(out, sizeof(out), &sp);
    CHECK(strlen(out) < 79);
    sp.rc = -1;
    mt_card_keys(out, sizeof(out), &w, &sp);
    CHECK_STR(out, "dirs=3 files=7 bytes=2199023255551 errors=0 statvfs_rc=-1");
    mt_card_free(out, sizeof(out), &sp);
    CHECK_STR(out, "No free space count: error -1.");
    /* The counts row fits a work row with the largest counts. */
    w.dirs = w.files = w.errors = 4294967295u;
    mt_card_counts(out, sizeof(out), &w);
    CHECK(strlen(out) < 79);
}
