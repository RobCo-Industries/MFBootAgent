#ifndef ROBCO_CONFIG_H
#define ROBCO_CONFIG_H

/* Preprocessor macros only: this file is also forced into asm and the linker script. */

#define ROBCO_STAGE_NAME        "testos"

/* 0 UOS, 2 maintenance, 3 diagnostics: the Makefile sets it. */
#ifndef TESTOS_SUBTYPE
#define TESTOS_SUBTYPE          0
#endif

#define ROBCO_IMG_MAGIC         RIMG_MAGIC_BOOT
#define ROBCO_IMG_SUBTYPE       TESTOS_SUBTYPE
#define ROBCO_IMG_ABI_VERSION   (2 << 16)
#define ROBCO_IMG_VERSION       1
#define ROBCO_IMG_NAME          "testos"

/* OS slot, 0x00400000 to 0x00FFFFFF. The link fails if the image does not fit. */
#ifndef ROBCO_SLOT_END
#define ROBCO_SLOT_END          0x01000000
#endif

#endif
