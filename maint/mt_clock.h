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

#endif
