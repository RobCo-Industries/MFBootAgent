#include "menu.h"

void mfb_menu_init(mfb_menu_t *m, unsigned count, unsigned def, int timeout_s, uint32_t now_ms)
{
    m->count = count != 0 ? count : 1;
    m->sel = def < m->count ? def : 0;
    m->drawn_sel = m->sel;
    m->counting = timeout_s >= 0;
    m->stopped = 0;
    m->start_ms = now_ms;
    m->timeout_ms = timeout_s > 0 ? (uint32_t)timeout_s * 1000u : 0;
    m->left_shown = mfb_menu_left(m, now_ms);
    m->dirty = 0;
    m->via = MFB_VIA_NONE;
}

static void stop_countdown(mfb_menu_t *m)
{
    if (m->counting) {
        m->counting = 0;
        m->stopped = 1;
        m->left_shown = 0;
        m->dirty |= MFB_DIRTY_STATUS;
    }
}

static void select_entry(mfb_menu_t *m, unsigned sel)
{
    if (sel != m->sel) {
        m->sel = sel;
        m->dirty |= MFB_DIRTY_SEL;
    }
}

int mfb_menu_event(mfb_menu_t *m, const input_event_t *ev)
{
    if (ev->type == INPUT_EV_KEY) {
        if (ev->value == INPUT_RELEASE) {
            return MFB_ACT_NONE;
        }
        stop_countdown(m);
        switch (ev->code) {
        case INPUT_KEY_UP:
            if (m->sel > 0) {
                select_entry(m, m->sel - 1);
            }
            return MFB_ACT_NONE;
        case INPUT_KEY_DOWN:
            if (m->sel + 1 < m->count) {
                select_entry(m, m->sel + 1);
            }
            return MFB_ACT_NONE;
        case INPUT_KEY_SELECT:
            /* A held ENTER repeats: only the press boots. */
            if (ev->value != INPUT_PRESS) {
                return MFB_ACT_NONE;
            }
            m->via = MFB_VIA_KEY;
            return MFB_ACT_BOOT;
        default:
            return MFB_ACT_NONE;
        }
    }
    if (ev->type == INPUT_EV_CHAR) {
        stop_countdown(m);
        if (ev->code >= '1' && ev->code <= '8') {
            unsigned n = (unsigned)(ev->code - '1');

            if (n < m->count) {
                select_entry(m, n);
            }
            return MFB_ACT_NONE;
        }
        if (ev->code == 'r' || ev->code == 'R') {
            return MFB_ACT_REBOOT;
        }
    }
    return MFB_ACT_NONE;
}

unsigned mfb_menu_left(const mfb_menu_t *m, uint32_t now_ms)
{
    uint32_t elapsed = now_ms - m->start_ms;

    if (!m->counting || elapsed >= m->timeout_ms) {
        return 0;
    }
    return (unsigned)((m->timeout_ms - elapsed + 999u) / 1000u);
}

int mfb_menu_tick(mfb_menu_t *m, uint32_t now_ms)
{
    unsigned left;

    if (!m->counting) {
        return MFB_ACT_NONE;
    }
    if (now_ms - m->start_ms >= m->timeout_ms) {
        m->counting = 0;
        m->left_shown = 0;
        m->dirty |= MFB_DIRTY_STATUS;
        m->via = MFB_VIA_TIMEOUT;
        return MFB_ACT_BOOT;
    }
    left = mfb_menu_left(m, now_ms);
    if (left != m->left_shown) {
        m->left_shown = left;
        m->dirty |= MFB_DIRTY_STATUS;
    }
    return MFB_ACT_NONE;
}

uint32_t mfb_menu_wait_ms(const mfb_menu_t *m, uint32_t now_ms)
{
    uint32_t elapsed = now_ms - m->start_ms;
    uint32_t rem;
    uint32_t next;

    if (!m->counting) {
        return 1000u;
    }
    if (elapsed >= m->timeout_ms) {
        return 1u;
    }
    rem = m->timeout_ms - elapsed;
    next = rem % 1000u;
    if (next == 0) {
        next = 1000u;
    }
    return next;
}

void mfb_menu_stop(mfb_menu_t *m)
{
    stop_countdown(m);
}

unsigned mfb_menu_take(mfb_menu_t *m, unsigned *old_sel)
{
    unsigned d = m->dirty;

    m->dirty = 0;
    if (d & MFB_DIRTY_SEL) {
        if (old_sel != 0) {
            *old_sel = m->drawn_sel;
        }
        m->drawn_sel = m->sel;
    }
    return d;
}
