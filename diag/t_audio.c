/*
 * The audio test. A sweep of tones, then a short tune, through audio.h.
 * It reports the backend the library chose: "pwm" on hardware, "trace"
 * where the PWM does not answer, as in QEMU. The trace backend prints
 * one "audio" marker per call, which proves the calls but not a sound.
 * Each sound has a bound: its own length plus WAIT_SLACK_MS. Errors from
 * the driver fail the test. A clean run gets its verdict from
 * dg_audio_judge(): OK on the PWM backend, ABSENT on the trace backend
 * when no PWM was found, FAILED when the PWM failed and the trace took
 * over. OK means the PWM registers answered and the calls ran in time;
 * a person must listen to know that sound came out.
 */

#include <stdint.h>

#include <robco/audio.h>
#include <robco/klibc.h>
#include <robco/timer.h>

#include "dg.h"

#define TONE_MS         150u
#define WAIT_SLACK_MS   500u

static const uint32_t sweep[] = { 250, 500, 1000, 2000, 4000 };
#define TONES (sizeof(sweep) / sizeof(sweep[0]))

static const audio_note_t tune[] = {
    { 523, 120 }, { 659, 120 }, { 784, 120 }, { 0, 60 }, { 784, 120 }, { 1047, 240 },
};
#define NOTES (sizeof(tune) / sizeof(tune[0]))

static int inited;
static int init_rc;

/* Waits while a sound plays, at most ms. 1 on ESC, 2 when the bound ran out, else 0. */
static int wait_done(uint32_t ms)
{
    uint32_t t0 = timer_now_ms();

    for (;;) {
        audio_poll();
        if (!audio_busy()) {
            return 0;
        }
        if (dg_poll_abort()) {
            audio_stop();
            return 1;
        }
        if (timer_now_ms() - t0 >= ms) {
            audio_stop();
            return 2;
        }
        timer_delay_ms(2);
    }
}

static void stopped(dg_run_t *r, int w, const char *step)
{
    if (w == 1) {
        r->result = DG_SKIPPED;
        dg_keys(r, "why=abort step=%s", step);
        dg_text(r, DG_TX_STOPPED);
    } else {
        r->result = DG_FAIL;
        dg_keys(r, "why=timeout step=%s", step);
        dg_text(r, DG_TX_AUD_LONG, step);
    }
}

void dg_test_audio(dg_run_t *r)
{
    uint32_t ms = 0;
    unsigned i;
    int rc;
    int w;
    int v;

    if (!inited) {
        init_rc = audio_init();
        inited = 1;
    }
    dg_keys(r, "backend=%s probe=%s init=%d", audio_backend(),
            audio_probe_name(audio_probe_result()), init_rc);
    if (dg_audio_start(init_rc) == DG_AUD_NOINIT) {
        r->result = DG_FAIL;
        dg_text(r, DG_TX_AUD_INIT, init_rc);
        return;
    }
    dg_work_line(0, "Sweep: 250 Hz to 4000 Hz", 1);
    for (i = 0; i < TONES; i++) {
        rc = audio_tone(sweep[i], TONE_MS);
        if (rc != 0) {
            r->result = DG_FAIL;
            dg_keys(r, "why=tone hz=%u rc=%d", (unsigned)sweep[i], rc);
            dg_text(r, DG_TX_AUD_TONE, (unsigned)sweep[i], rc);
            return;
        }
        w = wait_done(TONE_MS + WAIT_SLACK_MS);
        if (w != 0) {
            stopped(r, w, "sweep");
            return;
        }
    }
    dg_work_line(1, "Tune: six notes", 1);
    rc = audio_play(tune, NOTES, 0);
    if (rc != 0) {
        r->result = DG_FAIL;
        dg_keys(r, "why=play rc=%d", rc);
        dg_text(r, DG_TX_AUD_TUNE, rc);
        return;
    }
    for (i = 0; i < NOTES; i++) {
        ms += tune[i].ms;
    }
    w = wait_done(ms + WAIT_SLACK_MS);
    if (w != 0) {
        stopped(r, w, "tune");
        return;
    }
    dg_keys(r, "tones=%u notes=%u", (unsigned)TONES, (unsigned)NOTES);
    v = dg_audio_judge(init_rc, k_strcmp(audio_backend(), "trace") == 0);
    r->result = dg_audio_result(v);
    if (v == DG_AUD_FELLBACK) {
        /* PWM failed and the trace backend played: the calls ran, no sound did. */
        dg_keys(r, "why=pwm_failed");
        dg_text(r, DG_TX_AUD_PWM, init_rc);
    } else if (v == DG_AUD_TRACE) {
        /* No PWM found: the trace backend played, so no sound could come out. */
        dg_keys(r, "why=no_pwm output=none");
        dg_text(r, DG_TX_AUD_NONE);
    } else {
        dg_text(r, DG_TX_AUD_OK, audio_backend(), (unsigned)TONES, (unsigned)NOTES);
    }
}
