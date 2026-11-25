/* The menu state machine and the live view exit rule. Pure: see dg_menu.h. */

#include <stddef.h>

#include <robco/klibc.h>

#include "dg_menu.h"

static const char *const names[DG_TESTS] = {
    "mem", "sd", "video", "audio", "input", "timer", "rng", "rtc", "expander",
};

void dg_menu_init(dg_menu_t *m)
{
    m->sel = 0;
    m->old = 0;
    m->dirty = 0;
}

static void select_row(dg_menu_t *m, unsigned row)
{
    if (row == m->sel) {
        return;
    }
    if ((m->dirty & DG_DIRTY_SEL) == 0) {
        m->old = m->sel;
    }
    m->sel = row;
    m->dirty |= DG_DIRTY_SEL;
}

static int start(dg_menu_t *m, unsigned *item)
{
    if (m->sel == DG_ITEM_REBOOT) {
        return DG_ACT_REBOOT;
    }
    if (item != NULL) {
        *item = m->sel;
    }
    return DG_ACT_RUN;
}

int dg_menu_event(dg_menu_t *m, const input_event_t *ev, unsigned *item)
{
    if (m == NULL || ev == NULL) {
        return DG_ACT_NONE;
    }
    if (ev->type == INPUT_EV_KEY) {
        if (ev->value == INPUT_RELEASE) {
            return DG_ACT_NONE;
        }
        switch (ev->code) {
        case INPUT_KEY_UP:
            if (m->sel > 0) {
                select_row(m, m->sel - 1u);
            }
            return DG_ACT_NONE;
        case INPUT_KEY_DOWN:
            if (m->sel + 1u < DG_ITEMS) {
                select_row(m, m->sel + 1u);
            }
            return DG_ACT_NONE;
        case INPUT_KEY_SELECT:
            /* A held SELECT must not start the row again and again. */
            return ev->value == INPUT_PRESS ? start(m, item) : DG_ACT_NONE;
        default:
            return DG_ACT_NONE;
        }
    }
    if (ev->type == INPUT_EV_CHAR) {
        unsigned c = ev->code;

        if (c >= '1' && c <= '9') {
            select_row(m, c - '1');
            return start(m, item);
        }
        if (c == 'a' || c == 'A') {
            select_row(m, DG_ITEM_ALL);
            return start(m, item);
        }
        if (c == 'r' || c == 'R') {
            select_row(m, DG_ITEM_REBOOT);
            return start(m, item);
        }
    }
    return DG_ACT_NONE;
}

unsigned dg_menu_take(dg_menu_t *m, unsigned *old)
{
    unsigned d = m->dirty;

    if (old != NULL) {
        *old = m->old;
    }
    m->dirty = 0;
    m->old = m->sel;
    return d;
}

const char *dg_test_name(unsigned test)
{
    return test < DG_TESTS ? names[test] : NULL;
}

int dg_test_find(const char *name)
{
    unsigned i;

    if (name == NULL) {
        return -1;
    }
    if (k_strcmp(name, "all") == 0) {
        return DG_ITEM_ALL;
    }
    for (i = 0; i < DG_TESTS; i++) {
        if (k_strcmp(name, names[i]) == 0) {
            return (int)i;
        }
    }
    return -1;
}

int dg_test_interactive(unsigned test)
{
    return test == DG_T_INPUT;
}

int dg_is_abort(const input_event_t *ev)
{
    return ev != NULL && ev->type == INPUT_EV_KEY && ev->code == INPUT_KEY_BACK &&
           ev->value == INPUT_PRESS;
}

void dg_live_init(dg_live_t *l, uint32_t now_ms, uint32_t idle_ms)
{
    l->idle_ms = idle_ms;
    l->last_ms = now_ms;
    l->back_ms = 0;
    l->back_seen = 0;
    l->back_down = 0;
}

int dg_live_event(dg_live_t *l, const input_event_t *ev, uint32_t now_ms)
{
    if (l == NULL || ev == NULL) {
        return DG_LIVE_STAY;
    }
    l->last_ms = now_ms;
    if (ev->type != INPUT_EV_KEY || ev->code != INPUT_KEY_BACK) {
        return DG_LIVE_STAY;
    }
    if (ev->value == INPUT_RELEASE) {
        l->back_down = 0;
        return DG_LIVE_STAY;
    }
    if (ev->value != INPUT_PRESS) {
        return DG_LIVE_STAY;
    }
    if (l->back_seen && now_ms - l->back_ms <= DG_LIVE_DOUBLE_MS) {
        return DG_LIVE_DOUBLE;
    }
    l->back_seen = 1;
    l->back_down = 1;
    l->back_ms = now_ms;
    return DG_LIVE_STAY;
}

int dg_live_poll(const dg_live_t *l, uint32_t now_ms)
{
    if (l == NULL) {
        return DG_LIVE_STAY;
    }
    if (l->back_down && now_ms - l->back_ms >= DG_LIVE_LONG_MS) {
        return DG_LIVE_LONG;
    }
    if (l->idle_ms != 0 && now_ms - l->last_ms >= l->idle_ms) {
        return DG_LIVE_IDLE;
    }
    return DG_LIVE_STAY;
}

const char *dg_live_why(int code)
{
    switch (code) {
    case DG_LIVE_DOUBLE: return "back2";
    case DG_LIVE_LONG:   return "long";
    case DG_LIVE_IDLE:   return "idle";
    default:             return "none";
    }
}
