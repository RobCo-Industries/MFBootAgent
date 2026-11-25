/*
 * Input events to the keys the menu and the maintenance screen act on.
 * Pure code: the host tests link it.
 *
 * Every backend feeds the same queue: the console (ANSI keys and
 * characters), the GPIO buttons and the maintenance port's injected
 * events. This file turns one event into one logical key.
 */
#ifndef MFB_KEYMAP_H
#define MFB_KEYMAP_H

#include <robco/input.h>

#define MFB_K_NONE      0   /* nothing happens, the countdown goes on */
#define MFB_K_OTHER     1   /* no action, but it is a key: it stops the countdown */
#define MFB_K_UP        2
#define MFB_K_DOWN      3
#define MFB_K_SELECT    4   /* ENTER on the console, SELECT on the buttons */
#define MFB_K_BACK      5   /* BACK on the buttons, ESC on the console */
#define MFB_K_MAINT     6   /* M or m */
#define MFB_K_QUIT      7   /* Q or q */
#define MFB_K_REBOOT    8   /* R or r */
#define MFB_K_DIGIT1    9   /* 1 to 8: MFB_K_DIGIT1 + n - 1 */
#define MFB_K_DIGIT8    (MFB_K_DIGIT1 + 7)

/*
 * One event to one MFB_K_* key.
 *   KEY press:   UP, DOWN, SELECT and BACK map to their key. LEFT, RIGHT
 *                and every other code are MFB_K_OTHER.
 *   KEY repeat:  only UP and DOWN repeat. Others are MFB_K_NONE.
 *   KEY release: MFB_K_NONE.
 *   CHAR:        1 to 8, M, Q and R in either case map to their key.
 *                NUL is MFB_K_NONE: the stage pushes it to wake its loop.
 *                Any other character is MFB_K_OTHER.
 *   DIAL and anything else: MFB_K_NONE.
 */
int mfb_key_map(const input_event_t *ev);

/* Why the maintenance screen opened, for its marker. */
#define MFB_WHY_KEY     0   /* M on the console */
#define MFB_WHY_BUTTON  1   /* BACK on the buttons, or a BACK the port injected */
#define MFB_WHY_PORT    2   /* the maint command on the maintenance port */
#define MFB_WHY_FLAG    3   /* boot_flags bit 1 arrived set */

/* "key", "button", "port" or "flag". "unknown" for other values. */
const char *mfb_why_name(int why);

/*
 * The start wait for the GPIO guard. The guard reports held buttons as
 * presses in the tick where it ends, so the wait must leave the queue as
 * it is: the menu takes those events.
 *   checking: non-zero while the guard runs.
 *   now_ms:   the system timer.
 *   nap:      waits a short time and takes nothing.
 */
typedef struct {
    int (*checking)(void *ctx);
    uint32_t (*now_ms)(void *ctx);
    void (*nap)(void *ctx);
    void *ctx;
} mfb_guard_io_t;

/* Waits while the guard runs, for at most `limit_ms`. */
void mfb_guard_wait(const mfb_guard_io_t *io, uint32_t limit_ms);

#endif
