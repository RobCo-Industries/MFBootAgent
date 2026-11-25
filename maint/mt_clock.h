/*
 * The entry form of the set clock tool. Pure: the host tests link it.
 *
 * Six fields: year, month, day, hour, minute, second. LEFT and RIGHT
 * move between fields. UP and DOWN step the field and wrap inside its
 * range. Digits typed on a serial console replace the field: the first
 * digit starts it again, and a full field moves to the next one. ENTER,
 * which SELECT gives, asks to write. BACK, which ESC gives, or Ctrl-C
 * cancels. The date is checked only at ENTER, with dt_valid(): the years
 * 2000 to 2099 the clock chip holds, and the days of each month.
 */
#ifndef MT_CLOCK_H
#define MT_CLOCK_H

#include <stddef.h>

#include <robco/datetime.h>
#include <robco/input.h>

#define MT_CF_FIELDS    6u

/* mt_clock_event() results. */
#define MT_CF_NONE      0   /* nothing changed */
#define MT_CF_CHANGED   1   /* a field or the cursor changed: draw again */
#define MT_CF_ENTER     2   /* the person asks to write */
#define MT_CF_CANCEL    3

typedef struct {
    unsigned v[MT_CF_FIELDS];   /* year, month, day, hour, minute, second */
    unsigned field;             /* the field the cursor is on */
    unsigned typed;             /* digits typed into the field so far */
} mt_clock_form_t;

/* Starts the form at dt, or at 2000-01-01 00:00:00 for NULL or an invalid dt. */
void mt_clock_init(mt_clock_form_t *f, const datetime_t *dt);

int mt_clock_event(mt_clock_form_t *f, const input_event_t *ev);

/* The form as a date and time. Returns 0, or DT_ERR_RANGE when it is not valid. */
int mt_clock_get(const mt_clock_form_t *f, datetime_t *out);

/*
 * "2026-11-25 10:04:00" with the field under the cursor in brackets:
 * "[2026]-11-25 10:04:00". 24 bytes are enough.
 */
void mt_clock_text(const mt_clock_form_t *f, char *out, size_t size);

/* A read back this many seconds after the written time still matches. */
#define MT_CF_SLACK_S   2u

/* mt_clock_apply() results. */
#define MT_CF_OK        0
#define MT_CF_ESET      1   /* set() failed: *rc holds its code */
#define MT_CF_EREAD     2   /* get() failed after the write: *rc holds its code */
#define MT_CF_EDIFF     3   /* the read back is not 0 to MT_CF_SLACK_S s after want */

/*
 * Writes want with set(), reads it back with get() into *got and checks
 * the read back. The clock is behind the two calls so the host tests can
 * fake it. Returns MT_CF_OK or an MT_CF_E* code.
 */
int mt_clock_apply(const datetime_t *want, int (*set)(const datetime_t *dt),
                   int (*get)(datetime_t *out), datetime_t *got, int *rc);

/*
 * The probe: rtc_init()'s code and rtc_present(). MT_CP_ABSENT only for
 * RTC_ERR_ABSENT, nothing at 0x68. Any other failure, such as an I2C
 * timeout, could not tell whether a chip is there: MT_CP_ERROR.
 */
#define MT_CP_OK        0
#define MT_CP_ABSENT    1
#define MT_CP_ERROR     2
int mt_clock_probe(int init_rc, int present);

/*
 * The chip's time before the form: rtc_get()'s code. MT_CN_INVALID for
 * RTC_ERR_HALTED and RTC_ERR_DATA, a chip that answered with no valid
 * time. MT_CN_ERROR for any other code: the read could not tell.
 */
#define MT_CN_OK        0
#define MT_CN_INVALID   1
#define MT_CN_ERROR     2
int mt_clock_now(int get_rc);

#endif
