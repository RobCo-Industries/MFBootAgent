/*
 * examples/chain1 with the SD and framebuffer variants, unchanged but
 * for one step: before it seals the boot info it sets boot_flags bit 1,
 * "the user asked for maintenance mode", as a first stage would after
 * such a request. Not shipped.
 */
#include <robco/bootinfo.h>
#include <robco/klibc.h>

static void maint_bootinfo_seal(robco_bootinfo_t *bi)
{
    bi->boot_flags |= ROBCO_BOOT_MAINT;
    k_printf("@RC stage=chain1 ev=maint_flag flags=0x%08x\n", (unsigned)bi->boot_flags);
    bootinfo_seal(bi);
}

/* Only chain1's own call: the library's headers are already in. */
#define bootinfo_seal maint_bootinfo_seal
#include "../../external/robco-core/examples/chain1/main.c"
