/*
 * /ROBCO/BOOT.CFG: the [boot] section and the [entry NAME] sections.
 * Pure code on top of RobCo-Core's INI reader: the host tests link it.
 * The format is the draft in BOOT_PROTOCOL.md; docs/BOOT.md lists every
 * key, limit and default.
 */
#ifndef MFB_BOOTCFG_H
#define MFB_BOOTCFG_H

#include <stddef.h>
#include <stdint.h>

#define MFB_CFG_PATH        "/ROBCO/BOOT.CFG"
#define MFB_DEFAULT_NAME    "UOS"
#define MFB_DEFAULT_PATH    "/ROBCO/UOS.SYS"

#define MFB_MAX_ENTRIES     8
#define MFB_NAME_MAX        15      /* characters of an entry name */
#define MFB_PATH_MAX        63      /* characters of a path: boot_path holds 64 bytes */
#define MFB_CMDLINE_MAX     127     /* characters of a cmdline: cmdline holds 128 bytes */
#define MFB_TIMEOUT_MAX     99      /* seconds */
#define MFB_TIMEOUT_NONE    (-1)    /* no timeout key: the menu waits for ever */

/* Largest BOOT.CFG read. A longer file is a read error. */
#define MFB_CFG_FILE_MAX    4096

typedef struct {
    char name[MFB_NAME_MAX + 1];
    char path[MFB_PATH_MAX + 1];
    char cmdline[MFB_CMDLINE_MAX + 1];
} mfb_entry_t;

typedef struct {
    mfb_entry_t entry[MFB_MAX_ENTRIES];
    unsigned count;         /* at least 1 after mfb_cfg_parse() or mfb_cfg_default() */
    unsigned def;           /* index of the default entry */
    int timeout;            /* seconds 0 to 99, or MFB_TIMEOUT_NONE */
} mfb_cfg_t;

/* What the parser skipped, for the config marker. */
typedef struct {
    unsigned ignored;       /* valid entries past the eighth */
    unsigned dup;           /* entries whose name an earlier entry has */
    unsigned unknown;       /* unknown keys and keys of unknown sections */
    unsigned bad;           /* bad values, and entries with a bad name or no usable path */
    unsigned clamped;       /* timeout values above 99 */
    int default_found;      /* [boot] default names an entry */
} mfb_cfg_report_t;

/* Results of mfb_cfg_parse(). */
#define MFB_CFG_OK          0
#define MFB_CFG_SYNTAX      1   /* a line does not parse: *err_line holds it */
#define MFB_CFG_EMPTY       2   /* parses, but holds no usable entry */

/* The menu without a usable file: one entry UOS at /ROBCO/UOS.SYS, no timeout. */
void mfb_cfg_default(mfb_cfg_t *cfg);

/*
 * Parses `len` bytes. On MFB_CFG_OK, cfg holds the entries in file
 * order. On any other result cfg is mfb_cfg_default() and the report
 * counts what was seen up to the error. err_line may be NULL.
 */
int mfb_cfg_parse(const char *text, size_t len, mfb_cfg_t *cfg, mfb_cfg_report_t *rep,
                  unsigned *err_line);

/*
 * The strict number rule of delay_ms in BOOT_PROTOCOL.md: one or more
 * ASCII digits and nothing else, saturating at 4294967295. Returns 0 and
 * the value, or -1 for a bad value.
 */
int mfb_cfg_number(const char *s, uint32_t *out);

#endif
