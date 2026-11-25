/*
 * Marker values that carry text from the card: each marker is built with
 * a hostile name, then split and decoded again as a reader does. The
 * encoder is dg_mval(), shared by MAINT.SYS and DIAG.SYS.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <robco/fs.h>
#include <robco/rimg.h>

#include "dg_fmt.h"
#include "dg_sd.h"
#include "mt_walk.h"
#include "rc_test.h"

/* A reader: splits at spaces, each key at its first '=', decodes %XX. */
typedef struct {
    char key[32];
    char value[600];
    int cut;
} kv_t;

static int hexval(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* Splits keys, returns the count or -1 when a token breaks the grammar. */
static int split(const char *line, kv_t *out, int max)
{
    int n = 0;

    while (*line != '\0') {
        const char *end = strchr(line, ' ');
        const char *eq;
        size_t len = end != NULL ? (size_t)(end - line) : strlen(line);
        size_t i;
        size_t o = 0;

        eq = memchr(line, '=', len);
        if (eq == NULL || eq == line || eq + 1 == line + len || n == max) {
            return -1;
        }
        memcpy(out[n].key, line, (size_t)(eq - line));
        out[n].key[eq - line] = '\0';
        out[n].cut = 0;
        for (i = (size_t)(eq - line) + 1; i < len; i++) {
            unsigned char c = (unsigned char)line[i];

            if (c < 0x21 || c > 0x7e) {
                return -1;
            }
            if (c == '%') {
                int h;
                int l;

                if (i + 1 == len) {
                    out[n].cut = 1;
                    break;
                }
                if (i + 2 >= len + 0 || (h = hexval(line[i + 1])) < 0 ||
                    (l = hexval(line[i + 2])) < 0) {
                    return -1;
                }
                out[n].value[o++] = (char)(h * 16 + l);
                i += 2;
            } else {
                out[n].value[o++] = (char)c;
            }
        }
        out[n].value[o] = '\0';
        n++;
        line += len;
        if (*line == ' ') {
            line++;
        }
    }
    return n;
}

static const char *find(const kv_t *kv, int n, const char *key)
{
    int i;

    for (i = 0; i < n; i++) {
        if (strcmp(kv[i].key, key) == 0) {
            return kv[i].value;
        }
    }
    return NULL;
}

static char long_name[FS_NAME_MAX + 1];

static void add_v(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    dg_keys_add(buf, size, fmt, ap);
    va_end(ap);
}

/* Appends keys as a tool does, through dg_keys_add(). */
static void add(char *buf, size_t size, const char *keys)
{
    add_v(buf, size, "%s", keys);
}

static const char *hostile[] = {
    "My Image.SYS",
    "a=b c.SYS",
    "\x01\x1b[2J\r\n.SYS",
    "\xc3\xa9t\xc3\xa9.SYS",
    "100%.SYS",
    "%41.SYS",
    "TAB\there.IMG",
};

TEST(mval_rules)
{
    char out[DG_MVAL_SIZE];
    char small[8];

    CHECK_STR(dg_mval(out, sizeof(out), "/ROBCO/UOS.SYS"), "/ROBCO/UOS.SYS");
    CHECK_STR(dg_mval(out, sizeof(out), "My Image.SYS"), "My%20Image.SYS");
    CHECK_STR(dg_mval(out, sizeof(out), "a=b%"), "a%3Db%25");
    CHECK_STR(dg_mval(out, sizeof(out), "\x01\x7f\xff"), "%01%7F%FF");
    CHECK_STR(dg_mval(out, sizeof(out), ""), "%00");
    CHECK_STR(dg_mval(out, sizeof(out), NULL), "%00");
    /* Cut after a whole escape, with a lone '%' at the end. */
    CHECK_STR(dg_mval(small, sizeof(small), "abcdefgh"), "abcdef%");
    CHECK_STR(dg_mval(small, sizeof(small), "abcdefg"), "abcdefg");
    CHECK_STR(dg_mval(small, sizeof(small), "ab cdef"), "ab%20c%");
    CHECK_STR(dg_mval(small, sizeof(small), "abcd ef"), "abcd%");
    CHECK_STR(dg_mval(small, sizeof(small), "abcd e"), "abcd%");
    CHECK_STR(dg_mval(small, sizeof(small), "abcd "), "abcd%20");
}

TEST(image_markers_parse_back)
{
    char raw[DG_TEXT_RAW_SIZE];
    char line[600];
    char want[300];
    kv_t kv[8];
    dg_sd_entry_t e;
    dg_sd_scan_t s;
    unsigned i;
    int n;

    for (i = 0; i < sizeof(hostile) / sizeof(hostile[0]); i++) {
        e.name = hostile[i];
        e.size = 4096;
        e.is_dir = 0;
        snprintf(want, sizeof(want), "/ROBCO/%s", hostile[i]);
        dg_sd_image_keys(line, sizeof(line), "/ROBCO", &e, DG_SDF_SHORT);
        n = split(line, kv, 8);
        CHECK_EQ(n, 3);
        CHECK_STR(find(kv, n, "path"), want);
        CHECK_STR(find(kv, n, "size"), "4096");
        CHECK_STR(find(kv, n, "rc"), "short");
        memset(&s, 0, sizeof(s));
        memcpy(s.bad_name, hostile[i], strlen(hostile[i]) + 1);
        s.first_bad = DG_SDF_EMPTY;
        dg_sd_bad_keys(line, sizeof(line), &s);
        dg_keys_text(line, sizeof(line), "bad_path", dg_sd_path(raw, "/ROBCO", s.bad_name));
        n = split(line, kv, 8);
        CHECK_EQ(n, 2);
        CHECK_STR(find(kv, n, "bad_path"), want);
        CHECK_STR(find(kv, n, "bad_rc"), "empty");
    }
}

TEST(a_long_name_is_cut_and_marked)
{
    char line[600];
    kv_t kv[8];
    dg_sd_entry_t e;
    int n;

    memset(long_name, 'N', FS_NAME_MAX);
    long_name[3] = ' ';
    long_name[FS_NAME_MAX] = '\0';
    e.name = long_name;
    e.size = 1;
    e.is_dir = 0;
    dg_sd_image_keys(line, sizeof(line), "/ROBCO", &e, DG_SDF_SHORT);
    n = split(line, kv, 8);
    CHECK_EQ(n, 3);
    CHECK_STR(kv[2].key, "path");
    CHECK(kv[2].cut);
    CHECK_EQ(strncmp(find(kv, n, "path"), "/ROBCO/NNN NNN", 14), 0);
    CHECK(strlen(line) < DG_MVAL_SIZE + 32);
}

TEST(card_markers_parse_back)
{
    char line[600];
    char path[300];
    kv_t kv[8];
    mt_walk_t w;
    unsigned i;
    int n;

    for (i = 0; i < sizeof(hostile) / sizeof(hostile[0]); i++) {
        snprintf(path, sizeof(path), "/UOS/%s", hostile[i]);
        mt_card_error_keys(line, sizeof(line), path, MT_WE_FILE, FS_ERR_IO);
        n = split(line, kv, 8);
        CHECK_EQ(n, 3);
        CHECK_STR(find(kv, n, "path"), path);
        CHECK_STR(find(kv, n, "what"), "file");
        CHECK_STR(find(kv, n, "rc"), "-1");
        memset(&w, 0, sizeof(w));
        memcpy(w.first_path, path, strlen(path) + 1);
        w.first_what = MT_WE_OPENDIR;
        mt_card_first_keys(line, sizeof(line), &w);
        dg_keys_text(line, sizeof(line), "first_path", w.first_path);
        n = split(line, kv, 8);
        CHECK_EQ(n, 2);
        CHECK_STR(find(kv, n, "first_path"), path);
        CHECK_STR(find(kv, n, "first_what"), "opendir");
    }
}

/* ---- The worst case: a 255-byte name of bytes that all need escaping ---- */

static int cut_of(const kv_t *kv, int n, const char *key)
{
    int i;

    for (i = 0; i < n; i++) {
        if (strcmp(kv[i].key, key) == 0) {
            return kv[i].cut;
        }
    }
    return -1;
}

static const char *worst_name(void)
{
    memset(long_name, ' ', FS_NAME_MAX);
    long_name[FS_NAME_MAX] = '\0';
    return long_name;
}

/*
 * sd_image and image: at the size the constant gives, and at the 128 and
 * 160 bytes the two images gave before, rc, size and fs_rc all parse and
 * the path is cut and marked. Before, the path came first: 128 bytes
 * lost rc and fs_rc without a mark.
 */
TEST(image_keys_worst_case)
{
    static const size_t sizes[] = { DG_SD_IMAGE_KEYS_SIZE, 160, 128 };
    char line[DG_SD_IMAGE_KEYS_SIZE];
    kv_t kv[8];
    dg_sd_entry_t e;
    unsigned i;
    int n;

    e.name = worst_name();
    e.size = 4294967295u;
    e.is_dir = 0;
    e.fs_rc = -2147483647 - 1;
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        dg_sd_image_keys(line, sizes[i], "/ROBCO", &e, DG_SDF_OPEN);
        CHECK(strlen(line) < sizes[i]);
        n = split(line, kv, 8);
        CHECK_EQ(n, 4);
        CHECK_STR(find(kv, n, "rc"), "open");
        CHECK_STR(find(kv, n, "size"), "4294967295");
        CHECK_STR(find(kv, n, "fs_rc"), "-2147483648");
        CHECK_EQ(cut_of(kv, n, "path"), 1);
        CHECK_EQ(strncmp(find(kv, n, "path"), "/ROBCO/   ", 10), 0);
    }
    /* At the constant's size the path gets its whole DG_MVAL_MAX: dg_mval() alone cuts it. */
    dg_sd_image_keys(line, sizeof(line), "/ROBCO", &e, DG_SDF_OPEN);
    CHECK(strstr(line, " path=") != NULL);
    /* dg_mval() cuts after a whole escape: up to 2 bytes short of its room. */
    CHECK(strlen(strstr(line, " path=") + 6) + 2u >= (size_t)DG_MVAL_MAX);
}

/* card_error: what and rc parse, the path is cut and marked, at the 160 bytes of before too. */
TEST(card_error_keys_worst_case)
{
    static const size_t sizes[] = { MT_CARD_ERROR_KEYS_SIZE, 160 };
    char path[FS_PATH_MAX + 1];
    char line[MT_CARD_ERROR_KEYS_SIZE];
    kv_t kv[8];
    unsigned i;
    int n;

    memset(path, ' ', FS_PATH_MAX);
    path[0] = '/';
    path[FS_PATH_MAX] = '\0';
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        mt_card_error_keys(line, sizes[i], path, MT_WE_OPENDIR, -2147483647 - 1);
        CHECK(strlen(line) < sizes[i]);
        n = split(line, kv, 8);
        CHECK_EQ(n, 3);
        CHECK_STR(find(kv, n, "what"), "opendir");
        CHECK_STR(find(kv, n, "rc"), "-2147483648");
        CHECK_EQ(cut_of(kv, n, "path"), 1);
    }
}

/*
 * The result keys of a tool: the short keys whole, then the one text key
 * cut to the room left and marked. The worst short keys of the diag sd
 * test fill DG_KEYS_MAX, with the bad image's path last.
 */
TEST(tool_keys_worst_case)
{
    char keys[DG_KEYS_MAX];
    char bad[DG_SD_BAD_KEYS_SIZE];
    char raw[DG_TEXT_RAW_SIZE];
    kv_t kv[32];
    dg_sd_scan_t s;
    int n;

    memset(&s, 0, sizeof(s));
    memcpy(s.bad_name, worst_name(), FS_NAME_MAX + 1);
    s.first_bad = DG_SDF_OPEN;
    s.first_fs_rc = -2147483647 - 1;
    keys[0] = '\0';
    add(keys, sizeof(keys), "probe_rc=-2147483648");
    add(keys, sizeof(keys), "card=sdhc mid=0xff oid=XX pnm=XXXXX psn=0xffffffff blocks=4294967295");
    add(keys, sizeof(keys), "vol_lba=4294967295 vol_blocks=4294967295 read=4294967295 sampled=1 "
        "chunk_errors=4294967295 errors=4294967295");
    add(keys, sizeof(keys), "first_bad=4294967295");
    add(keys, sizeof(keys), "ms=4294967295 kib_s=4294967295");
    add(keys, sizeof(keys), "files=4294967295 images=4294967295 bad_images=4294967295 "
        "others=4294967295");
    add(keys, sizeof(keys), "dir_rc=-2147483648");
    dg_sd_bad_keys(bad, sizeof(bad), &s);
    add(keys, sizeof(keys), bad);
    add(keys, sizeof(keys), "writes=4294967295");
    dg_keys_text(keys, sizeof(keys), "bad_path", dg_sd_path(raw, "/ROBCO", s.bad_name));
    CHECK(strlen(keys) < sizeof(keys));
    n = split(keys, kv, 32);
    CHECK(n > 0);
    CHECK(find(kv, n, "keys_cut") == NULL);
    CHECK_STR(find(kv, n, "bad_rc"), "open");
    CHECK_STR(find(kv, n, "bad_fs_rc"), "-2147483648");
    CHECK_STR(find(kv, n, "dir_rc"), "-2147483648");
    CHECK_STR(find(kv, n, "writes"), "4294967295");
    CHECK_STR(kv[n - 1].key, "bad_path");
    CHECK(kv[n - 1].cut);
}

/* No room for even one escape: the text key is dropped and keys_cut=1 says so. */
TEST(text_key_without_room)
{
    char keys[28] = "rc=-1 why=read";

    dg_keys_text(keys, sizeof(keys), "first_path", "/ROBCO/X");
    CHECK_STR(keys, "rc=-1 why=read keys_cut=1");
}

/* A marker line past its buffer keeps whole keys and ends with keys_cut=1. */
TEST(mark_fit_marks_a_cut)
{
    char buf[40];
    /* As k_vsnprintf() leaves it: cut to the buffer, the full length returned. */
    const char *line = "tool name=x result=ok aaaa bbbbbbbbbbbbbbbbbb";
    int len = (int)strlen(line);

    memcpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    dg_mark_fit(buf, sizeof(buf), len);
    CHECK_STR(buf, "tool name=x result=ok aaaa keys_cut=1");
    len = snprintf(buf, sizeof(buf), "%s", "tool name=x");
    dg_mark_fit(buf, sizeof(buf), len);
    CHECK_STR(buf, "tool name=x");
}

/* The view tool's name= and path= keys go through dg_mval() alone. */
TEST(view_keys_parse_back)
{
    char v[DG_MVAL_SIZE];
    char line[600];
    char name[300];
    kv_t kv[8];
    unsigned i;
    int n;

    for (i = 0; i < sizeof(hostile) / sizeof(hostile[0]); i++) {
        snprintf(name, sizeof(name), "%s/TERMINAL.CFG", hostile[i]);
        snprintf(line, sizeof(line), "sel=4 name=%s", dg_mval(v, sizeof(v), name));
        n = split(line, kv, 8);
        CHECK_EQ(n, 2);
        CHECK_STR(find(kv, n, "name"), name);
    }
}

/* A screen row with a hostile name stays one row inside 79 columns. */
TEST(rows_stay_rows)
{
    char row[160];
    char out[40];
    dg_sd_entry_t e;
    unsigned i;
    size_t k;

    memset(long_name, 'N', FS_NAME_MAX);
    long_name[FS_NAME_MAX] = '\0';
    CHECK_STR(dg_sname(out, sizeof(out), "My Image.SYS", 12), "My Image.SYS");
    CHECK_STR(dg_sname(out, sizeof(out), "My Long Image.SYS", 12), "My Long I...");
    CHECK_STR(dg_sname(out, sizeof(out), "\x1b[2J\n.SYS", 12), "?[2J?.SYS");
    CHECK_STR(dg_sname(out, sizeof(out), "\xc3\xa9.SYS", 12), "??.SYS");
    for (i = 0; i <= sizeof(hostile) / sizeof(hostile[0]); i++) {
        e.name = i < sizeof(hostile) / sizeof(hostile[0]) ? hostile[i] : long_name;
        e.size = 4294967295u;
        e.is_dir = 0;
        dg_sd_row(row, sizeof(row), &e, DG_SDF_OK, "\x1b\x1b\x1b\x1b\x1b\x1b\x1b\x1b"
                  "\x1b\x1b\x1b\x1b\x1b\x1b\x1b\x1b", 65535);
        CHECK(strlen(row) <= 79);
        for (k = 0; row[k] != '\0'; k++) {
            CHECK((unsigned char)row[k] >= 0x20 && (unsigned char)row[k] <= 0x7e);
        }
        dg_sd_row(row, sizeof(row), &e, RIMG_E_MAGIC, "", 0);
        CHECK(strlen(row) <= 79);
    }
}
