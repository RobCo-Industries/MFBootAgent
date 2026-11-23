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
