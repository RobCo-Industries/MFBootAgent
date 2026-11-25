/*
 * The key rule of a screen that opens at start, before anyone could see
 * it: the agent's maintenance screen opened by a request or a received
 * boot_flags bit 1, and the menu of MAINT.SYS. Pure code: the host tests
 * link it, and both images build it.
 */
#ifndef MFB_STARTKEYS_H
#define MFB_STARTKEYS_H

#include <stdint.h>

#include <robco/input.h>

/*
 * The keys the screen opened at start acts on. Before it opens, the
 * main loop has not polled input, so the serial console and the
 * maintenance port have queued nothing: every event already queued came
 * from the GPIO buttons. That is the guard's press for a button held at
 * power-on, its release and repeats, or a button pressed while the card
 * mounted. Nobody could see the screen yet, so none of these is a
 * choice on it. The screen drops them, whatever opened it, a request or
 * a received bit 1. MAINT.SYS applies the same rule to its menu: a
 * button held through the agent comes again as a press from its own guard.
 *
 * mfb_start_keys_init() runs the moment the screen opens. `pending` is
 * input_pending() then, `held` the mask of the keys down then, bit
 * (1 << INPUT_KEY_*). The queue is first in, first out, so the next
 * `pending` events are the ones queued before the screen opened.
 */
typedef struct {
    unsigned pre;       /* events left that were queued before the screen opened */
    uint32_t stale;     /* GPIO keys held since before it opened, bit (1 << code) */
} mfb_start_keys_t;

void mfb_start_keys_init(mfb_start_keys_t *k, unsigned pending, uint32_t held);

/*
 * 1 when the screen acts on `ev`, 0 when it drops it. Dropped: a GPIO
 * key event queued before the screen opened, and the repeats and the
 * release of a GPIO key held since then. The first press of that key
 * after its release acts. Console and port events always act: they are
 * keys someone sent on purpose, one at a time.
 */
int mfb_start_keys_take(mfb_start_keys_t *k, const input_event_t *ev);

#endif
