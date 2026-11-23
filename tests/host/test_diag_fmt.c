/* The diagnostics lines, markers, progress rate and verdicts: exact texts and limits. */

#include <string.h>

#include "dg_fmt.h"
#include "dg_menu.h"
#include "rc_test.h"

static char buf[DG_LINE_MAX + DG_KEYS_MAX];

TEST(result_names)
{
    CHECK_STR(dg_result_name(DG_OK), "ok");
    CHECK_STR(dg_result_name(DG_FAIL), "fail");
    CHECK_STR(dg_result_name(DG_ABSENT), "absent");
    CHECK_STR(dg_result_name(DG_SKIPPED), "skipped");
    CHECK_STR(dg_result_name(DG_NONE), "none");
    CHECK_STR(dg_result_name(-1), "none");
    CHECK_STR(dg_result_word(DG_OK), "OK");
    CHECK_STR(dg_result_word(DG_FAIL), "FAILED");
    CHECK_STR(dg_result_word(DG_ABSENT), "ABSENT");
    CHECK_STR(dg_result_word(DG_SKIPPED), "SKIPPED");
    CHECK_STR(dg_result_word(DG_NONE), "");
}

TEST(counts_and_markers)
{
    dg_counts_t c;

    dg_counts_init(&c);
    dg_counts_add(&c, DG_OK);
    dg_counts_add(&c, DG_OK);
    dg_counts_add(&c, DG_FAIL);
    dg_counts_add(&c, DG_ABSENT);
    dg_counts_add(&c, DG_SKIPPED);
    dg_counts_add(&c, DG_NONE);
    dg_counts_add(&c, -1);
    dg_counts_add(NULL, DG_OK);
    dg_line_done(buf, sizeof(buf), &c);
    CHECK_STR(buf, "diag_done ok=2 fail=1 absent=1 skipped=1");
    dg_line_status_all(buf, sizeof(buf), &c);
    CHECK_STR(buf, "Run all: 2 OK, 1 FAILED, 1 ABSENT, 1 SKIPPED.");
    dg_line_diag(buf, sizeof(buf), "mem", DG_OK, "bytes=4096");
    CHECK_STR(buf, "diag name=mem result=ok bytes=4096");
    dg_line_diag(buf, sizeof(buf), "rtc", DG_ABSENT, "");
    CHECK_STR(buf, "diag name=rtc result=absent");
    dg_line_diag(buf, sizeof(buf), "rtc", DG_SKIPPED, NULL);
    CHECK_STR(buf, "diag name=rtc result=skipped");
    /* A short buffer cuts the line and ends it. */
    dg_line_diag(buf, 10, "mem", DG_OK, "bytes=4096");
    CHECK_STR(buf, "diag name");
}

TEST(menu_lines)
{
    dg_line_title(buf, sizeof(buf), "0.1.0");
    CHECK_STR(buf, "TERMINAL DIAGNOSTICS V0.1.0");
    CHECK_STR(dg_line_sub(), "Tests for this terminal. Each result also goes to the serial port.");
    CHECK_STR(dg_line_rule(), "========================================");
    CHECK_STR(dg_line_help(), "UP/DOWN, 1-9, A, R: select   ENTER: start   ESC: stop a test");
    CHECK_STR(dg_line_status_idle(), "Choose a test.");
    dg_line_item(buf, sizeof(buf), DG_T_MEM, 1, DG_NONE, "");
    CHECK_STR(buf, "> 1  MEM       RAM write and read");
    dg_line_item(buf, sizeof(buf), DG_T_EXPANDER, 0, DG_NONE, NULL);
    CHECK_STR(buf, "  9  EXPANDER  I/O expanders, read only");
    dg_line_item(buf, sizeof(buf), DG_ITEM_ALL, 0, DG_NONE, NULL);
    CHECK_STR(buf, "  A  RUN ALL   Every test without a person");
    dg_line_item(buf, sizeof(buf), DG_ITEM_REBOOT, 0, DG_NONE, NULL);
    CHECK_STR(buf, "  R  REBOOT    Restart the board");
    dg_line_item(buf, sizeof(buf), DG_T_RTC, 0, DG_ABSENT, "No clock chip at 0x68.");
    CHECK_STR(buf, "  8  RTC       Clock chip, read only        ABSENT   No clock chip at 0x68.");
    dg_line_item(buf, sizeof(buf), DG_ITEMS, 0, DG_OK, "x");
    CHECK_STR(buf, "");
    dg_line_item(buf, sizeof(buf), DG_T_MEM, 0, DG_OK, NULL);
    CHECK_STR(buf, "  1  MEM       RAM write and read           OK       ");
    dg_line_status_one(buf, sizeof(buf), DG_T_SD, DG_FAIL, "1 read errors, 0 bad images");
    CHECK_STR(buf, "SD: FAILED. 1 read errors, 0 bad images");
    dg_line_status_one(buf, sizeof(buf), DG_T_SD, DG_OK, "");
    CHECK_STR(buf, "SD: OK.");
    CHECK_STR(dg_item_title(DG_T_VIDEO), "VIDEO");
    CHECK_STR(dg_item_title(DG_ITEMS), "");
}

TEST(every_menu_row_fits_the_screen)
{
    unsigned i;
    char text[DG_TEXT_MAX];

    /* A full-length detail still fits in the line buffer, cut at its end. */
    memset(text, 'x', sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';
    for (i = 0; i < DG_ITEMS; i++) {
        dg_line_item(buf, DG_LINE_MAX, i, 1, DG_SKIPPED, text);
        /* 53 columns before the detail. */
        CHECK_EQ(strlen(buf), 53 + DG_TEXT_MAX - 1);
        dg_line_item(buf, DG_LINE_MAX, i, 1, DG_NONE, NULL);
        CHECK(strlen(buf) < DG_MIN_COLS);
    }
    CHECK(strlen(dg_line_sub()) < DG_MIN_COLS);
    CHECK(strlen(dg_line_help()) < DG_MIN_COLS);
    CHECK(DG_ROW_WORK + DG_WORK_ROWS == DG_MIN_ROWS);
    CHECK(DG_ROW_ITEM0 + DG_ITEMS < DG_ROW_HELP);
}

TEST(expander_lines)
{
    dg_line_xrow(buf, sizeof(buf), 0, 1, 0xFE, 1, 0x01);
    CHECK_STR(buf, "X0.0  LOW           X1.0  HIGH");
    dg_line_xrow(buf, sizeof(buf), 7, 1, 0x80, 0, 0xFF);
    CHECK_STR(buf, "X0.7  HIGH          X1.7  --  ");
    dg_line_xregs(buf, sizeof(buf), 1, 0x21, 1, 0xF0, 0xFA, 0x00, 0xDA);
    CHECK_STR(buf, "X1 at 0x21: IN 0xf0  OUT 0xfa  POL 0x00  CFG 0xda");
    dg_line_xregs(buf, sizeof(buf), 0, 0x20, 0, 0, 0, 0, 0);
    CHECK_STR(buf, "X0 at 0x20: absent");
}

TEST(input_lines)
{
    input_event_t ev = { 0 };

    dg_line_button(buf, sizeof(buf), "select", 1);
    CHECK_STR(buf, "select   PRESSED");
    dg_line_button(buf, sizeof(buf), "up", 0);
    CHECK_STR(buf, "up       released");
    dg_line_button(buf, sizeof(buf), NULL, 0);
    CHECK_STR(buf, "?        released");
    ev.type = INPUT_EV_KEY;
    ev.source = INPUT_SRC_GPIO;
    ev.code = INPUT_KEY_BACK;
    ev.value = INPUT_PRESS;
    dg_line_event(buf, sizeof(buf), &ev);
    CHECK_STR(buf, "gpio    key   back      press");
    ev.source = INPUT_SRC_SCRIPT;
    ev.code = INPUT_KEY_DIAL_PUSH;
    ev.value = INPUT_RELEASE;
    dg_line_event(buf, sizeof(buf), &ev);
    CHECK_STR(buf, "script  key   dial_push release");
    ev.code = 200;
    ev.value = INPUT_REPEAT;
    dg_line_event(buf, sizeof(buf), &ev);
    CHECK_STR(buf, "script  key   unknown   repeat");
    ev.type = INPUT_EV_CHAR;
    ev.source = INPUT_SRC_SERIAL;
    ev.code = 'q';
    dg_line_event(buf, sizeof(buf), &ev);
    CHECK_STR(buf, "serial  char  0x71 'q'");
    ev.code = 0x1b;
    dg_line_event(buf, sizeof(buf), &ev);
    CHECK_STR(buf, "serial  char  0x1b");
    ev.code = 0xffff;
    dg_line_event(buf, sizeof(buf), &ev);
    CHECK_STR(buf, "serial  char  0xffff");
    ev.type = INPUT_EV_DIAL;
    ev.source = 9;
    ev.code = 1;
    ev.value = -3;
    dg_line_event(buf, sizeof(buf), &ev);
    CHECK_STR(buf, "other   dial  1 steps -3");
    ev.type = 77;
    dg_line_event(buf, sizeof(buf), &ev);
    CHECK_STR(buf, "other   type 77");
    CHECK_STR(dg_key_name(INPUT_KEY_UP), "up");
    CHECK_STR(dg_key_name(INPUT_KEY_COUNT), "unknown");
}

TEST(progress_rate)
{
    dg_rate_t r;

    dg_rate_init(&r, 500);
    CHECK(dg_rate_due(&r, 1000));
    CHECK(!dg_rate_due(&r, 1499));
    CHECK(dg_rate_due(&r, 1500));
    CHECK(!dg_rate_due(&r, 1500));
    /* Across the wrap of the millisecond timer. */
    dg_rate_init(&r, 500);
    CHECK(dg_rate_due(&r, 0xFFFFFF00u));
    CHECK(!dg_rate_due(&r, 0x00000010u));
    CHECK(dg_rate_due(&r, 0x000001F4u));
    CHECK_EQ(dg_percent(0, 0), 0);
    CHECK_EQ(dg_percent(5, 0), 0);
    CHECK_EQ(dg_percent(0, 10), 0);
    CHECK_EQ(dg_percent(5, 10), 50);
    CHECK_EQ(dg_percent(10, 10), 100);
    CHECK_EQ(dg_percent(11, 10), 100);
    CHECK_EQ(dg_percent(UINT64_MAX - 1, UINT64_MAX), 99);
    CHECK_EQ(dg_percent(UINT64_MAX / 2, UINT64_MAX), 50);
}

TEST(timer_verdict)
{
    int32_t ppm = 1;
    uint32_t limit = 0;

    CHECK_EQ(dg_timer_judge(1000000, 100, 100, 0, &ppm, &limit), DG_OK);
    CHECK_EQ(ppm, 0);
    CHECK_EQ(limit, 10000);
    CHECK_EQ(dg_timer_judge(1010000, 100, 100, 0, &ppm, &limit), DG_OK);
    CHECK_EQ(ppm, 10000);
    CHECK_EQ(dg_timer_judge(1010001, 100, 100, 0, &ppm, &limit), DG_FAIL);
    CHECK_EQ(dg_timer_judge(989999, 100, 100, 0, &ppm, &limit), DG_FAIL);
    CHECK_EQ(ppm, -10001);
    CHECK_EQ(dg_timer_judge(997484, 100, 100, 0, &ppm, &limit), DG_OK);
    CHECK_EQ(ppm, -2516);
    CHECK_EQ(dg_timer_judge(1000000, 100, 100, 1, &ppm, &limit), DG_FAIL);
    CHECK_EQ(dg_timer_judge(0, 100, 100, 0, &ppm, &limit), DG_FAIL);
    CHECK_EQ((uint32_t)ppm, (uint32_t)-1000000);
    CHECK_EQ(dg_timer_judge(UINT64_MAX / 4, 100, 100, 0, &ppm, NULL), DG_FAIL);
    CHECK_EQ(ppm, INT32_MAX);
    CHECK_EQ(dg_timer_judge(1000000, 0, 100, 0, &ppm, &limit), DG_SKIPPED);
    CHECK_EQ(dg_timer_judge(1000000, 100, 0, 0, &ppm, &limit), DG_SKIPPED);
    CHECK_EQ(dg_timer_judge(1000000, 1, 2000000, 0, NULL, NULL), DG_SKIPPED);
}

TEST(rng_verdict)
{
    static uint32_t w[1024];
    uint32_t rep = 9;
    uint32_t bits = 9;
    uint32_t x = 0x12345678u;
    unsigned i;

    for (i = 0; i < 1024; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        w[i] = x;
    }
    CHECK_EQ(dg_rng_judge(w, 1024, &rep, &bits), DG_OK);
    CHECK_EQ(rep, 0);
    CHECK(bits + 512 >= 16384 && bits <= 16384 + 512);
    w[100] = w[99];
    CHECK_EQ(dg_rng_judge(w, 1024, &rep, &bits), DG_FAIL);
    CHECK_EQ(rep, 1);
    /* Half the bits exactly, then too many and too few. */
    for (i = 0; i < 1024; i++) {
        w[i] = (i & 1) ? 0x0000FFFFu : 0xFFFF0000u;
    }
    CHECK_EQ(dg_rng_judge(w, 1024, &rep, &bits), DG_OK);
    CHECK_EQ(bits, 16384);
    w[0] = 0xFFFFFFFFu;
    for (i = 1; i < 64; i++) {
        w[i] = (i & 1) ? 0xFFFFFFFEu : 0xFFFFFFFFu;
    }
    CHECK_EQ(dg_rng_judge(w, 1024, &rep, &bits), DG_FAIL);
    for (i = 0; i < 1024; i++) {
        w[i] = (i & 1) ? 1u : 2u;
    }
    CHECK_EQ(dg_rng_judge(w, 1024, &rep, &bits), DG_FAIL);
    CHECK_EQ(bits, 1024);
    CHECK_EQ(dg_rng_judge(w, 1, &rep, &bits), DG_SKIPPED);
    CHECK_EQ(dg_rng_judge(NULL, 1024, &rep, &bits), DG_SKIPPED);
}
