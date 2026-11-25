#include "menu.h"

#include "keymap.h"

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
    m->why = MFB_WHY_KEY;
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
    int k = mfb_key_map(ev);

    if (k == MFB_K_NONE) {
        return MFB_ACT_NONE;
    }
    stop_countdown(m);
    if (k >= MFB_K_DIGIT1 && k <= MFB_K_DIGIT8) {
        unsigned n = (unsigned)(k - MFB_K_DIGIT1);

        if (n < m->count) {
            select_entry(m, n);
        }
        return MFB_ACT_NONE;
    }
    switch (k) {
    case MFB_K_UP:
        if (m->sel > 0) {
            select_entry(m, m->sel - 1);
        }
        return MFB_ACT_NONE;
    case MFB_K_DOWN:
        if (m->sel + 1 < m->count) {
            select_entry(m, m->sel + 1);
        }
        return MFB_ACT_NONE;
    case MFB_K_SELECT:
        m->via = MFB_VIA_KEY;
        return MFB_ACT_BOOT;
    case MFB_K_REBOOT:
        return MFB_ACT_REBOOT;
    case MFB_K_MAINT:
        m->why = MFB_WHY_KEY;
        return MFB_ACT_MAINT;
    case MFB_K_BACK:
        if (ev->source == INPUT_SRC_SERIAL) {
            return MFB_ACT_NONE;
        }
        m->why = MFB_WHY_BUTTON;
        return MFB_ACT_MAINT;
    default:
        return MFB_ACT_NONE;
    }
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

int mfb_port_maint(const input_event_t *ev, int32_t live)
{
    if (ev->type != INPUT_EV_CHAR || ev->source != INPUT_SRC_SCRIPT || ev->code != 0 ||
        ev->value <= 0) {
        return MFB_PORT_NONE;
    }
    return live != 0 && ev->value == live ? MFB_PORT_OPEN : MFB_PORT_STALE;
}
