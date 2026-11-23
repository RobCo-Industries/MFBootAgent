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
#include <robco/irq.h>
#include <robco/klibc.h>
#include <robco/part.h>
#include <robco/rimg.h>
#include <robco/sd.h>
#include <robco/tick.h>
#include <robco/uart.h>

#include "boot.h"
#include "bootcfg.h"
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
    k_snprintf(buf, size, "phase=%s trusted=%d", phase_name(mfb.phase), mfb.inherited);
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

void robco_main(void)
{
    mfb.phase = MFB_PHASE_INIT;
    entry();
    mount();
    read_config();
    maint_idle();
}
