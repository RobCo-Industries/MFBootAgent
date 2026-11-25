/*
 * MAINT.SYS: the maintenance image of the terminal chain. MFBootAgent
 * loads it into the OS slot from its MAINT entry or its maintenance
 * screen; it also runs alone. It mounts the boot volume behind a write
 * guard, reads [maint] from BOOT.CFG and shows a menu of six tools.
 * docs/MAINT.md describes it.
 */

#include <stdarg.h>
#include <stdint.h>

#include <robco/board.h>
#include <robco/boot.h>
#include <robco/bootinfo.h>
#include <robco/console.h>
#include <robco/fb.h>
#include <robco/fs.h>
#include <robco/gpio.h>
#include <robco/i2c.h>
#include <robco/input.h>
#include <robco/irq.h>
#include <robco/klibc.h>
#include <robco/part.h>
#include <robco/power.h>
#include <robco/rimg.h>
#include <robco/sd.h>
#include <robco/tick.h>
#include <robco/timer.h>
#include <robco/uart.h>
#include <robco/uart_mini.h>

#include "mt.h"
#include "../src/portreq.h"

#define SCREEN_W 640u
#define SCREEN_H 480u
#define DEBOUNCE_MS 20u
/* The longest wait for the GPIO stuck-line guard at start. */
#define GUARD_WAIT_MS 1000u
/* The longest wait for one input event in the main loop. */
#define LOOP_MS 50u

mt_state_t mt;

/* The six buttons of BOARD_SUPPORT.md: each ties its pin to ground, with the pull-up. */
static const struct {
    unsigned pin;
    unsigned code;
} buttons[] = {
    { 5, INPUT_KEY_UP },
    { 6, INPUT_KEY_DOWN },
    { 17, INPUT_KEY_LEFT },
    { 22, INPUT_KEY_RIGHT },
    { 26, INPUT_KEY_SELECT },
    { 27, INPUT_KEY_BACK },
};

/* The boot volume is the one that holds one of these. */
static const char *const markers[] = { "/ROBCO/MAINT.SYS", "/ROBCO/BOOT.CFG", "/ROBCO/UOS.SYS" };

static mt_menu_t menu;
static int results[MT_TOOLS];
static char texts[MT_TOOLS][MT_TEXT_MAX];
static char status[MT_LINE_MAX];
static fs_boot_t vol;
static char cfg_text[MT_CFG_FILE_MAX];
/*
 * The port's tool request, see mfb_port_req(): req_live is the number of
 * the marked event that waits in the input queue, or 0, and req_tool the
 * tool it runs.
 */
static int32_t req_live;
static int32_t req_seq;
static unsigned req_tool;

/* ---- Output ---------------------------------------------------------- */

void mt_mark(const char *fmt, ...)
{
    char buf[MT_LINE_MAX + MT_KEYS_MAX];
    va_list ap;

    va_start(ap, fmt);
    k_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    k_printf("@RC stage=" ROBCO_STAGE_NAME " ev=%s\n", buf);
}

void mt_keys(mt_run_t *r, const char *fmt, ...)
{
    size_t n = k_strlen(r->keys);
    va_list ap;

    if (n + 2 >= sizeof(r->keys)) {
        return;
    }
    if (n != 0) {
        r->keys[n++] = ' ';
        r->keys[n] = '\0';
    }
    va_start(ap, fmt);
    k_vsnprintf(r->keys + n, sizeof(r->keys) - n, fmt, ap);
    va_end(ap);
}

void mt_text(mt_run_t *r, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    k_vsnprintf(r->text, sizeof(r->text), fmt, ap);
    va_end(ap);
}

/*
 * Writes one screen row, padded with spaces to the last column so every
 * cell of the row is known. The last column stays blank: the console
 * would wrap on the next character.
 */
static void put_row(unsigned row, const char *text, unsigned attr, int to_uart)
{
    if (mt.fb_ok && row < console_rows()) {
        unsigned cols = console_cols();
        unsigned n = (unsigned)k_strlen(text);

        if (n >= cols) {
            n = cols != 0 ? cols - 1 : 0;
        }
        console_set_cursor(0, row);
        console_set_attr(attr);
        console_write(text, n);
        console_set_attr(CONSOLE_ATTR_NORMAL);
        for (; n + 1 < cols; n++) {
            console_putc(' ');
        }
    }
    if (to_uart && text[0] != '\0') {
        uart_puts(text);
        uart_putc('\n');
    }
}

void mt_work_line(unsigned row, const char *text, int to_uart)
{
    if (row < MT_WORK_ROWS) {
        put_row(MT_ROW_WORK + row, text, CONSOLE_ATTR_NORMAL, to_uart);
    } else if (to_uart && text[0] != '\0') {
        uart_puts(text);
        uart_putc('\n');
    }
}

void mt_work_clear(void)
{
    unsigned i;

    for (i = 0; i < MT_WORK_ROWS; i++) {
        put_row(MT_ROW_WORK + i, "", CONSOLE_ATTR_NORMAL, 0);
    }
}

void mt_progress(dg_rate_t *rate, unsigned row, const char *name, uint64_t done, uint64_t total)
{
    char line[MT_LINE_MAX];
    unsigned pct;

    if (!dg_rate_due(rate, timer_now_ms())) {
        return;
    }
    pct = dg_percent(done, total);
    k_snprintf(line, sizeof(line), "Working: %u%%", pct);
    mt_work_line(row, line, 0);
    mt_mark("progress name=%s pct=%u", name, pct);
}

/* ---- Input ----------------------------------------------------------- */

int mt_poll_abort(void)
{
    input_event_t ev;
    int stop = 0;

    while (input_poll(&ev)) {
        if (mt_is_abort(&ev)) {
            stop = 1;
        }
    }
    return stop;
}

int mt_wait_event(input_event_t *ev, uint32_t ms)
{
    if (!input_wait(ev, ms)) {
        return 0;
    }
    /* A dropped port request is no key for the tool. */
    return mfb_port_req(ev, 0) == MFB_PORT_NONE;
}

int mt_ask(void)
{
    input_event_t ev;

    for (;;) {
        if (mt_wait_event(&ev, LOOP_MS)) {
            int a = mt_answer(&ev);

            if (a != MT_ANS_NONE) {
                return a;
            }
        }
    }
}

int mt_i2c(void)
{
    if (mt.i2c_rc == 1) {
        mt.i2c_rc = i2c_init(100000);
        mt_mark("i2c rc=%d", mt.i2c_rc);
    }
    return mt.i2c_rc;
}

int mt_need_card(mt_run_t *r)
{
    if (mt.mounted) {
        return 0;
    }
    if (mt.sd_rc != 0) {
        r->result = mt.sd_rc == SD_ERR_NOCARD ? MT_ABSENT : MT_FAIL;
        mt_keys(r, "why=sd rc=%d", mt.sd_rc);
        mt_text(r, mt.sd_rc == SD_ERR_NOCARD ? "No card." : "The card does not start.");
    } else {
        r->result = MT_FAIL;
        mt_keys(r, "why=mount rc=%d", mt.mount_rc);
        mt_text(r, "No volume mounts.");
    }
    return -1;
}

/* ---- Menu ------------------------------------------------------------ */

static void draw_item(unsigned i)
{
    char line[MT_LINE_MAX];
    int selected = i == menu.sel;

    mt_line_item(line, sizeof(line), i, selected, results[i], texts[i]);
    put_row(MT_ROW_ITEM0 + i, line, selected ? CONSOLE_ATTR_BRIGHT : CONSOLE_ATTR_NORMAL, 0);
}

static void draw_status(int to_uart)
{
    put_row(MT_ROW_STATUS, status, CONSOLE_ATTR_BRIGHT, to_uart);
}

/* The whole menu, every row once, on the screen; to_uart: also on the PL011. */
static void draw_menu(int to_uart)
{
    char line[MT_LINE_MAX];
    unsigned i;

    if (mt.fb_ok) {
        console_clear();
    }
    mt_line_title(line, sizeof(line), MT_VERSION_STR);
    put_row(MT_ROW_TITLE, line, CONSOLE_ATTR_BRIGHT, to_uart);
    put_row(MT_ROW_SUB, mt_line_sub(), CONSOLE_ATTR_NORMAL, to_uart);
    put_row(MT_ROW_RULE, mt_line_rule(), CONSOLE_ATTR_DIM, to_uart);
    for (i = 0; i < MT_TOOLS; i++) {
        draw_item(i);
        if (to_uart) {
            mt_line_item(line, sizeof(line), i, i == menu.sel, results[i], texts[i]);
            uart_puts(line);
            uart_putc('\n');
        }
    }
    put_row(MT_ROW_HELP, mt_line_help(), CONSOLE_ATTR_DIM, to_uart);
    draw_status(to_uart);
    put_row(MT_ROW_RULE2, mt_line_rule(), CONSOLE_ATTR_DIM, 0);
}

static void redraw_selection(void)
{
    unsigned old = 0;

    if (mt_menu_take(&menu, &old) & MT_DIRTY_SEL) {
        draw_item(old);
        draw_item(menu.sel);
        mt_mark("select index=%u name=%s", menu.sel + 1, mt_tool_name(menu.sel));
    }
}

/* ---- Running tools --------------------------------------------------- */

static void reboot(const char *source);

/*
 * Restart: the tool marker, then the reboot. The volume is flushed and
 * unmounted first, so nothing waits in a buffer.
 */
static void tool_restart(mt_run_t *r)
{
    char line[MT_LINE_MAX + MT_KEYS_MAX];

    r->result = MT_OK;
    mt_line_tool(line, sizeof(line), MT_T_RESTART, MT_OK, "");
    mt_mark("%s", line);
    reboot("tool");
}

typedef void (*tool_fn)(mt_run_t *r);

static const tool_fn tools[MT_TOOLS] = {
    mt_tool_card, mt_tool_images, mt_tool_view, mt_tool_clock, mt_tool_users, tool_restart,
};

/* Runs one tool and reports it. */
static void run_tool(unsigned t)
{
    static mt_run_t r;
    char line[MT_LINE_MAX + MT_KEYS_MAX];
    int ro = mt_tool_readonly(t);

    k_memset(&r, 0, sizeof(r));
    r.result = MT_FAIL;
    mt.phase = MT_PHASE_TOOL;
    mt.tool = (int)t;
    mt_work_clear();
    mt_line_running(line, sizeof(line), t);
    put_row(MT_ROW_STATUS, line, CONSOLE_ATTR_BRIGHT, 1);
    /* Read-only tools: every write is counted and refused. */
    mt.guard.pass = 0;
    mt.guard.writes = 0;
#ifdef MT_TEST
    if (ro && mt.write_probe) {
        static uint8_t block[BLK_SECTOR_SIZE];

        mt.write_probe = 0;
        mt_keys(&r, "probe_rc=%d", blk_write(&mt.guard.dev, 0, 1, block));
    }
#endif
    tools[t](&r);
    mt.guard.pass = 0;
    if (ro) {
        mt_keys(&r, "writes=%u", (unsigned)mt.guard.writes);
        if (mt.guard.writes != 0) {
            r.result = MT_FAIL;
            mt_text(&r, "%u writes refused.", (unsigned)mt.guard.writes);
        }
    }
    mt_line_tool(line, sizeof(line), t, r.result, r.keys);
    mt_mark("%s", line);
    results[t] = r.result;
    k_strncpy(texts[t], r.text, sizeof(texts[t]));
    texts[t][sizeof(texts[t]) - 1] = '\0';
    draw_item(t);
    mt_line_status(status, sizeof(status), t, r.result, r.text);
    draw_status(1);
    mt.tool = -1;
    mt.phase = MT_PHASE_MENU;
}

/*
 * An earlier action overtook the port's request: it never runs. Its
 * marked event stays in the queue and comes up stale.
 */
static void drop_request(const char *why)
{
    if (req_live != 0) {
        req_live = 0;
        mt_mark("tool_drop name=%s why=%s", mt_tool_name(req_tool), why);
    }
}

static void reboot(const char *source)
{
    drop_request("reboot");
    if (mt.mounted) {
        (void)fs_unmount(MT_VOL);
        mt.mounted = 0;
    }
    mt_mark("reboot source=%s", source);
    uart_flush();
    power_reboot();
}

/* ---- Maintenance port ------------------------------------------------ */

static input_script_reply_t *dump_reply;

static void dump_char(char c)
{
    char s[2] = { c, '\0' };

    dump_reply->write(s);
}

static const char *phase_name(int phase)
{
    switch (phase) {
    case MT_PHASE_INIT: return "init";
    case MT_PHASE_MENU: return "menu";
    case MT_PHASE_TOOL: return "tool";
    default:            return "unknown";
    }
}

static int command(int argc, char *argv[], input_script_reply_t *reply, void *ctx)
{
    char line[MT_LINE_MAX];

    (void)ctx;
    if (k_strcmp(argv[0], "state") == 0 && argc == 1) {
        /*
         * request: a tool the port asked for, not started yet. pending:
         * input events not taken yet. The port answers from inside
         * input_poll(), before it takes the next event, so pending=0
         * means the image handled every key sent before this command.
         */
        k_snprintf(line, sizeof(line),
                   "phase=%s trusted=%d fb=%d sel=%u tool=%s request=%s card=%d "
                   "restore_users=%s pending=%u\n",
                   phase_name(mt.phase), mt.trusted, mt.fb_ok, menu.sel + 1,
                   mt.tool >= 0 ? mt_tool_name((unsigned)mt.tool) : "none",
                   req_live == 0 ? "none" : mt_tool_name(req_tool), mt.mounted,
                   mt.cfg.restore_users ? "on" : "off", input_pending());
        reply->write(line);
        return 0;
    }
    if (k_strcmp(argv[0], "tool") == 0 && argc == 2) {
        int t = mt_tool_find(argv[1]);
        input_event_t wake = { 0 };

        if (t < 0) {
            reply->reason = "unknown tool";
            return -1;
        }
        if (mt.phase != MT_PHASE_MENU || req_live != 0) {
            reply->reason = "busy";
            return -1;
        }
        /*
         * Long work runs in the main loop, never in this handler. The
         * request goes through the input queue, behind the keys sent
         * before it: the menu takes them first.
         */
        wake.type = INPUT_EV_CHAR;
        wake.source = INPUT_SRC_SCRIPT;
        wake.value = req_seq < INT32_MAX ? req_seq + 1 : 1;
        if (input_push(&wake) != 0) {
            reply->reason = "busy";
            return -1;
        }
        req_seq = wake.value;
        req_live = wake.value;
        req_tool = (unsigned)t;
        return 0;
    }
    if (k_strcmp(argv[0], "dump") == 0 && argc == 1) {
        if (!mt.fb_ok) {
            reply->reason = "no screen";
            return -1;
        }
        dump_reply = reply;
        console_dump(dump_char);
        return 0;
    }
    if (k_strcmp(argv[0], "reboot") == 0 && argc == 1) {
        reboot("maint");
    }
#ifdef MT_TEST
    if (k_strcmp(argv[0], "wprobe") == 0 && argc == 1) {
        mt.write_probe = 1;
        return 0;
    }
#endif
    reply->reason = "unknown command";
    return -1;
}

/* ---- Entry ----------------------------------------------------------- */

static const char *mode_name(uint32_t mode)
{
    switch (mode) {
    case 0x13: return "svc";
    case 0x1a: return "hyp";
    default:   return "other";
    }
}

/*
 * The screen, by the rule of BOOT_PROTOCOL.md for a later stage: adopt
 * the earlier stage's framebuffer and console when the boot info is
 * trusted and carries them, else a fresh one. Then a clear: the menu
 * needs the whole screen.
 */
static void init_screen(void)
{
    const robco_bootinfo_t *bi = mt.trusted ? bootinfo_get() : NULL;
    unsigned scheme = CONSOLE_SCHEME_GREEN;
    fb_info_t info;
    int adopted = 0;
    int fb = -1;
    int con = -1;

    if (bi != NULL && bi->con_scheme < CONSOLE_SCHEME_COUNT) {
        scheme = bi->con_scheme;
    }
    if (bi != NULL && (bi->caps & ROBCO_CAP_FB) != 0 && bootinfo_fb_info(bi, &info) == 0 &&
        fb_adopt(&info) == 0) {
        fb = 0;
        con = console_adopt(bi->con_cols, bi->con_rows, 0, 0, scheme);
        if (con == 0 && (console_cols() < MT_MIN_COLS || console_rows() < MT_MIN_ROWS)) {
            con = -1;
        }
        adopted = con == 0;
    }
    if (!adopted) {
        fb = fb_init(SCREEN_W, SCREEN_H, 32);
        con = fb == 0 ? console_init(scheme) : -1;
        if (con == 0 && (console_cols() < MT_MIN_COLS || console_rows() < MT_MIN_ROWS)) {
            con = -1;
        }
    }
    mt.fb_ok = fb == 0 && con == 0;
    if (mt.fb_ok) {
        console_cursor_show(0);
        console_clear();
    }
    mt_mark("screen fb=%d console=%d path=%s scheme=%u cols=%u rows=%u", fb, con,
            adopted ? "adopt" : "init", scheme, mt.fb_ok ? console_cols() : 0,
            mt.fb_ok ? console_rows() : 0);
}

static void init_input(void)
{
    int mini = uart_mini_init(115200);
    int serial;
    int script = INPUT_ERR_NO_UART;
    int gpio;
    unsigned i;

    input_init();
    serial = input_serial_start(INPUT_SERIAL_MAIN);
    input_script_set_handler(command, NULL);
    if (mini == 0) {
        script = input_script_start();
    }
    for (i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        (void)input_gpio_add_button(buttons[i].pin, buttons[i].code, 0, GPIO_PULL_UP);
    }
    gpio = input_gpio_start(MT_TICK_HZ, DEBOUNCE_MS);
    if (gpio == 0) {
        /*
         * Wait for the stuck-line guard, as the agent does, so the first
         * press comes after it. Buttons held when it ends come as presses.
         */
        uint32_t t0 = timer_now_ms();

        while (input_gpio_status() == INPUT_GPIO_CHECKING && timer_now_ms() - t0 < GUARD_WAIT_MS) {
            __asm__ volatile("wfi" ::: "memory");
        }
    }
    mt_mark("input serial=%d mini=%d script=%d gpio=%d buttons=%s", serial, mini, script, gpio,
            input_gpio_status_name(input_gpio_status()));
}

/*
 * The boot volume, by the rule of BOOT_PROTOCOL.md, through the write
 * guard. It stays mounted while the image runs.
 */
static void mount_card(void)
{
    mt.sd_rc = sd_init();
    if (mt.sd_rc != 0) {
        mt_mark("error code=io what=sd rc=%d", mt.sd_rc);
        return;
    }
    dg_sd_guard_init(&mt.guard, sd_blkdev());
    mt.mount_rc = fs_mount_boot(MT_VOL, &mt.guard.dev, markers,
                                sizeof(markers) / sizeof(markers[0]), &vol);
    if (mt.mount_rc != PART_BOOT_OK) {
        mt_mark("error code=io what=fat rc=%d", mt.mount_rc);
        return;
    }
    mt.mounted = 1;
    mt_mark("mount lba=%u blocks=%u", (unsigned)vol.start_lba, (unsigned)vol.block_count);
}

/* [maint] of BOOT.CFG, read through the guard: the read writes nothing. */
static void read_config(void)
{
    size_t len = 0;
    unsigned line = 0;
    int rc;

    mt_cfg_default(&mt.cfg);
    if (!mt.mounted) {
        mt_mark("config result=bad path=" MT_CFG_PATH " why=read rc=%d restore_users=off",
                mt.sd_rc != 0 ? mt.sd_rc : mt.mount_rc);
        return;
    }
    rc = fs_read_file(MT_CFG_PATH, cfg_text, sizeof(cfg_text), &len);
    if (rc == FS_ERR_NOENT) {
        mt_mark("config result=missing path=" MT_CFG_PATH " restore_users=off");
    } else if (rc != 0) {
        mt_mark("config result=bad path=" MT_CFG_PATH " why=read rc=%d restore_users=off", rc);
    } else if (mt_cfg_parse(cfg_text, len, &mt.cfg, &line) != MT_CFG_OK) {
        mt_mark("config result=bad path=" MT_CFG_PATH " why=syntax line=%u restore_users=off",
                line);
    } else {
        mt_mark("config result=ok path=" MT_CFG_PATH " restore_users=%s bad=%u unknown=%u",
                mt.cfg.restore_users ? "on" : "off", mt.cfg.bad, mt.cfg.unknown);
    }
}

static void entry(void)
{
    const robco_board_t *b = board_get();
    extern const rimg_header_t robco_rimg;
    /* First, before any code writes initialised data, which the CRC covers. */
    int self = rimg_self_check();

    uart_init(115200);
    k_printf("\n");
    mt_mark("boot midr=0x%08x soc=%s base=0x%08x board_rev=0x%08x mem=0x%08x entry_mode=%s "
            "version=" MT_VERSION_STR,
            (unsigned)b->midr, b->soc_name, (unsigned)b->periph_base, (unsigned)b->board_rev,
            (unsigned)b->mem_size, mode_name(robco_boot_args()->entry_mode));
    mt.trusted = bootinfo_inherited();
    if (mt.trusted) {
        mt_mark("handoff trusted=1 flags=0x%08x", (unsigned)bootinfo_get()->boot_flags);
    } else {
        mt_mark("handoff trusted=0");
    }
    mt.tool = -1;
    mt.i2c_rc = 1;
    init_screen();
    irq_init();
    tick_init(MT_TICK_HZ);
    irq_cpu_enable();
    init_input();
    mt_mark("selfcheck rc=%s size=%u", rimg_strerror(self), (unsigned)robco_rimg.file_size);
#ifdef MT_TEST
    mt_mark("write_hook name=guard");
#endif
    mount_card();
    read_config();
}

void robco_main(void)
{
    input_event_t ev;
    unsigned i;
    unsigned t = 0;

    mt.phase = MT_PHASE_INIT;
    entry();
    for (i = 0; i < MT_TOOLS; i++) {
        results[i] = MT_NONE;
    }
    k_strncpy(status, mt_line_idle(), sizeof(status));
    mt_menu_init(&menu, MT_TOOLS);
    uart_putc('\n');
    draw_menu(1);
    mt.phase = MT_PHASE_MENU;
    mt_mark("ready");
    for (;;) {
        int act = MT_ACT_NONE;

        if (input_wait(&ev, LOOP_MS)) {
            int port = mfb_port_req(&ev, req_live);

            if (port == MFB_PORT_LIVE) {
                /* The port's tool, in its turn behind the keys before it. */
                req_live = 0;
                t = req_tool;
                act = MT_ACT_RUN;
            } else if (port == MFB_PORT_NONE) {
                act = mt_menu_event(&menu, &ev, &t);
                redraw_selection();
            }
        }
        if (act == MT_ACT_RUN) {
            /* A key started this run before the port's request came up. */
            drop_request("run");
            run_tool(t);
        }
    }
}
