/* The entry form of the set clock tool: see mt_clock.h. */

#include <stddef.h>

#include <robco/datetime.h>
#include <robco/klibc.h>

#include "mt_clock.h"

static const unsigned lo[MT_CF_FIELDS] = { DT_YEAR_MIN, 1, 1, 0, 0, 0 };
static const unsigned hi[MT_CF_FIELDS] = { DT_YEAR_MAX, 12, 31, 23, 59, 59 };
static const unsigned width[MT_CF_FIELDS] = { 4, 2, 2, 2, 2, 2 };

void mt_clock_init(mt_clock_form_t *f, const datetime_t *dt)
{
    if (dt != NULL && dt_valid(dt) == 0) {
        f->v[0] = dt->year;
        f->v[1] = dt->month;
        f->v[2] = dt->day;
        f->v[3] = dt->hour;
        f->v[4] = dt->minute;
        f->v[5] = dt->second;
    } else {
        f->v[0] = DT_YEAR_MIN;
        f->v[1] = 1;
        f->v[2] = 1;
        f->v[3] = 0;
        f->v[4] = 0;
        f->v[5] = 0;
    }
    f->field = 0;
    f->typed = 0;
}

static void step(mt_clock_form_t *f, int up)
{
    unsigned *v = &f->v[f->field];
    unsigned a = lo[f->field];
    unsigned b = hi[f->field];

    /* A typed value out of range starts again from the range end it passed. */
    if (*v < a || *v > b) {
        *v = up ? a : b;
    } else if (up) {
        *v = *v == b ? a : *v + 1u;
    } else {
        *v = *v == a ? b : *v - 1u;
    }
    f->typed = 0;
}

static void move(mt_clock_form_t *f, int right)
{
    if (right && f->field + 1u < MT_CF_FIELDS) {
        f->field++;
    } else if (!right && f->field > 0) {
        f->field--;
    }
    f->typed = 0;
}

int mt_clock_event(mt_clock_form_t *f, const input_event_t *ev)
{
    if (f == NULL || ev == NULL) {
        return MT_CF_NONE;
    }
    if (ev->type == INPUT_EV_KEY) {
        if (ev->value == INPUT_RELEASE) {
            return MT_CF_NONE;
        }
        switch (ev->code) {
        case INPUT_KEY_UP:
            step(f, 1);
            return MT_CF_CHANGED;
        case INPUT_KEY_DOWN:
            step(f, 0);
            return MT_CF_CHANGED;
        case INPUT_KEY_LEFT:
            move(f, 0);
            return MT_CF_CHANGED;
        case INPUT_KEY_RIGHT:
            move(f, 1);
            return MT_CF_CHANGED;
        case INPUT_KEY_SELECT:
            return ev->value == INPUT_PRESS ? MT_CF_ENTER : MT_CF_NONE;
        case INPUT_KEY_BACK:
            return ev->value == INPUT_PRESS ? MT_CF_CANCEL : MT_CF_NONE;
        default:
            return MT_CF_NONE;
        }
    }
    if (ev->type == INPUT_EV_CHAR) {
        unsigned c = ev->code;

        if (c == 0x03) {
            return MT_CF_CANCEL;
        }
        if (c >= '0' && c <= '9') {
            unsigned d = c - '0';

            f->v[f->field] = f->typed == 0 ? d : f->v[f->field] * 10u + d;
            f->typed++;
            if (f->typed >= width[f->field]) {
                move(f, 1);
            }
            return MT_CF_CHANGED;
        }
    }
    return MT_CF_NONE;
}

int mt_clock_get(const mt_clock_form_t *f, datetime_t *out)
{
    unsigned i;

    for (i = 0; i < MT_CF_FIELDS; i++) {
        if (f->v[i] < lo[i] || f->v[i] > hi[i]) {
            return DT_ERR_RANGE;
        }
    }
    out->year = (uint16_t)f->v[0];
    out->month = (uint8_t)f->v[1];
    out->day = (uint8_t)f->v[2];
    out->hour = (uint8_t)f->v[3];
    out->minute = (uint8_t)f->v[4];
    out->second = (uint8_t)f->v[5];
    if (dt_valid(out) != 0) {
        return DT_ERR_RANGE;
    }
    out->weekday = (uint8_t)dt_weekday(out->year, out->month, out->day);
    return 0;
}

void mt_clock_text(const mt_clock_form_t *f, char *out, size_t size)
{
    static const char seps[MT_CF_FIELDS] = { '-', '-', ' ', ':', ':', '\0' };
    size_t n = 0;
    unsigned i;

    if (size == 0) {
        return;
    }
    out[0] = '\0';
    for (i = 0; i < MT_CF_FIELDS && n + 1 < size; i++) {
        int cur = i == f->field;

        k_snprintf(out + n, size - n, i == 0 ? "%s%04u%s%c" : "%s%02u%s%c", cur ? "[" : "",
                   f->v[i] % (i == 0 ? 10000u : 100u), cur ? "]" : "", seps[i]);
        n = k_strlen(out);
    }
}

int mt_clock_apply(const datetime_t *want, int (*set)(const datetime_t *dt),
                   int (*get)(datetime_t *out), datetime_t *got, int *rc)
{
    uint32_t ws = 0;
    uint32_t gs = 0;

    *rc = set(want);
    if (*rc != 0) {
        return MT_CF_ESET;
    }
    *rc = get(got);
    if (*rc != 0) {
        return MT_CF_EREAD;
    }
    if (dt_to_seconds(want, &ws) != 0 || dt_to_seconds(got, &gs) != 0 || gs < ws ||
        gs - ws > MT_CF_SLACK_S) {
        return MT_CF_EDIFF;
    }
    return MT_CF_OK;
}
