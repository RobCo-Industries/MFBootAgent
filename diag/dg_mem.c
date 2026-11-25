/* The memory test's ranges and patterns. Pure: see dg_mem.h. */

#include <stddef.h>

#include "dg_mem.h"

#define PAGE_WORDS_WALK 32u

static uint64_t align_up(uint64_t v)
{
    return (v + DG_MEM_ALIGN - 1u) & ~(uint64_t)(DG_MEM_ALIGN - 1u);
}

static uint64_t align_down(uint64_t v)
{
    return v & ~(uint64_t)(DG_MEM_ALIGN - 1u);
}

/* Removes [lo, hi) from the n ranges in r. Returns the new count, or -1 when r is full. */
static int cut(uint64_t *lo_r, uint64_t *hi_r, int n, uint64_t lo, uint64_t hi)
{
    int i;
    int out = n;

    if (hi <= lo) {
        return n;
    }
    for (i = 0; i < n; i++) {
        uint64_t a = lo_r[i];
        uint64_t b = hi_r[i];

        if (hi <= a || lo >= b) {
            continue;
        }
        if (lo > a && hi < b) {
            /* A hole in the middle: split in two. */
            if (out >= (int)DG_MEM_RANGES_MAX) {
                return -1;
            }
            lo_r[out] = hi;
            hi_r[out] = b;
            out++;
            hi_r[i] = lo;
        } else if (lo > a) {
            hi_r[i] = lo;
        } else if (hi < b) {
            lo_r[i] = hi;
        } else {
            hi_r[i] = a;    /* emptied; dropped below */
        }
    }
    return out;
}

int dg_mem_ranges(const dg_mem_in_t *in, dg_range_t out[DG_MEM_RANGES_MAX], unsigned *n)
{
    uint64_t lo[DG_MEM_RANGES_MAX];
    uint64_t hi[DG_MEM_RANGES_MAX];
    uint64_t top;
    unsigned k = 0;
    int count;
    int i, j;

    if (in == NULL || out == NULL || n == NULL) {
        return DG_MEM_ERR_ARG;
    }
    *n = 0;
    top = (uint64_t)in->mem_base + in->mem_size;
    if (in->mem_size == 0 || in->image_end < in->image_start ||
        in->image_start < in->mem_base || in->image_end > top) {
        return DG_MEM_ERR_ARG;
    }
    lo[0] = align_up(in->image_end);
    hi[0] = align_down(top);
    count = lo[0] < hi[0] ? 1 : 0;
    /* The image itself, the handoff block and the blob copy, the framebuffer. */
    count = cut(lo, hi, count, align_down(in->image_start), align_up(in->image_end));
    if (count >= 0 && in->handoff_size != 0) {
        count = cut(lo, hi, count, align_down(in->handoff_addr),
                    align_up((uint64_t)in->handoff_addr + in->handoff_size));
    }
    if (count >= 0 && in->fb_size != 0) {
        count = cut(lo, hi, count, align_down(in->fb_addr),
                    align_up((uint64_t)in->fb_addr + in->fb_size));
    }
    if (count < 0) {
        return DG_MEM_ERR_ARG;
    }
    /* Drop the empty ones, then sort by start: at most four, insertion sort. */
    for (i = 0; i < count; i++) {
        if (hi[i] > lo[i]) {
            out[k].start = (uint32_t)lo[i];
            out[k].end = (uint32_t)hi[i];
            k++;
        }
    }
    for (i = 1; i < (int)k; i++) {
        dg_range_t t = out[i];

        for (j = i - 1; j >= 0 && out[j].start > t.start; j--) {
            out[j + 1] = out[j];
        }
        out[j + 1] = t;
    }
    *n = k;
    return k == 0 ? DG_MEM_ERR_NONE : DG_MEM_OK;
}

int dg_mem_agree(uint32_t bi_base, uint32_t bi_size, uint32_t mb_base, uint32_t mb_size)
{
    if (bi_size == 0 || mb_size == 0) {
        return DG_MEM_OK;
    }
    if (bi_base < mb_base ||
        (uint64_t)bi_base + bi_size > (uint64_t)mb_base + mb_size) {
        return DG_MEM_ERR_TOP;
    }
    return DG_MEM_OK;
}

uint32_t dg_mem_total(const dg_range_t *r, unsigned n)
{
    uint32_t t = 0;
    unsigned i;

    for (i = 0; r != NULL && i < n; i++) {
        t += r[i].end - r[i].start;
    }
    return t;
}

unsigned dg_mem_limit(dg_range_t *r, unsigned n, uint32_t limit)
{
    uint32_t left = limit;
    unsigned i;

    if (limit == 0 || r == NULL) {
        return n;
    }
    for (i = 0; i < n && left > 0; i++) {
        uint32_t size = r[i].end - r[i].start;

        if (size > left) {
            r[i].end = r[i].start + left;
            size = left;
        }
        left -= size;
    }
    return i;
}

/* Words from `addr` to the next page, where the walking ones start again. */
static uint32_t skip(uint32_t addr)
{
    return (DG_MEM_ALIGN - (addr & (DG_MEM_ALIGN - 1u))) / 4u;
}

const char *dg_pat_name(unsigned pat)
{
    switch (pat) {
    case DG_PAT_ADDR:    return "addr";
    case DG_PAT_WALK:    return "walk";
    case DG_PAT_CHECK:   return "check";
    case DG_PAT_INVERSE: return "inverse";
    default:             return "unknown";
    }
}

int dg_pat_touches(unsigned pat, uint32_t addr)
{
    if (pat == DG_PAT_WALK) {
        return (addr & (DG_MEM_ALIGN - 1u)) < PAGE_WORDS_WALK * 4u;
    }
    return 1;
}

uint32_t dg_pat_value(unsigned pat, uint32_t addr)
{
    uint32_t odd = (addr >> 2) & 1u;

    switch (pat) {
    case DG_PAT_ADDR:    return addr;
    case DG_PAT_WALK:    return 1u << ((addr >> 2) & 31u);
    case DG_PAT_CHECK:   return odd ? 0x55555555u : 0xAAAAAAAAu;
    case DG_PAT_INVERSE: return odd ? 0xAAAAAAAAu : 0x55555555u;
    default:             return 0;
    }
}

void dg_pat_fill(unsigned pat, volatile uint32_t *words, uint32_t addr, uint32_t n)
{
    uint32_t i;

    if (words == NULL) {
        return;
    }
    switch (pat) {
    case DG_PAT_ADDR:
        for (i = 0; i < n; i++) {
            words[i] = addr + i * 4u;
        }
        break;
    case DG_PAT_WALK:
        for (i = 0; i < n;) {
            uint32_t a = addr + i * 4u;

            if (dg_pat_touches(pat, a)) {
                words[i] = 1u << ((a >> 2) & 31u);
                i++;
            } else {
                i += skip(a);
            }
        }
        break;
    case DG_PAT_CHECK:
    case DG_PAT_INVERSE:
        for (i = 0; i < n; i++) {
            words[i] = dg_pat_value(pat, addr + i * 4u);
        }
        break;
    default:
        break;
    }
}

uint32_t dg_pat_check(unsigned pat, const volatile uint32_t *words, uint32_t addr, uint32_t n,
                      dg_fault_t *f)
{
    uint32_t bad = 0;
    uint32_t i;

    if (words == NULL || pat >= DG_PAT_COUNT) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        uint32_t a = addr + i * 4u;
        uint32_t want;
        uint32_t got;

        if (!dg_pat_touches(pat, a)) {
            i += skip(a) - 1u;
            continue;
        }
        want = dg_pat_value(pat, a);
        got = words[i];
        if (got != want) {
            if (bad == 0 && f != NULL) {
                f->addr = a;
                f->expected = want;
                f->found = got;
            }
            bad++;
        }
    }
    if (f != NULL && bad != 0) {
        f->count = bad;
    }
    return bad;
}
