/* MAINT.SYS: the menu, the answer rule, the names and the screen lines. */

#include <string.h>

#include <robco/input.h>

#include "mt_ui.h"
#include "rc_test.h"

static input_event_t key(unsigned code, int value)
{
    input_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = INPUT_EV_KEY;
    ev.code = (uint16_t)code;
    ev.value = value;
    return ev;
}

static input_event_t chr(unsigned c)
{
    input_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = INPUT_EV_CHAR;
    ev.code = (uint16_t)c;
    return ev;
}

TEST(menu_moves_and_starts)
{
    mt_menu_t m;
    input_event_t up = key(INPUT_KEY_UP, INPUT_PRESS);
    input_event_t down = key(INPUT_KEY_DOWN, INPUT_PRESS);
    input_event_t sel = key(INPUT_KEY_SELECT, INPUT_PRESS);
    input_event_t held = key(INPUT_KEY_SELECT, INPUT_REPEAT);
    input_event_t rel = key(INPUT_KEY_SELECT, INPUT_RELEASE);
    unsigned row = 99;
    unsigned old = 99;
    unsigned i;

    mt_menu_init(&m, MT_TOOLS);
    CHECK_EQ(mt_menu_event(&m, &up, &row), MT_ACT_NONE);
    CHECK_EQ(m.sel, 0u);
    for (i = 0; i < 10; i++) {
        (void)mt_menu_event(&m, &down, &row);
    }
    CHECK_EQ(m.sel, MT_TOOLS - 1u);
    CHECK_EQ(mt_menu_take(&m, &old), MT_DIRTY_SEL);
    CHECK_EQ(old, 0u);
    CHECK_EQ(mt_menu_event(&m, &held, &row), MT_ACT_NONE);
    CHECK_EQ(mt_menu_event(&m, &rel, &row), MT_ACT_NONE);
    CHECK_EQ(mt_menu_event(&m, &sel, &row), MT_ACT_RUN);
    CHECK_EQ(row, MT_TOOLS - 1u);
    for (i = 0; i < MT_TOOLS; i++) {
        input_event_t d = chr('1' + i);

        CHECK_EQ(mt_menu_event(&m, &d, &row), MT_ACT_RUN);
        CHECK_EQ(row, i);
    }
    {
        input_event_t seven = chr('7');
        input_event_t zero = chr('0');

        CHECK_EQ(mt_menu_event(&m, &seven, &row), MT_ACT_NONE);
        CHECK_EQ(mt_menu_event(&m, &zero, &row), MT_ACT_NONE);
    }
}

TEST(answer_is_yes_only_for_select_and_y)
{
    input_event_t ev;
    unsigned c;

    ev = key(INPUT_KEY_SELECT, INPUT_PRESS);
    CHECK_EQ(mt_answer(&ev), MT_ANS_YES);
    ev = chr('y');
    CHECK_EQ(mt_answer(&ev), MT_ANS_YES);
    ev = chr('Y');
    CHECK_EQ(mt_answer(&ev), MT_ANS_YES);
    /* BACK, which ESC gives, Ctrl-C and every wrong key are a no. */
    ev = key(INPUT_KEY_BACK, INPUT_PRESS);
    CHECK_EQ(mt_answer(&ev), MT_ANS_NO);
    ev = chr(0x03);
    CHECK_EQ(mt_answer(&ev), MT_ANS_NO);
    for (c = INPUT_KEY_UP; c < INPUT_KEY_COUNT; c++) {
        ev = key(c, INPUT_PRESS);
        CHECK_EQ(mt_answer(&ev), c == INPUT_KEY_SELECT ? MT_ANS_YES : MT_ANS_NO);
    }
    for (c = 1; c < 256; c++) {
        ev = chr(c);
        CHECK_EQ(mt_answer(&ev), c == 'y' || c == 'Y' ? MT_ANS_YES : MT_ANS_NO);
    }
    /* Releases and held repeats are no answer: the SELECT that started a tool is not a yes. */
    ev = key(INPUT_KEY_SELECT, INPUT_RELEASE);
    CHECK_EQ(mt_answer(&ev), MT_ANS_NONE);
    ev = key(INPUT_KEY_SELECT, INPUT_REPEAT);
    CHECK_EQ(mt_answer(&ev), MT_ANS_NONE);
    CHECK_EQ(mt_answer(NULL), MT_ANS_NONE);
}

TEST(abort_is_back_or_ctrl_c)
{
    input_event_t ev = key(INPUT_KEY_BACK, INPUT_PRESS);

    CHECK(mt_is_abort(&ev));
    ev = chr(0x03);
    CHECK(mt_is_abort(&ev));
    ev = key(INPUT_KEY_BACK, INPUT_RELEASE);
    CHECK(!mt_is_abort(&ev));
    ev = chr('q');
    CHECK(!mt_is_abort(&ev));
}

TEST(names)
{
    unsigned i;

    CHECK_STR(mt_tool_name(MT_T_CARD), "check_card");
    CHECK_STR(mt_tool_name(MT_T_RESTART), "restart");
    CHECK(mt_tool_name(MT_TOOLS) == NULL);
    for (i = 0; i < MT_TOOLS; i++) {
        CHECK_EQ(mt_tool_find(mt_tool_name(i)), (int)i);
    }
    CHECK_EQ(mt_tool_find("all"), -1);
    CHECK_EQ(mt_tool_find(NULL), -1);
    CHECK(mt_tool_readonly(MT_T_CARD) && mt_tool_readonly(MT_T_IMAGES) &&
          mt_tool_readonly(MT_T_VIEW));
    CHECK(!mt_tool_readonly(MT_T_CLOCK) && !mt_tool_readonly(MT_T_USERS) &&
          !mt_tool_readonly(MT_T_RESTART));
    CHECK_STR(mt_result_name(MT_REFUSED), "refused");
    CHECK_STR(mt_result_name(MT_OFF), "off");
    CHECK_STR(mt_result_name(MT_NONE), "none");
}

/* Every row fits the screen with the longest text the row shows. */
TEST(lines_fit)
{
    char line[MT_LINE_MAX];
    char text[MT_TEXT_MAX];
    unsigned i;
    int r;

    memset(text, 'x', MT_TEXT_COLS);
    text[MT_TEXT_COLS] = '\0';
    for (i = 0; i < MT_TOOLS; i++) {
        mt_line_item(line, sizeof(line), i, 1, MT_NONE, "");
        CHECK(strlen(line) < MT_MIN_COLS);
        for (r = MT_OK; r < MT_RESULTS; r++) {
            mt_line_item(line, sizeof(line), i, 0, r, text);
            CHECK_EQ(strlen(line), MT_ITEM_FIXED + MT_TEXT_COLS);
        }
    }
    mt_line_item(line, sizeof(line), MT_T_USERS, 0, MT_REFUSED, "No.");
    CHECK_STR(line, "  5  RESTORE USERS Copy the default user file     REFUSED  No.");
    mt_line_tool(line, sizeof(line), MT_T_VIEW, MT_ABSENT, "why=missing");
    CHECK_STR(line, "tool name=view_file result=absent why=missing");
    mt_line_tool(line, sizeof(line), MT_T_RESTART, MT_OK, "");
    CHECK_STR(line, "tool name=restart result=ok");
    mt_line_status(line, sizeof(line), MT_T_CARD, MT_OK, "");
    CHECK_STR(line, "Check card: OK.");
    CHECK(strlen(mt_line_help()) < MT_MIN_COLS);
    CHECK(strlen(mt_line_sub()) < MT_MIN_COLS);
}
