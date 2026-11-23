/*
 * The text of the menu screen. Pure code: the host tests link it.
 * Every line fits 79 columns, so none wraps on the 80-column console.
 *
 * Lore and real text: the title and the two lines under it are lore,
 * written for this project in the style of the boot agent screens of
 * the Fallout games. The version, the entries, the paths, the help line,
 * the countdown and the error lines are real.
 */
#ifndef MFB_MENULINES_H
#define MFB_MENULINES_H

#include <stddef.h>

#include "bootcfg.h"

/* Rows of the menu below its top row, for MFB_MAX_ENTRIES entries or fewer. */
#define MFB_ROW_TITLE       0
#define MFB_ROW_LORE        1   /* MFB_LORE_LINES rows */
#define MFB_LORE_LINES      2
#define MFB_ROW_RULE        3
#define MFB_ROW_ENTRY0      5   /* one row per entry */
/* The rows after the entries, as offsets from MFB_ROW_ENTRY0 + count. */
#define MFB_ROW_HELP_AFTER   1
#define MFB_ROW_STATUS_AFTER 2
#define MFB_ROW_ERROR_AFTER  3
#define MFB_ROW_START_AFTER  4
/* Rows the whole menu takes for `count` entries. */
#define MFB_MENU_ROWS(count) (MFB_ROW_ENTRY0 + (count) + MFB_ROW_START_AFTER + 1)

/* "MFBOOT AGENT 0.1.0 ..." with the version. Cut to size - 1. */
void mfb_line_title(char *buf, size_t size, const char *version);

/* Lore line i, 0 to MFB_LORE_LINES - 1. NULL past the last. */
const char *mfb_line_lore(unsigned i);

/* The rule under the header. */
const char *mfb_line_rule(void);

/*
 * One entry: "> 1  UOS             DEFAULT  /ROBCO/UOS.SYS". `index` is
 * 0-based and shows 1-based. The path is cut at the line end.
 */
void mfb_line_entry(char *buf, size_t size, unsigned index, const mfb_entry_t *e, int is_default,
                    int selected);

/* The keys. */
const char *mfb_line_help(void);

/*
 * The countdown line. counting: "Booting UOS in 3 s. Press any key to
 * stop." stopped: "Countdown stopped. Press ENTER to boot." Otherwise
 * an empty line.
 */
void mfb_line_status(char *buf, size_t size, int counting, int stopped, unsigned left,
                     const char *name);

/* "Loading UOS from /ROBCO/UOS.SYS". Cut to size - 1. */
void mfb_line_loading(char *buf, size_t size, const mfb_entry_t *e);

#endif
