/*
 * DIAG.SYS: the diagnostics image of the terminal chain. MFBootAgent
 * loads it into the OS slot from its DIAG entry; it also runs alone. It
 * shows a menu of hardware tests and reports each result on the screen,
 * the PL011 and the maintenance port. docs/DIAG.md describes it.
 */

#include <stdarg.h>
#include <stdint.h>

#include <robco/board.h>
#include <robco/boot.h>
#include <robco/bootinfo.h>
#include <robco/console.h>
#include <robco/fb.h>
#include <robco/gpio.h>
#include <robco/i2c.h>
#include <robco/input.h>
#include <robco/irq.h>
#include <robco/klibc.h>
#include <robco/mmu.h>
#include <robco/power.h>
#include <robco/rimg.h>
#include <robco/tick.h>
#include <robco/timer.h>
#include <robco/uart.h>
#include <robco/uart_mini.h>

#include "dg.h"
#include "../src/portreq.h"

#define SCREEN_W 640u
#define SCREEN_H 480u
#define DEBOUNCE_MS 20u
/* The longest wait for one input event in the main loop. */
#define LOOP_MS 50u

dg_state_t dg;

/* The button pins of BOARD_SUPPORT.md: each ties its pin to ground, with the pull-up. */
const dg_button_t dg_buttons[DG_BUTTONS] = {
    { 5, INPUT_KEY_UP },
    { 6, INPUT_KEY_DOWN },
    { 17, INPUT_KEY_LEFT },
    { 22, INPUT_KEY_RIGHT },
    { 26, INPUT_KEY_SELECT },
    { 27, INPUT_KEY_BACK },
    { 16, INPUT_KEY_DIAL_PUSH },
};
#define DIAL_A 23u
#define DIAL_B 24u

static dg_menu_t menu;
static int results[DG_ITEMS];
static char texts[DG_ITEMS][DG_TEXT_MAX];
static char status[DG_LINE_MAX];
/*
 * The port's diag request, see mfb_port_req(): req_live is the number of
 * the marked event that waits in the input queue, or 0, and req_item the
 * row it runs.
 */
static int32_t req_live;
static int32_t req_seq;
static unsigned req_item;

/* ---- Output ---------------------------------------------------------- */

void dg_mark(const char *fmt, ...)
{
    char buf[DG_MARK_SIZE];
    va_list ap;
    int len;

    va_start(ap, fmt);
    len = k_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    dg_mark_fit(buf, sizeof(buf), len);
    k_printf("@RC stage=" ROBCO_STAGE_NAME " ev=%s\n", buf);
}

void dg_keys(dg_run_t *r, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    dg_keys_add(r->keys, sizeof(r->keys), fmt, ap);
    va_end(ap);
}

void dg_keys_last(dg_run_t *r, const char *key, const char *text)
{
    r->last_key = key;
    k_strncpy(r->last_text, text, sizeof(r->last_text));
    r->last_text[sizeof(r->last_text) - 1] = '\0';
}

void dg_text(dg_run_t *r, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    k_vsnprintf(r->text, sizeof(r->text), fmt, ap);
    va_end(ap);
}

/*
 * Text from the card can hold any byte. Every byte outside 0x20 to 0x7E
 * goes out as '?', so a row stays one row on the screen and on the serial
 * port.
 */
static void put_text(void (*put)(char), const char *text, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)text[i];

        put(c >= 0x20u && c <= 0x7eu ? (char)c : '?');
    }
}

/*
 * Writes one screen row, padded with spaces to the last column so every
 * cell of the row is known. The last column stays blank: the console
 * would wrap on the next character.
 */
static void put_row(unsigned row, const char *text, unsigned attr, int to_uart)
{
    if (dg.fb_ok && row < console_rows()) {
        unsigned cols = console_cols();
        unsigned n = (unsigned)k_strlen(text);

        if (n >= cols) {
            n = cols != 0 ? cols - 1 : 0;
        }
        console_set_cursor(0, row);
        console_set_attr(attr);
        put_text(console_putc, text, n);
        console_set_attr(CONSOLE_ATTR_NORMAL);
        for (; n + 1 < cols; n++) {
            console_putc(' ');
        }
    }
    if (to_uart && text[0] != '\0') {
        put_text(uart_putc, text, k_strlen(text));
        uart_putc('\n');
    }
}

void dg_work_line(unsigned row, const char *text, int to_uart)
{
    if (row < DG_WORK_ROWS) {
        put_row(DG_ROW_WORK + row, text, CONSOLE_ATTR_NORMAL, to_uart);
    } else if (to_uart) {
        put_row(DG_ROW_WORK + DG_WORK_ROWS, text, CONSOLE_ATTR_NORMAL, 1);
    }
}

void dg_work_clear(void)
{
    unsigned i;

    for (i = 0; i < DG_WORK_ROWS; i++) {
        put_row(DG_ROW_WORK + i, "", CONSOLE_ATTR_NORMAL, 0);
    }
}

void dg_progress(dg_rate_t *rate, unsigned row, const char *name, const char *step,
                 uint64_t done, uint64_t total)
{
    char line[DG_LINE_MAX];
    unsigned pct;

    if (!dg_rate_due(rate, timer_now_ms())) {
        return;
    }
    pct = dg_percent(done, total);
    k_snprintf(line, sizeof(line), "%s: %u%%", step, pct);
    dg_work_line(row, line, 0);
    dg_mark("diag_progress name=%s step=%s pct=%u", name, step, pct);
}

/* ---- Input ----------------------------------------------------------- */

int dg_poll_abort(void)
{
    input_event_t ev;
    int stop = 0;

    while (input_poll(&ev)) {
        if (dg_is_abort(&ev)) {
            stop = 1;
        }
    }
    return stop;
}

int dg_wait_event(input_event_t *ev, uint32_t ms)
{
    if (!input_wait(ev, ms)) {
        return 0;
    }
    /* A dropped port request is no key for the test. */
    return mfb_port_req(ev, 0) == MFB_PORT_NONE;
}

int dg_i2c(void)
{
    if (dg.i2c_rc == 1) {
        dg.i2c_rc = i2c_init(100000);
        dg_mark("i2c rc=%d", dg.i2c_rc);
    }
    return dg.i2c_rc;
}

/* ---- Menu ------------------------------------------------------------ */

static void draw_item(unsigned i)
{
    char line[DG_LINE_MAX];
    int selected = i == menu.sel;

    dg_line_item(line, sizeof(line), i, selected, results[i], texts[i]);
    put_row(DG_ROW_ITEM0 + i, line, selected ? CONSOLE_ATTR_BRIGHT : CONSOLE_ATTR_NORMAL, 0);
}

static void draw_status(int to_uart)
{
    put_row(DG_ROW_STATUS, status, CONSOLE_ATTR_BRIGHT, to_uart);
}

/* The whole menu, every row once, on the screen; to_uart: also on the PL011. */
static void draw_menu(int to_uart)
{
    char line[DG_LINE_MAX];
    unsigned i;

    if (dg.fb_ok) {
        console_clear();
    }
    dg_line_title(line, sizeof(line), DG_VERSION_STR);
    put_row(DG_ROW_TITLE, line, CONSOLE_ATTR_BRIGHT, to_uart);
    put_row(DG_ROW_SUB, dg_line_sub(), CONSOLE_ATTR_NORMAL, to_uart);
    put_row(DG_ROW_RULE, dg_line_rule(), CONSOLE_ATTR_DIM, to_uart);
    for (i = 0; i < DG_ITEMS; i++) {
        draw_item(i);
        if (to_uart) {
            dg_line_item(line, sizeof(line), i, i == menu.sel, results[i], texts[i]);
            put_text(uart_putc, line, k_strlen(line));
            uart_putc('\n');
        }
    }
    put_row(DG_ROW_HELP, dg_line_help(), CONSOLE_ATTR_DIM, to_uart);
    draw_status(to_uart);
    put_row(DG_ROW_RULE2, dg_line_rule(), CONSOLE_ATTR_DIM, 0);
}

static void redraw_selection(void)
{
    unsigned old = 0;

    if (dg_menu_take(&menu, &old) & DG_DIRTY_SEL) {
        draw_item(old);
        draw_item(menu.sel);
        dg_mark("select index=%u name=%s", menu.sel + 1,
                menu.sel < DG_TESTS ? dg_test_name(menu.sel) :
                menu.sel == DG_ITEM_ALL ? "all" : "reboot");
    }
}

/* ---- Running tests --------------------------------------------------- */

typedef void (*test_fn)(dg_run_t *r);

static const test_fn tests[DG_TESTS] = {
    dg_test_mem, dg_test_sd, dg_test_video, dg_test_audio, dg_test_input,
    dg_test_timer, dg_test_rng, dg_test_rtc, dg_test_expander,
};

/* Runs one test and reports it. Returns its result. */
static int run_test(unsigned t, int mode)
{
    static dg_run_t r;
    char line[DG_MARK_SIZE];

    k_memset(&r, 0, sizeof(r));
    r.mode = mode;
    r.result = DG_FAIL;
    dg.test = (int)t;
    dg_work_clear();
    k_snprintf(line, sizeof(line), "%s: running. ESC stops it.", dg_item_title(t));
    put_row(DG_ROW_STATUS, line, CONSOLE_ATTR_BRIGHT, 1);
    if (mode == DG_MODE_ALL && dg_test_interactive(t)) {
        r.result = DG_SKIPPED;
        dg_keys(&r, "why=interactive");
        dg_text(&r, DG_TX_PERSON);
    } else {
        tests[t](&r);
    }
    /* Video drew over the whole screen, so the menu comes back in full. */
    if (t == DG_T_VIDEO && dg.fb_ok) {
        draw_menu(0);
    }
    if (r.last_key != NULL) {
        dg_keys_text(r.keys, sizeof(r.keys), r.last_key, r.last_text);
    }
    dg_line_diag(line, sizeof(line), dg_test_name(t), r.result, r.keys);
    dg_mark("%s", line);
    results[t] = r.result;
    k_strncpy(texts[t], r.text, sizeof(texts[t]));
    texts[t][sizeof(texts[t]) - 1] = '\0';
    draw_item(t);
    dg.test = -1;
    return r.result;
}

static void run_all(void)
{
    dg_counts_t c;
    char line[DG_LINE_MAX];
    unsigned t;
    int stop = 0;

    dg_counts_init(&c);
    for (t = 0; t < DG_TESTS; t++) {
        int res;

        if (stop) {
            /* ESC stopped an earlier test: the rest do not run. */
            res = DG_SKIPPED;
            dg_line_diag(line, sizeof(line), dg_test_name(t), res, "why=abort");
            dg_mark("%s", line);
            results[t] = res;
            k_strncpy(texts[t], "Stopped.", sizeof(texts[t]));
            draw_item(t);
        } else {
            res = run_test(t, DG_MODE_ALL);
            stop = res == DG_SKIPPED && !dg_test_interactive(t);
        }
        dg_counts_add(&c, res);
    }
    dg_line_done(line, sizeof(line), &c);
    dg_mark("%s", line);
    dg_line_status_all(status, sizeof(status), &c);
    /* A stopped run is never OK: the tests after the stop measured nothing. */
    results[DG_ITEM_ALL] = dg_counts_result(&c, stop);
    k_snprintf(texts[DG_ITEM_ALL], sizeof(texts[DG_ITEM_ALL]), DG_TX_ALL, c.n[DG_OK],
               c.n[DG_FAIL]);
    draw_item(DG_ITEM_ALL);
}

static void run_item(unsigned item)
{
    dg.phase = DG_PHASE_TEST;
    if (item == DG_ITEM_ALL) {
        run_all();
    } else {
        int res = run_test(item, DG_MODE_ONE);

        dg_line_status_one(status, sizeof(status), item, res, texts[item]);
    }
    draw_status(1);
    dg.phase = DG_PHASE_MENU;
}

static const char *item_name(unsigned item)
{
    return item == DG_ITEM_ALL ? "all" : dg_test_name(item);
}

/*
 * An earlier action overtook the port's request: it never runs. Its
 * marked event stays in the queue and comes up stale.
 */
static void drop_request(const char *why)
{
    if (req_live != 0) {
        req_live = 0;
        dg_mark("diag_drop name=%s why=%s", item_name(req_item), why);
    }
}

static void reboot(const char *source)
{
    drop_request("reboot");
    dg_mark("reboot source=%s", source);
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
    case DG_PHASE_INIT: return "init";
    case DG_PHASE_MENU: return "menu";
    case DG_PHASE_TEST: return "test";
    default:            return "unknown";
    }
}

static int command(int argc, char *argv[], input_script_reply_t *reply, void *ctx)
{
    char line[DG_LINE_MAX];

    (void)ctx;
    if (k_strcmp(argv[0], "state") == 0 && argc == 1) {
        /*
         * request: a test the port asked for, not started yet. pending:
         * input events not taken yet. The port answers from inside
         * input_poll(), before it takes the next event, so pending=0
         * means the image handled every key sent before this command.
         */
        k_snprintf(line, sizeof(line),
                   "phase=%s trusted=%d fb=%d sel=%u test=%s request=%s pending=%u\n",
                   phase_name(dg.phase), dg.trusted, dg.fb_ok, menu.sel + 1,
                   dg.test >= 0 ? dg_test_name((unsigned)dg.test) : "none",
                   req_live == 0 ? "none" : item_name(req_item),
                   input_pending());
        reply->write(line);
        return 0;
    }
    if (k_strcmp(argv[0], "diag") == 0 && argc == 2) {
        int item = dg_test_find(argv[1]);
        input_event_t wake = { 0 };

        if (item < 0) {
            reply->reason = "unknown test";
            return -1;
        }
        if (dg.phase != DG_PHASE_MENU || req_live != 0) {
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
        req_item = (unsigned)item;
        return 0;
    }
    if (k_strcmp(argv[0], "dump") == 0 && argc == 1) {
        if (!dg.fb_ok) {
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
#ifdef DG_TEST
    if (k_strcmp(argv[0], "fault") == 0 && argc == 2) {
        uint32_t a = 0;
        const char *p = argv[1];

        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            p += 2;
        }
        for (; *p != '\0'; p++) {
            unsigned d = (*p >= '0' && *p <= '9') ? (unsigned)(*p - '0') :
                         (*p >= 'a' && *p <= 'f') ? (unsigned)(*p - 'a' + 10) : 16u;

            if (d > 15u) {
                reply->reason = "bad address";
                return -1;
            }
            a = (a << 4) | d;
        }
        dg.fault_addr = a & ~3u;
        return 0;
    }
    if (k_strcmp(argv[0], "sdwrite") == 0 && argc == 1) {
        dg.sd_probe_write = 1;
        return 0;
    }
    if (k_strcmp(argv[0], "sdmulti") == 0 && argc == 1) {
        dg.sd_fail_multi = 1;
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
 * trusted and carries them, else a fresh one. Then a clear: the tests
 * need the whole screen.
 */
static void init_screen(void)
{
    const robco_bootinfo_t *bi = dg.trusted ? bootinfo_get() : NULL;
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
        if (con == 0 && (console_cols() < DG_MIN_COLS || console_rows() < DG_MIN_ROWS)) {
            con = -1;
        }
        adopted = con == 0;
    }
    if (!adopted) {
        fb = fb_init(SCREEN_W, SCREEN_H, 32);
        con = fb == 0 ? console_init(scheme) : -1;
        if (con == 0 && (console_cols() < DG_MIN_COLS || console_rows() < DG_MIN_ROWS)) {
            con = -1;
        }
    }
    dg.fb_ok = fb == 0 && con == 0;
    if (dg.fb_ok) {
        console_cursor_show(0);
        console_clear();
    }
    dg_mark("screen fb=%d console=%d path=%s scheme=%u cols=%u rows=%u", fb, con,
            adopted ? "adopt" : "init", scheme, dg.fb_ok ? console_cols() : 0,
            dg.fb_ok ? console_rows() : 0);
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
    for (i = 0; i < DG_BUTTONS; i++) {
        if (dg_buttons[i].code != INPUT_KEY_DIAL_PUSH) {
            (void)input_gpio_add_button(dg_buttons[i].pin, dg_buttons[i].code, 0, GPIO_PULL_UP);
        }
    }
    (void)input_gpio_add_dial(DIAL_A, DIAL_B, 16, 4);
    gpio = input_gpio_start(DG_TICK_HZ, DEBOUNCE_MS);
    dg_mark("input serial=%d mini=%d script=%d gpio=%d", serial, mini, script, gpio);
}

static void entry(void)
{
    const robco_board_t *b = board_get();
    const robco_bootinfo_t *bi;
    extern const rimg_header_t robco_rimg;
    /* First, before any code writes initialised data, which the CRC covers. */
    int self = rimg_self_check();
    int mmu;

    uart_init(115200);
    k_printf("\n");
    dg_mark("boot midr=0x%08x soc=%s base=0x%08x board_rev=0x%08x mem=0x%08x entry_mode=%s "
            "version=" DG_VERSION_STR,
            (unsigned)b->midr, b->soc_name, (unsigned)b->periph_base, (unsigned)b->board_rev,
            (unsigned)b->mem_size, mode_name(robco_boot_args()->entry_mode));
    dg.trusted = bootinfo_inherited();
    dg_mark("handoff trusted=%d", dg.trusted);
    bi = dg.trusted ? bootinfo_get() : NULL;
    dg.mem_base = bi != NULL ? bi->mem_base : b->mem_base;
    dg.mem_size = bi != NULL ? bi->mem_size : b->mem_size;
    dg.mb_base = b->mem_base;
    dg.mb_size = b->mem_size;
    dg.test = -1;
    dg.i2c_rc = 1;
    init_screen();
    /* The MMU and caches: the memory test runs several times faster. */
    mmu = mmu_init();
    if (mmu == 0) {
        mmu_enable();
    }
    dg_mark("mmu rc=%d on=%d", mmu, mmu_enabled());
    irq_init();
    tick_init(DG_TICK_HZ);
    (void)tick_register(dg_timer_tick);
    irq_cpu_enable();
    init_input();
    dg_mark("selfcheck rc=%s size=%u", rimg_strerror(self), (unsigned)robco_rimg.file_size);
#ifdef DG_TEST
    dg_mark("fault_hook name=mem");
    dg_mark("write_hook name=sd");
#endif
}

void robco_main(void)
{
    input_event_t ev;
    unsigned i;
    unsigned item = 0;

    dg.phase = DG_PHASE_INIT;
    entry();
    for (i = 0; i < DG_ITEMS; i++) {
        results[i] = DG_NONE;
    }
    k_strncpy(status, dg_line_status_idle(), sizeof(status));
    dg_menu_init(&menu);
    uart_putc('\n');
    draw_menu(1);
    dg.phase = DG_PHASE_MENU;
    dg_mark("ready");
    for (;;) {
        int act = DG_ACT_NONE;

        if (input_wait(&ev, LOOP_MS)) {
            int port = mfb_port_req(&ev, req_live);

            if (port == MFB_PORT_LIVE) {
                /* The port's diag, in its turn behind the keys before it. */
                req_live = 0;
                item = req_item;
                act = DG_ACT_RUN;
            } else if (port == MFB_PORT_NONE) {
                act = dg_menu_event(&menu, &ev, &item);
                redraw_selection();
            }
        }
        if (act == DG_ACT_REBOOT) {
            reboot("key");
        }
        if (act == DG_ACT_RUN) {
            /* A key started this run before the port's request came up. */
            drop_request("run");
            run_item(item);
            input_flush();
        }
    }
}
