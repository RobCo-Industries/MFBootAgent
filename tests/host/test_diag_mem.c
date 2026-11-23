/* The memory test: ranges for every board, the patterns and planted faults. */

#include <string.h>

#include "dg_mem.h"
#include "rc_test.h"

#define IMG_START   0x00400000u
#define IMG_END     0x00438000u
#define HANDOFF     0x00100000u
#define HANDOFF_SZ  0x00020000u

static dg_mem_in_t board(uint32_t mem_size)
{
    dg_mem_in_t in;

    memset(&in, 0, sizeof(in));
    in.image_start = IMG_START;
    in.image_end = IMG_END;
    in.mem_base = 0;
    in.mem_size = mem_size;
    in.handoff_addr = HANDOFF;
    in.handoff_size = HANDOFF_SZ;
    return in;
}

static unsigned ranges(const dg_mem_in_t *in, dg_range_t *r, int *rc)
{
    unsigned n = 99;

    *rc = dg_mem_ranges(in, r, &n);
    return n;
}

TEST(ranges_512_mib_board)
{
    /* Pi Zero and Pi 1 with gpu_mem=64: 448 MiB of ARM RAM, the framebuffer above it. */
    dg_mem_in_t in = board(0x1C000000u);
    dg_range_t r[DG_MEM_RANGES_MAX];
    int rc;

    in.fb_addr = 0x1C100000u;
    in.fb_size = 640u * 480u * 4u;
    CHECK_EQ(ranges(&in, r, &rc), 1);
    CHECK_EQ(rc, DG_MEM_OK);
    CHECK_EQ(r[0].start, IMG_END);
    CHECK_EQ(r[0].end, 0x1C000000u);
    CHECK_EQ(dg_mem_total(r, 1), 0x1C000000u - IMG_END);
}

TEST(ranges_1_gib_boards)
{
    /* QEMU raspi2b and raspi3b: 960 MiB; a real Pi 2: 948 MiB. */
    dg_mem_in_t in = board(0x3C000000u);
    dg_range_t r[DG_MEM_RANGES_MAX];
    int rc;

    CHECK_EQ(ranges(&in, r, &rc), 1);
    CHECK_EQ(r[0].end, 0x3C000000u);
    in.mem_size = 0x3B400000u;
    CHECK_EQ(ranges(&in, r, &rc), 1);
    CHECK_EQ(rc, DG_MEM_OK);
    CHECK_EQ(r[0].start, IMG_END);
    CHECK_EQ(r[0].end, 0x3B400000u);
}

TEST(odd_sizes_and_ends_are_cut_to_pages)
{
    dg_mem_in_t in = board(0x1BFFF123u);
    dg_range_t r[DG_MEM_RANGES_MAX];
    int rc;

    in.image_end = IMG_END + 0x10u;
    CHECK_EQ(ranges(&in, r, &rc), 1);
    CHECK_EQ(r[0].start, IMG_END + DG_MEM_ALIGN);
    CHECK_EQ(r[0].end, 0x1BFFF000u);
    /* A RAM base above 0: the range still starts at the image end. */
    in = board(0x10000000u);
    in.mem_base = 0x00200000u;
    CHECK_EQ(ranges(&in, r, &rc), 1);
    CHECK_EQ(r[0].end, 0x10200000u);
}

TEST(framebuffer_inside_ram_splits_the_range)
{
    dg_mem_in_t in = board(0x20000000u);
    dg_range_t r[DG_MEM_RANGES_MAX];
    int rc;

    in.fb_addr = 0x10000010u;   /* not page aligned: rounded out */
    in.fb_size = 0x0012C000u;
    CHECK_EQ(ranges(&in, r, &rc), 2);
    CHECK_EQ(rc, DG_MEM_OK);
    CHECK_EQ(r[0].start, IMG_END);
    CHECK_EQ(r[0].end, 0x10000000u);
    CHECK_EQ(r[1].start, 0x1012D000u);
    CHECK_EQ(r[1].end, 0x20000000u);
    /* At the top of RAM, across the end: one range, cut short. */
    in.fb_addr = 0x1FF00000u;
    in.fb_size = 0x00200000u;
    CHECK_EQ(ranges(&in, r, &rc), 1);
    CHECK_EQ(r[0].end, 0x1FF00000u);
    /* Right after the image: the range starts after it. */
    in.fb_addr = IMG_END;
    in.fb_size = 0x1000u;
    CHECK_EQ(ranges(&in, r, &rc), 1);
    CHECK_EQ(r[0].start, IMG_END + 0x1000u);
}

TEST(every_exclusion_is_left_out)
{
    dg_mem_in_t in = board(0x20000000u);
    dg_range_t r[DG_MEM_RANGES_MAX];
    unsigned n;
    unsigned i;
    int rc;

    /* A handoff block and a framebuffer both inside the range: three pieces, sorted. */
    in.handoff_addr = 0x18000000u;
    in.fb_addr = 0x08000000u;
    in.fb_size = 0x00100000u;
    n = ranges(&in, r, &rc);
    CHECK_EQ(n, 3);
    for (i = 0; i < n; i++) {
        CHECK(r[i].start < r[i].end);
        CHECK(r[i].start >= IMG_END);
        CHECK(r[i].end <= 0x08000000u || r[i].start >= 0x08100000u);
        CHECK(r[i].end <= 0x18000000u || r[i].start >= 0x18020000u);
        if (i > 0) {
            CHECK(r[i].start > r[i - 1].end);
        }
    }
    CHECK_EQ(r[0].end, 0x08000000u);
    CHECK_EQ(r[1].start, 0x08100000u);
    CHECK_EQ(r[2].start, 0x18020000u);
}

TEST(errors)
{
    dg_mem_in_t in = board(0x20000000u);
    dg_range_t r[DG_MEM_RANGES_MAX];
    unsigned n = 7;
    int rc;

    CHECK_EQ(dg_mem_ranges(NULL, r, &n), DG_MEM_ERR_ARG);
    CHECK_EQ(dg_mem_ranges(&in, NULL, &n), DG_MEM_ERR_ARG);
    CHECK_EQ(dg_mem_ranges(&in, r, NULL), DG_MEM_ERR_ARG);
    in.mem_size = 0;
    CHECK_EQ(ranges(&in, r, &rc), 0);
    CHECK_EQ(rc, DG_MEM_ERR_ARG);
    /* The image outside RAM. */
    in = board(0x00300000u);
    ranges(&in, r, &rc);
    CHECK_EQ(rc, DG_MEM_ERR_ARG);
    in = board(0x20000000u);
    in.mem_base = 0x00500000u;
    ranges(&in, r, &rc);
    CHECK_EQ(rc, DG_MEM_ERR_ARG);
    /* The image fills RAM: nothing left. */
    in = board(IMG_END);
    CHECK_EQ(ranges(&in, r, &rc), 0);
    CHECK_EQ(rc, DG_MEM_ERR_NONE);
    /* The framebuffer covers what is left. */
    in = board(0x01000000u);
    in.fb_addr = IMG_END;
    in.fb_size = 0x01000000u;
    CHECK_EQ(ranges(&in, r, &rc), 0);
    CHECK_EQ(rc, DG_MEM_ERR_NONE);
    /* Top of the 32-bit space: no overflow. */
    in = board(0xFFFFF000u);
    in.fb_addr = 0xFFFFF000u;
    in.fb_size = 0x1000u;
    CHECK_EQ(ranges(&in, r, &rc), 1);
    CHECK_EQ(r[0].end, 0xFFFFF000u);
}

TEST(limit_keeps_the_first_bytes)
{
    dg_range_t r[3] = { { 0x1000, 0x3000 }, { 0x5000, 0x9000 }, { 0xA000, 0xB000 } };

    CHECK_EQ(dg_mem_limit(r, 3, 0), 3);
    CHECK_EQ(dg_mem_limit(r, 3, 0x3000), 2);
    CHECK_EQ(r[1].end, 0x6000);
    CHECK_EQ(dg_mem_total(r, 2), 0x3000);
    CHECK_EQ(dg_mem_limit(r, 2, 0x2000), 1);
    CHECK_EQ(dg_mem_limit(r, 1, 0x100000), 1);
    CHECK_EQ(r[0].end, 0x3000);
    CHECK_EQ(dg_mem_limit(NULL, 2, 5), 2);
    CHECK_EQ(dg_mem_total(NULL, 2), 0);
}

TEST(pattern_values)
{
    CHECK_EQ(dg_pat_value(DG_PAT_ADDR, 0x01234568u), 0x01234568u);
    CHECK_EQ(dg_pat_value(DG_PAT_CHECK, 0x1000u), 0xAAAAAAAAu);
    CHECK_EQ(dg_pat_value(DG_PAT_CHECK, 0x1004u), 0x55555555u);
    CHECK_EQ(dg_pat_value(DG_PAT_INVERSE, 0x1000u), 0x55555555u);
    CHECK_EQ(dg_pat_value(DG_PAT_INVERSE, 0x1004u), 0xAAAAAAAAu);
    CHECK_EQ(dg_pat_value(DG_PAT_WALK, 0x2000u), 1u);
    CHECK_EQ(dg_pat_value(DG_PAT_WALK, 0x2004u), 2u);
    CHECK_EQ(dg_pat_value(DG_PAT_WALK, 0x207Cu), 0x80000000u);
    CHECK_EQ(dg_pat_value(DG_PAT_COUNT, 0x2000u), 0);
    CHECK(dg_pat_touches(DG_PAT_WALK, 0x207Cu));
    CHECK(!dg_pat_touches(DG_PAT_WALK, 0x2080u));
    CHECK(!dg_pat_touches(DG_PAT_WALK, 0x2FFCu));
    CHECK(dg_pat_touches(DG_PAT_ADDR, 0x2080u));
    CHECK_STR(dg_pat_name(DG_PAT_ADDR), "addr");
    CHECK_STR(dg_pat_name(DG_PAT_WALK), "walk");
    CHECK_STR(dg_pat_name(DG_PAT_CHECK), "check");
    CHECK_STR(dg_pat_name(DG_PAT_INVERSE), "inverse");
    CHECK_STR(dg_pat_name(DG_PAT_COUNT), "unknown");
}

/* A fake RAM: 4 pages at a made-up address. */
#define FAKE_WORDS 4096u
#define FAKE_ADDR  0x01000000u
static uint32_t fake[FAKE_WORDS];

TEST(clean_memory_passes_every_pattern)
{
    unsigned p;

    for (p = 0; p < DG_PAT_COUNT; p++) {
        dg_fault_t f = { 0, 0, 0, 0 };

        memset(fake, 0x5A, sizeof(fake));
        dg_pat_fill(p, fake, FAKE_ADDR, FAKE_WORDS);
        CHECK_EQ(dg_pat_check(p, fake, FAKE_ADDR, FAKE_WORDS, &f), 0);
        CHECK_EQ(f.count, 0);
    }
    /* The walking ones write 32 words per page and nothing else. */
    memset(fake, 0x5A, sizeof(fake));
    dg_pat_fill(DG_PAT_WALK, fake, FAKE_ADDR, FAKE_WORDS);
    CHECK_EQ(fake[0], 1u);
    CHECK_EQ(fake[31], 0x80000000u);
    CHECK_EQ(fake[32], 0x5A5A5A5Au);
    CHECK_EQ(fake[1024], 1u);
    CHECK_EQ(fake[1023], 0x5A5A5A5Au);
    /* A start inside a page. */
    memset(fake, 0x5A, sizeof(fake));
    dg_pat_fill(DG_PAT_WALK, fake, FAKE_ADDR + 0x40u, 64);
    CHECK_EQ(fake[0], 1u << 16);
    CHECK_EQ(fake[16], 0x5A5A5A5Au);
    CHECK_EQ(dg_pat_check(DG_PAT_WALK, fake, FAKE_ADDR + 0x40u, 64, NULL), 0);
}

TEST(planted_faults_are_found_at_their_address)
{
    unsigned p;

    for (p = 0; p < DG_PAT_COUNT; p++) {
        dg_fault_t f = { 0, 0, 0, 0 };
        uint32_t want;
        /* Word 1029: page 1, word 5, which every pattern writes. */
        unsigned k = 1029;

        dg_pat_fill(p, fake, FAKE_ADDR, FAKE_WORDS);
        want = fake[k];
        fake[k] ^= 0x00010000u;
        fake[k + 2] ^= 0x1u;
        CHECK_EQ(dg_pat_check(p, fake, FAKE_ADDR, FAKE_WORDS, &f), 2);
        CHECK_EQ(f.addr, FAKE_ADDR + k * 4u);
        CHECK_EQ(f.expected, want);
        CHECK_EQ(f.found, want ^ 0x00010000u);
        CHECK_EQ(f.count, 2);
    }
    /* A stuck bit at the last word of the buffer. */
    dg_pat_fill(DG_PAT_ADDR, fake, FAKE_ADDR, FAKE_WORDS);
    fake[FAKE_WORDS - 1] |= 0x80000000u;
    {
        dg_fault_t f = { 0, 0, 0, 0 };

        CHECK_EQ(dg_pat_check(DG_PAT_ADDR, fake, FAKE_ADDR, FAKE_WORDS, &f), 1);
        CHECK_EQ(f.addr, FAKE_ADDR + (FAKE_WORDS - 1) * 4u);
        CHECK_EQ(f.found, 0x80000000u | (FAKE_ADDR + (FAKE_WORDS - 1) * 4u));
    }
    /* The walking ones do not look at words they do not write. */
    dg_pat_fill(DG_PAT_WALK, fake, FAKE_ADDR, FAKE_WORDS);
    fake[40] ^= 1u;
    CHECK_EQ(dg_pat_check(DG_PAT_WALK, fake, FAKE_ADDR, FAKE_WORDS, NULL), 0);
    fake[3 * 1024 + 31] = 0;
    {
        dg_fault_t f = { 0, 0, 0, 0 };

        CHECK_EQ(dg_pat_check(DG_PAT_WALK, fake, FAKE_ADDR, FAKE_WORDS, &f), 1);
        CHECK_EQ(f.addr, FAKE_ADDR + 0x3000u + 31u * 4u);
        CHECK_EQ(f.expected, 0x80000000u);
        CHECK_EQ(f.found, 0);
    }
    /* An address line fault: two addresses share one word. */
    dg_pat_fill(DG_PAT_ADDR, fake, FAKE_ADDR, FAKE_WORDS);
    fake[2048] = fake[0];
    {
        dg_fault_t f = { 0, 0, 0, 0 };

        CHECK_EQ(dg_pat_check(DG_PAT_ADDR, fake, FAKE_ADDR, FAKE_WORDS, &f), 1);
        CHECK_EQ(f.addr, FAKE_ADDR + 0x2000u);
        CHECK_EQ(f.found, FAKE_ADDR);
    }
    CHECK_EQ(dg_pat_check(DG_PAT_COUNT, fake, FAKE_ADDR, FAKE_WORDS, NULL), 0);
    CHECK_EQ(dg_pat_check(DG_PAT_ADDR, NULL, FAKE_ADDR, FAKE_WORDS, NULL), 0);
    dg_pat_fill(DG_PAT_ADDR, NULL, FAKE_ADDR, FAKE_WORDS);
}
