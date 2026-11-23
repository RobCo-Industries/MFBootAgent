#include "menulines.h"

#include <robco/klibc.h>

/* Lore: original text in the style of the games' boot agent screen. */
static const char *const lore[MFB_LORE_LINES] = {
    "COPYRIGHT 2075-2077 ROBCO INDUSTRIES",
    "MULTI-FUNCTION BOOT AGENT. SELECT AN OPERATING IMAGE.",
};

void mfb_line_title(char *buf, size_t size, const char *version)
{
    /* Lore name, real version. */
    k_snprintf(buf, size, "ROBCO MFBOOT AGENT V%s", version);
}

const char *mfb_line_lore(unsigned i)
{
    return i < MFB_LORE_LINES ? lore[i] : NULL;
}

const char *mfb_line_rule(void)
{
    return "========================================";
}

void mfb_line_entry(char *buf, size_t size, unsigned index, const mfb_entry_t *e, int is_default,
                    int selected)
{
    k_snprintf(buf, size, "%c %u  %-15s %-8s %s", selected ? '>' : ' ', index + 1, e->name,
               is_default ? "DEFAULT" : "", e->path);
}

const char *mfb_line_help(void)
{
    return "UP/DOWN or 1-8: select   ENTER: boot   R: reboot";
}

void mfb_line_status(char *buf, size_t size, int counting, int stopped, unsigned left,
                     const char *name)
{
    if (counting) {
        k_snprintf(buf, size, "Booting %s in %u s. Press any key to stop.", name, left);
    } else if (stopped) {
        k_snprintf(buf, size, "Countdown stopped. Press ENTER to boot.");
    } else if (size > 0) {
        buf[0] = '\0';
    }
}

void mfb_line_loading(char *buf, size_t size, const mfb_entry_t *e)
{
    k_snprintf(buf, size, "Loading %s from %s", e->name, e->path);
}

int mfb_menu_grid(unsigned cols, unsigned rows)
{
    if (cols < MFB_MENU_MIN_COLS) {
        return MFB_GRID_COLS;
    }
    if (rows < MFB_MENU_MIN_ROWS) {
        return MFB_GRID_ROWS;
    }
    return MFB_GRID_OK;
}

const char *mfb_grid_name(int grid)
{
    switch (grid) {
    case MFB_GRID_OK:   return "ok";
    case MFB_GRID_COLS: return "cols";
    default:            return "rows";
    }
}

void mfb_menu_place(unsigned rows, unsigned menu_rows, unsigned cx, unsigned cy, mfb_place_t *p)
{
    unsigned row = cy;
    unsigned last_top;

    p->top = 0;
    p->scroll = 0;
    if (rows == 0 || menu_rows > rows) {
        return;
    }
    last_top = rows - menu_rows;
    if (row > rows - 1) {
        row = rows - 1;
    }
    /* A partial line ends first, then one blank row follows any earlier text. */
    if (cx != 0) {
        row++;
    }
    if (row != 0) {
        row++;
    }
    /* row is at most rows + 1 here. */
    if (row > last_top) {
        p->scroll = row - last_top;
        row = last_top;
    }
    p->top = row;
}
