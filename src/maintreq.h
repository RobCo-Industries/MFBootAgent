/*
 * The maintenance request of BOOT_PROTOCOL.md: the file /ROBCO/MAINT.REQ,
 * what it means for this boot, and the boot_flags bits 1 and 3 this
 * stage hands on. Pure code: the host tests link it. main.c runs the
 * file system calls and asks these functions what each result means.
 */
#ifndef MFB_MAINTREQ_H
#define MFB_MAINTREQ_H

#include <stddef.h>
#include <stdint.h>

#include <robco/input.h>

#include "bootcfg.h"

#define MFB_REQ_PATH        "/ROBCO/MAINT.REQ"
/* The maintenance image the screen starts when BOOT.CFG has no MAINT entry. */
#define MFB_MAINT_NAME      "MAINT"
#define MFB_MAINT_PATH      "/ROBCO/MAINT.SYS"
/* The longest request line, without its line end. */
#define MFB_REQ_LINE_MAX    63
/* The read buffer: the longest line and CRLF. A longer file does not fit and shows no line. */
#define MFB_REQ_READ_MAX    (MFB_REQ_LINE_MAX + 2)

/* `result` of the maint_req marker. */
#define MFB_REQ_NONE         0
#define MFB_REQ_FOUND        1
#define MFB_REQ_CLEAR_FAILED 2

/* "none", "found" or "clear_failed". "unknown" for other values. */
const char *mfb_req_name(int result);

/*
 * 1 when fs_stat() of the plain name found a request: it returned 0 and
 * the name is no folder. A missing name, a folder or a failed fs_stat()
 * is no request.
 */
int mfb_req_present(int stat_rc, int is_dir);

/*
 * The request line for the screen, from fs_read_file() into a buffer of
 * MFB_REQ_READ_MAX bytes: read_rc and the `len` bytes of `data`. One
 * line end, CR, LF or CRLF, may close the line. Copies the line into
 * `out` and returns 1 when it has 1 to MFB_REQ_LINE_MAX bytes, each
 * 0x20 to 0x7E. Returns 0 with `out` empty for an empty file, a failed
 * read, a longer content or any other byte. The request counts either way.
 * `size` is at least MFB_REQ_LINE_MAX + 1.
 */
int mfb_req_line(int read_rc, const char *data, size_t len, char *out, size_t size);

/*
 * The maint_req rc when there is no request: 0 for a missing name or a
 * folder, else the fs_stat() error. A request sets rc from the delete.
 */
int mfb_req_none_rc(int stat_rc);

/* The marker result after fs_unlink(): MFB_REQ_FOUND for 0, MFB_REQ_CLEAR_FAILED otherwise. */
int mfb_req_result(int unlink_rc);

/*
 * Whether the maintenance screen opens at start, before any key, and
 * why. `req` is the marker result. A request opens it with
 * MFB_WHY_REQUEST, with or without boot_flags bit 1 received. Else a
 * trusted block with bit 1 opens it with MFB_WHY_FLAG. Returns 1 and
 * sets *why, or returns 0 and leaves *why.
 */
int mfb_start_screen(int req, int trusted, uint32_t received, int *why);

/*
 * The keys the screen opened at start acts on. Before it opens, the
 * main loop has not polled input, so the serial console and the
 * maintenance port have queued nothing: every event already queued came
 * from the GPIO buttons. That is the guard's press for a button held at
 * power-on, its release and repeats, or a button pressed while the card
 * mounted. Nobody could see the screen yet, so none of these is a
 * choice on it. The screen drops them, whatever opened it, a request or
 * a received bit 1.
 *
 * mfb_start_keys_init() runs the moment the screen opens. `pending` is
 * input_pending() then, `held` the mask of the keys down then, bit
 * (1 << INPUT_KEY_*). The queue is first in, first out, so the next
 * `pending` events are the ones queued before the screen opened.
 */
typedef struct {
    unsigned pre;       /* events left that were queued before the screen opened */
    uint32_t stale;     /* GPIO keys held since before it opened, bit (1 << code) */
} mfb_start_keys_t;

void mfb_start_keys_init(mfb_start_keys_t *k, unsigned pending, uint32_t held);

/*
 * 1 when the screen acts on `ev`, 0 when it drops it. Dropped: a GPIO
 * key event queued before the screen opened, and the repeats and the
 * release of a GPIO key held since then. The first press of that key
 * after its release acts. Console and port events always act: they are
 * keys someone sent on purpose, one at a time.
 */
int mfb_start_keys_take(mfb_start_keys_t *k, const input_event_t *ev);

/*
 * boot_flags for the image this stage starts. `flags` is what
 * bootinfo_build() wrote. Bit 1 set when a request was found
 * (`req` not MFB_REQ_NONE), the screen opened this boot (`opened`, or a
 * trusted bit 1 received, which opens it), or the image is a
 * maintenance image, subtype 2. Clear otherwise. Bit 3 set when a
 * request was found, clear otherwise. Both are set or cleared whatever
 * arrived. Other bits stay.
 */
uint32_t mfb_boot_flags(uint32_t flags, int trusted, uint32_t received, int opened, int req,
                        uint32_t subtype);

/*
 * The maintenance image the screen starts: the first entry named MAINT,
 * in any case, as BOOT.CFG compares names, with *index its 1-based
 * menu index. Without one, NULL and *index 0: the caller uses
 * MFB_MAINT_PATH.
 */
const mfb_entry_t *mfb_maint_entry(const mfb_cfg_t *cfg, unsigned *index);

/* The choose index of MFB_MAINT_PATH with no MAINT entry. Entries count from 1. */
#define MFB_MAINT_FIXED_INDEX (-1)

/* An entry named MAINT at MFB_MAINT_PATH with no cmdline. */
void mfb_maint_fixed(mfb_entry_t *e);

#endif
