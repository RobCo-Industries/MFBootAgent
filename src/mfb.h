/*
 * Shared state and output of the hardware side: main.c and maint.c. Not
 * used by the pure files.
 */
#ifndef MFB_H
#define MFB_H

#include <stdint.h>

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
} mfb_state_t;

extern mfb_state_t mfb;

/* Starts the input backends and the maintenance port commands. */
void maint_start(void);
/* Stops for good: answers the maintenance port, never returns. */
void maint_idle(void) __attribute__((noreturn));
/* One line for the `state` command, without the line end. */
void mfb_state_line(char *buf, unsigned size);

#endif
