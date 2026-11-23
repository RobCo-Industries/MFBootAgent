/*
 * The short tests: timer, rng and rtc. None of them writes to a device
 * beyond what its driver needs to read: the RTC test never sets the time.
 */

#include <stdint.h>

#include <robco/datetime.h>
#include <robco/i2c.h>
#include <robco/klibc.h>
#include <robco/rng.h>
#include <robco/rtc.h>
#include <robco/tick.h>
#include <robco/timer.h>

#include "dg.h"

/* ---- Timer ----------------------------------------------------------- */

/*
 * The tick interrupt reads the system timer at its first tick and
 * TICKS later: 1 s at 100 Hz. The limit is one tick period over the
 * window, 10000 ppm; a missed tick fails.
 */
#define TICKS       100u
#define TIMER_BOUND_MS 3000u

static volatile int armed;      /* 1: waiting for the first tick, 2: counting */
static volatile uint32_t counted;
static volatile uint64_t t_first;
static volatile uint64_t t_last;

void dg_timer_tick(void)
{
    if (armed == 1) {
        t_first = timer_now_us();
        counted = 0;
        armed = 2;
    } else if (armed == 2) {
        counted++;
        if (counted == TICKS) {
            t_last = timer_now_us();
            armed = 0;
        }
    }
}

void dg_test_timer(dg_run_t *r)
{
    uint32_t t0 = timer_now_ms();
    uint32_t missed0 = tick_missed();
    uint32_t missed;
    uint32_t limit = 0;
    int32_t ppm = 0;

    counted = 0;
    armed = 1;
    while (armed != 0) {
        if (dg_poll_abort()) {
            armed = 0;
            r->result = DG_SKIPPED;
            dg_keys(r, "why=abort");
            dg_text(r, "Stopped.");
            return;
        }
        if (timer_now_ms() - t0 > TIMER_BOUND_MS) {
            armed = 0;
            r->result = DG_FAIL;
            dg_keys(r, "why=notick ticks=%u", (unsigned)counted);
            dg_text(r, "The tick stopped after %u ticks.", (unsigned)counted);
            return;
        }
        timer_delay_ms(5);
    }
    missed = tick_missed() - missed0;
    r->result = dg_timer_judge(t_last - t_first, TICKS, tick_hz(), missed, &ppm, &limit);
    dg_keys(r, "ticks=%u hz=%u us=%u ppm=%d limit=%u missed=%u", TICKS, (unsigned)tick_hz(),
            (unsigned)(t_last - t_first), (int)ppm, (unsigned)limit, (unsigned)missed);
    dg_text(r, "%d ppm over %u ticks, %u missed", (int)ppm, TICKS, (unsigned)missed);
}

/* ---- RNG ------------------------------------------------------------- */

#define RNG_WORDS 1024u

void dg_test_rng(dg_run_t *r)
{
    static uint32_t w[RNG_WORDS];
    uint32_t repeats = 0;
    uint32_t bits = 0;
    uint32_t i;
    int rc = rng_init();

    if (rc != 0) {
        r->result = rc == RNG_ERR_BOARD ? DG_ABSENT : DG_FAIL;
        dg_keys(r, "why=init rc=%d", rc);
        dg_text(r, "No generator.");
        return;
    }
    for (i = 0; i < RNG_WORDS; i++) {
        rc = rng_u32(&w[i]);
        if (rc != 0) {
            r->result = DG_FAIL;
            dg_keys(r, "why=read rc=%d words=%u", rc, (unsigned)i);
            dg_text(r, "No data after %u words.", (unsigned)i);
            return;
        }
    }
    r->result = dg_rng_judge(w, RNG_WORDS, &repeats, &bits);
    dg_keys(r, "words=%u repeats=%u bits=%u", RNG_WORDS, (unsigned)repeats, (unsigned)bits);
    dg_text(r, "%u repeats, %u of %u bits set", (unsigned)repeats, (unsigned)bits,
            RNG_WORDS * 32u);
}

/* ---- RTC ------------------------------------------------------------- */

void dg_test_rtc(dg_run_t *r)
{
    datetime_t now;
    char iso[DT_ISO_SIZE];
    int rc = dg_i2c();

    if (rc != 0) {
        r->result = DG_FAIL;
        dg_keys(r, "why=i2c rc=%d", rc);
        dg_text(r, "I2C does not start.");
        return;
    }
    rc = rtc_init();
    if (rc == RTC_ERR_ABSENT) {
        r->result = DG_ABSENT;
        dg_keys(r, "addr=0x%02x", RTC_I2C_ADDR);
        dg_text(r, "No clock chip at 0x%02x.", RTC_I2C_ADDR);
        return;
    }
    if (rc != 0) {
        r->result = DG_FAIL;
        dg_keys(r, "addr=0x%02x why=init rc=%d", RTC_I2C_ADDR, rc);
        dg_text(r, "The clock chip does not answer: %d.", rc);
        return;
    }
    dg_keys(r, "addr=0x%02x chip=%s", RTC_I2C_ADDR,
            rtc_chip() == RTC_CHIP_DS3231 ? "ds3231" : "ds1307");
    rc = rtc_get(&now);
    if (rc == RTC_ERR_HALTED) {
        r->result = DG_FAIL;
        dg_keys(r, "halted=1");
        dg_text(r, "The oscillator is stopped: the time is not valid.");
        return;
    }
    if (rc != 0 || dt_format_iso(&now, iso, sizeof(iso)) < 0) {
        r->result = DG_FAIL;
        dg_keys(r, "halted=0 why=read rc=%d", rc);
        dg_text(r, "The time does not read: %d.", rc);
        return;
    }
    r->result = DG_OK;
    dg_keys(r, "halted=0 time=%s", iso);
    dg_text(r, "%s UTC", iso);
    dg_work_line(0, r->text, 1);
}
