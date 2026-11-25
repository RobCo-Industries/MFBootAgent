/* The menu state machine: every key in every state, the countdown and the time wrap. */

#include "menu.h"
#include "portreq.h"
#include "rc_test.h"

static mfb_menu_t m;

static input_event_t key(unsigned code, int value)
{
    input_event_t ev = { 0 };

    ev.type = INPUT_EV_KEY;
    ev.code = (uint16_t)code;
    ev.value = value;
    return ev;
}

static input_event_t chr(char c)
{
    input_event_t ev = { 0 };

    ev.type = INPUT_EV_CHAR;
    ev.code = (uint8_t)c;
    return ev;
}

static int press(unsigned code)
{
    input_event_t ev = key(code, INPUT_PRESS);

    return mfb_menu_event(&m, &ev);
}

static int type(char c)
{
    input_event_t ev = chr(c);

    return mfb_menu_event(&m, &ev);
}

TEST(init_clamps_and_waits)
{
    mfb_menu_init(&m, 3, 7, -1, 0);
    CHECK_EQ(m.sel, 0);
    CHECK_EQ(m.counting, 0);
    CHECK_EQ(mfb_menu_left(&m, 5000), 0);
    CHECK_EQ(mfb_menu_tick(&m, 0xFFFFFFFFu), MFB_ACT_NONE);
    CHECK_EQ(mfb_menu_wait_ms(&m, 0), 1000);
    mfb_menu_init(&m, 3, 2, -1, 0);
    CHECK_EQ(m.sel, 2);
    CHECK_EQ(mfb_menu_take(&m, NULL), 0);
}

TEST(up_and_down_stop_at_the_ends)
{
    unsigned old = 99;

    mfb_menu_init(&m, 3, 0, -1, 0);
    CHECK_EQ(press(INPUT_KEY_UP), MFB_ACT_NONE);
    CHECK_EQ(m.sel, 0);
    CHECK_EQ(mfb_menu_take(&m, &old), 0);
    CHECK_EQ(press(INPUT_KEY_DOWN), MFB_ACT_NONE);
    CHECK_EQ(m.sel, 1);
    CHECK_EQ(mfb_menu_take(&m, &old), MFB_DIRTY_SEL);
    CHECK_EQ(old, 0);
    press(INPUT_KEY_DOWN);
    press(INPUT_KEY_DOWN);
    CHECK_EQ(m.sel, 2);
    /* Two moves before a redraw: the old line is the one drawn. */
    CHECK_EQ(mfb_menu_take(&m, &old), MFB_DIRTY_SEL);
    CHECK_EQ(old, 1);
    press(INPUT_KEY_UP);
    CHECK_EQ(m.sel, 1);
    mfb_menu_take(&m, &old);
    CHECK_EQ(old, 2);
}

TEST(repeat_moves_release_does_nothing)
{
    input_event_t rep = key(INPUT_KEY_DOWN, INPUT_REPEAT);
    input_event_t rel = key(INPUT_KEY_DOWN, INPUT_RELEASE);

    mfb_menu_init(&m, 3, 0, 5, 0);
    CHECK_EQ(mfb_menu_event(&m, &rel), MFB_ACT_NONE);
    CHECK_EQ(m.sel, 0);
    CHECK_EQ(m.counting, 1);
    CHECK_EQ(mfb_menu_event(&m, &rep), MFB_ACT_NONE);
    CHECK_EQ(m.sel, 1);
    CHECK_EQ(m.counting, 0);
}

TEST(enter_boots_the_selection)
{
    input_event_t rep = key(INPUT_KEY_SELECT, INPUT_REPEAT);

    mfb_menu_init(&m, 3, 1, -1, 0);
    CHECK_EQ(mfb_menu_event(&m, &rep), MFB_ACT_NONE);
    CHECK_EQ(press(INPUT_KEY_SELECT), MFB_ACT_BOOT);
    CHECK_EQ(m.sel, 1);
    CHECK_EQ(m.via, MFB_VIA_KEY);
}

TEST(digits_select)
{
    unsigned i;

    mfb_menu_init(&m, 3, 0, -1, 0);
    CHECK_EQ(type('3'), MFB_ACT_NONE);
    CHECK_EQ(m.sel, 2);
    CHECK_EQ(type('1'), MFB_ACT_NONE);
    CHECK_EQ(m.sel, 0);
    /* Beyond the entries, and 0 and 9: no change. */
    CHECK_EQ(type('4'), MFB_ACT_NONE);
    CHECK_EQ(type('8'), MFB_ACT_NONE);
    CHECK_EQ(type('9'), MFB_ACT_NONE);
    CHECK_EQ(type('0'), MFB_ACT_NONE);
    CHECK_EQ(m.sel, 0);
    mfb_menu_init(&m, 8, 0, -1, 0);
    for (i = 0; i < 8; i++) {
        type((char)('1' + i));
        CHECK_EQ(m.sel, i);
    }
}

TEST(r_reboots_in_every_state)
{
    mfb_menu_init(&m, 2, 0, -1, 0);
    CHECK_EQ(type('r'), MFB_ACT_REBOOT);
    CHECK_EQ(type('R'), MFB_ACT_REBOOT);
    mfb_menu_init(&m, 2, 0, 5, 0);
    CHECK_EQ(type('R'), MFB_ACT_REBOOT);
    CHECK_EQ(m.counting, 0);
}

TEST(other_keys_only_stop_the_countdown)
{
    static const unsigned keys[] = { INPUT_KEY_LEFT, INPUT_KEY_RIGHT,
                                     INPUT_KEY_F1, INPUT_KEY_F4, INPUT_KEY_DIAL_PUSH };
    unsigned i;

    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        mfb_menu_init(&m, 3, 1, 5, 0);
        CHECK_EQ(press(keys[i]), MFB_ACT_NONE);
        CHECK_EQ(m.sel, 1);
        CHECK_EQ(m.counting, 0);
        CHECK_EQ(m.stopped, 1);
        CHECK_EQ(mfb_menu_take(&m, NULL), MFB_DIRTY_STATUS);
    }
    mfb_menu_init(&m, 3, 1, 5, 0);
    CHECK_EQ(type('x'), MFB_ACT_NONE);
    CHECK_EQ(m.counting, 0);
    CHECK_EQ(m.sel, 1);
    /* A dial turn is no key. */
    {
        input_event_t dial = { 0 };

        dial.type = INPUT_EV_DIAL;
        dial.value = 1;
        mfb_menu_init(&m, 3, 1, 5, 0);
        CHECK_EQ(mfb_menu_event(&m, &dial), MFB_ACT_NONE);
        CHECK_EQ(m.counting, 1);
    }
}

TEST(maintenance_keys)
{
    input_event_t ev;

    /* M or m from any backend, BACK from the buttons or the port. */
    mfb_menu_init(&m, 3, 1, 5, 0);
    CHECK_EQ(type('m'), MFB_ACT_MAINT);
    CHECK_EQ(m.why, MFB_WHY_KEY);
    CHECK_EQ(m.counting, 0);
    CHECK_EQ(m.stopped, 1);
    CHECK_EQ(type('M'), MFB_ACT_MAINT);
    mfb_menu_init(&m, 3, 1, 5, 0);
    ev = key(INPUT_KEY_BACK, INPUT_PRESS);
    ev.source = INPUT_SRC_GPIO;
    CHECK_EQ(mfb_menu_event(&m, &ev), MFB_ACT_MAINT);
    CHECK_EQ(m.why, MFB_WHY_BUTTON);
    CHECK_EQ(m.counting, 0);
    ev.source = INPUT_SRC_SCRIPT;
    m.why = MFB_WHY_KEY;
    CHECK_EQ(mfb_menu_event(&m, &ev), MFB_ACT_MAINT);
    CHECK_EQ(m.why, MFB_WHY_BUTTON);
    /* ESC on the console is BACK from the serial source: it only stops the countdown. */
    mfb_menu_init(&m, 3, 1, 5, 0);
    ev.source = INPUT_SRC_SERIAL;
    CHECK_EQ(mfb_menu_event(&m, &ev), MFB_ACT_NONE);
    CHECK_EQ(m.counting, 0);
    CHECK_EQ(m.sel, 1);
    /* A held BACK does not open it again. */
    mfb_menu_init(&m, 3, 1, -1, 0);
    ev = key(INPUT_KEY_BACK, INPUT_REPEAT);
    CHECK_EQ(mfb_menu_event(&m, &ev), MFB_ACT_NONE);
    /* The wake-up NUL is no key: the countdown goes on. */
    mfb_menu_init(&m, 3, 1, 5, 0);
    CHECK_EQ(type('\0'), MFB_ACT_NONE);
    CHECK_EQ(m.counting, 1);
}

TEST(countdown_to_zero)
{
    unsigned t;

    mfb_menu_init(&m, 3, 2, 3, 1000);
    CHECK_EQ(m.counting, 1);
    CHECK_EQ(mfb_menu_left(&m, 1000), 3);
    CHECK_EQ(mfb_menu_wait_ms(&m, 1000), 1000);
    CHECK_EQ(mfb_menu_tick(&m, 1001), MFB_ACT_NONE);
    CHECK_EQ(mfb_menu_take(&m, NULL), 0);
    CHECK_EQ(mfb_menu_left(&m, 1001), 3);
    CHECK_EQ(mfb_menu_wait_ms(&m, 1001), 999);
    CHECK_EQ(mfb_menu_tick(&m, 2000), MFB_ACT_NONE);
    CHECK_EQ(mfb_menu_left(&m, 2000), 2);
    CHECK_EQ(mfb_menu_take(&m, NULL), MFB_DIRTY_STATUS);
    CHECK_EQ(mfb_menu_tick(&m, 3999), MFB_ACT_NONE);
    CHECK_EQ(mfb_menu_left(&m, 3999), 1);
    CHECK_EQ(mfb_menu_wait_ms(&m, 3999), 1);
    /* Never before the full time. */
    for (t = 1000; t < 4000; t += 7) {
        mfb_menu_t copy = m;

        CHECK_EQ(mfb_menu_tick(&copy, t), MFB_ACT_NONE);
    }
    CHECK_EQ(mfb_menu_tick(&m, 4000), MFB_ACT_BOOT);
    CHECK_EQ(m.sel, 2);
    CHECK_EQ(m.via, MFB_VIA_TIMEOUT);
    CHECK_EQ(m.counting, 0);
    CHECK_EQ(m.stopped, 0);
    /* Once only. */
    CHECK_EQ(mfb_menu_tick(&m, 5000), MFB_ACT_NONE);
}

TEST(late_tick_still_boots)
{
    mfb_menu_init(&m, 1, 0, 2, 0);
    CHECK_EQ(mfb_menu_tick(&m, 60000), MFB_ACT_BOOT);
}

TEST(key_stops_the_countdown)
{
    mfb_menu_init(&m, 3, 0, 2, 0);
    CHECK_EQ(press(INPUT_KEY_DOWN), MFB_ACT_NONE);
    CHECK_EQ(m.counting, 0);
    CHECK_EQ(m.stopped, 1);
    CHECK_EQ(mfb_menu_take(&m, NULL), MFB_DIRTY_SEL | MFB_DIRTY_STATUS);
    CHECK_EQ(mfb_menu_tick(&m, 2000), MFB_ACT_NONE);
    CHECK_EQ(mfb_menu_tick(&m, 100000), MFB_ACT_NONE);
    CHECK_EQ(mfb_menu_left(&m, 1000), 0);
    CHECK_EQ(mfb_menu_wait_ms(&m, 1000), 1000);
    /* A second key does not mark the line again. */
    press(INPUT_KEY_UP);
    CHECK_EQ(mfb_menu_take(&m, NULL), MFB_DIRTY_SEL);
}

TEST(timeout_zero_boots_at_once)
{
    mfb_menu_init(&m, 3, 1, 0, 777);
    CHECK_EQ(mfb_menu_left(&m, 777), 0);
    CHECK_EQ(mfb_menu_wait_ms(&m, 777), 1);
    CHECK_EQ(mfb_menu_tick(&m, 777), MFB_ACT_BOOT);
    CHECK_EQ(m.sel, 1);
    CHECK_EQ(m.via, MFB_VIA_TIMEOUT);
}

TEST(countdown_across_the_time_wrap)
{
    /* timer_now_ms() is a uint32_t: it wraps from 0xFFFFFFFF to 0. */
    uint32_t start = 0xFFFFF000u;   /* 4096 ms before the wrap */

    mfb_menu_init(&m, 2, 0, 5, start);
    CHECK_EQ(mfb_menu_left(&m, start), 5);
    CHECK_EQ(mfb_menu_tick(&m, 0xFFFFFFFFu), MFB_ACT_NONE);
    CHECK_EQ(mfb_menu_left(&m, 0xFFFFFFFFu), 1);
    CHECK_EQ(mfb_menu_tick(&m, 0), MFB_ACT_NONE);
    CHECK_EQ(mfb_menu_left(&m, 0), 1);
    CHECK_EQ(mfb_menu_tick(&m, 903), MFB_ACT_NONE);
    CHECK_EQ(mfb_menu_wait_ms(&m, 903), 1);
    CHECK_EQ(mfb_menu_tick(&m, 904), MFB_ACT_BOOT);
}

TEST(stop_after_a_failed_boot)
{
    mfb_menu_init(&m, 2, 0, 1, 0);
    CHECK_EQ(mfb_menu_tick(&m, 1000), MFB_ACT_BOOT);
    mfb_menu_take(&m, NULL);
    mfb_menu_stop(&m);
    CHECK_EQ(m.counting, 0);
    CHECK_EQ(mfb_menu_tick(&m, 3000), MFB_ACT_NONE);
    /* Stopping a running countdown. */
    mfb_menu_init(&m, 2, 0, 9, 0);
    mfb_menu_stop(&m);
    CHECK_EQ(m.counting, 0);
    CHECK_EQ(m.stopped, 1);
    CHECK_EQ(mfb_menu_tick(&m, 9000), MFB_ACT_NONE);
}

TEST(single_entry)
{
    mfb_menu_init(&m, 1, 0, -1, 0);
    press(INPUT_KEY_DOWN);
    press(INPUT_KEY_UP);
    type('2');
    CHECK_EQ(m.sel, 0);
    CHECK_EQ(press(INPUT_KEY_SELECT), MFB_ACT_BOOT);
    /* Zero entries count as one. */
    mfb_menu_init(&m, 0, 0, -1, 0);
    CHECK_EQ(m.count, 1);
}

static input_event_t port_req(int32_t seq)
{
    input_event_t ev = chr(0);

    ev.source = INPUT_SRC_SCRIPT;
    ev.value = seq;
    return ev;
}

TEST(port_maint_in_queue_order)
{
    input_event_t ev = port_req(3);

    /* The live request opens the screen; an older or dropped one does nothing. */
    CHECK_EQ(mfb_port_req(&ev, 3), MFB_PORT_LIVE);
    CHECK_EQ(mfb_port_req(&ev, 4), MFB_PORT_STALE);
    CHECK_EQ(mfb_port_req(&ev, 0), MFB_PORT_STALE);
    /* Keys are no request, NUL from another source or with value 0 neither. */
    ev = chr('m');
    ev.source = INPUT_SRC_SCRIPT;
    CHECK_EQ(mfb_port_req(&ev, 3), MFB_PORT_NONE);
    ev = port_req(0);
    CHECK_EQ(mfb_port_req(&ev, 3), MFB_PORT_NONE);
    ev = port_req(3);
    ev.source = INPUT_SRC_SERIAL;
    CHECK_EQ(mfb_port_req(&ev, 3), MFB_PORT_NONE);
    ev = key(INPUT_KEY_SELECT, INPUT_PRESS);
    ev.source = INPUT_SRC_SCRIPT;
    CHECK_EQ(mfb_port_req(&ev, 3), MFB_PORT_NONE);
    /* Not a key for the menu either. */
    ev = port_req(3);
    mfb_menu_init(&m, 2, 0, 5, 0);
    CHECK_EQ(mfb_menu_event(&m, &ev), MFB_ACT_NONE);
    CHECK_EQ(m.counting, 1);
}
