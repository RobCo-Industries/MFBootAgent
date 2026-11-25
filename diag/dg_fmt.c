/* Results, lines, progress and verdicts. Pure: see dg_fmt.h. */

#include <stdint.h>

#include <robco/klibc.h>

#include "dg_fmt.h"
#include "dg_menu.h"

static const char *const titles[DG_ITEMS] = {
    "MEM", "SD", "VIDEO", "AUDIO", "INPUT", "TIMER", "RNG", "RTC", "EXPANDER", "RUN ALL", "REBOOT",
};

static const char *const what[DG_ITEMS] = {
    "RAM write and read",
    "Card reads, image checks",
    "Screen patterns",
    "Tones and a short tune",
    "Live view of every key",
    "Timer against the tick",
    "Random number generator",
    "Clock chip, read only",
    "I/O expanders, read only",
    "Every test without a person",
    "Restart the board",
};

const char *dg_result_name(int r)
{
    switch (r) {
    case DG_OK:      return "ok";
    case DG_FAIL:    return "fail";
    case DG_ABSENT:  return "absent";
    case DG_SKIPPED: return "skipped";
    default:         return "none";
    }
}

const char *dg_result_word(int r)
{
    switch (r) {
    case DG_OK:      return "OK";
    case DG_FAIL:    return "FAILED";
    case DG_ABSENT:  return "ABSENT";
    case DG_SKIPPED: return "SKIPPED";
    default:         return "";
    }
}

void dg_counts_init(dg_counts_t *c)
{
    k_memset(c, 0, sizeof(*c));
}

void dg_counts_add(dg_counts_t *c, int r)
{
    if (c != NULL && r >= DG_OK && r < DG_RESULTS) {
        c->n[r]++;
    }
}

int dg_counts_result(const dg_counts_t *c, int stopped)
{
    if (c->n[DG_FAIL] != 0) {
        return DG_FAIL;
    }
    if (stopped) {
        return DG_SKIPPED;
    }
    return c->n[DG_OK] != 0 ? DG_OK : DG_ABSENT;
}

void dg_line_title(char *buf, size_t size, const char *version)
{
    k_snprintf(buf, size, "TERMINAL DIAGNOSTICS V%s", version);
}

const char *dg_line_sub(void)
{
    return "Tests for this terminal. Each result also goes to the serial port.";
}

const char *dg_line_rule(void)
{
    return "========================================";
}

const char *dg_line_help(void)
{
    return "UP/DOWN, 1-9, A, R: select   ENTER: start   ESC: stop a test";
}

const char *dg_item_title(unsigned item)
{
    return item < DG_ITEMS ? titles[item] : "";
}

static char item_key(unsigned item)
{
    if (item < DG_TESTS) {
        return (char)('1' + item);
    }
    return item == DG_ITEM_ALL ? 'A' : 'R';
}

void dg_line_item(char *buf, size_t size, unsigned item, int selected, int result,
                  const char *text)
{
    if (item >= DG_ITEMS) {
        if (size > 0) {
            buf[0] = '\0';
        }
        return;
    }
    if (result == DG_NONE || result < 0 || result > DG_NONE) {
        k_snprintf(buf, size, "%c %c  %-9s %s", selected ? '>' : ' ', item_key(item),
                   titles[item], what[item]);
    } else {
        k_snprintf(buf, size, "%c %c  %-9s %-28s %-8s %s", selected ? '>' : ' ', item_key(item),
                   titles[item], what[item], dg_result_word(result), text != NULL ? text : "");
    }
}

void dg_line_status_one(char *buf, size_t size, unsigned test, int result, const char *text)
{
    k_snprintf(buf, size, "%s: %s.%s%s", dg_item_title(test), dg_result_word(result),
               text != NULL && text[0] != '\0' ? " " : "", text != NULL ? text : "");
}

void dg_line_status_all(char *buf, size_t size, const dg_counts_t *c)
{
    k_snprintf(buf, size, "Run all: %u OK, %u FAILED, %u ABSENT, %u SKIPPED.", c->n[DG_OK],
               c->n[DG_FAIL], c->n[DG_ABSENT], c->n[DG_SKIPPED]);
}

const char *dg_line_status_idle(void)
{
    return "Choose a test.";
}

void dg_line_diag(char *buf, size_t size, const char *name, int result, const char *keys)
{
    k_snprintf(buf, size, "diag name=%s result=%s%s%s", name, dg_result_name(result),
               keys != NULL && keys[0] != '\0' ? " " : "", keys != NULL ? keys : "");
}

void dg_line_done(char *buf, size_t size, const dg_counts_t *c)
{
    k_snprintf(buf, size, "diag_done ok=%u fail=%u absent=%u skipped=%u", c->n[DG_OK],
               c->n[DG_FAIL], c->n[DG_ABSENT], c->n[DG_SKIPPED]);
}

static const char *level(int have, uint8_t bits, unsigned pin)
{
    if (!have) {
        return "--";
    }
    return (bits >> pin) & 1u ? "HIGH" : "LOW";
}

void dg_line_xrow(char *buf, size_t size, unsigned pin, int have0, uint8_t bits0, int have1,
                  uint8_t bits1)
{
    k_snprintf(buf, size, "X0.%u  %-4s          X1.%u  %-4s", pin & 7u, level(have0, bits0, pin & 7u),
               pin & 7u, level(have1, bits1, pin & 7u));
}

void dg_line_xregs(char *buf, size_t size, unsigned chip, unsigned addr, int present,
                   uint8_t in, uint8_t out, uint8_t pol, uint8_t cfg)
{
    if (!present) {
        k_snprintf(buf, size, "X%u at 0x%02x: absent", chip, addr);
    } else {
        k_snprintf(buf, size, "X%u at 0x%02x: IN 0x%02x  OUT 0x%02x  POL 0x%02x  CFG 0x%02x", chip,
                   addr, in, out, pol, cfg);
    }
}

void dg_line_button(char *buf, size_t size, const char *key, int pressed)
{
    k_snprintf(buf, size, "%-8s %s", key != NULL ? key : "?", pressed ? "PRESSED" : "released");
}

const char *dg_key_name(unsigned code)
{
    /* The names of the script backend's key command. */
    static const char *const keys[INPUT_KEY_COUNT] = {
        "none", "up", "down", "left", "right", "select", "back",
        "f1", "f2", "f3", "f4", "dial_push",
    };

    return code < INPUT_KEY_COUNT ? keys[code] : "unknown";
}

const char *dg_source_name(unsigned source)
{
    switch (source) {
    case INPUT_SRC_GPIO:   return "gpio";
    case INPUT_SRC_SERIAL: return "serial";
    case INPUT_SRC_SCRIPT: return "script";
    default:               return "other";
    }
}

void dg_line_event(char *buf, size_t size, const input_event_t *ev)
{
    const char *src = dg_source_name(ev->source);

    if (ev->type == INPUT_EV_KEY) {
        const char *name = dg_key_name(ev->code);
        const char *v = ev->value == INPUT_PRESS ? "press" :
                        ev->value == INPUT_RELEASE ? "release" : "repeat";

        k_snprintf(buf, size, "%-7s key   %-9s %s", src, name, v);
    } else if (ev->type == INPUT_EV_CHAR) {
        unsigned c = ev->code;

        if (c >= 0x20u && c < 0x7fu) {
            k_snprintf(buf, size, "%-7s char  0x%02x '%c'", src, c, (char)c);
        } else {
            k_snprintf(buf, size, "%-7s char  0x%02x", src, c & 0xffffu);
        }
    } else if (ev->type == INPUT_EV_DIAL) {
        k_snprintf(buf, size, "%-7s dial  %u steps %d", src, (unsigned)ev->code, (int)ev->value);
    } else {
        k_snprintf(buf, size, "%-7s type %u", src, (unsigned)ev->type);
    }
}

void dg_rate_init(dg_rate_t *r, uint32_t period_ms)
{
    r->period_ms = period_ms;
    r->last_ms = 0;
    r->started = 0;
}

int dg_rate_due(dg_rate_t *r, uint32_t now_ms)
{
    if (!r->started || now_ms - r->last_ms >= r->period_ms) {
        r->started = 1;
        r->last_ms = now_ms;
        return 1;
    }
    return 0;
}

unsigned dg_percent(uint64_t done, uint64_t total)
{
    if (total == 0 || done >= total) {
        return total == 0 ? 0 : 100;
    }
    /* done < total: divide first when done * 100 could overflow, and stay below 100. */
    if (done > UINT64_MAX / 100u) {
        uint64_t pct = done / (total / 100u);

        return pct > 99u ? 99u : (unsigned)pct;
    }
    return (unsigned)(done * 100u / total);
}

int dg_timer_judge(uint64_t timer_us, uint32_t ticks, uint32_t hz, uint32_t missed,
                   int32_t *ppm, uint32_t *limit_ppm)
{
    uint64_t want;
    int64_t diff;
    int64_t p;
    uint32_t limit;

    if (ticks == 0 || hz == 0) {
        return DG_SKIPPED;
    }
    want = (uint64_t)ticks * 1000000u / hz;
    limit = 1000000u / ticks;
    if (want == 0) {
        return DG_SKIPPED;
    }
    if (timer_us > (uint64_t)INT32_MAX * want / 1000000u + want) {
        p = INT32_MAX;     /* far too slow: no overflow below */
    } else {
        diff = (int64_t)timer_us - (int64_t)want;
        p = diff * 1000000 / (int64_t)want;
    }
    if (p > INT32_MAX) {
        p = INT32_MAX;
    } else if (p < -INT32_MAX) {
        p = -INT32_MAX;
    }
    if (ppm != NULL) {
        *ppm = (int32_t)p;
    }
    if (limit_ppm != NULL) {
        *limit_ppm = limit;
    }
    if (missed != 0 || p > (int64_t)limit || p < -(int64_t)limit) {
        return DG_FAIL;
    }
    return DG_OK;
}

int dg_rng_judge(const uint32_t *w, uint32_t n, uint32_t *repeats, uint32_t *bits)
{
    uint32_t rep = 0;
    uint64_t set = 0;
    uint64_t half;
    uint32_t i;

    if (w == NULL || n < 2) {
        return DG_SKIPPED;
    }
    for (i = 0; i < n; i++) {
        uint32_t v = w[i];

        if (i > 0 && v == w[i - 1]) {
            rep++;
        }
        while (v != 0) {
            v &= v - 1u;
            set++;
        }
    }
    half = (uint64_t)n * 16u;
    if (repeats != NULL) {
        *repeats = rep;
    }
    if (bits != NULL) {
        *bits = (uint32_t)set;
    }
    if (rep != 0 || set + n / 2u < half || set > half + n / 2u) {
        return DG_FAIL;
    }
    return DG_OK;
}

int dg_x_attach(uint8_t pol, int (*attach)(unsigned, uint8_t), unsigned chip, uint8_t addr,
                int *rc)
{
    *rc = 0;
    if (pol != 0) {
        return DG_X_RAW;
    }
    *rc = attach(chip, addr);
    return *rc == 0 ? DG_X_ATTACHED : DG_X_ATTACH_ERR;
}
