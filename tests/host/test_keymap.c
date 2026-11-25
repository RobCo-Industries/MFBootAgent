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
    CHECK_STR(mfb_why_name(MFB_WHY_REQUEST), "request");
    CHECK_STR(mfb_why_name(5), "unknown");
    CHECK_STR(mfb_why_name(-1), "unknown");
}

/* A fake queue and guard for mfb_guard_wait(). */
typedef struct {
    input_event_t q[4];
    unsigned head, count;
    unsigned naps;      /* the guard ends after this many naps */
    unsigned ends_at;
    uint32_t now;
} guard_fake_t;

static void fake_push(guard_fake_t *f, unsigned code)
{
    input_event_t ev = { 0 };

    ev.type = INPUT_EV_KEY;
    ev.source = INPUT_SRC_GPIO;
    ev.code = (uint16_t)code;
    ev.value = INPUT_PRESS;
    f->q[(f->head + f->count++) % 4] = ev;
}

static int fake_checking(void *ctx)
{
    return ((guard_fake_t *)ctx)->naps < ((guard_fake_t *)ctx)->ends_at;
}

static uint32_t fake_now(void *ctx)
{
    return ((guard_fake_t *)ctx)->now;
}

static int fake_take(void *ctx, input_event_t *ev)
{
    guard_fake_t *f = ctx;

    if (f->count == 0) {
        return 0;
    }
    *ev = f->q[f->head];
    f->head = (f->head + 1) % 4;
    f->count--;
    return 1;
}

/* One tick: the guard ends on the last one and reports BACK and SELECT held. */
static void fake_nap(void *ctx)
{
    guard_fake_t *f = ctx;

    f->now += 1;
    if (++f->naps == f->ends_at) {
        fake_push(f, INPUT_KEY_BACK);
        fake_push(f, INPUT_KEY_SELECT);
    }
}

static const mfb_guard_io_t *fake_io(guard_fake_t *f, mfb_guard_io_t *io)
{
    io->checking = fake_checking;
    io->now_ms = fake_now;
    io->nap = fake_nap;
    io->ctx = f;
    return io;
}

TEST(guard_wait_keeps_held_buttons)
{
    guard_fake_t f = { 0 };
    mfb_guard_io_t io;
    input_event_t ev = { 0 };

    f.ends_at = 5;
    mfb_guard_wait(fake_io(&f, &io), 300);
    CHECK_EQ(f.naps, 5u);
    CHECK_EQ(f.count, 2u);
    CHECK_EQ(fake_take(&f, &ev), 1);
    CHECK_EQ(ev.code, INPUT_KEY_BACK);
    CHECK_EQ(mfb_key_map(&ev), MFB_K_BACK);
    CHECK_EQ(fake_take(&f, &ev), 1);
    CHECK_EQ(ev.code, INPUT_KEY_SELECT);
}

TEST(guard_wait_keeps_earlier_events)
{
    guard_fake_t f = { 0 };
    mfb_guard_io_t io;
    input_event_t ev = { 0 };

    f.ends_at = 3;
    fake_push(&f, INPUT_KEY_UP);
    mfb_guard_wait(fake_io(&f, &io), 300);
    CHECK_EQ(f.count, 3u);
    CHECK_EQ(fake_take(&f, &ev), 1);
    CHECK_EQ(ev.code, INPUT_KEY_UP);
}

TEST(guard_wait_stops_at_the_limit)
{
    guard_fake_t f = { 0 };
    mfb_guard_io_t io;

    f.ends_at = 1000;
    mfb_guard_wait(fake_io(&f, &io), 30);
    CHECK_EQ(f.naps, 30u);
    CHECK_EQ(f.count, 0u);
}
