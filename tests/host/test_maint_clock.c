/* MAINT.SYS: the entry form of set clock: keys, typed digits, wraps and the check. */

#include <string.h>

#include <robco/datetime.h>
#include <robco/input.h>

#include "mt_clock.h"
#include "rc_test.h"

static int key(mt_clock_form_t *f, unsigned code)
{
    input_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = INPUT_EV_KEY;
    ev.code = (uint16_t)code;
    ev.value = INPUT_PRESS;
    return mt_clock_event(f, &ev);
}

static int type(mt_clock_form_t *f, const char *s)
{
    input_event_t ev;
    int r = MT_CF_NONE;

    memset(&ev, 0, sizeof(ev));
    ev.type = INPUT_EV_CHAR;
    for (; *s != '\0'; s++) {
        ev.code = (uint8_t)*s;
        r = mt_clock_event(f, &ev);
    }
    return r;
}

static const char *text(const mt_clock_form_t *f)
{
    static char out[32];

    mt_clock_text(f, out, sizeof(out));
    return out;
}

TEST(starts_at_the_chip_time_or_2000)
{
    mt_clock_form_t f;
    datetime_t dt = { 2026, 11, 25, 10, 4, 5, 0 };
    datetime_t bad = { 2026, 2, 30, 0, 0, 0, 0 };

    mt_clock_init(&f, &dt);
    CHECK_STR(text(&f), "[2026]-11-25 10:04:05");
    mt_clock_init(&f, NULL);
    CHECK_STR(text(&f), "[2000]-01-01 00:00:00");
    mt_clock_init(&f, &bad);
    CHECK_STR(text(&f), "[2000]-01-01 00:00:00");
}

TEST(buttons_move_and_wrap)
{
    mt_clock_form_t f;
    datetime_t out;

    mt_clock_init(&f, NULL);
    CHECK_EQ(key(&f, INPUT_KEY_DOWN), MT_CF_CHANGED);
    CHECK_STR(text(&f), "[2099]-01-01 00:00:00");
    CHECK_EQ(key(&f, INPUT_KEY_UP), MT_CF_CHANGED);
    CHECK_STR(text(&f), "[2000]-01-01 00:00:00");
    key(&f, INPUT_KEY_LEFT);
    CHECK_EQ(f.field, 0u);
    key(&f, INPUT_KEY_RIGHT);
    key(&f, INPUT_KEY_DOWN);
    CHECK_STR(text(&f), "2000-[12]-01 00:00:00");
    for (int i = 0; i < 10; i++) {
        key(&f, INPUT_KEY_RIGHT);
    }
    CHECK_EQ(f.field, 5u);
    key(&f, INPUT_KEY_DOWN);
    CHECK_STR(text(&f), "2000-12-01 00:00:[59]");
    CHECK_EQ(key(&f, INPUT_KEY_SELECT), MT_CF_ENTER);
    CHECK_EQ(mt_clock_get(&f, &out), 0);
    CHECK_EQ(out.year, 2000u);
    CHECK_EQ(out.month, 12u);
    CHECK_EQ(out.second, 59u);
    CHECK_EQ(out.weekday, 5u);  /* 2000-12-01 was a Friday */
    CHECK_EQ(key(&f, INPUT_KEY_BACK), MT_CF_CANCEL);
}

TEST(digits_fill_fields_in_order)
{
    mt_clock_form_t f;
    datetime_t out;

    mt_clock_init(&f, NULL);
    type(&f, "20261125");
    CHECK_STR(text(&f), "2026-11-25 [00]:00:00");
    type(&f, "093007");
    CHECK_EQ(f.field, 5u);
    CHECK_STR(text(&f), "2026-11-25 09:30:[07]");
    CHECK_EQ(mt_clock_get(&f, &out), 0);
    CHECK_EQ(out.hour, 9u);
    CHECK_EQ(out.minute, 30u);
    CHECK_EQ(out.second, 7u);
    /* Ctrl-C cancels. */
    CHECK_EQ(type(&f, "\003"), MT_CF_CANCEL);
}

TEST(the_check_holds_the_chip_range_and_the_calendar)
{
    mt_clock_form_t f;
    datetime_t out;

    mt_clock_init(&f, NULL);
    type(&f, "20270229000000");
    CHECK_EQ(mt_clock_get(&f, &out), DT_ERR_RANGE);
    mt_clock_init(&f, NULL);
    type(&f, "20280229000000");
    CHECK_EQ(mt_clock_get(&f, &out), 0);
    mt_clock_init(&f, NULL);
    type(&f, "21000101000000");
    CHECK_EQ(mt_clock_get(&f, &out), DT_ERR_RANGE);
    mt_clock_init(&f, NULL);
    type(&f, "19991231235959");
    CHECK_EQ(mt_clock_get(&f, &out), DT_ERR_RANGE);
    mt_clock_init(&f, NULL);
    type(&f, "20261301000000");
    CHECK_EQ(mt_clock_get(&f, &out), DT_ERR_RANGE);
    mt_clock_init(&f, NULL);
    type(&f, "20260431000000");
    CHECK_EQ(mt_clock_get(&f, &out), DT_ERR_RANGE);
    mt_clock_init(&f, NULL);
    type(&f, "20260101240000");
    CHECK_EQ(mt_clock_get(&f, &out), DT_ERR_RANGE);
    mt_clock_init(&f, NULL);
    type(&f, "20260101006000");
    CHECK_EQ(mt_clock_get(&f, &out), DT_ERR_RANGE);
    mt_clock_init(&f, NULL);
    type(&f, "20991231235959");
    CHECK_EQ(mt_clock_get(&f, &out), 0);
    /* A typed month of 13 steps back into range with UP or DOWN. */
    mt_clock_init(&f, NULL);
    type(&f, "202613");
    key(&f, INPUT_KEY_LEFT);
    key(&f, INPUT_KEY_DOWN);
    CHECK_STR(text(&f), "2026-[12]-01 00:00:00");
}

TEST(releases_do_nothing)
{
    mt_clock_form_t f;
    input_event_t ev;

    mt_clock_init(&f, NULL);
    memset(&ev, 0, sizeof(ev));
    ev.type = INPUT_EV_KEY;
    ev.code = INPUT_KEY_SELECT;
    ev.value = INPUT_RELEASE;
    CHECK_EQ(mt_clock_event(&f, &ev), MT_CF_NONE);
    ev.value = INPUT_REPEAT;
    CHECK_EQ(mt_clock_event(&f, &ev), MT_CF_NONE);
    ev.code = INPUT_KEY_UP;
    CHECK_EQ(mt_clock_event(&f, &ev), MT_CF_CHANGED);
}
