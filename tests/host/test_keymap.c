/* The key mapping: every key code and value, every character class, and the other events. */

#include "keymap.h"
#include "rc_test.h"

static int map(uint8_t type, unsigned code, int value)
{
    input_event_t ev = { 0 };

    ev.type = type;
    ev.code = (uint16_t)code;
    ev.value = value;
    return mfb_key_map(&ev);
}

TEST(key_presses)
{
    unsigned code;

    CHECK_EQ(map(INPUT_EV_KEY, INPUT_KEY_UP, INPUT_PRESS), MFB_K_UP);
    CHECK_EQ(map(INPUT_EV_KEY, INPUT_KEY_DOWN, INPUT_PRESS), MFB_K_DOWN);
    CHECK_EQ(map(INPUT_EV_KEY, INPUT_KEY_SELECT, INPUT_PRESS), MFB_K_SELECT);
    CHECK_EQ(map(INPUT_EV_KEY, INPUT_KEY_BACK, INPUT_PRESS), MFB_K_BACK);
    CHECK_EQ(map(INPUT_EV_KEY, INPUT_KEY_LEFT, INPUT_PRESS), MFB_K_OTHER);
    CHECK_EQ(map(INPUT_EV_KEY, INPUT_KEY_RIGHT, INPUT_PRESS), MFB_K_OTHER);
    for (code = INPUT_KEY_F1; code < INPUT_KEY_COUNT + 3; code++) {
        CHECK_EQ(map(INPUT_EV_KEY, code, INPUT_PRESS), MFB_K_OTHER);
    }
    CHECK_EQ(map(INPUT_EV_KEY, INPUT_KEY_NONE, INPUT_PRESS), MFB_K_OTHER);
}

TEST(only_up_and_down_repeat)
{
    unsigned code;

    CHECK_EQ(map(INPUT_EV_KEY, INPUT_KEY_UP, INPUT_REPEAT), MFB_K_UP);
    CHECK_EQ(map(INPUT_EV_KEY, INPUT_KEY_DOWN, INPUT_REPEAT), MFB_K_DOWN);
    for (code = 0; code < INPUT_KEY_COUNT; code++) {
        if (code != INPUT_KEY_UP && code != INPUT_KEY_DOWN) {
            CHECK_EQ(map(INPUT_EV_KEY, code, INPUT_REPEAT), MFB_K_NONE);
        }
    }
}

TEST(releases_and_odd_values_do_nothing)
{
    unsigned code;

    for (code = 0; code < INPUT_KEY_COUNT; code++) {
        CHECK_EQ(map(INPUT_EV_KEY, code, INPUT_RELEASE), MFB_K_NONE);
        CHECK_EQ(map(INPUT_EV_KEY, code, 3), MFB_K_NONE);
        CHECK_EQ(map(INPUT_EV_KEY, code, -1), MFB_K_NONE);
    }
}

TEST(characters)
{
    unsigned c;

    for (c = '1'; c <= '8'; c++) {
        CHECK_EQ(map(INPUT_EV_CHAR, c, 0), MFB_K_DIGIT1 + (int)(c - '1'));
    }
    CHECK_EQ(MFB_K_DIGIT8, MFB_K_DIGIT1 + 7);
    CHECK_EQ(map(INPUT_EV_CHAR, 'm', 0), MFB_K_MAINT);
    CHECK_EQ(map(INPUT_EV_CHAR, 'M', 0), MFB_K_MAINT);
    CHECK_EQ(map(INPUT_EV_CHAR, 'q', 0), MFB_K_QUIT);
    CHECK_EQ(map(INPUT_EV_CHAR, 'Q', 0), MFB_K_QUIT);
    CHECK_EQ(map(INPUT_EV_CHAR, 'r', 0), MFB_K_REBOOT);
    CHECK_EQ(map(INPUT_EV_CHAR, 'R', 0), MFB_K_REBOOT);
    CHECK_EQ(map(INPUT_EV_CHAR, 0, 0), MFB_K_NONE);
    for (c = 1; c < 256; c++) {
        int k = map(INPUT_EV_CHAR, c, 0);

        if ((c < '1' || c > '8') && c != 'm' && c != 'M' && c != 'q' && c != 'Q' && c != 'r' &&
            c != 'R') {
            CHECK_EQ(k, MFB_K_OTHER);
        }
    }
    CHECK_EQ(map(INPUT_EV_CHAR, 0x1234, 0), MFB_K_OTHER);
}

TEST(other_events)
{
    CHECK_EQ(map(INPUT_EV_DIAL, 0, 1), MFB_K_NONE);
    CHECK_EQ(map(INPUT_EV_DIAL, 1, -1), MFB_K_NONE);
    CHECK_EQ(map(0, INPUT_KEY_UP, INPUT_PRESS), MFB_K_NONE);
    CHECK_EQ(map(9, 'm', 0), MFB_K_NONE);
}

TEST(why_names)
{
    CHECK_STR(mfb_why_name(MFB_WHY_KEY), "key");
    CHECK_STR(mfb_why_name(MFB_WHY_BUTTON), "button");
    CHECK_STR(mfb_why_name(MFB_WHY_PORT), "port");
    CHECK_STR(mfb_why_name(MFB_WHY_FLAG), "flag");
    CHECK_STR(mfb_why_name(4), "unknown");
    CHECK_STR(mfb_why_name(-1), "unknown");
}
