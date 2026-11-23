/* The SD test's read plan: whole volumes, samples, and every bound. */

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
