/*
 * /ROBCO/BOOT.CFG: the [boot] section and the [entry NAME] sections.
 * Pure code on top of RobCo-Core's INI reader: the host tests link it.
 * The rules are those of BOOT_PROTOCOL.md; docs/BOOT.md lists every
 * key, limit, default and counter.
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
#define MFB_PATH_COMPONENTS 8       /* components of a path, each 8.3 */
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

/*
 * What the parser skipped, for the config marker. Each [entry ...]
 * header counts once: in a slot, or in bad, dup or ignored.
 */
typedef struct {
    unsigned ignored;       /* usable entries past the eighth */
    unsigned dup;           /* entry headers whose name an earlier entry header used */
    unsigned unknown;       /* unknown keys, keys before any section, keys of unknown sections */
    unsigned bad;           /* bad or missing name, missing or bad path, bad cmdline, bad timeout */
    unsigned nopath;        /* the part of bad that had no path key */
    unsigned clamped;       /* timeout values above 99 */
    int default_found;      /* [boot] default names an entry in a slot */
} mfb_cfg_report_t;

/* Results of mfb_cfg_parse(). */
#define MFB_CFG_OK          0
#define MFB_CFG_SYNTAX      1   /* a line does not parse: *err_line holds it */
#define MFB_CFG_EMPTY       2   /* parses, but holds no usable entry */

/* The menu without a usable file: one entry UOS at /ROBCO/UOS.SYS, no timeout. */
void mfb_cfg_default(mfb_cfg_t *cfg);

/*
 * Parses `len` bytes with ini_parse_sections(). Nothing counts until the
 * whole text parses. On MFB_CFG_OK, cfg holds the usable entries in file
 * order, each path in upper case. On MFB_CFG_EMPTY cfg is
 * mfb_cfg_default() and the report counts what the file held. On
 * MFB_CFG_SYNTAX cfg is mfb_cfg_default() and the report is zero.
 * err_line may be NULL.
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
