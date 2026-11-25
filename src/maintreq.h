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
 * 1 when the screen opened at start ignores its first BACK press: the
 * request opened it and BACK was held through the button guard. The
 * guard queues that press, and the person held BACK to reach the
 * screen, not to close it. So a request and a held BACK give one
 * screen, with why=request.
 */
int mfb_start_skip_back(int why, int back_held);

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

/* An entry named MAINT at MFB_MAINT_PATH with no cmdline. */
void mfb_maint_fixed(mfb_entry_t *e);

#endif
