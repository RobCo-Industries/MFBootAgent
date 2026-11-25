/*
 * The boot menu as a state machine: selection, countdown, keys and what
 * to redraw. Pure code: the host tests link it. main.c feeds it input
 * events and the time, and draws what it marks dirty.
 *
 * Times are timer_now_ms() values: milliseconds in a uint32_t that wraps
 * after 2^32 ms, about 49.7 days. Every comparison uses the unsigned
 * difference from the start, so the countdown works across the wrap.
 */
#ifndef MFB_MENU_H
#define MFB_MENU_H

#include <stdint.h>

#include <robco/input.h>

#include "keymap.h"

/* What the caller does next. */
#define MFB_ACT_NONE    0
#define MFB_ACT_BOOT    1   /* boot the selected entry */
#define MFB_ACT_REBOOT  2   /* restart the board */
#define MFB_ACT_MAINT   3   /* open the maintenance screen: why holds MFB_WHY_* */

/* What an event means for the port's maint request: mfb_port_maint(). */
#define MFB_PORT_NONE   0   /* not a port request: a key for the menu */
#define MFB_PORT_OPEN   1   /* the live request: open the maintenance screen */
#define MFB_PORT_STALE  2   /* a request dropped earlier: take no action */

/* Dirty bits: the lines to redraw. */
#define MFB_DIRTY_SEL     (1u << 0)   /* the old and the new selected entry */
#define MFB_DIRTY_STATUS  (1u << 1)   /* the countdown line */

/* Why the selected entry boots. */
#define MFB_VIA_NONE    0
#define MFB_VIA_KEY     1   /* ENTER */
#define MFB_VIA_TIMEOUT 2   /* the countdown ran out, or timeout = 0 */

typedef struct {
    unsigned count;         /* entries, 1 to 8 */
    unsigned sel;           /* selected entry, 0-based */
    unsigned drawn_sel;     /* the entry drawn as selected */
    int counting;           /* the countdown runs */
    int stopped;            /* a key stopped the countdown */
    uint32_t start_ms;      /* when the countdown started */
    uint32_t timeout_ms;    /* its length */
    unsigned left_shown;    /* the seconds the countdown line shows */
    unsigned dirty;         /* MFB_DIRTY_* since the last mfb_menu_take() */
    int via;                /* MFB_VIA_* of the last MFB_ACT_BOOT */
    int why;                /* MFB_WHY_* of the last MFB_ACT_MAINT */
} mfb_menu_t;

/*
 * Starts the menu on entry `def` (clamped to the entries) at time
 * `now_ms`. timeout_s < 0: no countdown, the menu waits for a key.
 * timeout_s 0: the next mfb_menu_tick() boots the default.
 */
void mfb_menu_init(mfb_menu_t *m, unsigned count, unsigned def, int timeout_s, uint32_t now_ms);

/*
 * One input event, through mfb_key_map(). Any key but MFB_K_NONE stops
 * the countdown. UP and DOWN move the selection and stop at the ends; a
 * held UP or DOWN repeats. A digit 1 to 8 selects that entry when it
 * exists. ENTER (SELECT) boots the selection. R or r reboots. M or m
 * opens the maintenance screen, and so does BACK from the buttons or
 * the maintenance port. BACK from the console, the ESC key, does not.
 * LEFT, RIGHT and releases do nothing else.
 */
int mfb_menu_event(mfb_menu_t *m, const input_event_t *ev);

/* The time passes: returns MFB_ACT_BOOT when the countdown runs out. */
int mfb_menu_tick(mfb_menu_t *m, uint32_t now_ms);

/* Whole seconds left, rounded up. 0 when no countdown runs. */
unsigned mfb_menu_left(const mfb_menu_t *m, uint32_t now_ms);

/*
 * How long the caller may wait for input before the next tick: up to the
 * next change of the seconds shown, at most 1000 ms, at least 1 ms.
 */
uint32_t mfb_menu_wait_ms(const mfb_menu_t *m, uint32_t now_ms);

/* After a failed boot: the menu stays, the countdown does not restart. */
void mfb_menu_stop(mfb_menu_t *m);

/*
 * Returns the dirty bits and clears them. With MFB_DIRTY_SEL, *old_sel
 * gets the entry drawn as selected until now, and the selection counts
 * as drawn.
 */
unsigned mfb_menu_take(mfb_menu_t *m, unsigned *old_sel);

/*
 * The port's maint command queues a NUL character from the script source
 * with value `seq`, its request number, 1 or more. No key has that form:
 * the `char` command refuses NUL and pushes value 0. `live` is the
 * number of the request still waiting, or 0 when none waits. So the
 * screen opens in queue order, and a request dropped earlier never
 * opens it.
 */
int mfb_port_maint(const input_event_t *ev, int32_t live);

#endif
