/*
 * Load errors, their screen lines, and the boot info fields this stage
 * writes. Pure code: the host tests link it.
 */
#ifndef MFB_BOOT_H
#define MFB_BOOT_H

#include <stddef.h>
#include <stdint.h>

#include <robco/bootinfo.h>

/* The OS slot, from MEMORY_MAP.md: 0x00400000 to 0x00FFFFFF. */
#define MFB_OS_SLOT_BASE    0x00400000u
#define MFB_OS_SLOT_SIZE    0x00C00000u
#define MFB_OS_ABI          2u

/*
 * Error codes: the rimg_err_t values of the loader, with the same
 * numbers and names. MFB_ERR_SUBTYPE is not a code of its own: it is a
 * BOOT image whose subtype is not an operating image, such as another
 * boot agent. IMAGE_FORMAT.md puts the subtype check in step 4, so it
 * shows as code 6, `bounds`, with its own reason.
 */
#define MFB_ERR_NOFILE      1
#define MFB_ERR_IO          2
#define MFB_ERR_MAGIC       3
#define MFB_ERR_VERSION     4
#define MFB_ERR_SIZE        5
#define MFB_ERR_BOUNDS      6
#define MFB_ERR_ABI         7
#define MFB_ERR_CRC         8
#define MFB_ERR_SUBTYPE     0x106

/* The numeric code a reason shows as: MFB_ERR_SUBTYPE is 6. */
int mfb_err_code(int err);
/* Marker name, such as "crc". "unknown" for others. */
const char *mfb_err_name(int err);
/* Plain words, such as "image CRC does not match". */
const char *mfb_err_text(int err);

/* Longest screen line, with the NUL: 79 characters, so a line never wraps on 80 columns. */
#define MFB_LINE_MAX 80

/*
 * The screen line of a failed entry: "ERROR E08 CRC: UOS: image CRC does
 * not match". name may be NULL or empty. Cut to size - 1 characters.
 */
void mfb_error_line(char *buf, size_t size, int err, const char *name);

/* 1 for the BOOT subtypes this stage starts: UOS, maintenance and diagnostics. */
int mfb_subtype_ok(uint32_t subtype);

/*
 * Writes the fields this stage owns as the loader of the next stage into
 * a block bootinfo_build() made, then reseals it: boot_dev, boot_part_lba,
 * boot_path and cmdline, each cut to its array and NUL-padded. It clears
 * the fallback flag, since this loader used no backup file, and keeps
 * every other flag.
 */
void mfb_bootinfo_set_boot(robco_bootinfo_t *bi, uint32_t boot_dev, uint32_t part_lba,
                           const char *path, const char *cmdline);

#endif
