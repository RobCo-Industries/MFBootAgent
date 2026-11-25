/*
 * Input backends and the maintenance port. The port is the mini UART
 * script channel: in QEMU it is the second serial port. On hardware the
 * mini UART reaches no pin, see BOARD_SUPPORT.md in RobCo-Core.
 *
 * Project commands, besides the library's built-in ones (key, char,
 * text and the rest, which inject input events):
 *   state   one line: phase, selection, countdown and last error
 *   dump    the screen text, through console_dump()
 *   reboot  restarts the board through the watchdog
 *   maint   opens the maintenance screen, from the boot menu only
 *
 * The GPIO buttons use the example pins of BOARD_SUPPORT.md in
 * RobCo-Core, each to ground with the internal pull-up.
 */

#include <robco/console.h>
#include <robco/gpio.h>
#include <robco/timer.h>
#include <robco/input.h>
#include <robco/klibc.h>
#include <robco/power.h>
#include <robco/uart.h>
#include <robco/uart_mini.h>

#include "keymap.h"
#include "mfb.h"

static input_script_reply_t *dump_reply;

static void dump_char(char c)
{
    char s[2] = { c, '\0' };

    dump_reply->write(s);
}

static int command(int argc, char *argv[], input_script_reply_t *reply, void *ctx)
{
    char line[128];

    (void)ctx;
    if (k_strcmp(argv[0], "state") == 0 && argc == 1) {
        mfb_state_line(line, sizeof(line));
        reply->write(line);
        reply->write("\n");
        return 0;
    }
    if (k_strcmp(argv[0], "dump") == 0 && argc == 1) {
        if (!mfb.fb_ok) {
            reply->reason = "no screen";
            return -1;
        }
        dump_reply = reply;
        console_dump(dump_char);
        return 0;
    }
    if (k_strcmp(argv[0], "reboot") == 0 && argc == 1) {
        mfb_reboot("maint");
    }
    if (k_strcmp(argv[0], "maint") == 0 && argc == 1) {
        input_event_t wake = { 0 };

        if (mfb.maint_open || mfb.maint_req != 0) {
            reply->reason = "already open";
            return -1;
        }
        if (mfb.phase != MFB_PHASE_MENU) {
            reply->reason = "not in the menu";
            return -1;
        }
        /*
         * The request goes through the input queue, behind the keys sent
         * before it: the menu takes them first. See mfb_port_req().
         */
        wake.type = INPUT_EV_CHAR;
        wake.source = INPUT_SRC_SCRIPT;
        wake.value = ++mfb.maint_seq;
        if (input_push(&wake) != 0) {
            reply->reason = "busy";
            return -1;
        }
        mfb.maint_req = wake.value;
        return 0;
    }
    reply->reason = "unknown command";
    return -1;
}

/* The buttons, BCM pin numbers. LEFT and RIGHT are wired but do nothing. */
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

#define GPIO_DEBOUNCE_MS    20
#define REPEAT_DELAY_MS     500
#define REPEAT_RATE_MS      150
/* The guard ends after INPUT_GPIO_GUARD_MS of ticks: wait a little longer, then go on. */
#define GPIO_GUARD_WAIT_MS  (INPUT_GPIO_GUARD_MS * 3)

static int guard_checking(void *ctx)
{
    (void)ctx;
    return input_gpio_status() == INPUT_GPIO_CHECKING;
}

static uint32_t guard_now(void *ctx)
{
    (void)ctx;
    return timer_now_ms();
}

/* Sleeps until the next interrupt, the tick at the latest. Takes no event. */
static void guard_nap(void *ctx)
{
    (void)ctx;
    __asm__ volatile("wfi" ::: "memory");
}

/*
 * Starts the GPIO backend and waits for its stuck-line guard. Returns
 * input_gpio_start()'s code. With no buttons wired every line reads
 * pressed, and the backend disables itself. Buttons held when the guard
 * ends come as presses, and the wait leaves them for the menu.
 */
static int gpio_start(void)
{
    mfb_guard_io_t io = { guard_checking, guard_now, guard_nap, NULL };
    unsigned i;
    int rc = 0;

    for (i = 0; i < sizeof(buttons) / sizeof(buttons[0]) && rc == 0; i++) {
        rc = input_gpio_add_button(buttons[i].pin, buttons[i].code, 0, GPIO_PULL_UP);
    }
    if (rc == 0) {
        rc = input_gpio_start(MFB_TICK_HZ, GPIO_DEBOUNCE_MS);
    }
    if (rc == 0) {
        mfb_guard_wait(&io, GPIO_GUARD_WAIT_MS);
    }
    return rc;
}

/* Appends ",name" to the live list, or "name" to an empty one, when `on`. */
static void add_live(char *buf, unsigned size, int on, const char *name)
{
    unsigned n = (unsigned)k_strlen(buf);

    if (on) {
        k_snprintf(buf + n, size - n, "%s%s", n != 0 ? "," : "", name);
    }
}

void maint_start(void)
{
    int mini = uart_mini_init(115200);
    int serial;
    int script = INPUT_ERR_NO_UART;
    int gpio;
    int repeat;
    int status;
    char live[24];

    input_init();
    gpio = gpio_start();
    status = input_gpio_status();
    repeat = input_set_repeat(REPEAT_DELAY_MS, REPEAT_RATE_MS);
    serial = input_serial_start(INPUT_SERIAL_MAIN);
    input_script_set_handler(command, NULL);
    if (mini == 0) {
        script = input_script_start();
    }
    live[0] = '\0';
    add_live(live, sizeof(live), serial == 0, "serial");
    add_live(live, sizeof(live), status == INPUT_GPIO_ACTIVE, "gpio");
    add_live(live, sizeof(live), script == 0, "script");
    MFB_MARK("input serial=%d mini=%d script=%d gpio=%d buttons=%s repeat=%d live=%s",
             serial, mini, script, gpio, input_gpio_status_name(status), repeat,
             live[0] != '\0' ? live : "none");
}

void maint_idle(void)
{
    input_event_t ev;

    mfb.phase = MFB_PHASE_HALTED;
    MFB_MARK("idle");
    for (;;) {
        /* Console keys are read and dropped; script lines are answered. */
        (void)input_wait(&ev, 1000);
    }
}
