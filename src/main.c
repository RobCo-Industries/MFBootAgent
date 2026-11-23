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
#include "menu.h"
#include "menulines.h"
#include "mfb.h"
#include "screen.h"

#define TICK_HZ 100
#define SCREEN_W 640
#define SCREEN_H 480

mfb_state_t mfb;

/*
 * The boot volume markers: BOOT.CFG and the default OS path. A volume
 * with either file is the one this stage boots from.
 */
static const char *const vol_markers[] = { MFB_CFG_PATH, MFB_DEFAULT_PATH };
static fs_boot_t vol;
static mfb_cfg_t cfg;
static char cfg_text[MFB_CFG_FILE_MAX];

static loader_result_t res;
static mfb_menu_t menu;
static unsigned menu_top;       /* console row of the menu's first row */
static unsigned menu_rows;      /* MFB_MENU_ROWS(cfg.count) */

/* The text the previous stage left on the screen, read before fb_init(). */
static mfb_cell_t kept[ROBCO_CONSOLE_MAX_ROWS * ROBCO_CONSOLE_MAX_COLS];

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
    k_snprintf(buf, size, "phase=%s trusted=%d entries=%u sel=%u counting=%d left=%u error=%s",
               phase_name(mfb.phase), mfb.inherited, mfb.menu_ready ? cfg.count : 0,
               mfb.menu_ready ? menu.sel + 1 : 0, mfb.menu_ready ? menu.counting : 0,
               mfb.menu_ready ? mfb_menu_left(&menu, timer_now_ms()) : 0,
               mfb.error != 0 ? mfb_err_name(mfb.error) : "none");
}

/*
 * 1 when the trusted block describes a console this stage can take over:
 * a 32-bit framebuffer of the size this stage uses, with a text grid
 * that fits it and the console buffer.
 */
static int screen_usable(const robco_bootinfo_t *bi)
{
    return bi != NULL && (bi->caps & ROBCO_CAP_FB) != 0 && bi->fb_addr != 0 &&
           bi->fb_depth == 32 && bi->fb_width == SCREEN_W && bi->fb_height == SCREEN_H &&
           bi->fb_pitch >= SCREEN_W * 4 && bi->con_cols == SCREEN_W / 8 &&
           bi->con_rows == SCREEN_H / 16 && bi->con_scheme < CONSOLE_SCHEME_COUNT &&
           bi->con_cols <= ROBCO_CONSOLE_MAX_COLS && bi->con_rows <= ROBCO_CONSOLE_MAX_ROWS &&
           bi->con_cy < bi->con_rows;
}

/* Writes the kept cells into the fresh console and puts the cursor where it was. */
static void replay(unsigned cols, unsigned rows, unsigned cx, unsigned cy)
{
    unsigned r, c;

    for (r = 0; r < rows; r++) {
        for (c = 0; c < cols; c++) {
            const mfb_cell_t *k = &kept[r * cols + c];

            if (k->ch == ' ' && k->attr == CONSOLE_ATTR_NORMAL) {
                continue;
            }
            console_set_cursor(c, r);
            console_set_attr(k->attr);
            console_putc((char)k->ch);
        }
    }
    console_set_attr(CONSOLE_ATTR_NORMAL);
    console_set_cursor(cx < cols ? cx : 0, cy);
}

/*
 * The screen. After a trusted handoff it continues the previous stage's
 * screen: same mode, same colour scheme, the old text in place and the
 * cursor where it was. Cold, it starts a green screen.
 */
static void init_screen(void)
{
    const robco_bootinfo_t *bi = bootinfo_get();
    unsigned scheme = CONSOLE_SCHEME_GREEN;
    unsigned unknown = 0;
    int keep = 0;
    int fb;
    int con = -1;

    if (bi != NULL && bi->con_scheme < CONSOLE_SCHEME_COUNT) {
        scheme = bi->con_scheme;
    }
    if (screen_usable(bi)) {
        mfb_palette_t pal;

        if (mfb_palette(scheme, bi->fb_order, &pal) == 0) {
            unknown = mfb_screen_read((const uint8_t *)bi->fb_addr, bi->fb_pitch, bi->con_cols,
                                      bi->con_rows, &pal, kept);
            keep = 1;
        }
    }
    fb = fb_init(SCREEN_W, SCREEN_H, 32);
    if (fb == 0) {
        con = console_init(scheme);
    }
    mfb.fb_ok = fb == 0 && con == 0;
    if (keep && mfb.fb_ok && console_cols() == bi->con_cols && console_rows() == bi->con_rows) {
        replay(bi->con_cols, bi->con_rows, bi->con_cx, bi->con_cy);
    } else {
        keep = 0;
    }
    MFB_MARK("screen fb=%d console=%d kept=%d unknown=%u scheme=%u", fb, con, keep, unknown,
             scheme);
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
    MFB_MARK("handoff trusted=%d", mfb.inherited);
    if (!mfb.inherited) {
        /* Cold: copy the firmware blob as a first stage does. bootinfo_build() records it. */
        bootinfo_copy_fw_blob(ROBCO_HANDOFF_FDT_ADDR, ROBCO_HANDOFF_FDT_MAX);
    }
    init_screen();
    irq_init();
    tick_init(TICK_HZ);
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

/* Step 2: the boot volume, by the rule every stage shares. */
static void mount(void)
{
    int rc = sd_init();

    if (rc != 0) {
        halt_io("sd", rc);
    }
    rc = fs_mount_boot("sd", sd_blkdev(), vol_markers,
                       sizeof(vol_markers) / sizeof(vol_markers[0]), &vol);
    if (rc != PART_BOOT_OK) {
        halt_io("fat", rc);
    }
    MFB_MARK("mount lba=%u blocks=%u", (unsigned)vol.start_lba, (unsigned)vol.block_count);
}

/* Step 3: the menu entries from BOOT.CFG, or the one default entry. */
static void read_config(void)
{
    mfb_cfg_report_t r;
    size_t len = 0;
    unsigned err_line = 0;
    char timeout[8];
    int rc;

    mfb_cfg_default(&cfg);
    rc = fs_read_file(MFB_CFG_PATH, cfg_text, sizeof(cfg_text), &len);
    if (rc == FS_ERR_NOENT) {
        MFB_MARK("config result=missing path=" MFB_CFG_PATH);
    } else if (rc != 0) {
        MFB_MARK("config result=bad path=" MFB_CFG_PATH " why=read rc=%d", rc);
    } else {
        rc = mfb_cfg_parse(cfg_text, len, &cfg, &r, &err_line);
        if (rc == MFB_CFG_SYNTAX) {
            MFB_MARK("config result=bad path=" MFB_CFG_PATH " why=syntax line=%u", err_line);
        } else if (rc == MFB_CFG_EMPTY) {
            MFB_MARK("config result=bad path=" MFB_CFG_PATH " why=empty ignored=%u dup=%u "
                     "unknown=%u bad=%u", r.ignored, r.dup, r.unknown, r.bad);
        } else {
            MFB_MARK("config result=ok path=" MFB_CFG_PATH " entries=%u ignored=%u dup=%u "
                     "unknown=%u bad=%u clamped=%u default_found=%d",
                     cfg.count, r.ignored, r.dup, r.unknown, r.bad, r.clamped, r.default_found);
        }
    }
    if (cfg.timeout == MFB_TIMEOUT_NONE) {
        k_snprintf(timeout, sizeof(timeout), "none");
    } else {
        k_snprintf(timeout, sizeof(timeout), "%d", cfg.timeout);
    }
    MFB_MARK("entries count=%u default=%s timeout=%s", cfg.count, cfg.entry[cfg.def].name,
             timeout);
}

/*
 * Writes one menu row on the screen, padded with spaces to column 79 so
 * that it covers the text it replaces. to_uart: also one line on the PL011.
 */
static void put_row(unsigned row, const char *text, unsigned attr, int to_uart)
{
    if (mfb.fb_ok) {
        unsigned cols = console_cols();
        unsigned n = (unsigned)k_strlen(text);

        if (n > cols - 1) {
            n = cols - 1;
        }
        console_set_cursor(0, menu_top + row);
        console_set_attr(attr);
        console_write(text, n);
        console_set_attr(CONSOLE_ATTR_NORMAL);
        for (; n < cols - 1; n++) {
            console_putc(' ');
        }
    }
    if (to_uart && text[0] != '\0') {
        uart_puts(text);
        uart_putc('\n');
    }
}

static unsigned entry_row(unsigned i)
{
    return MFB_ROW_ENTRY0 + i;
}

static unsigned after_row(unsigned offset)
{
    return MFB_ROW_ENTRY0 + cfg.count + offset;
}

static void draw_entry(unsigned i, int to_uart)
{
    char line[MFB_LINE_MAX];
    int selected = i == menu.sel;

    mfb_line_entry(line, sizeof(line), i, &cfg.entry[i], i == cfg.def, selected);
    put_row(entry_row(i), line, selected ? CONSOLE_ATTR_BRIGHT : CONSOLE_ATTR_NORMAL, to_uart);
}

static void draw_status(int to_uart)
{
    char line[MFB_LINE_MAX];

    mfb_line_status(line, sizeof(line), menu.counting, menu.stopped,
                    mfb_menu_left(&menu, timer_now_ms()), cfg.entry[cfg.def].name);
    put_row(after_row(MFB_ROW_STATUS_AFTER), line, CONSOLE_ATTR_NORMAL, to_uart);
}

/*
 * Finds the rows for the menu below the text on the screen: a blank row
 * after earlier text, and a scroll when the menu does not fit under it.
 */
static void place_menu(void)
{
    unsigned col, row, rows, i;

    menu_rows = MFB_MENU_ROWS(cfg.count);
    menu_top = 0;
    if (!mfb.fb_ok) {
        return;
    }
    rows = console_rows();
    console_get_cursor(&col, &row);
    if (col != 0) {
        row++;
    }
    if (row != 0) {
        row++;
    }
    if (row + menu_rows > rows) {
        for (i = 0; i < row + menu_rows - rows; i++) {
            console_set_cursor(0, rows - 1);
            console_putc('\n');
        }
        row = rows - menu_rows;
    }
    menu_top = row;
}

/* The whole menu, on the screen and the PL011. */
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
    for (i = 0; i < cfg.count; i++) {
        draw_entry(i, 1);
    }
    put_row(after_row(MFB_ROW_HELP_AFTER), mfb_line_help(), CONSOLE_ATTR_DIM, 1);
    draw_status(1);
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
        MFB_MARK("select index=%u name=%s", menu.sel + 1, cfg.entry[menu.sel].name);
    }
    if (d & MFB_DIRTY_STATUS) {
        draw_status(1);
        if (menu.stopped && !menu.counting) {
            MFB_MARK("countdown state=stopped");
        }
    }
}

/* Waits in the menu until an entry is chosen. Returns MFB_ACT_BOOT, or reboots. */
static int menu_loop(void)
{
    input_event_t ev;
    int act;

    for (;;) {
        act = mfb_menu_tick(&menu, timer_now_ms());
        redraw();
        if (act == MFB_ACT_NONE &&
            input_wait(&ev, mfb_menu_wait_ms(&menu, timer_now_ms())) != 0) {
            act = mfb_menu_event(&menu, &ev);
            redraw();
        }
        if (act == MFB_ACT_REBOOT) {
            MFB_MARK("reboot source=key");
            uart_flush();
            power_reboot();
        }
        if (act == MFB_ACT_BOOT) {
            return act;
        }
    }
}

/* Step 4: the menu. */
static void show_menu(void)
{
    mfb_menu_init(&menu, cfg.count, cfg.def, cfg.timeout, timer_now_ms());
    mfb.menu_ready = 1;
    place_menu();
    draw_menu();
    if (mfb.fb_ok) {
        console_set_cursor(0, menu_top + menu_rows - 1);
    }
    mfb.phase = MFB_PHASE_MENU;
    if (cfg.timeout > 0) {
        MFB_MARK("countdown state=start seconds=%d", cfg.timeout);
    }
    MFB_MARK("ready");
}

/* Clears a menu row. */
static void clear_row(unsigned row)
{
    put_row(row, "", CONSOLE_ATTR_NORMAL, 0);
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
static void handoff(const mfb_entry_t *e)
{
    robco_bootinfo_t *bi = (robco_bootinfo_t *)ROBCO_BOOTINFO_ADDR;
    char line[MFB_LINE_MAX];
    char name[sizeof(res.hdr->name) + 1];

    fs_unmount("sd");
    k_snprintf(line, sizeof(line), "Starting %s.", e->name);
    clear_row(after_row(MFB_ROW_ERROR_AFTER));
    put_row(after_row(MFB_ROW_START_AFTER), line, CONSOLE_ATTR_NORMAL, 1);
    if (mfb.fb_ok) {
        console_set_cursor(0, menu_top + after_row(MFB_ROW_START_AFTER));
        console_putc('\n');
        console_cursor_show(0);
    }
    bootinfo_build(bi, ROBCO_STAGE_BOOTAGENT);
    mfb_bootinfo_set_boot(bi, ROBCO_BOOT_DEV_EMMC, (uint32_t)vol.start_lba, e->path, e->cmdline);
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

/* Step 5: loads the selected entry into the OS slot. Returns only when it fails. */
static void boot_selected(void)
{
    const loader_slot_t slot = { MFB_OS_SLOT_BASE, MFB_OS_SLOT_SIZE, NULL };
    const rimg_expect_t expect = { .magic = RIMG_MAGIC_BOOT, .abi_major = MFB_OS_ABI };
    const mfb_entry_t *e = &cfg.entry[menu.sel];
    char line[MFB_LINE_MAX];
    char name[sizeof(res.hdr->name) + 1];
    int rc;

    mfb.phase = MFB_PHASE_LOAD;
    MFB_MARK("choose index=%u name=%s path=%s via=%s", menu.sel + 1, e->name, e->path,
             menu.via == MFB_VIA_KEY ? "key" : "timeout");
    redraw();
    mfb_line_loading(line, sizeof(line), e);
    put_row(after_row(MFB_ROW_START_AFTER), line, CONSOLE_ATTR_NORMAL, 1);
    rc = loader_load(e->path, &slot, &expect, &res);
    if (rc == RIMG_OK && !mfb_subtype_ok(res.hdr->subtype)) {
        rc = MFB_ERR_SUBTYPE;
    }
    if (rc != RIMG_OK) {
        load_failed(e, rc);
        return;
    }
    k_memcpy(name, res.hdr->name, sizeof(res.hdr->name));
    name[sizeof(res.hdr->name)] = '\0';
    MFB_MARK("load path=%s size=%u crc=0x%08x addr=0x%08x name=%s subtype=%u", res.path,
             (unsigned)res.file_size, (unsigned)res.crc32, (unsigned)res.entry, name,
             (unsigned)res.hdr->subtype);
    handoff(e);
}

void robco_main(void)
{
    mfb.phase = MFB_PHASE_INIT;
    entry();
    mount();
    read_config();
    show_menu();
    for (;;) {
        (void)menu_loop();
        boot_selected();
    }
}
