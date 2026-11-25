/*
 * The menu, the tool names and results, the screen lines and the answer
 * rule of a question. Pure: the host tests link them.
 */
#ifndef MT_UI_H
#define MT_UI_H

#include <stddef.h>
#include <stdint.h>

#include <robco/input.h>

#include "../diag/dg_fmt.h"

/* Tool results, as BOOT_PROTOCOL.md gives them for the tool marker. */
enum {
    MT_OK = 0,
    MT_FAIL,
    MT_ABSENT,
    MT_OFF,
    MT_REFUSED,
    MT_RESULTS,
    MT_NONE = MT_RESULTS    /* not run yet */
};

/* "ok", "fail", "absent", "off", "refused"; "none" otherwise. */
const char *mt_result_name(int r);
/* "OK", "FAILED", "ABSENT", "OFF", "REFUSED"; "" otherwise. */
const char *mt_result_word(int r);

/* The tools, in menu order. */
enum {
    MT_T_CARD = 0,
    MT_T_IMAGES,
    MT_T_VIEW,
    MT_T_CLOCK,
    MT_T_USERS,
    MT_T_RESTART,
    MT_TOOLS
};

/* Marker name of a tool ("check_card" ...), NULL past the tools. */
const char *mt_tool_name(unsigned tool);
/* The tool with this marker name, or -1. */
int mt_tool_find(const char *name);
/* 1 for a tool that never writes and so runs behind the write guard. */
int mt_tool_readonly(unsigned tool);

/* The screen grid the menu needs. */
#define MT_MIN_COLS     80u
#define MT_MIN_ROWS     30u

/* Screen rows. The tool work area runs from MT_ROW_WORK to the last row. */
#define MT_ROW_TITLE    0u
#define MT_ROW_SUB      1u
#define MT_ROW_RULE     2u
#define MT_ROW_ITEM0    4u      /* MT_TOOLS rows */
#define MT_ROW_HELP     11u
#define MT_ROW_STATUS   12u
#define MT_ROW_RULE2    13u
#define MT_ROW_WORK     14u
#define MT_WORK_ROWS    16u     /* rows 14 to 29 of a 30-row grid */

/* A screen or serial line, without its end. */
#define MT_LINE_MAX     160u
/* The detail text of a result on its menu row. */
#define MT_TEXT_MAX     64u
/* The tool keys of a result marker: its short keys, then one encoded text. */
#define MT_KEYS_MAX     448u
/*
 * One marker after "ev=", as DG_MARK_SIZE: the only limit of a marker
 * line. It holds the tool marker with the longest name and result.
 */
#define MT_MARK_SIZE    (MT_LINE_MAX + MT_KEYS_MAX)
#define MT_TOOL_HEAD_MAX DG_STR_LEN("tool name=restore_users result=refused ")
_Static_assert(MT_MARK_SIZE >= MT_TOOL_HEAD_MAX + MT_KEYS_MAX, "tool marker buffer");
/* Read-only tools: room kept at the end of the keys for " writes=<n>". */
#define MT_WRITES_KEY   DG_KEY_UINT("writes")

/*
 * A menu row with a result has MT_ITEM_FIXED columns before its text, and
 * the console leaves the last of the MT_MIN_COLS columns blank. So the
 * row shows MT_TEXT_COLS characters of the text. The work rows show all.
 */
#define MT_ITEM_FIXED   59u
#define MT_TEXT_COLS    (MT_MIN_COLS - 1u - MT_ITEM_FIXED)

/* Menu actions of mt_menu_event(). */
#define MT_ACT_NONE     0
#define MT_ACT_RUN      1   /* run the tool in *tool */

/* Redraw flags of mt_menu_take(). */
#define MT_DIRTY_SEL    1u

typedef struct {
    unsigned sel;       /* selected row, 0 to rows - 1 */
    unsigned rows;      /* row count */
    unsigned old;       /* the row drawn as selected before the last change */
    unsigned dirty;     /* MT_DIRTY_* */
} mt_menu_t;

/* A menu of `rows` rows, 1 to 9, with the first selected. */
void mt_menu_init(mt_menu_t *m, unsigned rows);

/*
 * One input event. UP and DOWN move and stop at the ends. A press of
 * SELECT, which ENTER gives, starts the selected row. A digit 1 to the
 * row count selects that row and starts it. Releases, repeats of SELECT
 * and other events do nothing. Returns MT_ACT_* and, for MT_ACT_RUN, the
 * row in *row.
 */
int mt_menu_event(mt_menu_t *m, const input_event_t *ev, unsigned *row);

/* Returns and clears the redraw flags; *old gets the row to draw unselected. */
unsigned mt_menu_take(mt_menu_t *m, unsigned *old);

/* Answers of mt_answer(). */
#define MT_ANS_NONE     0   /* not an answer: a release, a repeat */
#define MT_ANS_YES      1
#define MT_ANS_NO       2

/*
 * The answer an event gives to a yes or no question. A press of SELECT,
 * which ENTER gives, and the characters y and Y are yes. Any other key
 * press, character or dial turn is no: BACK, which ESC gives, Ctrl-C and
 * a wrong key. Releases and held repeats are no answer.
 */
int mt_answer(const input_event_t *ev);

/* 1 when the event asks to stop a tool or close a view: a press of BACK, or Ctrl-C. */
int mt_is_abort(const input_event_t *ev);

/* Screen lines. Each fits MT_MIN_COLS - 1 columns. */
void mt_line_title(char *out, size_t size, const char *version);
const char *mt_line_sub(void);
const char *mt_line_rule(void);
const char *mt_line_help(void);
const char *mt_line_idle(void);

/* A menu row: "> 1  CHECK CARD     Read every file, report errors  OK       text". */
void mt_line_item(char *out, size_t size, unsigned tool, int selected, int result,
                  const char *text);

/* The status line after a tool: "Check card: OK. text". */
void mt_line_status(char *out, size_t size, unsigned tool, int result, const char *text);

/* The status line while a tool runs: "Check card: running. ESC stops or cancels it." */
void mt_line_running(char *out, size_t size, unsigned tool);

/* The marker tail "tool name=<name> result=<result> <keys>", keys may be "". */
void mt_line_tool(char *out, size_t size, unsigned tool, int result, const char *keys);

#endif
