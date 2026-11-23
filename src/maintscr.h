/*
 * The maintenance screen: its action list as a state machine, and the
 * text of each of its rows. Pure code: the host tests link it. main.c
 * and maintui.c feed it keys, run the actions and draw the rows.
 *
 * The screen takes MFB_MS_ROWS rows, the minimum grid height:
 *   row 0      title
 *   row 1      rule
 *   rows 2-6   the actions, one per row
 *   row 7      the keys
 *   rows 8-17  the output of the last action, MFB_MS_OUT_ROWS rows
 * Every line fits MFB_LINE_MAX - 1 columns, as the menu's do.
 */
#ifndef MFB_MAINTSCR_H
#define MFB_MAINTSCR_H

#include <stddef.h>
#include <stdint.h>

#include "boot.h"
#include "bootcfg.h"

/* The actions, in screen order. */
#define MFB_MS_VERIFY   0
#define MFB_MS_SYSTEM   1
#define MFB_MS_CONFIG   2
#define MFB_MS_REBOOT   3
#define MFB_MS_BACK     4
#define MFB_MS_ITEMS    5

#define MFB_MS_ROW_TITLE    0
#define MFB_MS_ROW_RULE     1
#define MFB_MS_ROW_ITEM0    2
#define MFB_MS_ROW_HELP     (MFB_MS_ROW_ITEM0 + MFB_MS_ITEMS)
#define MFB_MS_ROW_OUT0     (MFB_MS_ROW_HELP + 1)
/* The verify table at its largest: a header, MFB_MAX_ENTRIES rows and the counts. */
#define MFB_MS_OUT_ROWS     (MFB_MAX_ENTRIES + 2)
#define MFB_MS_ROWS         (MFB_MS_ROW_OUT0 + MFB_MS_OUT_ROWS)

/* What the caller does next. */
#define MFB_MS_ACT_NONE     0
#define MFB_MS_ACT_RUN      1   /* run the action in *item: verify, system, config or reboot */
#define MFB_MS_ACT_CLOSE    2   /* back to the boot menu */

typedef struct {
    unsigned sel;           /* selected action, MFB_MS_* */
    unsigned drawn_sel;     /* the action drawn as selected */
    int dirty;              /* the selection moved since the last mfb_ms_take() */
} mfb_ms_t;

/* Starts on the first action. */
void mfb_ms_init(mfb_ms_t *s);

/*
 * One MFB_K_* key. UP and DOWN move the bar and stop at the ends.
 * SELECT runs the selected action: MFB_MS_ACT_RUN with *item set, or
 * MFB_MS_ACT_CLOSE for "Back to the boot menu". BACK and QUIT close the
 * screen. Every other key does nothing.
 */
int mfb_ms_key(mfb_ms_t *s, int key, unsigned *item);

/* 1 when the selection moved, and *old gets the action drawn as selected until now. */
int mfb_ms_take(mfb_ms_t *s, unsigned *old);

/* Marker name of an action: "verify", "system", "config", "reboot" or "back". */
const char *mfb_ms_name(unsigned item);

/* The fixed rows. */
void mfb_ms_title(char *buf, size_t size, const char *version);
const char *mfb_ms_help(void);
/* "> Verify entries" or "  Verify entries". Empty past the last action. */
void mfb_ms_item(char *buf, size_t size, unsigned item, int selected);

/* ---- Output pages: each fills up to MFB_MS_OUT_ROWS lines of MFB_LINE_MAX ---- */

typedef char mfb_ms_page_t[MFB_MS_OUT_ROWS][MFB_LINE_MAX];

/* Verify: the header row, one row per entry, the counts, and the no-volume line. */
void mfb_ms_verify_head(char *buf, size_t size);
/* "1  UOS             OK          /ROBCO/UOS.SYS". err is 0 or MFB_ERR_*. */
void mfb_ms_verify_row(char *buf, size_t size, unsigned index, const mfb_entry_t *e, int err);
/* "3 entries checked: 1 good, 2 failed." */
void mfb_ms_verify_sum(char *buf, size_t size, unsigned good, unsigned failed);

/* What the System page shows. */
typedef struct {
    const char *board;      /* model name */
    const char *soc;
    uint32_t mem_bytes;
    uint32_t vol_lba;       /* first block of the boot volume */
    uint32_t vol_blocks;
    int trusted;            /* the boot info passed the trust rule */
    uint32_t flags_in;      /* boot_flags received, when trusted */
    const char *version;    /* the stage's version */
    const char *core_pin;   /* the library's commit */
    int screen;             /* 0 none, 1 adopted, 2 fresh */
} mfb_ms_sys_t;

#define MFB_MS_SCREEN_NONE  0
#define MFB_MS_SCREEN_ADOPT 1
#define MFB_MS_SCREEN_FRESH 2

/* Fills `page`. Returns the line count, at most MFB_MS_OUT_ROWS. */
unsigned mfb_ms_system(mfb_ms_page_t page, const mfb_ms_sys_t *sys);

/* What the Configuration page shows: the config marker's values. */
#define MFB_MS_CFG_OK       0   /* read and used */
#define MFB_MS_CFG_MISSING  1
#define MFB_MS_CFG_READ     2   /* it did not read */
#define MFB_MS_CFG_SYNTAX   3   /* a line did not parse */
#define MFB_MS_CFG_EMPTY    4   /* it parsed with no usable entry */

typedef struct {
    int result;             /* MFB_MS_CFG_* */
    unsigned line;          /* the line that did not parse */
    const char *path;
    const char *def;        /* the default entry's name */
    int timeout;            /* seconds, or MFB_TIMEOUT_NONE */
    unsigned entries;
    unsigned ignored, dup, bad, clamped;
} mfb_ms_cfg_t;

unsigned mfb_ms_config(mfb_ms_page_t page, const mfb_ms_cfg_t *c);

#endif
