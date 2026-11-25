#include "startkeys.h"

void mfb_start_keys_init(mfb_start_keys_t *k, unsigned pending, uint32_t held)
{
    k->pre = pending;
    k->stale = held;
}

int mfb_start_keys_take(mfb_start_keys_t *k, const input_event_t *ev)
{
    int pre = k->pre != 0;
    uint32_t bit;

    if (pre) {
        k->pre--;
    }
    if (ev->type != INPUT_EV_KEY || ev->source != INPUT_SRC_GPIO || ev->code >= 32) {
        return 1;
    }
    bit = (uint32_t)1 << ev->code;
    if (pre) {
        if (ev->value == INPUT_RELEASE) {
            k->stale &= ~bit;
        } else {
            k->stale |= bit;
        }
        return 0;
    }
    if ((k->stale & bit) == 0) {
        return 1;
    }
    if (ev->value == INPUT_PRESS) {
        /* A release was lost to a full queue: a new press is a new key. */
        k->stale &= ~bit;
        return 1;
    }
    if (ev->value == INPUT_RELEASE) {
        k->stale &= ~bit;
    }
    return 0;
}
