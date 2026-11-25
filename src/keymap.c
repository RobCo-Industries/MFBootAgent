#include "keymap.h"

static int map_key(unsigned code, int value)
{
    if (value == INPUT_RELEASE) {
        return MFB_K_NONE;
    }
    if (value == INPUT_REPEAT) {
        /* A held key repeats only for moving the bar. */
        if (code == INPUT_KEY_UP) {
            return MFB_K_UP;
        }
        return code == INPUT_KEY_DOWN ? MFB_K_DOWN : MFB_K_NONE;
    }
    if (value != INPUT_PRESS) {
        return MFB_K_NONE;
    }
    switch (code) {
    case INPUT_KEY_UP:     return MFB_K_UP;
    case INPUT_KEY_DOWN:   return MFB_K_DOWN;
    case INPUT_KEY_SELECT: return MFB_K_SELECT;
    case INPUT_KEY_BACK:   return MFB_K_BACK;
    default:               return MFB_K_OTHER;
    }
}

static int map_char(unsigned c)
{
    if (c == 0) {
        return MFB_K_NONE;
    }
    if (c >= '1' && c <= '8') {
        return MFB_K_DIGIT1 + (int)(c - '1');
    }
    switch (c) {
    case 'm': case 'M': return MFB_K_MAINT;
    case 'q': case 'Q': return MFB_K_QUIT;
    case 'r': case 'R': return MFB_K_REBOOT;
    default:            return MFB_K_OTHER;
    }
}

int mfb_key_map(const input_event_t *ev)
{
    if (ev->type == INPUT_EV_KEY) {
        return map_key(ev->code, ev->value);
    }
    if (ev->type == INPUT_EV_CHAR) {
        return map_char(ev->code);
    }
    return MFB_K_NONE;
}

const char *mfb_why_name(int why)
{
    switch (why) {
    case MFB_WHY_KEY:    return "key";
    case MFB_WHY_BUTTON: return "button";
    case MFB_WHY_PORT:   return "port";
    case MFB_WHY_FLAG:   return "flag";
    case MFB_WHY_REQUEST: return "request";
    default:             return "unknown";
    }
}

void mfb_guard_wait(const mfb_guard_io_t *io, uint32_t limit_ms)
{
    uint32_t t0 = io->now_ms(io->ctx);

    /* It never takes an event: the menu loop takes them all, in order. */
    while (io->checking(io->ctx) && io->now_ms(io->ctx) - t0 < limit_ms) {
        io->nap(io->ctx);
    }
}
