#include "bootcfg.h"

#include <robco/ini.h>
#include <robco/klibc.h>

#define ENTRY_PREFIX "entry "

/* Section kinds. */
#define SEC_NONE    0   /* before any section, or an unknown one */
#define SEC_BOOT    1
#define SEC_BIOS    2   /* RETROS-BIOS reads it; MFBootAgent ignores it */
#define SEC_ENTRY   3   /* a valid [entry NAME] */
#define SEC_SKIP    4   /* an entry that is ignored: duplicate, bad name or past the eighth */

typedef struct {
    mfb_cfg_t *cfg;
    mfb_cfg_report_t *rep;
    char section[INI_MAX_SECTION + 1];
    int kind;
    int path_ok[MFB_MAX_ENTRIES];
    unsigned seen;              /* [entry NAME] sections with a valid name, in file order */
    char def[INI_MAX_VALUE + 1];
    int have_def;
    const char *text;           /* the file, scanned for section header lines */
    size_t len;
    size_t pos;                 /* start of line `line` */
    unsigned line;              /* next line to scan, from 1 */
} parse_t;

/*
 * 1 when a section header line lies before line `upto` and after the
 * lines scanned so far. ini_parse() reports keys only, so this is how a
 * repeated [entry NAME] header shows as a new section.
 */
static int crossed_header(parse_t *p, unsigned upto)
{
    int found = 0;

    while (p->line < upto && p->pos < p->len) {
        size_t i = p->pos;

        while (i < p->len && (p->text[i] == ' ' || p->text[i] == '\t')) {
            i++;
        }
        if (i < p->len && p->text[i] == '[') {
            found = 1;
        }
        while (i < p->len && p->text[i] != '\n') {
            i++;
        }
        p->pos = i < p->len ? i + 1 : i;
        p->line++;
    }
    /* Skip the key line itself. */
    if (p->line == upto && p->pos < p->len) {
        size_t i = p->pos;

        while (i < p->len && p->text[i] != '\n') {
            i++;
        }
        p->pos = i < p->len ? i + 1 : i;
        p->line++;
    }
    return found;
}

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static int same_nocase(const char *a, const char *b)
{
    while (*a != '\0' && lower(*a) == lower(*b)) {
        a++;
        b++;
    }
    return lower(*a) == lower(*b);
}

static int prefix_nocase(const char *s, const char *prefix)
{
    while (*prefix != '\0') {
        if (lower(*s) != lower(*prefix)) {
            return 0;
        }
        s++;
        prefix++;
    }
    return 1;
}

static void copy(char *dst, size_t size, const char *src)
{
    size_t n = k_strlen(src);

    if (n > size - 1) {
        n = size - 1;
    }
    k_memset(dst, 0, size);
    k_memcpy(dst, src, n);
}

int mfb_cfg_number(const char *s, uint32_t *out)
{
    uint32_t v = 0;

    if (*s == '\0') {
        return -1;
    }
    for (; *s != '\0'; s++) {
        uint32_t d;

        if (*s < '0' || *s > '9') {
            return -1;
        }
        d = (uint32_t)(*s - '0');
        v = v > (0xFFFFFFFFu - d) / 10u ? 0xFFFFFFFFu : v * 10u + d;
    }
    *out = v;
    return 0;
}

void mfb_cfg_default(mfb_cfg_t *cfg)
{
    k_memset(cfg, 0, sizeof(*cfg));
    copy(cfg->entry[0].name, sizeof(cfg->entry[0].name), MFB_DEFAULT_NAME);
    copy(cfg->entry[0].path, sizeof(cfg->entry[0].path), MFB_DEFAULT_PATH);
    cfg->count = 1;
    cfg->def = 0;
    cfg->timeout = MFB_TIMEOUT_NONE;
}

/* A path the loader can use and a marker can print: absolute, no blanks, fits boot_path. */
static int path_valid(const char *p)
{
    size_t n = k_strlen(p);
    size_t i;

    if (n < 2 || n > MFB_PATH_MAX || p[0] != '/') {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (p[i] == ' ' || p[i] == '\t') {
            return 0;
        }
    }
    return 1;
}

/* A new section: decides what its keys do. */
static void enter_section(parse_t *p, const char *section)
{
    const char *name;
    size_t n;
    unsigned i;

    copy(p->section, sizeof(p->section), section);
    if (same_nocase(section, "boot")) {
        p->kind = SEC_BOOT;
        return;
    }
    if (same_nocase(section, "bios")) {
        p->kind = SEC_BIOS;
        return;
    }
    if (same_nocase(section, "entry")) {
        /* [entry] with no name. */
        p->rep->bad++;
        p->kind = SEC_SKIP;
        return;
    }
    if (!prefix_nocase(section, ENTRY_PREFIX)) {
        p->kind = SEC_NONE;
        return;
    }
    name = section + sizeof(ENTRY_PREFIX) - 1;
    while (*name == ' ') {
        name++;
    }
    n = k_strlen(name);
    if (n == 0 || n > MFB_NAME_MAX) {
        p->rep->bad++;
        p->kind = SEC_SKIP;
        return;
    }
    for (i = 0; i < p->cfg->count; i++) {
        if (same_nocase(p->cfg->entry[i].name, name)) {
            p->rep->dup++;
            p->kind = SEC_SKIP;
            return;
        }
    }
    p->seen++;
    if (p->cfg->count == MFB_MAX_ENTRIES) {
        p->rep->ignored++;
        p->kind = SEC_SKIP;
        return;
    }
    copy(p->cfg->entry[p->cfg->count].name, sizeof(p->cfg->entry[0].name), name);
    p->path_ok[p->cfg->count] = 0;
    p->cfg->count++;
    p->kind = SEC_ENTRY;
}

static void boot_key(parse_t *p, const char *key, const char *value)
{
    uint32_t v;

    if (same_nocase(key, "default")) {
        copy(p->def, sizeof(p->def), value);
        p->have_def = 1;
    } else if (same_nocase(key, "timeout")) {
        if (mfb_cfg_number(value, &v) != 0) {
            p->rep->bad++;
            return;
        }
        if (v > MFB_TIMEOUT_MAX) {
            v = MFB_TIMEOUT_MAX;
            p->rep->clamped++;
        }
        p->cfg->timeout = (int)v;
    } else {
        p->rep->unknown++;
    }
}

static void entry_key(parse_t *p, const char *key, const char *value)
{
    unsigned i = p->cfg->count - 1;
    mfb_entry_t *e = &p->cfg->entry[i];

    if (same_nocase(key, "path")) {
        if (!path_valid(value)) {
            p->rep->bad++;
            return;
        }
        copy(e->path, sizeof(e->path), value);
        p->path_ok[i] = 1;
    } else if (same_nocase(key, "cmdline")) {
        /* ini.h caps a value at 127 bytes: every cmdline that parses fits. */
        copy(e->cmdline, sizeof(e->cmdline), value);
    } else {
        p->rep->unknown++;
    }
}

static int handler(const char *section, const char *key, const char *value, unsigned line,
                   void *ctx)
{
    parse_t *p = ctx;

    /* Always scan, so the scan stays in step with the keys. */
    if (crossed_header(p, line) || k_strcmp(section, p->section) != 0) {
        enter_section(p, section);
    }
    switch (p->kind) {
    case SEC_BOOT:
        boot_key(p, key, value);
        break;
    case SEC_ENTRY:
        entry_key(p, key, value);
        break;
    case SEC_BIOS:
    case SEC_SKIP:
        break;
    default:
        p->rep->unknown++;
        break;
    }
    return 0;
}

/* Drops the entries that got no usable path, keeping the order. */
static void drop_pathless(parse_t *p)
{
    mfb_cfg_t *cfg = p->cfg;
    unsigned in, out = 0;

    for (in = 0; in < cfg->count; in++) {
        if (!p->path_ok[in]) {
            p->rep->bad++;
            continue;
        }
        if (out != in) {
            cfg->entry[out] = cfg->entry[in];
        }
        out++;
    }
    for (in = out; in < cfg->count; in++) {
        k_memset(&cfg->entry[in], 0, sizeof(cfg->entry[in]));
    }
    cfg->count = out;
}

int mfb_cfg_parse(const char *text, size_t len, mfb_cfg_t *cfg, mfb_cfg_report_t *rep,
                  unsigned *err_line)
{
    static parse_t p;
    unsigned line = 0;
    unsigned i;
    int rc;

    k_memset(&p, 0, sizeof(p));
    k_memset(cfg, 0, sizeof(*cfg));
    k_memset(rep, 0, sizeof(*rep));
    cfg->timeout = MFB_TIMEOUT_NONE;
    p.cfg = cfg;
    p.rep = rep;
    p.text = text;
    p.len = len;
    p.line = 1;
    /* No section yet: keys before the first one are unknown. */
    p.kind = SEC_NONE;
    rc = ini_parse(text, len, handler, &p, &line);
    if (err_line != NULL) {
        *err_line = line;
    }
    if (rc != 0) {
        mfb_cfg_default(cfg);
        return MFB_CFG_SYNTAX;
    }
    drop_pathless(&p);
    if (cfg->count == 0) {
        mfb_cfg_default(cfg);
        return MFB_CFG_EMPTY;
    }
    cfg->def = 0;
    if (p.have_def) {
        for (i = 0; i < cfg->count; i++) {
            if (same_nocase(cfg->entry[i].name, p.def)) {
                cfg->def = i;
                rep->default_found = 1;
                break;
            }
        }
    }
    return MFB_CFG_OK;
}
