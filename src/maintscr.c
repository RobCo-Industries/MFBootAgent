#include "maintscr.h"

#include <stdarg.h>

#include <robco/klibc.h>

#include "keymap.h"
#include "maintreq.h"

static const char *const items[MFB_MS_ITEMS] = {
    "Verify entries",
    "System",
    "Configuration",
    "Start the maintenance image",
    "Reboot",
    "Back to the boot menu",
};

static const char *const names[MFB_MS_ITEMS] = {
    "verify", "system", "config", "start", "reboot", "back",
};

void mfb_ms_init(mfb_ms_t *s, int skip_back)
{
    s->sel = 0;
    s->drawn_sel = 0;
    s->dirty = 0;
    s->skip_back = skip_back != 0;
}

static void move(mfb_ms_t *s, unsigned sel)
{
    if (sel != s->sel) {
        s->sel = sel;
        s->dirty = 1;
    }
}

int mfb_ms_key(mfb_ms_t *s, int key, unsigned *item)
{
    switch (key) {
    case MFB_K_UP:
        if (s->sel > 0) {
            move(s, s->sel - 1);
        }
        return MFB_MS_ACT_NONE;
    case MFB_K_DOWN:
        if (s->sel + 1 < MFB_MS_ITEMS) {
            move(s, s->sel + 1);
        }
        return MFB_MS_ACT_NONE;
    case MFB_K_SELECT:
        if (s->sel == MFB_MS_BACK) {
            return MFB_MS_ACT_CLOSE;
        }
        *item = s->sel;
        return MFB_MS_ACT_RUN;
    case MFB_K_BACK:
        if (s->skip_back) {
            s->skip_back = 0;
            return MFB_MS_ACT_NONE;
        }
        return MFB_MS_ACT_CLOSE;
    case MFB_K_QUIT:
        return MFB_MS_ACT_CLOSE;
    default:
        return MFB_MS_ACT_NONE;
    }
}

int mfb_ms_take(mfb_ms_t *s, unsigned *old)
{
    int d = s->dirty;

    s->dirty = 0;
    if (d) {
        *old = s->drawn_sel;
        s->drawn_sel = s->sel;
    }
    return d;
}

const char *mfb_ms_name(unsigned item)
{
    return item < MFB_MS_ITEMS ? names[item] : "unknown";
}

void mfb_ms_title(char *buf, size_t size, const char *version)
{
    k_snprintf(buf, size, "MFBOOT AGENT V%s MAINTENANCE", version);
}

const char *mfb_ms_help(void)
{
    return "UP/DOWN: move   ENTER: run   ESC or Q: back to the boot menu";
}

void mfb_ms_item(char *buf, size_t size, unsigned item, int selected)
{
    if (item >= MFB_MS_ITEMS) {
        if (size > 0) {
            buf[0] = '\0';
        }
        return;
    }
    k_snprintf(buf, size, "%c %s", selected ? '>' : ' ', items[item]);
}

void mfb_ms_verify_head(char *buf, size_t size)
{
    k_snprintf(buf, size, "#  %-15s %-11s %s", "ENTRY", "RESULT", "PATH");
}

void mfb_ms_verify_row(char *buf, size_t size, unsigned index, const mfb_entry_t *e, int err)
{
    char result[16];

    if (err == 0) {
        k_snprintf(result, sizeof(result), "OK");
    } else {
        mfb_err_short(result, sizeof(result), err);
    }
    k_snprintf(buf, size, "%u  %-15s %-11s %s", index + 1, e->name, result, e->path);
}

void mfb_ms_verify_sum(char *buf, size_t size, unsigned good, unsigned failed)
{
    k_snprintf(buf, size, "%u %s checked: %u good, %u failed.", good + failed,
               good + failed == 1 ? "entry" : "entries", good, failed);
}

static unsigned add(mfb_ms_page_t page, unsigned n, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static unsigned add(mfb_ms_page_t page, unsigned n, const char *fmt, ...)
{
    va_list ap;

    if (n >= MFB_MS_OUT_ROWS) {
        return n;
    }
    va_start(ap, fmt);
    k_vsnprintf(page[n], MFB_LINE_MAX, fmt, ap);
    va_end(ap);
    return n + 1;
}

static const char *screen_name(int screen)
{
    switch (screen) {
    case MFB_MS_SCREEN_ADOPT: return "adopted from the previous stage";
    case MFB_MS_SCREEN_FRESH: return "started by this stage";
    default:                  return "none, serial only";
    }
}

unsigned mfb_ms_system(mfb_ms_page_t page, const mfb_ms_sys_t *sys)
{
    unsigned n = 0;

    n = add(page, n, "SYSTEM");
    n = add(page, n, "Board:        %s, %s", sys->board, sys->soc);
    n = add(page, n, "Memory:       %u MiB", (unsigned)(sys->mem_bytes >> 20));
    if (sys->mounted) {
        n = add(page, n, "Boot volume:  SD card, block %u, %u blocks", (unsigned)sys->vol_lba,
                (unsigned)sys->vol_blocks);
    } else {
        n = add(page, n, "Boot volume:  not mounted, step %s failed, code %d", sys->fail_what,
                sys->fail_rc);
        if (sys->have_good) {
            n = add(page, n, "Last good:    SD card, block %u, %u blocks", (unsigned)sys->vol_lba,
                    (unsigned)sys->vol_blocks);
        } else {
            n = add(page, n, "Last good:    none since power-on");
        }
    }
    if (sys->trusted) {
        n = add(page, n, "Boot info:    trusted, from the previous stage");
        n = add(page, n, "Boot flags:   0x%08x received", (unsigned)sys->flags_in);
    } else {
        n = add(page, n, "Boot info:    none trusted, this stage cold-booted");
        n = add(page, n, "Boot flags:   none received");
    }
    n = add(page, n, "Version:      %s", sys->version);
    n = add(page, n, "RobCo-Core:   %s", sys->core_pin);
    n = add(page, n, "Screen:       %s", screen_name(sys->screen));
    return n;
}

static const char *timeout_text(char *buf, size_t size, int timeout)
{
    if (timeout == MFB_TIMEOUT_NONE) {
        return "none, the menu waits for a key";
    }
    k_snprintf(buf, size, "%d s", timeout);
    return buf;
}

unsigned mfb_ms_config(mfb_ms_page_t page, const mfb_ms_cfg_t *c)
{
    char t[16];
    unsigned n = 0;

    n = add(page, n, "CONFIGURATION");
    switch (c->result) {
    case MFB_MS_CFG_OK:
        n = add(page, n, "File:         %s, in use", c->path);
        break;
    case MFB_MS_CFG_MISSING:
        n = add(page, n, "File:         %s missing, default menu", c->path);
        break;
    case MFB_MS_CFG_READ:
        n = add(page, n, "File:         %s does not read, default menu", c->path);
        break;
    case MFB_MS_CFG_SYNTAX:
        n = add(page, n, "File:         %s, line %u does not parse, default menu", c->path,
                c->line);
        break;
    default:
        n = add(page, n, "File:         %s has no usable entry, default menu", c->path);
        break;
    }
    n = add(page, n, "Default:      %s", c->def);
    n = add(page, n, "Timeout:      %s", timeout_text(t, sizeof(t), c->timeout));
    n = add(page, n, "Entries:      %u", c->entries);
    n = add(page, n, "Ignored:      %u", c->ignored);
    n = add(page, n, "Duplicate:    %u", c->dup);
    n = add(page, n, "Bad:          %u", c->bad);
    n = add(page, n, "Clamped:      %u", c->clamped);
    return n;
}

unsigned mfb_ms_request(mfb_ms_page_t page, int result, int rc, const char *line)
{
    unsigned n = 0;

    n = add(page, n, "MAINTENANCE REQUESTED");
    if (line != NULL && line[0] != '\0') {
        n = add(page, n, "Request:      %s", line);
    }
    if (result == MFB_REQ_CLEAR_FAILED) {
        n = add(page, n, "NOTICE: %s could not be deleted, code %d.", MFB_REQ_PATH, rc);
        n = add(page, n, "The request comes back at each start until the file is removed.");
    }
    return n;
}
