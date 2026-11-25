/*
 * The input test: a live view. Every event of every backend shows on the
 * screen and as an "input_event" marker, and a row per GPIO button shows
 * whether it is held. It leaves on BACK twice within a second, BACK held
 * for 1.5 s, or after IDLE_MS with no event. A single BACK stays, so the
 * BACK button can be tried too. "Run all" skips it: it needs a person.
 */

#include <stdint.h>

#include <robco/input.h>
#include <robco/klibc.h>
#include <robco/timer.h>

#include "dg.h"
#include "dg_menu.h"

#define IDLE_MS     20000u
#define ROW_BUTTONS 1u      /* four rows, two buttons each */
#define ROW_EVENTS  6u      /* the last EVENT_ROWS events, newest at the bottom */
#define EVENT_ROWS  (DG_WORK_ROWS - ROW_EVENTS)

static char events[EVENT_ROWS][DG_LINE_MAX];

static void draw_buttons(void)
{
    char a[DG_LINE_MAX / 2];
    char b[DG_LINE_MAX / 2];
    char line[DG_LINE_MAX];
    unsigned i;

    for (i = 0; i < DG_BUTTONS; i += 2) {
        dg_line_button(a, sizeof(a), dg_key_name(dg_buttons[i].code),
                       input_key_down(dg_buttons[i].code));
        b[0] = '\0';
        if (i + 1 < DG_BUTTONS) {
            dg_line_button(b, sizeof(b), dg_key_name(dg_buttons[i + 1].code),
                           input_key_down(dg_buttons[i + 1].code));
        }
        k_snprintf(line, sizeof(line), "%-30s %s", a, b);
        dg_work_line(ROW_BUTTONS + i / 2, line, 0);
    }
}

static void add_event(const input_event_t *ev)
{
    unsigned i;

    for (i = 0; i + 1 < EVENT_ROWS; i++) {
        k_memcpy(events[i], events[i + 1], sizeof(events[i]));
    }
    dg_line_event(events[EVENT_ROWS - 1], sizeof(events[0]), ev);
    for (i = 0; i < EVENT_ROWS; i++) {
        dg_work_line(ROW_EVENTS + i, events[i], 0);
    }
}

void dg_test_input(dg_run_t *r)
{
    dg_live_t live;
    input_event_t ev;
    char line[DG_LINE_MAX];
    unsigned count = 0;
    int why = DG_LIVE_STAY;

    k_memset(events, 0, sizeof(events));
    k_snprintf(line, sizeof(line), "Press keys. ESC ESC or a long BACK leaves. Idle %u s leaves.",
               IDLE_MS / 1000u);
    dg_work_line(0, line, 1);
    k_snprintf(line, sizeof(line), "GPIO buttons: %s", input_gpio_status_name(input_gpio_status()));
    dg_work_line(ROW_EVENTS - 1, line, 1);
    draw_buttons();
    dg_mark("input_view state=open gpio=%s idle_ms=%u", input_gpio_status_name(input_gpio_status()),
            IDLE_MS);
    dg_live_init(&live, timer_now_ms(), IDLE_MS);
    while (why == DG_LIVE_STAY) {
        if (dg_wait_event(&ev, 50)) {
            char code[12];

            count++;
            if (ev.type == INPUT_EV_KEY) {
                k_strncpy(code, dg_key_name(ev.code), sizeof(code));
                code[sizeof(code) - 1] = '\0';
            } else {
                k_snprintf(code, sizeof(code), "0x%02x", (unsigned)ev.code & 0xffffu);
            }
            dg_mark("input_event source=%s type=%s code=%s value=%d", dg_source_name(ev.source),
                    ev.type == INPUT_EV_KEY ? "key" : ev.type == INPUT_EV_CHAR ? "char" : "dial",
                    code, (int)ev.value);
            add_event(&ev);
            draw_buttons();
            why = dg_live_event(&live, &ev, timer_now_ms());
        }
        if (why == DG_LIVE_STAY) {
            why = dg_live_poll(&live, timer_now_ms());
        }
    }
    r->result = DG_OK;
    dg_keys(r, "events=%u why=%s gpio=%s", count, dg_live_why(why),
            input_gpio_status_name(input_gpio_status()));
    dg_text(r, DG_TX_IN_OK, count, dg_live_why(why));
}
