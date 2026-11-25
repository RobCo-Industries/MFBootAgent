/*
 * View file. Read only, behind the write guard. Offers the files of
 * the table below by name, reads the chosen one, at most MT_VIEW_MAX
 * bytes, and shows it in a pager by the rules of mt_view.h. It does not
 * parse the file.
 */

#include <stdint.h>

#include <robco/console.h>
#include <robco/fs.h>
#include <robco/klibc.h>

#include "mt.h"
#include "mt_view.h"

/*
 * The files offered by name, with their paths from BOOT_PROTOCOL.md.
 * The spec also names UOS.CFG, DEVICES.CFG and TERMINAL.CFG but gives no
 * path for them: they join this table once it does.
 */
static const struct {
    const char *name;
    const char *path;
} files[] = {
    { "BOOT.CFG", "/ROBCO/BOOT.CFG" },
};
#define FILES (sizeof(files) / sizeof(files[0]))

/* Work rows: 0 the header, 1 to PAGE the text, then the footer. */
#define PAGE        (MT_WORK_ROWS - 2u)
#define ROW_FOOT    (MT_WORK_ROWS - 1u)
#define COLS        (MT_MIN_COLS - 1u)

static uint8_t text[MT_VIEW_MAX];
static mt_view_t view;

/* The chooser. Returns the file index, or -1 when BACK cancels it. */
static int choose(void)
{
    char line[MT_LINE_MAX];
    input_event_t ev;
    mt_menu_t m;
    unsigned i;
    unsigned row = 0;

    mt_menu_init(&m, FILES);
    for (;;) {
        mt_work_line(0, "Choose a file. ENTER: view   ESC: back", 0);
        for (i = 0; i < FILES; i++) {
            k_snprintf(line, sizeof(line), "%c %u  %-14s%s", i == m.sel ? '>' : ' ', i + 1,
                       files[i].name, files[i].path);
            mt_work_line(1 + i, line, 0);
        }
        mt_mark("view_choose sel=%u name=%s", m.sel + 1, files[m.sel].name);
        for (;;) {
            if (!mt_wait_event(&ev, 50)) {
                continue;
            }
            if (mt_is_abort(&ev)) {
                return -1;
            }
            if (mt_menu_event(&m, &ev, &row) == MT_ACT_RUN) {
                return (int)row;
            }
            if (mt_menu_take(&m, 0) & MT_DIRTY_SEL) {
                break;
            }
        }
    }
}

/* Reads at most MT_VIEW_MAX bytes of path into text. Returns the count or a negative error. */
static int read_head(const char *path)
{
    static fs_file_t f;
    uint32_t got = 0;
    int rc = fs_open(&f, path, FS_READ);

    if (rc != 0) {
        return rc;
    }
    while (got < MT_VIEW_MAX) {
        int n = fs_read(&f, text + got, MT_VIEW_MAX - got);

        if (n < 0) {
            (void)fs_close(&f);
            return n;
        }
        if (n == 0) {
            break;
        }
        got += (uint32_t)n;
    }
    (void)fs_close(&f);
    return (int)got;
}

static void draw_page(const char *path, uint32_t size, unsigned first)
{
    char line[MT_LINE_MAX];
    unsigned i;

    if (size > MT_VIEW_MAX) {
        k_snprintf(line, sizeof(line), "%s: %u bytes, the first %u shown", path,
                   (unsigned)size, MT_VIEW_MAX);
    } else {
        k_snprintf(line, sizeof(line), "%s: %u bytes", path, (unsigned)size);
    }
    if (view.binary != 0) {
        size_t n = k_strlen(line);

        k_snprintf(line + n, sizeof(line) - n, ", %u bytes shown as '.'",
                   (unsigned)view.binary);
    }
    mt_work_line(0, line, 0);
    for (i = 0; i < PAGE; i++) {
        if (view.lines == 0 && i == 0) {
            mt_work_line(1, "(empty file)", 0);
        } else {
            mt_view_line(&view, first + i, line, sizeof(line));
            mt_work_line(1 + i, line, 0);
        }
    }
    if (view.lines == 0) {
        k_snprintf(line, sizeof(line), "No lines.   ESC or ENTER: close");
    } else {
        unsigned last = first + PAGE < view.lines ? first + PAGE : view.lines;

        k_snprintf(line, sizeof(line), "Lines %u-%u of %u.   UP/DOWN: page   ESC or ENTER: close",
                   first + 1, last, view.lines);
    }
    mt_work_line(ROW_FOOT, line, 0);
    mt_mark("view_page first=%u last=%u lines=%u", view.lines == 0 ? 0 : first + 1,
            first + PAGE < view.lines ? first + PAGE : view.lines, view.lines);
}

/* The pager: until ESC, Ctrl-C or ENTER. */
static void page(const char *path, uint32_t size)
{
    input_event_t ev;
    unsigned first = 0;

    draw_page(path, size, first);
    for (;;) {
        unsigned next = first;

        if (!mt_wait_event(&ev, 50)) {
            continue;
        }
        if (mt_is_abort(&ev)) {
            return;
        }
        if (ev.type != INPUT_EV_KEY || ev.value == INPUT_RELEASE) {
            continue;
        }
        if (ev.code == INPUT_KEY_SELECT && ev.value == INPUT_PRESS) {
            return;
        }
        if ((ev.code == INPUT_KEY_DOWN || ev.code == INPUT_KEY_RIGHT) &&
            first + PAGE < view.lines) {
            next = first + PAGE;
        } else if ((ev.code == INPUT_KEY_UP || ev.code == INPUT_KEY_LEFT) && first > 0) {
            next = first > PAGE ? first - PAGE : 0;
        }
        if (next != first) {
            first = next;
            draw_page(path, size, first);
        }
    }
}

void mt_tool_view(mt_run_t *r)
{
    fs_stat_t st;
    int i;
    int rc;
    int n;

    if (mt_need_card(r) != 0) {
        return;
    }
    i = choose();
    if (i < 0) {
        r->result = MT_REFUSED;
        mt_keys(r, "why=cancel");
        mt_text(r, "Cancelled.");
        mt_work_clear();
        return;
    }
    mt_keys(r, "path=%s", files[i].path);
    mt_work_clear();
    rc = fs_stat(files[i].path, &st);
    if (rc == FS_ERR_NOENT) {
        r->result = MT_ABSENT;
        mt_keys(r, "why=missing");
        mt_text(r, "No %s.", files[i].name);
        mt_work_line(0, "File not found.", 1);
        return;
    }
    if (rc == 0 && st.is_dir) {
        r->result = MT_FAIL;
        mt_keys(r, "why=isdir");
        mt_text(r, "%s is a folder.", files[i].name);
        return;
    }
    n = rc == 0 ? read_head(files[i].path) : rc;
    if (n < 0) {
        r->result = MT_FAIL;
        mt_keys(r, "why=read rc=%d", n);
        mt_text(r, "Read error %d.", n);
        mt_work_line(0, "The file does not read.", 1);
        return;
    }
    mt_view_init(&view, text, (uint32_t)n, COLS);
    page(files[i].path, st.size);
    mt_work_clear();
    r->result = MT_OK;
    mt_keys(r, "size=%u shown=%u lines=%u binary=%u truncated=%d", (unsigned)st.size,
            (unsigned)view.len, view.lines, (unsigned)view.binary, st.size > view.len);
    mt_text(r, "%s, %u lines.", files[i].name, view.lines);
}
