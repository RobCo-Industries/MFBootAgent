/* The diagnostics menu state machine and the live view exit rule. */

#include "dg_menu.h"
#include "portreq.h"
#include "rc_test.h"

static dg_menu_t m;
static unsigned item;

static int ev_key(unsigned code, int value)
{
    input_event_t ev = { 0 };

    ev.type = INPUT_EV_KEY;
    ev.code = (uint16_t)code;
    ev.value = value;
    item = 99;
    return dg_menu_event(&m, &ev, &item);
}

static int ev_char(char c)
{
    input_event_t ev = { 0 };

    ev.type = INPUT_EV_CHAR;
    ev.code = (uint8_t)c;
    item = 99;
    return dg_menu_event(&m, &ev, &item);
}

TEST(moves_stop_at_the_ends)
{
    unsigned old = 99;
    unsigned i;

    dg_menu_init(&m);
    CHECK_EQ(m.sel, 0);
    CHECK_EQ(ev_key(INPUT_KEY_UP, INPUT_PRESS), DG_ACT_NONE);
    CHECK_EQ(m.sel, 0);
    CHECK_EQ(dg_menu_take(&m, &old), 0);
    CHECK_EQ(ev_key(INPUT_KEY_DOWN, INPUT_PRESS), DG_ACT_NONE);
    CHECK_EQ(dg_menu_take(&m, &old), DG_DIRTY_SEL);
    CHECK_EQ(old, 0);
    for (i = 0; i < 20; i++) {
        ev_key(INPUT_KEY_DOWN, INPUT_REPEAT);
    }
    CHECK_EQ(m.sel, DG_ITEMS - 1);
    /* Several moves before a redraw: the old row is the one drawn last. */
    CHECK_EQ(dg_menu_take(&m, &old), DG_DIRTY_SEL);
    CHECK_EQ(old, 1);
    CHECK_EQ(ev_key(INPUT_KEY_DOWN, INPUT_RELEASE), DG_ACT_NONE);
    CHECK_EQ(ev_key(INPUT_KEY_LEFT, INPUT_PRESS), DG_ACT_NONE);
    CHECK_EQ(dg_menu_take(&m, NULL), 0);
}

TEST(enter_starts_the_row)
{
    dg_menu_init(&m);
    CHECK_EQ(ev_key(INPUT_KEY_SELECT, INPUT_PRESS), DG_ACT_RUN);
    CHECK_EQ(item, DG_T_MEM);
    CHECK_EQ(ev_key(INPUT_KEY_SELECT, INPUT_REPEAT), DG_ACT_NONE);
    CHECK_EQ(ev_key(INPUT_KEY_SELECT, INPUT_RELEASE), DG_ACT_NONE);
    ev_key(INPUT_KEY_DOWN, INPUT_PRESS);
    CHECK_EQ(ev_key(INPUT_KEY_SELECT, INPUT_PRESS), DG_ACT_RUN);
    CHECK_EQ(item, DG_T_SD);
    m.sel = DG_ITEM_ALL;
    CHECK_EQ(ev_key(INPUT_KEY_SELECT, INPUT_PRESS), DG_ACT_RUN);
    CHECK_EQ(item, DG_ITEM_ALL);
    m.sel = DG_ITEM_REBOOT;
    CHECK_EQ(ev_key(INPUT_KEY_SELECT, INPUT_PRESS), DG_ACT_REBOOT);
    CHECK_EQ(item, 99);
}

TEST(shortcut_characters)
{
    unsigned c;

    dg_menu_init(&m);
    for (c = '1'; c <= '9'; c++) {
        CHECK_EQ(ev_char((char)c), DG_ACT_RUN);
        CHECK_EQ(item, c - '1');
        CHECK_EQ(m.sel, c - '1');
    }
    CHECK_EQ(ev_char('a'), DG_ACT_RUN);
    CHECK_EQ(item, DG_ITEM_ALL);
    CHECK_EQ(ev_char('A'), DG_ACT_RUN);
    CHECK_EQ(item, DG_ITEM_ALL);
    CHECK_EQ(ev_char('r'), DG_ACT_REBOOT);
    CHECK_EQ(m.sel, DG_ITEM_REBOOT);
    CHECK_EQ(ev_char('R'), DG_ACT_REBOOT);
    CHECK_EQ(ev_char('0'), DG_ACT_NONE);
    CHECK_EQ(ev_char('x'), DG_ACT_NONE);
    CHECK_EQ(dg_menu_event(NULL, NULL, NULL), DG_ACT_NONE);
    CHECK_EQ(dg_menu_event(&m, NULL, &item), DG_ACT_NONE);
}

TEST(test_names)
{
    CHECK_STR(dg_test_name(DG_T_MEM), "mem");
    CHECK_STR(dg_test_name(DG_T_EXPANDER), "expander");
    CHECK(dg_test_name(DG_TESTS) == NULL);
    CHECK_EQ(dg_test_find("mem"), DG_T_MEM);
    CHECK_EQ(dg_test_find("sd"), DG_T_SD);
    CHECK_EQ(dg_test_find("video"), DG_T_VIDEO);
    CHECK_EQ(dg_test_find("audio"), DG_T_AUDIO);
    CHECK_EQ(dg_test_find("input"), DG_T_INPUT);
    CHECK_EQ(dg_test_find("timer"), DG_T_TIMER);
    CHECK_EQ(dg_test_find("rng"), DG_T_RNG);
    CHECK_EQ(dg_test_find("rtc"), DG_T_RTC);
    CHECK_EQ(dg_test_find("expander"), DG_T_EXPANDER);
    CHECK_EQ(dg_test_find("all"), DG_ITEM_ALL);
    CHECK_EQ(dg_test_find("reboot"), -1);
    CHECK_EQ(dg_test_find("MEM"), -1);
    CHECK_EQ(dg_test_find(""), -1);
    CHECK_EQ(dg_test_find(NULL), -1);
    CHECK(dg_test_interactive(DG_T_INPUT));
    CHECK(!dg_test_interactive(DG_T_EXPANDER));
    CHECK(!dg_test_interactive(DG_T_VIDEO));
}

TEST(abort_is_a_back_press)
{
    input_event_t ev = { 0 };

    ev.type = INPUT_EV_KEY;
    ev.code = INPUT_KEY_BACK;
    ev.value = INPUT_PRESS;
    CHECK(dg_is_abort(&ev));
    ev.value = INPUT_RELEASE;
    CHECK(!dg_is_abort(&ev));
    ev.value = INPUT_PRESS;
    ev.code = INPUT_KEY_SELECT;
    CHECK(!dg_is_abort(&ev));
    ev.type = INPUT_EV_CHAR;
    ev.code = 0x1b;
    CHECK(!dg_is_abort(&ev));
    CHECK(!dg_is_abort(NULL));
}

static int live_key(dg_live_t *l, unsigned code, int value, uint32_t t)
{
    input_event_t ev = { 0 };

    ev.type = INPUT_EV_KEY;
    ev.code = (uint16_t)code;
    ev.value = value;
    return dg_live_event(l, &ev, t);
}

TEST(live_view_exits)
{
    dg_live_t l;

    /* BACK twice within a second. */
    dg_live_init(&l, 1000, 20000);
    CHECK_EQ(live_key(&l, INPUT_KEY_BACK, INPUT_PRESS, 1000), DG_LIVE_STAY);
    CHECK_EQ(live_key(&l, INPUT_KEY_BACK, INPUT_RELEASE, 1001), DG_LIVE_STAY);
    CHECK_EQ(dg_live_poll(&l, 1900), DG_LIVE_STAY);
    CHECK_EQ(live_key(&l, INPUT_KEY_BACK, INPUT_PRESS, 2000), DG_LIVE_DOUBLE);
    /* Too slow: the second press starts again. */
    dg_live_init(&l, 0, 20000);
    live_key(&l, INPUT_KEY_BACK, INPUT_PRESS, 100);
    live_key(&l, INPUT_KEY_BACK, INPUT_RELEASE, 150);
    CHECK_EQ(live_key(&l, INPUT_KEY_BACK, INPUT_PRESS, 1101), DG_LIVE_STAY);
    live_key(&l, INPUT_KEY_BACK, INPUT_RELEASE, 1150);
    CHECK_EQ(live_key(&l, INPUT_KEY_BACK, INPUT_PRESS, 2100), DG_LIVE_DOUBLE);
    /* Other keys between do not count as BACK. */
    dg_live_init(&l, 0, 20000);
    live_key(&l, INPUT_KEY_UP, INPUT_PRESS, 10);
    CHECK_EQ(live_key(&l, INPUT_KEY_BACK, INPUT_PRESS, 20), DG_LIVE_STAY);
    /* BACK held. */
    dg_live_init(&l, 0, 20000);
    live_key(&l, INPUT_KEY_BACK, INPUT_PRESS, 500);
    CHECK_EQ(dg_live_poll(&l, 500 + DG_LIVE_LONG_MS - 1), DG_LIVE_STAY);
    CHECK_EQ(dg_live_poll(&l, 500 + DG_LIVE_LONG_MS), DG_LIVE_LONG);
    live_key(&l, INPUT_KEY_BACK, INPUT_RELEASE, 600);
    CHECK_EQ(dg_live_poll(&l, 500 + DG_LIVE_LONG_MS), DG_LIVE_STAY);
    /* Idle, across the timer wrap. */
    dg_live_init(&l, 0xFFFFF000u, 20000);
    CHECK_EQ(dg_live_poll(&l, 0xFFFFF000u + 19999u), DG_LIVE_STAY);
    CHECK_EQ(dg_live_poll(&l, 0xFFFFF000u + 20000u), DG_LIVE_IDLE);
    live_key(&l, INPUT_KEY_UP, INPUT_PRESS, 0xFFFFF000u + 10000u);
    CHECK_EQ(dg_live_poll(&l, 0xFFFFF000u + 20000u), DG_LIVE_STAY);
    /* No idle bound with 0. */
    dg_live_init(&l, 0, 0);
    CHECK_EQ(dg_live_poll(&l, 0x7FFFFFFFu), DG_LIVE_STAY);
    CHECK_EQ(dg_live_poll(NULL, 0), DG_LIVE_STAY);
    CHECK_EQ(dg_live_event(NULL, NULL, 0), DG_LIVE_STAY);
    CHECK_STR(dg_live_why(DG_LIVE_DOUBLE), "back2");
    CHECK_STR(dg_live_why(DG_LIVE_LONG), "long");
    CHECK_STR(dg_live_why(DG_LIVE_IDLE), "idle");
    CHECK_STR(dg_live_why(DG_LIVE_STAY), "none");
}

TEST(port_diag_request_in_queue_order)
{
    input_event_t req = { 0 };
    input_event_t sel = { 0 };

    req.type = INPUT_EV_CHAR;
    req.source = INPUT_SRC_SCRIPT;
    req.value = 1;
    sel.type = INPUT_EV_KEY;
    sel.source = INPUT_SRC_SCRIPT;
    sel.code = INPUT_KEY_SELECT;
    sel.value = INPUT_PRESS;
    /* diag then SELECT: the request comes up live and runs first. */
    CHECK_EQ(mfb_port_req(&req, 1), MFB_PORT_LIVE);
    /* SELECT then diag: SELECT is a key, its run drops the request, live becomes 0. */
    CHECK_EQ(mfb_port_req(&sel, 1), MFB_PORT_NONE);
    CHECK_EQ(mfb_port_req(&req, 0), MFB_PORT_STALE);
    /* A newer request does not revive the dropped one. */
    CHECK_EQ(mfb_port_req(&req, 2), MFB_PORT_STALE);
    /* The marked event is no key for the menu: it neither moves nor starts a row. */
    dg_menu_init(&m);
    item = 99;
    CHECK_EQ(dg_menu_event(&m, &req, &item), DG_ACT_NONE);
    CHECK_EQ(m.sel, 0u);
    CHECK_EQ(item, 99u);
}
