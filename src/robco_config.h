#ifndef ROBCO_CONFIG_H
#define ROBCO_CONFIG_H

/* Preprocessor macros only: this file is also forced into asm and the linker script. */

#define MFB_VERSION_MAJOR       0
#define MFB_VERSION_MINOR       1
#define MFB_VERSION_PATCH       0
#define MFB_VERSION_STR         "0.1.0"

/* The @RC name of MFBootAgent, from the stage id table in BOOT_PROTOCOL.md. */
#define ROBCO_STAGE_NAME        "bootagent"

#define ROBCO_IMG_MAGIC         RIMG_MAGIC_BOOT
#define ROBCO_IMG_SUBTYPE       RIMG_BOOT_AGENT
#define ROBCO_IMG_ABI_VERSION   (2 << 16)
#define ROBCO_IMG_VERSION       ((MFB_VERSION_MAJOR << 16) | (MFB_VERSION_MINOR << 8) | \
                                 MFB_VERSION_PATCH)
#define ROBCO_IMG_NAME          "MFBOOT"

/* MFBootAgent range, 0x00200000 to 0x003FFFFF. The link fails if the image does not fit. */
#ifndef ROBCO_SLOT_END
#define ROBCO_SLOT_END          0x00400000
#endif

/* One volume: the boot volume. The holotape slot is not used here. */
#define ROBCO_FS_VOLUMES        1

#endif
