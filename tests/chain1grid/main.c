/*
 * examples/chain1, unchanged, with one step added before it builds the
 * boot info: the console takes a GRID_COLS by GRID_ROWS grid on the same
 * framebuffer with console_adopt(), so the boot info hands that grid on.
 * The cursor stays where chain1 left it. Not shipped.
 */
#include <robco/bootinfo.h>
#include <robco/console.h>
#include <robco/klibc.h>

static void grid_bootinfo_build(robco_bootinfo_t *bi, uint32_t stage)
{
    unsigned cx, cy;
    int rc;

    console_get_cursor(&cx, &cy);
    rc = console_adopt(GRID_COLS, GRID_ROWS, cx, cy, CONSOLE_SCHEME_AMBER);
    k_printf("@RC stage=chain1 ev=grid cols=%u rows=%u rc=%d\n", (unsigned)GRID_COLS,
             (unsigned)GRID_ROWS, rc);
    bootinfo_build(bi, stage);
}

/* Only chain1's own call: the library's headers are already in. */
#define bootinfo_build grid_bootinfo_build
#include "../../external/robco-core/examples/chain1/main.c"
