/*
 * The memory test: which RAM it may write and the patterns. Pure: no
 * hardware access, the host tests link it.
 *
 * Addresses are 32-bit ARM physical addresses. A range is [start, end):
 * end is the first address past it, and 0x100000000 does not fit, so a
 * range never reaches the top of the 4 GiB space. No board here comes
 * close.
 */
#ifndef DG_MEM_H
#define DG_MEM_H

#include <stdint.h>

/* Ranges are cut to this alignment: the walking-ones sample is one word per page. */
#define DG_MEM_ALIGN        0x1000u
/* At most this many ranges come out: the RAM minus the exclusions. */
#define DG_MEM_RANGES_MAX   4u
/* "Run all" tests only the first DG_MEM_QUICK_BYTES of the ranges. */
#define DG_MEM_QUICK_BYTES  (16u << 20)

typedef struct {
    uint32_t start;
    uint32_t end;   /* first address past the range */
} dg_range_t;

/*
 * What the image knows. image_end is the end of the image, its BSS, its
 * stacks and its heap, if any. mem_base and mem_size give ARM RAM, from
 * the boot info or the mailbox. The framebuffer, the handoff block and
 * the firmware blob copy are excluded wherever they lie. A size of 0
 * means there is no such range.
 */
typedef struct {
    uint32_t image_start;
    uint32_t image_end;
    uint32_t mem_base;
    uint32_t mem_size;
    uint32_t fb_addr;
    uint32_t fb_size;
    uint32_t handoff_addr;
    uint32_t handoff_size;
} dg_mem_in_t;

#define DG_MEM_OK       0
#define DG_MEM_ERR_ARG  (-1)    /* NULL, or the image is not inside RAM */
#define DG_MEM_ERR_NONE (-2)    /* nothing is left to test */

/*
 * Fills out[] with the ranges the test may write: from image_end, rounded
 * up to DG_MEM_ALIGN, to the end of RAM, rounded down, minus every
 * excluded range rounded out to DG_MEM_ALIGN. Pieces under DG_MEM_ALIGN
 * are dropped. Sorted by address. Stores the count in *n.
 */
int dg_mem_ranges(const dg_mem_in_t *in, dg_range_t out[DG_MEM_RANGES_MAX], unsigned *n);

/* Total bytes of n ranges. */
uint32_t dg_mem_total(const dg_range_t *r, unsigned n);

/*
 * Cuts the ranges to their first `limit` bytes in address order, in place.
 * Returns the new count. limit 0 keeps everything.
 */
unsigned dg_mem_limit(dg_range_t *r, unsigned n, uint32_t limit);

/* The patterns, in the order the test runs them. */
enum {
    DG_PAT_ADDR = 0,    /* each word holds its own address */
    DG_PAT_WALK,        /* walking ones over the first 32 words of every page */
    DG_PAT_CHECK,       /* 0xAAAAAAAA and 0x55555555, alternating by word */
    DG_PAT_INVERSE,     /* the checkerboard inverted */
    DG_PAT_COUNT
};

/* Short name of a pattern for markers: "addr", "walk", "check", "inverse". */
const char *dg_pat_name(unsigned pat);

/*
 * 1 when the pattern writes the word at `addr`. Only DG_PAT_WALK skips
 * words: it writes the first 32 words of each page.
 */
int dg_pat_touches(unsigned pat, uint32_t addr);

/* The value the pattern puts at `addr`. */
uint32_t dg_pat_value(unsigned pat, uint32_t addr);

/*
 * Writes the pattern over n words at `words`, which stand for the RAM at
 * `addr` (word aligned). The host tests pass a buffer and a made-up
 * address; the target passes the address itself.
 */
void dg_pat_fill(unsigned pat, volatile uint32_t *words, uint32_t addr, uint32_t n);

typedef struct {
    uint32_t addr;      /* first bad address */
    uint32_t expected;
    uint32_t found;
    uint32_t count;     /* bad words in the call */
} dg_fault_t;

/*
 * Reads n words back and compares them with the pattern. Returns the
 * count of bad words; the first one goes in *f when f is not NULL and
 * the call found one.
 */
uint32_t dg_pat_check(unsigned pat, const volatile uint32_t *words, uint32_t addr, uint32_t n,
                      dg_fault_t *f);

#endif
