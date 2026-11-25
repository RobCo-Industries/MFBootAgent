/* The [maint] section of BOOT.CFG: see mt_cfg.h. */

#include <stddef.h>

#include <robco/ini.h>
#include <robco/klibc.h>

#include "mt_cfg.h"

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

static int same(const char *a, const char *b)
{
    while (*a != '\0' && lower(*a) == lower(*b)) {
        a++;
        b++;
    }
    return lower(*a) == lower(*b);
}

void mt_cfg_default(mt_cfg_t *c)
{
    c->restore_users = 0;
    c->bad = 0;
    c->unknown = 0;
    c->read_rc = 0;
    c->syntax_line = 0;
}

int mt_cfg_known(const mt_cfg_t *c)
{
    return c->read_rc == 0 && c->syntax_line == 0;
}

void mt_cfg_status(char *out, size_t size, const mt_cfg_t *c)
{
    if (c->read_rc != 0) {
        k_snprintf(out, size, "BOOT.CFG does not read: error %d. Restore users is off.",
                   c->read_rc);
    } else if (c->syntax_line != 0) {
        k_snprintf(out, size, "BOOT.CFG line %u does not parse. Restore users is off.",
                   c->syntax_line);
    } else if (size != 0) {
        out[0] = '\0';
    }
}

static int on_key(const char *section, const char *key, const char *value, unsigned line,
                  void *ctx)
{
    mt_cfg_t *c = ctx;

    (void)line;
    if (!same(section, "maint")) {
        return 0;
    }
    if (!same(key, "restore_users")) {
        c->unknown++;
        return 0;
    }
    if (same(value, "on")) {
        c->restore_users = 1;
    } else {
        /* off, or a bad value: either way the tool is off. */
        c->restore_users = 0;
        if (!same(value, "off")) {
            c->bad++;
        }
    }
    return 0;
}

int mt_cfg_parse(const char *text, size_t len, mt_cfg_t *c, unsigned *err_line)
{
    unsigned line = 0;
    int rc;

    mt_cfg_default(c);
    rc = ini_parse(text, len, on_key, c, &line);
    if (err_line != NULL) {
        *err_line = rc == 0 ? 0 : line;
    }
    if (rc != 0) {
        c->restore_users = 0;
        c->syntax_line = line != 0 ? line : 1;
        return MT_CFG_SYNTAX;
    }
    return MT_CFG_OK;
}
