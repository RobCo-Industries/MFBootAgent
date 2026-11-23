/* The SD test's read plan. Pure: see dg_sd.h. */

#include <stddef.h>

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
