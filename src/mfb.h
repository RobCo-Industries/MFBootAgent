/*
 * Shared state and output of the hardware side: main.c and maint.c. Not
 * used by the pure files.
 */
#ifndef MFB_H
#define MFB_H

#include <stdint.h>

#include <robco/loader.h>

#include "bootcfg.h"
#include "maintscr.h"

/* The library's commit, from the CI workflow pin. The Makefile passes it. */
#ifndef MFB_CORE_PIN
#define MFB_CORE_PIN "unknown"
#endif

/* The tick rate: the input backends sample on it. */
#define MFB_TICK_HZ 100

/* Prints one marker line on the PL011. fmt starts after "ev=". */
#define MFB_MARK(...) mfb_mark(__VA_ARGS__)
void mfb_mark(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Shows one line on the screen console and the PL011. */
void mfb_say(const char *line);

/* Phases the maintenance port reports. */
#define MFB_PHASE_INIT   0
#define MFB_PHASE_MENU   1
#define MFB_PHASE_LOAD   2
#define MFB_PHASE_HALTED 3

typedef struct {
    int phase;          /* MFB_PHASE_* */
    int fb_ok;          /* the framebuffer console works */
    int inherited;      /* the boot info passed the trust rule */
    int error;          /* the last error code, 0 for none */
    int menu_ready;     /* the menu state below is valid */
    int screen;         /* MFB_MS_SCREEN_*: the screen path taken */
    uint32_t flags_in;  /* boot_flags received, 0 when not trusted */
    int maint_open;     /* the maintenance screen shows */
    int maint_req;      /* MFB_WHY_* the port asked for, or -1 */
    int user_maint;     /* the user opened the maintenance screen during this boot */
} mfb_state_t;

extern mfb_state_t mfb;

/* Starts the input backends and the maintenance port commands. */
void maint_start(void);
/* Stops for good: answers the maintenance port, never returns. */
void maint_idle(void) __attribute__((noreturn));
/* One line for the `state` command, without the line end. */
void mfb_state_line(char *buf, unsigned size);

/* ---- main.c, for the maintenance screen ---- */

/* The menu entries in use. */
extern mfb_cfg_t mfb_cfg;

/*
 * Writes `text` at console row top + row, padded to the last column, in
 * `attr`. to_uart: also one line on the PL011, unless text is empty.
 */
void mfb_put_row(unsigned top, unsigned row, const char *text, unsigned attr, int to_uart);
/* Makes room for `rows` rows below the text on screen, as the menu does. Returns the top row. */
unsigned mfb_place(unsigned rows);
/* sd_init() and the boot volume rule. 0, or -1 with the failed step and its code. */
int mfb_mount(const char **what, int *rc);
/* Every check a boot runs on an entry, loading it into the OS slot. A rimg_err_t. */
int mfb_check_entry(const mfb_entry_t *e, loader_result_t *out);
/* Prints the reboot marker with `source` and restarts the board. */
void mfb_reboot(const char *source) __attribute__((noreturn));
void mfb_sys_info(mfb_ms_sys_t *sys);
void mfb_cfg_info(mfb_ms_cfg_t *c);

/* ---- maintui.c ---- */

/* Shows the maintenance screen until the user closes it. why: MFB_WHY_*. */
void mfb_maint_run(int why);

#endif
