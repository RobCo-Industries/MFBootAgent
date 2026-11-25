/*
 * Shared state and helpers of the hardware side of the diagnostics image:
 * main.c and the t_*.c test files. Not used by the pure files.
 */
#ifndef DG_H
#define DG_H

#include <stdint.h>

#include <robco/input.h>

#include "dg_fmt.h"
#include "dg_menu.h"

#define DG_TICK_HZ      100u
/* Progress lines on the screen and the serial port: at most one per period. */
#define DG_PROGRESS_MS  500u

/* How a test runs: started alone, with a person, or from "run all". */
#define DG_MODE_ONE     0
#define DG_MODE_ALL     1

/* Phases the maintenance port reports. */
#define DG_PHASE_INIT   0
#define DG_PHASE_MENU   1
#define DG_PHASE_TEST   2

/* One test run: what it reports. */
typedef struct {
    int mode;                   /* DG_MODE_* */
    int result;                 /* DG_OK ... */
    char text[DG_TEXT_MAX];     /* short detail for the screen */
    char keys[DG_KEYS_MAX];     /* marker keys after result=, space separated */
} dg_run_t;

typedef struct {
    int phase;          /* DG_PHASE_* */
    int fb_ok;          /* the framebuffer console works */
    int trusted;        /* the boot info passed the trust rule */
    int test;           /* the running test, or -1 */
    int i2c_rc;         /* i2c_init() result, 1 before the first call */
    uint32_t mem_base;  /* ARM RAM from the boot info or the mailbox */
    uint32_t mem_size;
#ifdef DG_TEST
    uint32_t fault_addr;    /* test builds: the memory test plants a fault here, 0 for none */
    int sd_probe_write;     /* test builds: the SD test sends one write to its guard */
#endif
} dg_state_t;

extern dg_state_t dg;

/* One marker line on the PL011. fmt starts after "ev=". */
void dg_mark(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Appends to the marker keys of a run, with a space between keys. */
void dg_keys(dg_run_t *r, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* Sets the screen detail of a run. */
void dg_text(dg_run_t *r, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* Clears the work area under the menu. */
void dg_work_clear(void);
/* Writes row `row` of the work area, padded to the last column. to_uart: also one PL011 line. */
void dg_work_line(unsigned row, const char *text, int to_uart);

/*
 * Reads every waiting input event and drops it. Returns 1 when one of them
 * asked to stop: a press of BACK or ESC. Runs the maintenance port too.
 */
int dg_poll_abort(void);

/*
 * Waits up to ms for an input event, running the maintenance port.
 * Returns 1 with the event, or 0 when the time ran out.
 */
int dg_wait_event(input_event_t *ev, uint32_t ms);

/*
 * One progress line when the rate allows: a work row and a marker
 * "diag_progress name=<name> step=<step> pct=<n>".
 */
void dg_progress(dg_rate_t *rate, unsigned row, const char *name, const char *step,
                 uint64_t done, uint64_t total);

/* Starts I2C1 once at 100 kHz. Returns the i2c_init() result. */
int dg_i2c(void);

/* The tests. Each fills r->result, r->text and r->keys. */
void dg_test_mem(dg_run_t *r);
void dg_test_sd(dg_run_t *r);
void dg_test_video(dg_run_t *r);
void dg_test_audio(dg_run_t *r);
void dg_test_input(dg_run_t *r);
void dg_test_timer(dg_run_t *r);
void dg_test_rng(dg_run_t *r);
void dg_test_rtc(dg_run_t *r);
void dg_test_expander(dg_run_t *r);

/* For the timer test: main.c registers it as a tick callback at start. */
void dg_timer_tick(void);

/* The GPIO buttons the input backend watches, for the input live view. */
typedef struct {
    unsigned pin;
    unsigned code;
} dg_button_t;

#define DG_BUTTONS 7u
extern const dg_button_t dg_buttons[DG_BUTTONS];

#endif
