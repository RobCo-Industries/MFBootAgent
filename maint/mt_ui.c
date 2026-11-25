/* The menu, names, screen lines and the answer rule. Pure: see mt_ui.h. */

#include <stddef.h>

#include <robco/klibc.h>

#include "mt_ui.h"

static const char *const names[MT_TOOLS] = {
    "check_card", "check_images", "view_file", "set_clock", "restore_users", "restart",
};

static const char *const titles[MT_TOOLS] = {
    "CHECK CARD", "CHECK IMAGES", "VIEW FILE", "SET CLOCK", "RESTORE USERS", "RESTART",
};

static const char *const words[MT_TOOLS] = {
    "Check card", "Check images", "View file", "Set clock", "Restore users", "Restart",
};

/* At most 30 characters each. */
static const char *const descs[MT_TOOLS] = {
    "Read every file of the card",
    "Validate each image in /ROBCO",
    "Show a configuration file",
    "Set the clock chip",
    "Copy the default user file",
    "Restart the terminal",
};

const char *mt_result_name(int r)
{
    switch (r) {
    case MT_OK:      return "ok";
    case MT_FAIL:    return "fail";
    case MT_ABSENT:  return "absent";
    case MT_OFF:     return "off";
    case MT_REFUSED: return "refused";
    default:         return "none";
    }
}

const char *mt_result_word(int r)
{
    switch (r) {
    case MT_OK:      return "OK";
    case MT_FAIL:    return "FAILED";
    case MT_ABSENT:  return "ABSENT";
    case MT_OFF:     return "OFF";
    case MT_REFUSED: return "REFUSED";
    default:         return "";
    }
}

const char *mt_tool_name(unsigned tool)
{
    return tool < MT_TOOLS ? names[tool] : NULL;
}

int mt_tool_find(const char *name)
{
    unsigned i;

    if (name == NULL) {
        return -1;
    }
    for (i = 0; i < MT_TOOLS; i++) {
        if (k_strcmp(name, names[i]) == 0) {
            return (int)i;
        }
    }
    return -1;
}

int mt_tool_readonly(unsigned tool)
{
    return tool == MT_T_CARD || tool == MT_T_IMAGES || tool == MT_T_VIEW;
}

void mt_menu_init(mt_menu_t *m, unsigned rows)
{
    m->sel = 0;
    m->rows = rows == 0 ? 1 : rows > 9 ? 9 : rows;
    m->old = 0;
    m->dirty = 0;
}

static void select_row(mt_menu_t *m, unsigned row)
{
    if (row == m->sel) {
        return;
    }
    if ((m->dirty & MT_DIRTY_SEL) == 0) {
        m->old = m->sel;
    }
    m->sel = row;
    m->dirty |= MT_DIRTY_SEL;
}

int mt_menu_event(mt_menu_t *m, const input_event_t *ev, unsigned *row)
{
    if (m == NULL || ev == NULL) {
        return MT_ACT_NONE;
    }
    if (ev->type == INPUT_EV_KEY) {
        if (ev->value == INPUT_RELEASE) {
            return MT_ACT_NONE;
        }
        switch (ev->code) {
        case INPUT_KEY_UP:
            if (m->sel > 0) {
                select_row(m, m->sel - 1u);
            }
            return MT_ACT_NONE;
        case INPUT_KEY_DOWN:
            if (m->sel + 1u < m->rows) {
                select_row(m, m->sel + 1u);
            }
            return MT_ACT_NONE;
        case INPUT_KEY_SELECT:
            /* A held SELECT must not start the row again and again. */
            if (ev->value != INPUT_PRESS) {
                return MT_ACT_NONE;
            }
            if (row != NULL) {
                *row = m->sel;
            }
            return MT_ACT_RUN;
        default:
            return MT_ACT_NONE;
        }
    }
    if (ev->type == INPUT_EV_CHAR && ev->code >= '1' && ev->code < '1' + m->rows) {
        select_row(m, ev->code - '1');
        if (row != NULL) {
            *row = m->sel;
        }
        return MT_ACT_RUN;
    }
    return MT_ACT_NONE;
}

unsigned mt_menu_take(mt_menu_t *m, unsigned *old)
{
    unsigned d = m->dirty;

    if (old != NULL) {
        *old = m->old;
    }
    m->dirty = 0;
    m->old = m->sel;
    return d;
}

int mt_answer(const input_event_t *ev)
{
    if (ev == NULL) {
        return MT_ANS_NONE;
    }
    if (ev->type == INPUT_EV_KEY) {
        if (ev->value != INPUT_PRESS) {
            return MT_ANS_NONE;
        }
        return ev->code == INPUT_KEY_SELECT ? MT_ANS_YES : MT_ANS_NO;
    }
    if (ev->type == INPUT_EV_CHAR) {
        return ev->code == 'y' || ev->code == 'Y' ? MT_ANS_YES : MT_ANS_NO;
    }
    if (ev->type == INPUT_EV_DIAL) {
        return MT_ANS_NO;
    }
    return MT_ANS_NONE;
}

int mt_is_abort(const input_event_t *ev)
{
    if (ev == NULL) {
        return 0;
    }
    if (ev->type == INPUT_EV_KEY) {
        return ev->code == INPUT_KEY_BACK && ev->value == INPUT_PRESS;
    }
    return ev->type == INPUT_EV_CHAR && ev->code == 0x03;
}

void mt_line_title(char *out, size_t size, const char *version)
{
    k_snprintf(out, size, "TERMINAL MAINTENANCE V%s", version);
}

const char *mt_line_sub(void)
{
    return "Card, clock and user file tools. Each result also goes to the serial port.";
}

const char *mt_line_rule(void)
{
    return "========================================";
}

const char *mt_line_help(void)
{
    return "UP/DOWN, 1-6: select   ENTER: start   ESC: stop or cancel";
}

const char *mt_line_idle(void)
{
    return "Choose a tool.";
}

void mt_line_item(char *out, size_t size, unsigned tool, int selected, int result,
                  const char *text)
{
    if (tool >= MT_TOOLS) {
        k_snprintf(out, size, "%s", "");
        return;
    }
    if (result == MT_NONE || result < 0 || result > MT_NONE) {
        k_snprintf(out, size, "%c %u  %-14s%s", selected ? '>' : ' ', tool + 1u, titles[tool],
                   descs[tool]);
        return;
    }
    k_snprintf(out, size, "%c %u  %-14s%-31s%-9s%s", selected ? '>' : ' ', tool + 1u,
               titles[tool], descs[tool], mt_result_word(result), text != NULL ? text : "");
}

void mt_line_status(char *out, size_t size, unsigned tool, int result, const char *text)
{
    if (tool >= MT_TOOLS) {
        k_snprintf(out, size, "%s", "");
        return;
    }
    k_snprintf(out, size, "%s: %s.%s%s", words[tool], mt_result_word(result),
               text != NULL && text[0] != '\0' ? " " : "", text != NULL ? text : "");
}

void mt_line_running(char *out, size_t size, unsigned tool)
{
    k_snprintf(out, size, "%s: running. ESC stops or cancels it.",
               tool < MT_TOOLS ? words[tool] : "Tool");
}

void mt_line_tool(char *out, size_t size, unsigned tool, int result, const char *keys)
{
    k_snprintf(out, size, "tool name=%s result=%s%s%s", tool < MT_TOOLS ? names[tool] : "none",
               mt_result_name(result), keys != NULL && keys[0] != '\0' ? " " : "",
               keys != NULL ? keys : "");
}
