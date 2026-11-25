/*
 * The menu state machine and the exit rule of the live views. Pure: the
 * host tests link them.
 */
#ifndef DG_MENU_H
#define DG_MENU_H

#include <stdint.h>

#include <robco/input.h>

/* Menu rows: the tests in order, then "run all" and "reboot". */
enum {
    DG_T_MEM = 0,
    DG_T_SD,
    DG_T_VIDEO,
    DG_T_AUDIO,
    DG_T_INPUT,
    DG_T_TIMER,
    DG_T_RNG,
    DG_T_RTC,
    DG_T_EXPANDER,
    DG_TESTS,                   /* count of tests */
    DG_ITEM_ALL = DG_TESTS,     /* run every test that needs no person */
    DG_ITEM_REBOOT,
    DG_ITEMS
};

/* Actions of dg_menu_event(). */
#define DG_ACT_NONE     0
#define DG_ACT_RUN      1   /* run the item in *item: a test or DG_ITEM_ALL */
#define DG_ACT_REBOOT   2

/* Redraw flags of dg_menu_take(). */
#define DG_DIRTY_SEL    1u

typedef struct {
    unsigned sel;       /* selected row, 0 to DG_ITEMS - 1 */
    unsigned old;       /* the row drawn as selected before the last change */
    unsigned dirty;     /* DG_DIRTY_* */
} dg_menu_t;

void dg_menu_init(dg_menu_t *m);

/*
 * One input event. UP and DOWN move and stop at the ends. ENTER or
 * SELECT starts the selected row. A digit 1 to 9 selects that test and
 * starts it, A selects "run all" and starts it, R selects "reboot" and
 * starts it. Releases and other events do nothing. Returns DG_ACT_* and,
 * for DG_ACT_RUN, the row in *item.
 */
int dg_menu_event(dg_menu_t *m, const input_event_t *ev, unsigned *item);

/* Returns and clears the redraw flags; *old gets the row to draw unselected. */
unsigned dg_menu_take(dg_menu_t *m, unsigned *old);

/* Short name of a test for markers and commands ("mem" ...), NULL past the tests. */
const char *dg_test_name(unsigned test);

/* The test with this name, or -1. "all" gives DG_ITEM_ALL. */
int dg_test_find(const char *name);

/* 1 for a test that needs a person and so is skipped by "run all". */
int dg_test_interactive(unsigned test);

/* 1 when the event asks to stop a test: a press of BACK, which ESC gives. */
int dg_is_abort(const input_event_t *ev);

/*
 * The exit rule of the input live view: BACK twice within DG_LIVE_DOUBLE_MS,
 * BACK held for DG_LIVE_LONG_MS, or nothing at all for the idle time.
 * A single short BACK stays in the view, so the BACK button can be tried.
 */
#define DG_LIVE_DOUBLE_MS   1000u
#define DG_LIVE_LONG_MS     1500u

#define DG_LIVE_STAY    0
#define DG_LIVE_DOUBLE  1
#define DG_LIVE_LONG    2
#define DG_LIVE_IDLE    3

typedef struct {
    uint32_t idle_ms;
    uint32_t last_ms;   /* time of the last event */
    uint32_t back_ms;   /* time of the last BACK press */
    int back_seen;      /* back_ms is valid */
    int back_down;      /* BACK is held since back_ms */
} dg_live_t;

void dg_live_init(dg_live_t *l, uint32_t now_ms, uint32_t idle_ms);
/* Feeds one event. Returns DG_LIVE_STAY or DG_LIVE_DOUBLE. */
int dg_live_event(dg_live_t *l, const input_event_t *ev, uint32_t now_ms);
/* Checks the clock. Returns DG_LIVE_STAY, DG_LIVE_LONG or DG_LIVE_IDLE. */
int dg_live_poll(const dg_live_t *l, uint32_t now_ms);
/* "back2", "long" or "idle" for a DG_LIVE_* exit, "none" otherwise. */
const char *dg_live_why(int code);

#endif
