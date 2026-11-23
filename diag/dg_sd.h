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

#include <stdint.h>

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

#endif
