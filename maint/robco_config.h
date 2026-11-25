/*
 * Build configuration of the maintenance image, forced into every file
 * by RobCo-Core's build.
 */
#ifndef ROBCO_CONFIG_H
#define ROBCO_CONFIG_H

/* Preprocessor macros only: this file is also forced into asm and the linker script. */

#define MT_VERSION_MAJOR        0
#define MT_VERSION_MINOR        1
#define MT_VERSION_PATCH        0
#define MT_VERSION_STR          "0.1.0"

/* The @RC name of the maintenance image, from the stage id table in BOOT_PROTOCOL.md. */
#define ROBCO_STAGE_NAME        "maint"

#define ROBCO_IMG_MAGIC         RIMG_MAGIC_BOOT
#define ROBCO_IMG_SUBTYPE       RIMG_BOOT_MAINT
#define ROBCO_IMG_ABI_VERSION   (2 << 16)
#define ROBCO_IMG_VERSION       ((MT_VERSION_MAJOR << 16) | (MT_VERSION_MINOR << 8) | \
                                 MT_VERSION_PATCH)
#define ROBCO_IMG_NAME          "MAINT"

/* OS slot, 0x00400000 to 0x00FFFFFF. The link fails if the image does not fit. */
#ifndef ROBCO_SLOT_END
#define ROBCO_SLOT_END          0x01000000
#endif

/* One volume: the boot volume. */
#define ROBCO_FS_VOLUMES        1

#endif
