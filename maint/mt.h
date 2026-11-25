/*
 * Shared state and helpers of the hardware side of the maintenance image:
 * main.c and the t_*.c tool files. Not used by the pure files.
 */
#ifndef MT_H
#define MT_H

#include <stdint.h>

#include <robco/fs.h>
#include <robco/input.h>

#include "mt_cfg.h"
#include "mt_ui.h"
#include "../diag/dg_fmt.h"
#include "../diag/dg_sd.h"

#define MT_TICK_HZ      100u
/* Progress lines on the screen and the serial port: at most one per period. */
#define MT_PROGRESS_MS  500u
/* The volume name of the boot volume. */
#define MT_VOL          "sd"

/* Phases the maintenance port reports. */
#define MT_PHASE_INIT   0
#define MT_PHASE_MENU   1
#define MT_PHASE_TOOL   2

/* One tool run: what it reports. */
typedef struct {
    int result;                 /* MT_OK ... */
    char text[MT_TEXT_MAX];     /* short detail for the screen */
    char keys[MT_KEYS_MAX];     /* marker keys after result=, space separated */
} mt_run_t;

typedef struct {
    int phase;          /* MT_PHASE_* */
    int fb_ok;          /* the framebuffer console works */
    int trusted;        /* the boot info passed the trust rule */
    int tool;           /* the running tool, or -1 */
    int sd_rc;          /* sd_init() result */
    int mount_rc;       /* fs_mount_boot() result, PART_BOOT_OK when mounted */
    int mounted;        /* the boot volume is mounted as MT_VOL */
    int i2c_rc;         /* i2c_init() result, 1 before the first call */
    dg_sd_guard_t guard; /* every card access goes through it */
    mt_cfg_t cfg;       /* the [maint] settings in use */
#ifdef MT_TEST
    int write_probe;    /* test builds: the next read-only tool sends one write */
#endif
} mt_state_t;

extern mt_state_t mt;

/* One marker line on the PL011. fmt starts after "ev=". */
void mt_mark(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Appends to the marker keys of a run, with a space between keys. */
void mt_keys(mt_run_t *r, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* Sets the screen detail of a run. */
void mt_text(mt_run_t *r, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* Clears the work area under the menu. */
void mt_work_clear(void);
/* Writes row `row` of the work area, padded to the last column. to_uart: also one PL011 line. */
void mt_work_line(unsigned row, const char *text, int to_uart);

/*
 * Reads every waiting input event and drops it. Returns 1 when one of them
 * asked to stop: BACK, ESC or Ctrl-C. Runs the maintenance port too.
 */
int mt_poll_abort(void);

/*
 * Waits up to ms for an input event, running the maintenance port.
 * Returns 1 with the event, or 0 when the time ran out or the event was
 * a port request, which is no key.
 */
int mt_wait_event(input_event_t *ev, uint32_t ms);

/* Waits for the answer to a yes or no question. Returns MT_ANS_YES or MT_ANS_NO. */
int mt_ask(void);

/*
 * One progress line when the rate allows: a work row and a marker
 * "progress name=<tool> pct=<n>".
 */
void mt_progress(dg_rate_t *rate, unsigned row, const char *name, uint64_t done, uint64_t total);

/* Starts I2C1 once at 100 kHz. Returns the i2c_init() result. */
int mt_i2c(void);

/*
 * The card check of a tool: 0 when the boot volume is mounted, else
 * fills r with ABSENT for no card or FAIL and returns -1.
 */
int mt_need_card(mt_run_t *r);

/* The tools. Each fills r->result, r->text and r->keys. */
void mt_tool_card(mt_run_t *r);
void mt_tool_images(mt_run_t *r);
void mt_tool_view(mt_run_t *r);
void mt_tool_clock(mt_run_t *r);
void mt_tool_users(mt_run_t *r);

#endif
