#include "bootcfg.h"

#include <robco/ini.h>
#include <robco/klibc.h>

/* Section kinds. */
#define SEC_NONE    0   /* before any section, or an unknown one */
#define SEC_BOOT    1
#define SEC_BIOS    2   /* [bios] or [maint]: another program reads it; MFBootAgent skips it */
#define SEC_ENTRY   3   /* an [entry ...] header with a good, new name */
#define SEC_SKIP    4   /* an [entry ...] header already counted as bad or dup */

/*
 * Every header line is at least "[entry a]" and a line end: a file of
 * MFB_CFG_FILE_MAX bytes holds at most this many entry headers.
 */
#define MAX_HEADERS (MFB_CFG_FILE_MAX / 10 + 1)

typedef struct {
    mfb_cfg_t *cfg;
    mfb_cfg_report_t *rep;
    int kind;
    /* The open [entry NAME] section. Its keys take their last value. */
    char name[MFB_NAME_MAX + 1];
    char path[INI_MAX_VALUE + 1];
    char cmdline[INI_MAX_VALUE + 1];
    int have_path;
    unsigned unknown;
    /* Every good name an entry header used, slots or not. */
    char names[MAX_HEADERS][MFB_NAME_MAX + 1];
    unsigned names_n;
    /* [boot], one section however often it appears. */
    char def[INI_MAX_VALUE + 1];
    int have_def;
    char timeout[INI_MAX_VALUE + 1];
    int have_timeout;
} parse_t;

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

/* The boot partition name rule, upper-cased into `out`. Returns 1 when `p` follows it. */
static int path_upper(const char *p, char *out)
{
    size_t n = k_strlen(p);
    unsigned comps = 0;
    size_t i = 0;

    if (n < 2 || n > MFB_PATH_MAX || p[0] != '/') {
        return 0;
    }
    while (i < n) {
        unsigned base = 0, ext = 0;
        int dot = 0;

        out[i] = '/';
        i++;
        if (++comps > MFB_PATH_COMPONENTS) {
            return 0;
        }
        for (; i < n && p[i] != '/'; i++) {
            char c = p[i];

            if (c == '.') {
                if (dot) {
                    return 0;
                }
                dot = 1;
            } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '~') {
                if (dot) {
                    ext++;
                } else {
                    base++;
                }
            } else {
                return 0;
            }
            out[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
        }
        /* An empty component, a trailing '/', "." and ".." all fail here. */
        if (base < 1 || base > 8 || (dot && (ext < 1 || ext > 3))) {
            return 0;
        }
    }
    out[n] = '\0';
    return 1;
}

/* Printable ASCII only: 0x20 to 0x7E. */
static int cmdline_ok(const char *s)
{
    for (; *s != '\0'; s++) {
        if ((unsigned char)*s < 0x20 || (unsigned char)*s > 0x7e) {
            return 0;
        }
    }
    return 1;
}

static int name_ok(const char *s)
{
    size_t n = k_strlen(s);

    if (n < 1 || n > MFB_NAME_MAX) {
        return 0;
    }
    for (; *s != '\0'; s++) {
        char c = *s;

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-')) {
            return 0;
        }
    }
    return 1;
}

/* The open entry section ends: steps 3 to 5 of the spec's order. */
static void close_entry(parse_t *p)
{
    mfb_cfg_t *cfg = p->cfg;
    mfb_entry_t *e;
    char up[MFB_PATH_MAX + 1];

    if (p->kind != SEC_ENTRY) {
        return;
    }
    p->kind = SEC_NONE;
    if (!p->have_path) {
        p->rep->bad++;
        p->rep->nopath++;
        return;
    }
    if (!path_upper(p->path, up) || !cmdline_ok(p->cmdline)) {
        p->rep->bad++;
        return;
    }
    if (cfg->count == MFB_MAX_ENTRIES) {
        p->rep->ignored++;
        return;
    }
    e = &cfg->entry[cfg->count++];
    copy(e->name, sizeof(e->name), p->name);
    copy(e->path, sizeof(e->path), up);
    /* ini.h caps a value at 127 bytes: every cmdline that parses fits. */
    copy(e->cmdline, sizeof(e->cmdline), p->cmdline);
    p->rep->unknown += p->unknown;
}

/* A new header: steps 1 and 2 of the spec's order for an entry header. */
static int on_section(const char *section, unsigned line, void *ctx)
{
    parse_t *p = ctx;
    const char *name;
    unsigned i;

    (void)line;
    close_entry(p);
    if (same_nocase(section, "boot")) {
        p->kind = SEC_BOOT;
        return 0;
    }
    if (same_nocase(section, "bios") || same_nocase(section, "maint")) {
        p->kind = SEC_BIOS;
        return 0;
    }
    if (!prefix_nocase(section, "entry") ||
        (section[5] != '\0' && section[5] != ' ' && section[5] != '\t')) {
        p->kind = SEC_NONE;
        return 0;
    }
    /* ini.h trims the header, so "[entry ]" arrives as "entry". */
    name = section + 5;
    while (*name == ' ' || *name == '\t') {
        name++;
    }
    p->kind = SEC_SKIP;
    if (!name_ok(name)) {
        p->rep->bad++;
        return 0;
    }
    for (i = 0; i < p->names_n; i++) {
        if (same_nocase(p->names[i], name)) {
            p->rep->dup++;
            return 0;
        }
    }
    if (p->names_n < MAX_HEADERS) {
        copy(p->names[p->names_n++], sizeof(p->names[0]), name);
    }
    p->kind = SEC_ENTRY;
    copy(p->name, sizeof(p->name), name);
    p->path[0] = '\0';
    p->cmdline[0] = '\0';
    p->have_path = 0;
    p->unknown = 0;
    return 0;
}

static int on_key(const char *section, const char *key, const char *value, unsigned line,
                  void *ctx)
{
    parse_t *p = ctx;

    (void)section;
    (void)line;
    switch (p->kind) {
    case SEC_BOOT:
        if (same_nocase(key, "default")) {
            copy(p->def, sizeof(p->def), value);
            p->have_def = 1;
        } else if (same_nocase(key, "timeout")) {
            copy(p->timeout, sizeof(p->timeout), value);
            p->have_timeout = 1;
        } else {
            p->rep->unknown++;
        }
        break;
    case SEC_ENTRY:
        if (same_nocase(key, "path")) {
            copy(p->path, sizeof(p->path), value);
            p->have_path = 1;
        } else if (same_nocase(key, "cmdline")) {
            copy(p->cmdline, sizeof(p->cmdline), value);
        } else {
            p->unknown++;
        }
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

/* The [boot] values, read once the whole file is known: the last value of a key counts. */
static void apply_boot(parse_t *p)
{
    mfb_cfg_t *cfg = p->cfg;
    uint32_t v;
    unsigned i;

    if (p->have_timeout) {
        if (mfb_cfg_number(p->timeout, &v) != 0) {
            p->rep->bad++;
        } else {
            if (v > MFB_TIMEOUT_MAX) {
                v = MFB_TIMEOUT_MAX;
                p->rep->clamped++;
            }
            cfg->timeout = (int)v;
        }
    }
    cfg->def = 0;
    if (p->have_def) {
        for (i = 0; i < cfg->count; i++) {
            if (same_nocase(cfg->entry[i].name, p->def)) {
                cfg->def = i;
                p->rep->default_found = 1;
                break;
            }
        }
    }
}

int mfb_cfg_parse(const char *text, size_t len, mfb_cfg_t *cfg, mfb_cfg_report_t *rep,
                  unsigned *err_line)
{
    static parse_t p;
    unsigned line = 0;
    int rc;

    k_memset(&p, 0, sizeof(p));
    k_memset(cfg, 0, sizeof(*cfg));
    k_memset(rep, 0, sizeof(*rep));
    /* First pass: the whole file must parse before anything counts. */
    p.cfg = cfg;
    p.rep = rep;
    rc = ini_parse_sections(text, len, NULL, on_section, &p, &line);
    k_memset(rep, 0, sizeof(*rep));
    if (err_line != NULL) {
        *err_line = line;
    }
    if (rc != 0) {
        mfb_cfg_default(cfg);
        return MFB_CFG_SYNTAX;
    }
    /* Second pass: the file parses, so act on each section and key. */
    k_memset(&p, 0, sizeof(p));
    cfg->timeout = MFB_TIMEOUT_NONE;
    p.cfg = cfg;
    p.rep = rep;
    p.kind = SEC_NONE;  /* keys before the first section are unknown */
    (void)ini_parse_sections(text, len, on_key, on_section, &p, NULL);
    close_entry(&p);
    apply_boot(&p);
    if (cfg->count == 0) {
        mfb_cfg_default(cfg);
        return MFB_CFG_EMPTY;
    }
    return MFB_CFG_OK;
}
