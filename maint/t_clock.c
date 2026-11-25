/*
 * Set clock. Finds the clock chip with the library's RTC driver, shows
 * its time in the form of mt_clock.h, checks the new time, asks, writes
 * it with rtc_set(), reads it back and shows both. With no chip it says
 * so and writes nothing.
 */

#include <stdint.h>

#include <robco/datetime.h>
#include <robco/klibc.h>
#include <robco/rtc.h>

#include "mt.h"
#include "mt_clock.h"

static const char *chip_name(int chip)
{
    return chip == RTC_CHIP_DS3231 ? "ds3231" : chip == RTC_CHIP_DS1307 ? "ds1307" : "none";
}

static void draw_form(const mt_clock_form_t *f, const char *now)
{
    char line[MT_LINE_MAX];
    char text[24];

    mt_work_line(0, "Set the clock chip, UTC, 2000-01-01 to 2099-12-31.", 0);
    mt_work_line(1, "LEFT/RIGHT: field  UP/DOWN: change  0-9: type  ENTER: write  ESC: cancel", 0);
    k_snprintf(line, sizeof(line), "Clock now: %s", now);
    mt_work_line(3, line, 0);
    mt_clock_text(f, text, sizeof(text));
    k_snprintf(line, sizeof(line), "New time:  %s", text);
    mt_work_line(4, line, 0);
}

void mt_tool_clock(mt_run_t *r)
{
    static mt_clock_form_t form;
    datetime_t now;
    datetime_t want;
    datetime_t got;
    input_event_t ev;
    char now_text[DT_ISO_SIZE + 16];
    char want_text[DT_ISO_SIZE];
    char got_text[DT_ISO_SIZE];
    char line[MT_LINE_MAX];
    int rc;
    int ap;

    rc = mt_i2c();
    if (rc != 0) {
        r->result = MT_FAIL;
        mt_keys(r, "why=i2c rc=%d", rc);
        mt_text(r, "I2C does not start.");
        return;
    }
    rc = rtc_init();
    ap = mt_clock_probe(rc, rtc_present());
    if (ap == MT_CP_ABSENT) {
        r->result = MT_ABSENT;
        mt_keys(r, "addr=0x68 rc=%d", rc);
        mt_text(r, "No clock chip at 0x68.");
        mt_work_line(0, "No clock chip at 0x68. Nothing to set.", 1);
        return;
    }
    if (ap == MT_CP_ERROR) {
        /* The probe could not tell whether a chip is there. */
        r->result = MT_FAIL;
        mt_keys(r, "why=probe addr=0x68 rc=%d", rc);
        mt_text(r, "Clock probe error %d.", rc);
        k_snprintf(line, sizeof(line), "The probe of 0x68 failed: error %d. Nothing written.", rc);
        mt_work_line(0, line, 1);
        return;
    }
    mt_keys(r, "chip=%s", chip_name(rtc_chip()));
    rc = rtc_get(&now);
    ap = mt_clock_now(rc);
    if (ap == MT_CN_OK) {
        (void)dt_format_iso(&now, now_text, sizeof(now_text));
    } else if (ap == MT_CN_INVALID) {
        k_snprintf(now_text, sizeof(now_text), "not valid (%d)", rc);
    } else {
        k_snprintf(now_text, sizeof(now_text), "read error %d", rc);
        mt_keys(r, "now_rc=%d", rc);
    }
    mt_clock_init(&form, ap == MT_CN_OK ? &now : NULL);
    draw_form(&form, now_text);
    if (ap == MT_CN_OK) {
        mt_mark("clock_form now=%s", now_text);
    } else {
        mt_mark("clock_form now=%s rc=%d", ap == MT_CN_INVALID ? "invalid" : "error", rc);
    }
    for (;;) {
        int e;

        if (!mt_wait_event(&ev, 50)) {
            continue;
        }
        e = mt_clock_event(&form, &ev);
        if (e == MT_CF_CHANGED) {
            draw_form(&form, now_text);
            mt_work_line(6, "", 0);
        } else if (e == MT_CF_CANCEL) {
            r->result = MT_REFUSED;
            mt_keys(r, "why=cancel");
            mt_text(r, "Cancelled. Nothing written.");
            mt_work_clear();
            return;
        } else if (e == MT_CF_ENTER) {
            if (mt_clock_get(&form, &want) == 0) {
                break;
            }
            mt_mark("clock_invalid value=%04u-%02u-%02uT%02u:%02u:%02u", form.v[0], form.v[1],
                    form.v[2], form.v[3], form.v[4], form.v[5]);
            mt_work_line(6, "Not a valid date and time. Change it, or ESC.", 1);
        }
    }
    (void)dt_format_iso(&want, want_text, sizeof(want_text));
    k_snprintf(line, sizeof(line), "Write %s to the clock chip? ENTER or Y: yes. Other keys: no.",
               want_text);
    mt_work_line(6, line, 1);
    mt_mark("ask what=set_clock value=%s", want_text);
    if (mt_ask() != MT_ANS_YES) {
        r->result = MT_REFUSED;
        mt_keys(r, "why=no");
        mt_text(r, "No. Nothing written.");
        mt_work_line(7, "No. Nothing written.", 1);
        return;
    }
    mt_keys(r, "set=%s", want_text);
    ap = mt_clock_apply(&want, rtc_set, rtc_get, &got, &rc);
    if (ap == MT_CF_ESET) {
        r->result = MT_FAIL;
        mt_keys(r, "why=set rc=%d", rc);
        mt_text(r, "Write failed: %d.", rc);
        return;
    }
    if (ap == MT_CF_EREAD) {
        r->result = MT_FAIL;
        mt_keys(r, "why=readback rc=%d", rc);
        mt_text(r, "Read back failed: %d.", rc);
        return;
    }
    (void)dt_format_iso(&got, got_text, sizeof(got_text));
    clock_set(&got);
    mt_keys(r, "read=%s", got_text);
    k_snprintf(line, sizeof(line), "Wrote %s, read back %s.", want_text, got_text);
    mt_work_line(7, line, 1);
    if (ap == MT_CF_EDIFF) {
        r->result = MT_FAIL;
        mt_keys(r, "why=readback");
        mt_text(r, "Read back differs.");
        return;
    }
    r->result = MT_OK;
    mt_text(r, "Set to %s.", want_text);
}
