#ifndef ROBCO_CONFIG_H
#define ROBCO_CONFIG_H

/* Preprocessor macros only: this file is also forced into asm and the linker script. */

#define DG_VERSION_MAJOR        0
#define DG_VERSION_MINOR        1
#define DG_VERSION_PATCH        0
#define DG_VERSION_STR          "0.1.0"

/* The @RC name of the diagnostics image, from the stage id table in BOOT_PROTOCOL.md. */
#define ROBCO_STAGE_NAME        "diag"

#define ROBCO_IMG_MAGIC         RIMG_MAGIC_BOOT
#define ROBCO_IMG_SUBTYPE       RIMG_BOOT_DIAG
#define ROBCO_IMG_ABI_VERSION   (2 << 16)
#define ROBCO_IMG_VERSION       ((DG_VERSION_MAJOR << 16) | (DG_VERSION_MINOR << 8) | \
                                 DG_VERSION_PATCH)
#define ROBCO_IMG_NAME          "DIAG"

/* OS slot, 0x00400000 to 0x00FFFFFF. The link fails if the image does not fit. */
#ifndef ROBCO_SLOT_END
#define ROBCO_SLOT_END          0x01000000
#endif

#endif
