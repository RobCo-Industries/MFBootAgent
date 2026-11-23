/*
 * The maintenance screen on the console and the PL011: drawing, the key
 * loop and the actions. The decisions and the text are in maintscr.c.
 *
 * Like the menu, the screen writes only its own rows, each in full. It
 * takes MFB_MS_ROWS rows below the text on screen, scrolling up when
 * they do not fit, and the menu is drawn again below it on close.
 */

#include <robco/console.h>
#include <robco/fs.h>
#include <robco/input.h>
#include <robco/klibc.h>
#include <robco/loader.h>
#include <robco/uart.h>

#include "boot.h"
#include "keymap.h"
#include "maintscr.h"
#include "menulines.h"
#include "mfb.h"

static mfb_ms_t ms;
static unsigned top;

static void row(unsigned r, const char *text, unsigned attr, int to_uart)
{
    mfb_put_row(top, r, text, attr, to_uart);
}

static void draw_item(unsigned i, int to_uart)
{
    char line[MFB_LINE_MAX];
    int selected = i == ms.sel;

    mfb_ms_item(line, sizeof(line), i, selected);
    row(MFB_MS_ROW_ITEM0 + i, line, selected ? CONSOLE_ATTR_BRIGHT : CONSOLE_ATTR_NORMAL,
        to_uart);
}

/* Output row i: text in `attr`, or blank. */
static void out_row(unsigned i, const char *text, unsigned attr)
{
    row(MFB_MS_ROW_OUT0 + i, text, attr, 1);
}

/* Blanks output rows from `first` to the end. */
static void clear_out(unsigned first)
{
    unsigned i;

    for (i = first; i < MFB_MS_OUT_ROWS; i++) {
        out_row(i, "", CONSOLE_ATTR_NORMAL);
    }
}

static void park_cursor(void)
{
    if (mfb.fb_ok) {
        console_set_cursor(0, top + MFB_MS_ROWS - 1);
    }
}

static void draw_all(void)
{
    char line[MFB_LINE_MAX];
    unsigned i;

    uart_putc('\n');
    mfb_ms_title(line, sizeof(line), MFB_VERSION_STR);
    row(MFB_MS_ROW_TITLE, line, CONSOLE_ATTR_BRIGHT, 1);
    row(MFB_MS_ROW_RULE, mfb_line_rule(), CONSOLE_ATTR_DIM, 1);
    for (i = 0; i < MFB_MS_ITEMS; i++) {
        draw_item(i, 1);
    }
    row(MFB_MS_ROW_HELP, mfb_ms_help(), CONSOLE_ATTR_DIM, 1);
    clear_out(0);
}

static void show_page(mfb_ms_page_t page, unsigned n)
{
    unsigned i;

    for (i = 0; i < n; i++) {
        out_row(i, page[i], i == 0 ? CONSOLE_ATTR_BRIGHT : CONSOLE_ATTR_NORMAL);
    }
    clear_out(n);
}

/*
 * Verify entries: the volume again by the boot rule, then every check
 * of a boot on each entry, in menu order, without the jump. The OS slot
 * is the work buffer: nothing runs there yet.
 */
static void verify(void)
{
    static loader_result_t r;
    char line[MFB_LINE_MAX];
    const char *what;
    unsigned good = 0;
    unsigned failed = 0;
    unsigned i;
    int rc;

    /* The card may have changed since the boot: mount it afresh. */
    (void)fs_unmount("sd");
    if (mfb_mount(&what, &rc) != 0) {
        mfb_error_line(line, sizeof(line), MFB_ERR_IO, NULL);
        out_row(0, line, CONSOLE_ATTR_BRIGHT);
        clear_out(1);
        MFB_MARK("verify_done result=io what=%s rc=%d entries=%u good=0 failed=0", what, rc,
                 mfb_cfg.count);
        return;
    }
    mfb_ms_verify_head(line, sizeof(line));
    out_row(0, line, CONSOLE_ATTR_DIM);
    for (i = 0; i < mfb_cfg.count; i++) {
        const mfb_entry_t *e = &mfb_cfg.entry[i];
        int err = mfb_check_entry(e, &r);

        if (err == 0) {
            good++;
        } else {
            failed++;
        }
        MFB_MARK("verify_entry index=%u name=%s path=%s result=%s", i + 1, e->name, e->path,
                 err == 0 ? "ok" : mfb_err_name(err));
        mfb_ms_verify_row(line, sizeof(line), i, e, err);
        out_row(1 + i, line, err == 0 ? CONSOLE_ATTR_NORMAL : CONSOLE_ATTR_BRIGHT);
    }
    mfb_ms_verify_sum(line, sizeof(line), good, failed);
    out_row(1 + i, line, CONSOLE_ATTR_NORMAL);
    clear_out(2 + i);
    MFB_MARK("verify_done result=ok entries=%u good=%u failed=%u", mfb_cfg.count, good, failed);
}

static void run(unsigned item)
{
    static mfb_ms_page_t page;
    mfb_ms_sys_t sys;
    mfb_ms_cfg_t c;

    MFB_MARK("maint_action name=%s", mfb_ms_name(item));
    switch (item) {
    case MFB_MS_VERIFY:
        verify();
        break;
    case MFB_MS_SYSTEM:
        mfb_sys_info(&sys);
        show_page(page, mfb_ms_system(page, &sys));
        break;
    case MFB_MS_CONFIG:
        mfb_cfg_info(&c);
        show_page(page, mfb_ms_config(page, &c));
        break;
    case MFB_MS_REBOOT:
        mfb_reboot("screen");
    default:
        break;
    }
}

void mfb_maint_run(int why)
{
    input_event_t ev;
    unsigned item = 0;
    unsigned old = 0;
    int act;

    mfb_ms_init(&ms);
    top = mfb_place(MFB_MS_ROWS);
    draw_all();
    park_cursor();
    MFB_MARK("maint_open why=%s", mfb_why_name(why));
    for (;;) {
        int key;

        if (input_wait(&ev, 1000) == 0) {
            continue;
        }
        key = mfb_key_map(&ev);
        act = mfb_ms_key(&ms, key, &item);
        if (mfb_ms_take(&ms, &old)) {
            draw_item(old, 0);
            draw_item(ms.sel, 1);
        }
        if (act == MFB_MS_ACT_RUN) {
            run(item);
        }
        park_cursor();
        if (act == MFB_MS_ACT_CLOSE) {
            /* The Back action, as against ESC, Q or BACK. */
            if (key == MFB_K_SELECT) {
                MFB_MARK("maint_action name=back");
            }
            break;
        }
    }
    MFB_MARK("maint_close");
}
