/*
 * MFBootAgent: the second stage of the terminal chain. Shows the boot
 * menu from /ROBCO/BOOT.CFG, loads the chosen operating image into the
 * OS slot and enters it, as docs/spec/BOOT_PROTOCOL.md in RobCo-Core says.
 */

#include <stdarg.h>
#include <stdint.h>

#include <robco/board.h>
#include <robco/boot.h>
#include <robco/bootinfo.h>
#include <robco/console.h>
#include <robco/fb.h>
#include <robco/fs.h>
#include <robco/input.h>
#include <robco/irq.h>
#include <robco/klibc.h>
#include <robco/loader.h>
#include <robco/part.h>
#include <robco/power.h>
#include <robco/rimg.h>
#include <robco/sd.h>
#include <robco/tick.h>
#include <robco/timer.h>
#include <robco/uart.h>

#include "boot.h"
#include "bootcfg.h"
#include "maintscr.h"
#include "menu.h"
#include "menulines.h"
#include "mfb.h"
#include "portreq.h"

#define SCREEN_W 640
#define SCREEN_H 480

mfb_state_t mfb;

/*
 * The boot volume markers: BOOT.CFG and the default OS path. A volume
 * with either file is the one this stage boots from.
 */
static const char *const vol_markers[] = { MFB_CFG_PATH, MFB_DEFAULT_PATH };
static fs_boot_t vol;
/*
 * fs_mount_boot() clears `vol` on a failure, so the System page reads
 * these instead: whether a volume is mounted now, the last volume that
 * mounted, and the last failed step with its code.
 */
static int vol_mounted;
static int vol_have_good;
static uint32_t good_lba;
static uint32_t good_blocks;
static const char *fail_what = "none";
static int fail_rc;
mfb_cfg_t mfb_cfg;
static char cfg_text[MFB_CFG_FILE_MAX];
static mfb_cfg_report_t cfg_rep;    /* what read_config() found, for the Configuration page */
static int cfg_result;              /* MFB_MS_CFG_* */
static unsigned cfg_line;           /* the line that did not parse */

static loader_result_t res;
static mfb_menu_t menu;
static unsigned menu_top;       /* console row of the menu's first row */
static unsigned menu_rows;      /* MFB_MENU_ROWS(mfb_cfg.count) */
static int start_maint;         /* the screen asked to start the maintenance image */
static mfb_entry_t maint_fixed; /* MAINT at MFB_MAINT_PATH, for a BOOT.CFG without one */


void mfb_mark(const char *fmt, ...)
{
    char buf[224];
    va_list ap;

    va_start(ap, fmt);
    k_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    k_printf("@RC stage=" ROBCO_STAGE_NAME " ev=%s\n", buf);
}

void mfb_say(const char *line)
{
    if (mfb.fb_ok) {
        console_puts(line);
        console_putc('\n');
    }
    uart_puts(line);
    uart_putc('\n');
}

static const char *mode_name(uint32_t mode)
{
    switch (mode) {
    case 0x13: return "svc";
    case 0x1a: return "hyp";
    default:   return "other";
    }
}

static const char *phase_name(int phase)
{
    switch (phase) {
    case MFB_PHASE_INIT:   return "init";
    case MFB_PHASE_MENU:   return "menu";
    case MFB_PHASE_LOAD:   return "load";
    case MFB_PHASE_HALTED: return "halted";
    default:               return "unknown";
    }
}

void mfb_state_line(char *buf, unsigned size)
{
    /*
     * pending: input events not taken yet. The port answers from inside
     * input_poll(), before it takes the next event, so a reply with
     * pending=0 means the stage handled every key sent before it.
     */
    k_snprintf(buf, size, "phase=%s trusted=%d entries=%u sel=%u counting=%d left=%u error=%s "
               "maint=%d pending=%u",
               phase_name(mfb.phase), mfb.inherited, mfb.menu_ready ? mfb_cfg.count : 0,
               mfb.menu_ready ? menu.sel + 1 : 0, mfb.menu_ready ? menu.counting : 0,
               mfb.menu_ready ? mfb_menu_left(&menu, timer_now_ms()) : 0,
               mfb.error != 0 ? mfb_err_name(mfb.error) : "none", mfb.maint_open,
               input_pending());
}

/*
 * The screen. After a trusted handoff with a framebuffer it adopts the
 * previous stage's framebuffer and console: no pixel changes, the old
 * text stays, the cursor stays where it was. The console does not know
 * the old characters, so the menu only ever rewrites its own rows. Cold,
 * or when an adopt call fails, it starts a fresh screen.
 */
/* An adopt call's result for the screen marker: its code, or "none" when it did not run. */
static const char *adopt_rc(char *buf, unsigned size, int ran, int rc)
{
    if (!ran) {
        return "none";
    }
    k_snprintf(buf, size, "%d", rc);
    return buf;
}

static void init_screen(void)
{
    const robco_bootinfo_t *bi = mfb.inherited ? bootinfo_get() : NULL;
    unsigned scheme = CONSOLE_SCHEME_GREEN;
    unsigned unknown = 0;
    fb_info_t info;
    char fb_buf[12];
    char con_buf[12];
    int fb_ran = 0;
    int con_ran = 0;
    int rc_fb = -1;
    int rc_con = -1;
    int grid = -1;
    int kept;
    int fb;
    int con;

    if (bi != NULL && bi->con_scheme < CONSOLE_SCHEME_COUNT) {
        scheme = bi->con_scheme;
    }
    if (bi != NULL && (bi->caps & ROBCO_CAP_FB) != 0) {
        fb_ran = 1;
        rc_fb = bootinfo_fb_info(bi, &info);
        if (rc_fb == 0) {
            rc_fb = fb_adopt(&info);
        }
        if (rc_fb == 0) {
            con_ran = 1;
            rc_con = console_adopt(bi->con_cols, bi->con_rows, bi->con_cx, bi->con_cy,
                                   bi->con_scheme);
        }
        if (rc_con == 0) {
            /* console_adopt() takes any grid that fits the screen: the menu needs its minimum. */
            grid = mfb_menu_grid(bi->con_cols, bi->con_rows);
        }
    }
    kept = rc_fb == 0 && rc_con == 0 && grid == MFB_GRID_OK;
    if (kept) {
        fb = 0;
        con = 0;
        unknown = bi->con_cols * bi->con_rows;
    } else {
        fb = fb_init(SCREEN_W, SCREEN_H, 32);
        con = fb == 0 ? console_init(scheme) : -1;
        /*
         * SCREEN_W by SCREEN_H with the 8 by 16 font gives 80 by 30, above
         * the minimum. The check keeps the menu off the screen if that changes.
         */
        if (con == 0 && mfb_menu_grid(console_cols(), console_rows()) != MFB_GRID_OK) {
            con = -1;
        }
    }
    mfb.fb_ok = fb == 0 && con == 0;
    mfb.screen = !mfb.fb_ok ? MFB_MS_SCREEN_NONE : kept ? MFB_MS_SCREEN_ADOPT : MFB_MS_SCREEN_FRESH;
    MFB_MARK("screen fb=%d console=%d kept=%d unknown=%u scheme=%u path=%s adopt_fb=%s "
             "adopt_console=%s grid=%s",
             fb, con, kept, unknown, scheme, kept ? "adopt" : "init",
             adopt_rc(fb_buf, sizeof(fb_buf), fb_ran, rc_fb),
             adopt_rc(con_buf, sizeof(con_buf), con_ran, rc_con),
             grid < 0 ? "none" : mfb_grid_name(grid));
}

/* Step 1: entry. Console, trust rule, screen, tick, input and the self check. */
static void entry(void)
{
    const robco_board_t *b = board_get();
    extern const rimg_header_t robco_rimg;
    /* First, before any code writes initialised data, which the CRC covers. */
    int self = rimg_self_check();

    uart_init(115200);
    k_printf("\n");
    MFB_MARK("boot midr=0x%08x soc=%s base=0x%08x board_rev=0x%08x mem=0x%08x entry_mode=%s "
             "version=" MFB_VERSION_STR,
             (unsigned)b->midr, b->soc_name, (unsigned)b->periph_base, (unsigned)b->board_rev,
             (unsigned)b->mem_size, mode_name(robco_boot_args()->entry_mode));
    mfb.inherited = bootinfo_inherited();
    mfb.flags_in = mfb.inherited ? bootinfo_get()->boot_flags : 0;
    MFB_MARK("handoff trusted=%d", mfb.inherited);
    if (!mfb.inherited) {
        /* Cold: copy the firmware blob as a first stage does. bootinfo_build() records it. */
        bootinfo_copy_fw_blob(ROBCO_HANDOFF_FDT_ADDR, ROBCO_HANDOFF_FDT_MAX);
    }
    init_screen();
    irq_init();
    tick_init(MFB_TICK_HZ);
    irq_cpu_enable();
    maint_start();
    /* Information only: a failed check is reported and the boot goes on. */
    MFB_MARK("selfcheck rc=%s size=%u", rimg_strerror(self),
             (unsigned)robco_rimg.file_size);
}

/* No card, or nothing mounts: the error screen, then the idle loop. */
static void halt_io(const char *what, int rc)
{
    char line[MFB_LINE_MAX];

    mfb.error = MFB_ERR_IO;
    MFB_MARK("error code=io path=" MFB_CFG_PATH " what=%s rc=%d", what, rc);
    mfb_error_line(line, sizeof(line), MFB_ERR_IO, NULL);
    mfb_say(line);
    mfb_say("BOOT HALTED. Fix the card and reboot.");
    maint_idle();
}

/*
 * The boot volume, by the rule every stage shares. Returns 0, or the
 * failed step in *what ("sd" or "fat") with its code in *rc.
 */
static int mount_failed(const char *what, int rc)
{
    vol_mounted = 0;
    fail_what = what;
    fail_rc = rc;
    return -1;
}

int mfb_mount(const char **what, int *rc)
{
    vol_mounted = 0;
    *what = "sd";
    *rc = sd_init();
    if (*rc != 0) {
        return mount_failed(*what, *rc);
    }
    *what = "fat";
    *rc = fs_mount_boot("sd", sd_blkdev(), vol_markers,
                        sizeof(vol_markers) / sizeof(vol_markers[0]), &vol);
    if (*rc != PART_BOOT_OK) {
        return mount_failed(*what, *rc);
    }
    vol_mounted = 1;
    vol_have_good = 1;
    good_lba = (uint32_t)vol.start_lba;
    good_blocks = (uint32_t)vol.block_count;
    return 0;
}

/*
 * Drops the boot volume, whatever card is in the slot now. 0 when
 * nothing is mounted on return, or the fs_unmount() error.
 *
 * fs_unmount() syncs the device first and keeps the volume on a failed
 * sync. With the card out, or after a failed sd_init(), the driver is
 * not ready and the sync returns BLK_ERR_NOMEDIA. So on a failure the
 * driver is set up again and the unmount tried once more: with a card
 * that answers, the sync passes and the volume goes. The library has no
 * way to drop a volume without the sync.
 *
 * Nothing is lost by dropping the volume. The stage writes one thing on
 * the card: the delete of the maintenance request in check_request(),
 * and fs_unlink() syncs the volume before it returns. Every other file
 * is opened with FS_READ only (fs_read_file(), precheck() and
 * loader_load()), so FatFs holds no dirty sector.
 *
 * When the volume stays, sd_deinit() leaves the driver not ready, so
 * every read through the old volume fails with BLK_ERR_NOMEDIA. A stale
 * volume never reads another card.
 */
static int release(const char **what, int *rc)
{
    *what = "release";
    *rc = fs_unmount("sd");
    if (*rc != FS_OK && *rc != FS_ERR_NOVOL) {
        int sd = sd_init();

        if (sd != 0) {
            /* No card answers: that is the cause to report. */
            *what = "sd";
            *rc = sd;
            sd_deinit();
            return -1;
        }
        *rc = fs_unmount("sd");
    }
    if (*rc == FS_OK || *rc == FS_ERR_NOVOL) {
        *rc = 0;
        return 0;
    }
    sd_deinit();
    return -1;
}

int mfb_remount(const char **what, int *rc)
{
    const char *w;
    int r;

    vol_mounted = 0;
    if (release(what, rc) != 0) {
        return mount_failed(*what, *rc);
    }
    if (mfb_mount(what, rc) == 0) {
        return 0;
    }
    /* PART_BOOT_ERR_RELEASE keeps a volume: drop it too. */
    (void)release(&w, &r);
    return -1;
}

/* Step 2: the boot volume. */
static void mount(void)
{
    const char *what;
    int rc;

    if (mfb_mount(&what, &rc) != 0) {
        halt_io(what, rc);
    }
    /* halt_io() never returns: here the mount worked and `vol` holds it. */
    MFB_MARK("mount lba=%u blocks=%u", (unsigned)good_lba, (unsigned)good_blocks);
}

/*
 * Step 2b: the maintenance request, before the menu and the countdown.
 * The plain name only, no recovery of work names. The file is read,
 * then deleted, then acted on: the delete makes it one-shot. This
 * delete is the only write the stage makes on the card.
 */
static void check_request(void)
{
    static char buf[MFB_REQ_READ_MAX];
    fs_stat_t st;
    size_t len = 0;
    int rc;

    mfb.req = MFB_REQ_NONE;
    mfb.req_rc = 0;
    mfb.req_line[0] = '\0';
    k_memset(&st, 0, sizeof(st));
    rc = fs_stat(MFB_REQ_PATH, &st);
    if (mfb_req_present(rc, st.is_dir)) {
        rc = fs_read_file(MFB_REQ_PATH, buf, sizeof(buf), &len);
        (void)mfb_req_line(rc, buf, len, mfb.req_line, sizeof(mfb.req_line));
        rc = fs_unlink(MFB_REQ_PATH);
        mfb.req = mfb_req_result(rc);
        mfb.req_rc = rc;
    }
    MFB_MARK("maint_req result=%s rc=%d", mfb_req_name(mfb.req), mfb.req_rc);
}

/* Step 3: the menu entries from BOOT.CFG, or the one default entry. */
static void read_config(void)
{
    mfb_cfg_report_t r;
    size_t len = 0;
    unsigned err_line = 0;
    char timeout[8];
    int rc;

    mfb_cfg_default(&mfb_cfg);
    k_memset(&r, 0, sizeof(r));
    rc = fs_read_file(MFB_CFG_PATH, cfg_text, sizeof(cfg_text), &len);
    if (rc == FS_ERR_NOENT) {
        cfg_result = MFB_MS_CFG_MISSING;
        MFB_MARK("config result=missing path=" MFB_CFG_PATH);
    } else if (rc != 0) {
        cfg_result = MFB_MS_CFG_READ;
        MFB_MARK("config result=bad path=" MFB_CFG_PATH " why=read rc=%d", rc);
    } else {
        rc = mfb_cfg_parse(cfg_text, len, &mfb_cfg, &r, &err_line);
        cfg_result = rc == MFB_CFG_SYNTAX ? MFB_MS_CFG_SYNTAX :
                     rc == MFB_CFG_EMPTY ? MFB_MS_CFG_EMPTY : MFB_MS_CFG_OK;
        cfg_line = err_line;
        if (rc == MFB_CFG_SYNTAX) {
            MFB_MARK("config result=bad path=" MFB_CFG_PATH " why=syntax line=%u", err_line);
        } else if (rc == MFB_CFG_EMPTY) {
            MFB_MARK("config result=bad path=" MFB_CFG_PATH " why=empty entries=0 ignored=%u "
                     "dup=%u unknown=%u bad=%u clamped=%u nopath=%u",
                     r.ignored, r.dup, r.unknown, r.bad, r.clamped, r.nopath);
        } else {
            MFB_MARK("config result=ok path=" MFB_CFG_PATH " entries=%u ignored=%u dup=%u "
                     "unknown=%u bad=%u clamped=%u default_found=%d nopath=%u",
                     mfb_cfg.count, r.ignored, r.dup, r.unknown, r.bad, r.clamped, r.default_found,
                     r.nopath);
        }
    }
    cfg_rep = r;
    if (mfb_cfg.timeout == MFB_TIMEOUT_NONE) {
        k_snprintf(timeout, sizeof(timeout), "none");
    } else {
        k_snprintf(timeout, sizeof(timeout), "%d", mfb_cfg.timeout);
    }
    MFB_MARK("entries count=%u default=%s timeout=%s", mfb_cfg.count, mfb_cfg.entry[mfb_cfg.def].name,
             timeout);
}

/*
 * Writes one menu row on the screen, padded with spaces to the last
 * column so that every cell of the row is known and covers what was
 * there. The console wraps only on the next character, and every caller
 * moves the cursor first. to_uart: also one line on the PL011.
 */
void mfb_put_row(unsigned top, unsigned row, const char *text, unsigned attr, int to_uart)
{
    if (mfb.fb_ok) {
        unsigned cols = console_cols();
        unsigned n = (unsigned)k_strlen(text);

        /* The last column stays blank. cols is at least MFB_MENU_MIN_COLS here. */
        if (n >= cols) {
            n = cols != 0 ? cols - 1 : 0;
        }
        console_set_cursor(0, top + row);
        console_set_attr(attr);
        console_write(text, n);
        console_set_attr(CONSOLE_ATTR_NORMAL);
        for (; n < cols; n++) {
            console_putc(' ');
        }
    }
    if (to_uart && text[0] != '\0') {
        uart_puts(text);
        uart_putc('\n');
    }
}

static void put_row(unsigned row, const char *text, unsigned attr, int to_uart)
{
    mfb_put_row(menu_top, row, text, attr, to_uart);
}

static unsigned entry_row(unsigned i)
{
    return MFB_ROW_ENTRY0 + i;
}

static unsigned after_row(unsigned offset)
{
    return MFB_ROW_ENTRY0 + mfb_cfg.count + offset;
}

static void draw_entry(unsigned i, int to_uart)
{
    char line[MFB_LINE_MAX];
    int selected = i == menu.sel;

    mfb_line_entry(line, sizeof(line), i, &mfb_cfg.entry[i], i == mfb_cfg.def, selected);
    put_row(entry_row(i), line, selected ? CONSOLE_ATTR_BRIGHT : CONSOLE_ATTR_NORMAL, to_uart);
}

static void draw_status(int to_uart)
{
    char line[MFB_LINE_MAX];

    mfb_line_status(line, sizeof(line), menu.counting, menu.stopped,
                    mfb_menu_left(&menu, timer_now_ms()), mfb_cfg.entry[mfb_cfg.def].name);
    put_row(after_row(MFB_ROW_STATUS_AFTER), line, CONSOLE_ATTR_NORMAL, to_uart);
}

/*
 * Finds the rows for the menu below the text on the screen: a blank row
 * after earlier text, and a scroll when the menu does not fit under it.
 */
unsigned mfb_place(unsigned rows)
{
    mfb_place_t p;
    unsigned col, row, i;

    if (!mfb.fb_ok) {
        return 0;
    }
    console_get_cursor(&col, &row);
    /* The grid is at least MFB_MENU_MIN_ROWS rows: init_screen() checked it. */
    mfb_menu_place(console_rows(), rows, col, row, &p);
    for (i = 0; i < p.scroll; i++) {
        console_set_cursor(0, console_rows() - 1);
        console_putc('\n');
    }
    return p.top;
}

static void place_menu(void)
{
    menu_rows = MFB_MENU_ROWS(mfb_cfg.count);
    menu_top = mfb_place(menu_rows);
}

/* Clears a menu row. */
static void clear_row(unsigned row)
{
    put_row(row, "", CONSOLE_ATTR_NORMAL, 0);
}

/*
 * The whole menu, on the screen and the PL011. Every row of the menu is
 * written once, the blank ones too, so the console knows each cell of
 * the menu and nothing of the earlier text shows inside it.
 */
static void draw_menu(void)
{
    char line[MFB_LINE_MAX];
    unsigned i;

    uart_putc('\n');
    mfb_line_title(line, sizeof(line), MFB_VERSION_STR);
    put_row(MFB_ROW_TITLE, line, CONSOLE_ATTR_BRIGHT, 1);
    for (i = 0; i < MFB_LORE_LINES; i++) {
        put_row(MFB_ROW_LORE + i, mfb_line_lore(i), CONSOLE_ATTR_NORMAL, 1);
    }
    put_row(MFB_ROW_RULE, mfb_line_rule(), CONSOLE_ATTR_DIM, 1);
    clear_row(MFB_ROW_RULE + 1);
    for (i = 0; i < mfb_cfg.count; i++) {
        draw_entry(i, 1);
    }
    clear_row(after_row(0));
    put_row(after_row(MFB_ROW_HELP_AFTER), mfb_line_help(), CONSOLE_ATTR_DIM, 1);
    draw_status(1);
    clear_row(after_row(MFB_ROW_ERROR_AFTER));
    clear_row(after_row(MFB_ROW_START_AFTER));
    (void)mfb_menu_take(&menu, NULL);
}

/* Redraws what the state machine marked, and prints a marker for it. */
static void redraw(void)
{
    unsigned old = 0;
    unsigned d = mfb_menu_take(&menu, &old);

    if (d & MFB_DIRTY_SEL) {
        draw_entry(old, 0);
        draw_entry(menu.sel, 1);
        MFB_MARK("select index=%u name=%s", menu.sel + 1, mfb_cfg.entry[menu.sel].name);
    }
    if (d & MFB_DIRTY_STATUS) {
        draw_status(1);
        if (menu.stopped && !menu.counting) {
            MFB_MARK("countdown state=stopped");
        }
    }
}

void mfb_reboot(const char *source)
{
    MFB_MARK("reboot source=%s", source);
    uart_flush();
    power_reboot();
}

/*
 * The maintenance screen, then the menu again below it. The menu state
 * stays as it was: the countdown does not start again.
 */
static void maint_screen(int why, int skip_back)
{
    if (mfb.maint_req != 0) {
        /* A key opened it before the port's request came up: the screen it asked for shows. */
        mfb.maint_req = 0;
        MFB_MARK("maint_drop why=open");
    }
    mfb.maint_open = 1;
    if (why != MFB_WHY_FLAG) {
        mfb.user_maint = 1;
    }
    start_maint = mfb_maint_run(why, skip_back);
    mfb.maint_open = 0;
    place_menu();
    draw_menu();
    if (mfb.fb_ok) {
        console_set_cursor(0, menu_top + menu_rows - 1);
    }
}

/* Waits in the menu until an entry is chosen. Returns MFB_ACT_BOOT, or reboots. */
static int menu_loop(void)
{
    input_event_t ev;
    int act;

    for (;;) {
        if (start_maint) {
            /* The screen's Start the maintenance image: boot_selected() picks it. */
            return MFB_ACT_BOOT;
        }
        act = mfb_menu_tick(&menu, timer_now_ms());
        redraw();
        if (act == MFB_ACT_NONE &&
            input_wait(&ev, mfb_menu_wait_ms(&menu, timer_now_ms())) != 0) {
            int port = mfb_port_req(&ev, mfb.maint_req);

            if (port == MFB_PORT_LIVE) {
                /* The maint command on the port: it stops the countdown as a key does. */
                mfb.maint_req = 0;
                menu.why = MFB_WHY_PORT;
                mfb_menu_stop(&menu);
                act = MFB_ACT_MAINT;
            } else if (port == MFB_PORT_NONE) {
                act = mfb_menu_event(&menu, &ev);
            }
            redraw();
        }
        if (act == MFB_ACT_MAINT) {
            maint_screen(menu.why, 0);
        }
        if (act == MFB_ACT_REBOOT) {
            mfb_reboot("key");
        }
        if (act == MFB_ACT_BOOT || start_maint) {
            if (mfb.maint_req != 0) {
                /* An earlier key or the countdown chose the boot: the request behind it goes. */
                mfb.maint_req = 0;
                MFB_MARK("maint_drop why=boot");
            }
            return MFB_ACT_BOOT;
        }
    }
}

/* Step 4: the menu. */
static void show_menu(void)
{
    /*
     * A request, or bit 1 received: the maintenance screen opens in place
     * of the countdown. A request with BACK held gives one screen: the
     * BACK press the guard queued does not close it.
     */
    int why = MFB_WHY_FLAG;
    int at_start = mfb_start_screen(mfb.req, mfb.inherited, mfb.flags_in, &why);

    mfb_menu_init(&menu, mfb_cfg.count, mfb_cfg.def, at_start ? MFB_TIMEOUT_NONE : mfb_cfg.timeout,
                  timer_now_ms());
    mfb.menu_ready = 1;
    place_menu();
    draw_menu();
    if (mfb.fb_ok) {
        console_set_cursor(0, menu_top + menu_rows - 1);
    }
    mfb.phase = MFB_PHASE_MENU;
    if (mfb_cfg.timeout > 0 && !at_start) {
        MFB_MARK("countdown state=start seconds=%d", mfb_cfg.timeout);
    }
    MFB_MARK("ready");
    if (at_start) {
        maint_screen(why, mfb_start_skip_back(why, input_key_down(INPUT_KEY_BACK)));
    }
}

/* Step 6: a failed load. The error line under the menu, and the menu stays. */
static void load_failed(const mfb_entry_t *e, int err)
{
    char line[MFB_LINE_MAX];

    mfb.error = err;
    MFB_MARK("error code=%s path=%s name=%s", mfb_err_name(err), e->path, e->name);
    mfb_error_line(line, sizeof(line), err, e->name);
    clear_row(after_row(MFB_ROW_START_AFTER));
    put_row(after_row(MFB_ROW_ERROR_AFTER), line, CONSOLE_ATTR_BRIGHT, 1);
    mfb_menu_stop(&menu);
    redraw();
    if (mfb.fb_ok) {
        console_set_cursor(0, menu_top + menu_rows - 1);
    }
    mfb.phase = MFB_PHASE_MENU;
}

/*
 * Step 5: the boot info and the jump. The cursor goes to the row after
 * the menu, so the next stage writes below it.
 */
static void handoff(const mfb_entry_t *e, uint32_t subtype)
{
    robco_bootinfo_t *bi = (robco_bootinfo_t *)ROBCO_BOOTINFO_ADDR;
    char line[MFB_LINE_MAX];
    char name[sizeof(res.hdr->name) + 1];

    fs_unmount("sd");
    vol_mounted = 0;
    k_snprintf(line, sizeof(line), "Starting %s.", e->name);
    clear_row(after_row(MFB_ROW_ERROR_AFTER));
    put_row(after_row(MFB_ROW_START_AFTER), line, CONSOLE_ATTR_NORMAL, 1);
    if (mfb.fb_ok) {
        console_set_cursor(0, menu_top + after_row(MFB_ROW_START_AFTER));
        console_putc('\n');
        console_cursor_show(0);
    }
    bootinfo_build(bi, ROBCO_STAGE_BOOTAGENT);
    bi->boot_flags = mfb_boot_flags(bi->boot_flags, mfb.inherited, mfb.flags_in, mfb.user_maint,
                                    mfb.req, subtype);
    /*
     * handoff() has one caller, boot_selected(), and it runs only after
     * mfb_remount() returned 0 and the entry loaded from that volume.
     * So good_lba is the volume the image came from, never a cleared one.
     */
    mfb_bootinfo_set_boot(bi, ROBCO_BOOT_DEV_EMMC, good_lba, e->path, e->cmdline);
    k_memcpy(name, res.hdr->name, sizeof(res.hdr->name));
    name[sizeof(res.hdr->name)] = '\0';
    MFB_MARK("bootinfo flags=0x%08x caps=0x%08x dev=%u part_lba=%u path=%s cmdline_len=%u "
             "scheme=%u cx=%u cy=%u",
             (unsigned)bi->boot_flags, (unsigned)bi->caps, (unsigned)bi->boot_dev,
             (unsigned)bi->boot_part_lba, bi->boot_path[0] != '\0' ? bi->boot_path : "none",
             (unsigned)k_strlen(bi->cmdline), (unsigned)bi->con_scheme, (unsigned)bi->con_cx,
             (unsigned)bi->con_cy);
    MFB_MARK("jump entry=0x%08x name=%s", (unsigned)res.entry, name);
    uart_flush();
    robco_handoff(res.entry, bi, bi->fdt_size != 0 ? bi->fdt_addr : 0);
}

/*
 * The header steps up to the subtype check, before loader_load() trusts
 * anything else in the image. Reads the first 0x60 bytes of the file.
 * A file that does not open or read returns RIMG_OK: loader_load() then
 * reports nofile or io in its own words.
 */
static int precheck(const char *path)
{
    static fs_file_t f;
    uint8_t head[RIMG_META_OFFSET];
    int64_t size;
    int n;

    if (fs_open(&f, path, FS_READ) != 0) {
        return RIMG_OK;
    }
    size = fs_size(&f);
    n = fs_read(&f, head, sizeof(head));
    (void)fs_close(&f);
    if (size < 0 || size > 0xFFFFFFFF || n < 0) {
        return RIMG_OK;
    }
    return mfb_check_os_header(head, (size_t)n, (uint32_t)size);
}

int mfb_check_entry(const mfb_entry_t *e, loader_result_t *out)
{
    const loader_slot_t slot = { MFB_OS_SLOT_BASE, MFB_OS_SLOT_SIZE, NULL };
    const rimg_expect_t expect = { .magic = RIMG_MAGIC_BOOT, .abi_major = MFB_OS_ABI };
    int rc = precheck(e->path);

    if (rc == RIMG_OK) {
        rc = loader_load(e->path, &slot, &expect, out);
    }
    return rc;
}

/* Step 5: loads the selected entry into the OS slot. Returns only when it fails. */
static void boot_selected(void)
{
    const mfb_entry_t *e = &mfb_cfg.entry[menu.sel];
    unsigned index = menu.sel + 1;
    int via_key = menu.via == MFB_VIA_KEY;
    char line[MFB_LINE_MAX];
    char name[sizeof(res.hdr->name) + 1];
    const char *what;
    int rc;

    if (start_maint) {
        /* The screen's action: the MAINT entry, or MFB_MAINT_PATH with index 0. */
        start_maint = 0;
        via_key = 1;
        e = mfb_maint_entry(&mfb_cfg, &index);
        if (e == NULL) {
            mfb_maint_fixed(&maint_fixed);
            e = &maint_fixed;
        }
    }
    mfb.phase = MFB_PHASE_LOAD;
    MFB_MARK("choose index=%u name=%s path=%s via=%s", index, e->name, e->path,
             via_key ? "key" : "timeout");
    redraw();
    mfb_line_loading(line, sizeof(line), e);
    put_row(after_row(MFB_ROW_START_AFTER), line, CONSOLE_ATTR_NORMAL, 1);
    /* The card may have changed since the boot: mount the one in the slot now. */
    if (mfb_remount(&what, &rc) != 0) {
        MFB_MARK("remount what=%s rc=%d", what, rc);
        load_failed(e, MFB_ERR_IO);
        return;
    }
    rc = mfb_check_entry(e, &res);
    if (rc != RIMG_OK) {
        load_failed(e, rc);
        return;
    }
    k_memcpy(name, res.hdr->name, sizeof(res.hdr->name));
    name[sizeof(res.hdr->name)] = '\0';
    MFB_MARK("load path=%s size=%u crc=0x%08x addr=0x%08x name=%s subtype=%u", res.path,
             (unsigned)res.file_size, (unsigned)res.crc32, (unsigned)res.entry, name,
             (unsigned)res.hdr->subtype);
    handoff(e, res.hdr->subtype);
}

void robco_main(void)
{
    mfb.phase = MFB_PHASE_INIT;
    entry();
    mount();
    check_request();
    read_config();
    show_menu();
    for (;;) {
        (void)menu_loop();
        boot_selected();
    }
}

void mfb_sys_info(mfb_ms_sys_t *sys)
{
    const robco_board_t *b = board_get();

    sys->board = board_model_name(b->board_rev);
    sys->soc = b->soc_name;
    sys->mem_bytes = b->mem_size;
    sys->mounted = vol_mounted;
    sys->have_good = vol_have_good;
    sys->vol_lba = good_lba;
    sys->vol_blocks = good_blocks;
    sys->fail_what = fail_what;
    sys->fail_rc = fail_rc;
    sys->trusted = mfb.inherited;
    sys->flags_in = mfb.flags_in;
    sys->version = MFB_VERSION_STR;
    sys->core_pin = MFB_CORE_PIN;
    sys->screen = mfb.screen;
}

void mfb_cfg_info(mfb_ms_cfg_t *c)
{
    c->result = cfg_result;
    c->line = cfg_line;
    c->path = MFB_CFG_PATH;
    c->def = mfb_cfg.entry[mfb_cfg.def].name;
    c->timeout = mfb_cfg.timeout;
    c->entries = mfb_cfg.count;
    c->ignored = cfg_rep.ignored;
    c->dup = cfg_rep.dup;
    c->bad = cfg_rep.bad;
    c->clamped = cfg_rep.clamped;
}
