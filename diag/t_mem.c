/*
 * The memory test. Writes each pattern over every range dg_mem_ranges()
 * allows, then reads it all back. With the MMU on, RAM is cached
 * write-back. Each step is cleaned and invalidated from the D-cache
 * just before it is read back, so the check reads RAM, never a dirty
 * line the fill left behind. The result keys report the bytes flushed.
 * "Run all" tests the first DG_MEM_QUICK_BYTES only.
 */

#include <stdint.h>

#include <robco/bootinfo.h>
#include <robco/cache.h>
#include <robco/fb.h>
#include <robco/klibc.h>
#include <robco/mmu.h>
#include <robco/timer.h>

#include "dg.h"
#include "dg_mem.h"

/* Work per step between two abort checks: 1 MiB. */
#define STEP_BYTES 0x00100000u
/* The handoff block and the firmware blob copy: 0x00100000 to 0x0011FFFF. */
#define HANDOFF_SIZE 0x00020000u

extern char __image_start[];
extern char __image_mem_end[];

/* Test builds: the fault the maintenance port planted, after a fill. */
static void plant(uint32_t a, uint32_t n)
{
#ifdef DG_TEST
    uint32_t f = dg.fault_addr;

    if (f != 0 && f >= a && f - a < n * 4u) {
        *(volatile uint32_t *)(uintptr_t)f ^= 0x00000100u;
    }
#else
    (void)a;
    (void)n;
#endif
}

void dg_test_mem(dg_run_t *r)
{
    dg_mem_in_t in;
    dg_range_t rg[DG_MEM_RANGES_MAX];
    dg_fault_t fault;
    dg_rate_t rate;
    const fb_info_t *fb = dg.fb_ok ? fb_get() : 0;
    char line[DG_LINE_MAX];
    unsigned n = 0;
    unsigned pat;
    unsigned i;
    uint32_t total;
    uint64_t done = 0;
    uint64_t flushed = 0;
    uint32_t t0;
    uint32_t ms;
    int rc;

    k_memset(&in, 0, sizeof(in));
    k_memset(&fault, 0, sizeof(fault));
    in.image_start = (uint32_t)(uintptr_t)__image_start;
    in.image_end = (uint32_t)(uintptr_t)__image_mem_end;
    in.mem_base = dg.mem_base;
    in.mem_size = dg.mem_size;
    in.handoff_addr = ROBCO_BOOTINFO_ADDR;
    in.handoff_size = HANDOFF_SIZE;
    if (fb != 0) {
        in.fb_addr = (uint32_t)(uintptr_t)fb->addr;
        in.fb_size = fb->size;
    }
    rc = dg_mem_ranges(&in, rg, &n);
    if (rc != DG_MEM_OK) {
        r->result = DG_FAIL;
        dg_keys(r, "why=ranges rc=%d mem=0x%08x", rc, (unsigned)dg.mem_size);
        dg_text(r, DG_TX_MEM_NORAM);
        return;
    }
    if (r->mode == DG_MODE_ALL) {
        n = dg_mem_limit(rg, n, DG_MEM_QUICK_BYTES);
    }
#ifdef DG_MEM_CAP_BYTES
    n = dg_mem_limit(rg, n, DG_MEM_CAP_BYTES);
#endif
    total = dg_mem_total(rg, n);
    for (i = 0; i < n; i++) {
        k_snprintf(line, sizeof(line), "Range %u: 0x%08x to 0x%08x, %u KiB", i + 1,
                   (unsigned)rg[i].start, (unsigned)rg[i].end,
                   (unsigned)((rg[i].end - rg[i].start) >> 10));
        dg_work_line(i, line, 1);
    }
    dg_rate_init(&rate, DG_PROGRESS_MS);
    t0 = timer_now_ms();
    for (pat = 0; pat < DG_PAT_COUNT; pat++) {
        /* Pass 0 writes the pattern everywhere, pass 1 reads it all back. */
        unsigned pass;

        for (pass = 0; pass < 2; pass++) {
            for (i = 0; i < n; i++) {
                uint32_t a;

                for (a = rg[i].start; a < rg[i].end; a += STEP_BYTES) {
                    uint32_t left = rg[i].end - a;
                    uint32_t words = (left < STEP_BYTES ? left : STEP_BYTES) / 4u;
                    volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)a;

                    if (pass == 0) {
                        dg_pat_fill(pat, p, a, words);
                        plant(a, words);
                    } else {
                        /* Push dirty fill lines to RAM and drop them from the cache. */
                        cache_flush_range((const void *)(uintptr_t)a, words * 4u);
                        flushed += words * 4u;
                    }
                    if (pass == 1 && dg_pat_check(pat, p, a, words, &fault) != 0) {
                        r->result = DG_FAIL;
                        dg_keys(r, "pattern=%s addr=0x%08x expected=0x%08x found=0x%08x",
                                dg_pat_name(pat), (unsigned)fault.addr,
                                (unsigned)fault.expected, (unsigned)fault.found);
                        dg_keys(r, "bytes=%u", (unsigned)total);
                        dg_text(r, DG_TX_MEM_FAULT, (unsigned)fault.addr);
                        k_snprintf(line, sizeof(line), "Fault at 0x%08x: read 0x%08x, wrote 0x%08x",
                                   (unsigned)fault.addr, (unsigned)fault.found,
                                   (unsigned)fault.expected);
                        dg_work_line(DG_WORK_ROWS - 1, line, 1);
                        return;
                    }
                    done += words * 4u;
                    if (dg_poll_abort()) {
                        r->result = DG_SKIPPED;
                        dg_keys(r, "why=abort pattern=%s", dg_pat_name(pat));
                        dg_text(r, DG_TX_STOPPED);
                        return;
                    }
                    dg_progress(&rate, DG_WORK_ROWS - 2, "mem", dg_pat_name(pat), done,
                                (uint64_t)total * DG_PAT_COUNT * 2u);
                }
            }
        }
    }
    ms = timer_now_ms() - t0;
    r->result = DG_OK;
    dg_keys(r, "ranges=%u start=0x%08x end=0x%08x bytes=%u patterns=%u ms=%u", n,
            (unsigned)rg[0].start, (unsigned)rg[n - 1].end, (unsigned)total, DG_PAT_COUNT,
            (unsigned)ms);
    /* Bytes cleaned and invalidated before the reads, and whether the MMU and caches are on. */
    dg_keys(r, "flushed=%u cache=%d", (unsigned)(flushed / DG_PAT_COUNT), mmu_enabled());
    dg_text(r, DG_TX_MEM_OK, (unsigned)(total >> 20), DG_PAT_COUNT, (unsigned)((ms + 999u) / 1000u));
}
