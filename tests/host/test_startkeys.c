/*
 * The start key rule of src/startkeys.c on the road of MAINT.SYS: a
 * button held at power-on through the agent and into the image. The
 * image's own guard queues the held button as a press before its menu
 * shows. The agent's start screen cases are in test_maintreq.c.
 */

#include <string.h>

#include <robco/input.h>

#include "rc_test.h"
#include "startkeys.h"

#define HELD(code) ((uint32_t)1 << (code))

static int take(mfb_start_keys_t *k, unsigned type, unsigned source, unsigned code, int value)
{
    input_event_t e;

    memset(&e, 0, sizeof(e));
    e.type = (uint8_t)type;
    e.source = (uint8_t)source;
    e.code = (uint16_t)code;
    e.value = value;
    return mfb_start_keys_take(k, &e);
}

static int gpio(mfb_start_keys_t *k, unsigned code, int value)
{
    return take(k, INPUT_EV_KEY, INPUT_SRC_GPIO, code, value);
}

/* Every button, held through the handoff: the guard's press and the
 * repeats while held are dropped, the release too, and the next press acts. */
TEST(image_held_button_is_no_choice)
{
    static const unsigned keys[] = { INPUT_KEY_UP, INPUT_KEY_DOWN, INPUT_KEY_LEFT,
                                     INPUT_KEY_RIGHT, INPUT_KEY_SELECT, INPUT_KEY_BACK };
    unsigned i;

    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        mfb_start_keys_t k;

        mfb_start_keys_init(&k, 1, HELD(keys[i]));
        CHECK_EQ(gpio(&k, keys[i], INPUT_PRESS), 0);
        CHECK_EQ(gpio(&k, keys[i], INPUT_REPEAT), 0);
        CHECK_EQ(gpio(&k, keys[i], INPUT_REPEAT), 0);
        CHECK_EQ(gpio(&k, keys[i], INPUT_RELEASE), 0);
        CHECK_EQ(gpio(&k, keys[i], INPUT_PRESS), 1);
        CHECK_EQ(gpio(&k, keys[i], INPUT_RELEASE), 1);
    }
}

/* A port tool request and a console key, sent while SELECT is held, act. */
TEST(image_port_and_console_act_while_held)
{
    mfb_start_keys_t k;

    mfb_start_keys_init(&k, 1, HELD(INPUT_KEY_SELECT));
    CHECK_EQ(gpio(&k, INPUT_KEY_SELECT, INPUT_PRESS), 0);
    CHECK_EQ(take(&k, INPUT_EV_CHAR, INPUT_SRC_SCRIPT, 0, 1), 1);
    CHECK_EQ(take(&k, INPUT_EV_KEY, INPUT_SRC_SERIAL, INPUT_KEY_DOWN, INPUT_PRESS), 1);
    /* SELECT is still held: no repeat or release of it acts. */
    CHECK_EQ(gpio(&k, INPUT_KEY_SELECT, INPUT_RELEASE), 0);
    CHECK_EQ(gpio(&k, INPUT_KEY_SELECT, INPUT_PRESS), 1);
}

/* Another button pressed after the menu showed acts while one is held. */
TEST(image_other_button_acts)
{
    mfb_start_keys_t k;

    mfb_start_keys_init(&k, 1, HELD(INPUT_KEY_BACK));
    CHECK_EQ(gpio(&k, INPUT_KEY_BACK, INPUT_PRESS), 0);
    CHECK_EQ(gpio(&k, INPUT_KEY_DOWN, INPUT_PRESS), 1);
    CHECK_EQ(gpio(&k, INPUT_KEY_DOWN, INPUT_RELEASE), 1);
    CHECK_EQ(gpio(&k, INPUT_KEY_BACK, INPUT_REPEAT), 0);
}

/* No button held and nothing queued: every event acts. */
TEST(image_nothing_held)
{
    mfb_start_keys_t k;

    mfb_start_keys_init(&k, 0, 0);
    CHECK_EQ(gpio(&k, INPUT_KEY_SELECT, INPUT_PRESS), 1);
    CHECK_EQ(gpio(&k, INPUT_KEY_SELECT, INPUT_RELEASE), 1);
}
