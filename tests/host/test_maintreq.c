/*
 * The maintenance request of BOOT_PROTOCOL.md: each row of its failure
 * table, the request line, the screen at start and every road for
 * boot_flags bits 1 and 3.
 */

#include <string.h>

#include <robco/bootinfo.h>
#include <robco/fs.h>
#include <robco/rimg.h>

#include "keymap.h"
#include "maintreq.h"
#include "rc_test.h"

#define BIT1 ROBCO_BOOT_MAINT
#define BIT3 ROBCO_BOOT_MAINT_REQ

static char line[MFB_REQ_LINE_MAX + 1];

static int line_of(const char *data, size_t len)
{
    memset(line, 'x', sizeof(line));
    return mfb_req_line(0, data, len, line, sizeof(line));
}

TEST(names)
{
    CHECK_STR(mfb_req_name(MFB_REQ_NONE), "none");
    CHECK_STR(mfb_req_name(MFB_REQ_FOUND), "found");
    CHECK_STR(mfb_req_name(MFB_REQ_CLEAR_FAILED), "clear_failed");
    CHECK_STR(mfb_req_name(3), "unknown");
    CHECK_STR(MFB_REQ_PATH, "/ROBCO/MAINT.REQ");
    CHECK_STR(MFB_MAINT_PATH, "/ROBCO/MAINT.SYS");
}

/* Failure table: no file, or a folder named MAINT.REQ, is no request. */
TEST(only_a_regular_file_counts)
{
    CHECK_EQ(mfb_req_present(FS_OK, 0), 1);
    CHECK_EQ(mfb_req_present(FS_OK, 1), 0);
    CHECK_EQ(mfb_req_present(FS_ERR_NOENT, 0), 0);
    CHECK_EQ(mfb_req_present(FS_ERR_IO, 0), 0);
}

/* rc of a none result: 0 only for a missing name or a folder, else the stat error. */
TEST(none_rc_carries_the_stat_error)
{
    CHECK_EQ(mfb_req_none_rc(FS_ERR_NOENT), 0);
    CHECK_EQ(mfb_req_none_rc(FS_OK), 0);
    CHECK_EQ(mfb_req_none_rc(FS_ERR_IO), FS_ERR_IO);
    CHECK_EQ(mfb_req_none_rc(FS_ERR_CORRUPT), FS_ERR_CORRUPT);
    CHECK_EQ(mfb_req_none_rc(FS_ERR_GONE), FS_ERR_GONE);
    /* Such an error is still no request: a normal boot. */
    CHECK_EQ(mfb_req_present(FS_ERR_IO, 0), 0);
    CHECK_EQ(MFB_MAINT_FIXED_INDEX, -1);
}

/* Failure table: an empty file is a request with no line. */
TEST(empty_file_has_no_line)
{
    CHECK_EQ(line_of("", 0), 0);
    CHECK_STR(line, "");
    CHECK_EQ(line_of("\n", 1), 0);
    CHECK_EQ(line_of("\r\n", 2), 0);
    CHECK_EQ(line_of("\r", 1), 0);
}

TEST(a_line_with_each_line_end)
{
    CHECK_EQ(line_of("OPER at 12:00", 13), 1);
    CHECK_STR(line, "OPER at 12:00");
    CHECK_EQ(line_of("OPER\n", 5), 1);
    CHECK_STR(line, "OPER");
    CHECK_EQ(line_of("OPER\r", 5), 1);
    CHECK_STR(line, "OPER");
    CHECK_EQ(line_of("OPER\r\n", 6), 1);
    CHECK_STR(line, "OPER");
    CHECK_EQ(line_of(" ~", 2), 1);
    CHECK_STR(line, " ~");
}

/* Failure table: a content that is too long is ignored. */
TEST(sixty_three_bytes_show_sixty_four_do_not)
{
    char b[MFB_REQ_READ_MAX + 1];

    memset(b, 'A', sizeof(b));
    CHECK_EQ(line_of(b, 63), 1);
    CHECK_EQ(strlen(line), 63);
    b[63] = '\r';
    b[64] = '\n';
    CHECK_EQ(line_of(b, 65), 1);
    CHECK_EQ(strlen(line), 63);
    memset(b, 'A', sizeof(b));
    CHECK_EQ(line_of(b, 64), 0);
    CHECK_STR(line, "");
    b[64] = '\n';
    CHECK_EQ(line_of(b, 65), 0);
    /* Two line ends: the line keeps one, which is not printable. */
    CHECK_EQ(line_of("A\n\n", 3), 0);
    /* A file larger than the read buffer reads as FS_ERR_TOOBIG. */
    CHECK_EQ(mfb_req_line(FS_ERR_TOOBIG, b, 0, line, sizeof(line)), 0);
    CHECK_STR(line, "");
}

/* Failure table: a content that is not printable is ignored. */
TEST(binary_content_is_ignored)
{
    CHECK_EQ(line_of("A\tB", 3), 0);
    CHECK_EQ(line_of("A\x7f", 2), 0);
    CHECK_EQ(line_of("\x80", 1), 0);
    CHECK_EQ(line_of("\xff\xfe", 2), 0);
    CHECK_EQ(line_of("A\0B", 3), 0);
    CHECK_EQ(line_of("A\nB", 3), 0);
    CHECK_EQ(line_of("A\rB", 3), 0);
    CHECK_STR(line, "");
}

TEST(a_failed_read_has_no_line)
{
    CHECK_EQ(mfb_req_line(FS_ERR_IO, "OPER", 4, line, sizeof(line)), 0);
    CHECK_STR(line, "");
    CHECK_EQ(mfb_req_line(0, NULL, 0, line, sizeof(line)), 0);
}

/* Failure table: a delete that fails is clear_failed, with the code kept by the caller. */
TEST(delete_result)
{
    CHECK_EQ(mfb_req_result(FS_OK), MFB_REQ_FOUND);
    CHECK_EQ(mfb_req_result(FS_ERR_RO), MFB_REQ_CLEAR_FAILED);
    CHECK_EQ(mfb_req_result(FS_ERR_IO), MFB_REQ_CLEAR_FAILED);
    CHECK_EQ(mfb_req_result(FS_ERR_DENIED), MFB_REQ_CLEAR_FAILED);
    CHECK_EQ(mfb_req_result(FS_ERR_GONE), MFB_REQ_CLEAR_FAILED);
}

/* Every request result opens the screen with why=request: no image boots without a key. */
TEST(screen_at_start)
{
    int why = -1;

    CHECK_EQ(mfb_start_screen(MFB_REQ_NONE, 0, 0, &why), 0);
    CHECK_EQ(why, -1);
    CHECK_EQ(mfb_start_screen(MFB_REQ_NONE, 1, 0, &why), 0);
    CHECK_EQ(mfb_start_screen(MFB_REQ_NONE, 0, BIT1, &why), 0);
    CHECK_EQ(mfb_start_screen(MFB_REQ_NONE, 1, BIT1, &why), 1);
    CHECK_EQ(why, MFB_WHY_FLAG);
    CHECK_EQ(mfb_start_screen(MFB_REQ_FOUND, 0, 0, &why), 1);
    CHECK_EQ(why, MFB_WHY_REQUEST);
    why = -1;
    CHECK_EQ(mfb_start_screen(MFB_REQ_CLEAR_FAILED, 0, 0, &why), 1);
    CHECK_EQ(why, MFB_WHY_REQUEST);
    why = -1;
    /* A request with bit 1 received: one screen, why=request. */
    CHECK_EQ(mfb_start_screen(MFB_REQ_FOUND, 1, BIT1, &why), 1);
    CHECK_EQ(why, MFB_WHY_REQUEST);
}

static input_event_t key_ev(unsigned source, unsigned code, int value)
{
    input_event_t e;

    memset(&e, 0, sizeof(e));
    e.type = INPUT_EV_KEY;
    e.source = (uint8_t)source;
    e.code = (uint16_t)code;
    e.value = value;
    return e;
}

static int take(mfb_start_keys_t *k, unsigned source, unsigned code, int value)
{
    input_event_t e = key_ev(source, code, value);

    return mfb_start_keys_take(k, &e);
}

#define GPIO INPUT_SRC_GPIO
#define HELD(code) ((uint32_t)1 << (code))

static const unsigned start_keys[] = { INPUT_KEY_BACK, INPUT_KEY_SELECT };

/* The guard's press, then its release, both before the screen opened. */
TEST(start_guard_press_and_release_before)
{
    unsigned i;

    for (i = 0; i < 2; i++) {
        unsigned c = start_keys[i];
        mfb_start_keys_t k;

        mfb_start_keys_init(&k, 2, 0);
        CHECK_EQ(take(&k, GPIO, c, INPUT_PRESS), 0);
        CHECK_EQ(take(&k, GPIO, c, INPUT_RELEASE), 0);
        CHECK_EQ(k.stale, 0);
        /* The next press, after the screen opened, acts. */
        CHECK_EQ(take(&k, GPIO, c, INPUT_PRESS), 1);
    }
}

/* The guard's press, the key still held: its repeats and release do nothing. */
TEST(start_guard_press_still_held)
{
    unsigned i;

    for (i = 0; i < 2; i++) {
        unsigned c = start_keys[i];
        mfb_start_keys_t k;

        mfb_start_keys_init(&k, 2, HELD(c));
        CHECK_EQ(take(&k, GPIO, c, INPUT_PRESS), 0);
        CHECK_EQ(take(&k, GPIO, c, INPUT_REPEAT), 0);
        CHECK_EQ(take(&k, GPIO, c, INPUT_REPEAT), 0);
        CHECK_EQ(take(&k, GPIO, c, INPUT_RELEASE), 0);
        CHECK_EQ(take(&k, GPIO, c, INPUT_PRESS), 1);
        CHECK_EQ(take(&k, GPIO, c, INPUT_RELEASE), 1);
    }
}

/* Press and release before, a second press after the screen opened: it acts. */
TEST(start_second_press_after_open)
{
    unsigned i;

    for (i = 0; i < 2; i++) {
        unsigned c = start_keys[i];
        mfb_start_keys_t k;

        mfb_start_keys_init(&k, 2, 0);
        CHECK_EQ(take(&k, GPIO, c, INPUT_PRESS), 0);
        CHECK_EQ(take(&k, GPIO, c, INPUT_RELEASE), 0);
        CHECK_EQ(take(&k, GPIO, c, INPUT_PRESS), 1);
        /* Pressed after the screen showed: its repeats are the person's. */
        CHECK_EQ(take(&k, GPIO, c, INPUT_REPEAT), 1);
    }
}

/* Console and port keys before the screen opened act: someone sent them on purpose. */
TEST(start_console_and_port_keys_act)
{
    static const unsigned sources[] = { INPUT_SRC_SERIAL, INPUT_SRC_SCRIPT };
    unsigned i, j;

    for (i = 0; i < 2; i++) {
        for (j = 0; j < 2; j++) {
            mfb_start_keys_t k;

            mfb_start_keys_init(&k, 2, 0);
            CHECK_EQ(take(&k, sources[j], start_keys[i], INPUT_PRESS), 1);
            CHECK_EQ(take(&k, sources[j], start_keys[i], INPUT_RELEASE), 1);
            CHECK_EQ(k.pre, 0);
        }
    }
}

/* A port request and a console character count as queued events but always act. */
TEST(start_chars_act)
{
    mfb_start_keys_t k;
    input_event_t e;

    memset(&e, 0, sizeof(e));
    e.type = INPUT_EV_CHAR;
    e.source = INPUT_SRC_SCRIPT;
    e.value = 1;
    mfb_start_keys_init(&k, 1, 0);
    CHECK_EQ(mfb_start_keys_take(&k, &e), 1);
    CHECK_EQ(k.pre, 0);
    /* The queued GPIO press behind it is now after the screen opened. */
    CHECK_EQ(take(&k, GPIO, INPUT_KEY_BACK, INPUT_PRESS), 1);
}

/* A held key whose press the queue lost: its repeats do nothing, a new press acts. */
TEST(start_held_without_a_queued_press)
{
    mfb_start_keys_t k;

    mfb_start_keys_init(&k, 0, HELD(INPUT_KEY_SELECT));
    CHECK_EQ(take(&k, GPIO, INPUT_KEY_SELECT, INPUT_REPEAT), 0);
    CHECK_EQ(take(&k, GPIO, INPUT_KEY_BACK, INPUT_PRESS), 1);
    CHECK_EQ(take(&k, GPIO, INPUT_KEY_SELECT, INPUT_PRESS), 1);
    CHECK_EQ(take(&k, GPIO, INPUT_KEY_SELECT, INPUT_REPEAT), 1);
}

/* Every GPIO key queued before the screen opened does nothing, the others too. */
TEST(start_every_queued_key)
{
    unsigned c;

    for (c = 1; c < INPUT_KEY_COUNT; c++) {
        mfb_start_keys_t k;

        mfb_start_keys_init(&k, 3, 0);
        CHECK_EQ(take(&k, GPIO, c, INPUT_PRESS), 0);
        CHECK_EQ(take(&k, GPIO, c, INPUT_REPEAT), 0);
        CHECK_EQ(take(&k, GPIO, c, INPUT_RELEASE), 0);
        CHECK_EQ(take(&k, GPIO, c, INPUT_PRESS), 1);
    }
}

/* Every road for bits 1 and 3, from a cold block and from received flags. */
TEST(bits_for_every_road)
{
    static const uint32_t subtypes[] = { RIMG_BOOT_UOS, RIMG_BOOT_MAINT, RIMG_BOOT_DIAG };
    static const int reqs[] = { MFB_REQ_NONE, MFB_REQ_FOUND, MFB_REQ_CLEAR_FAILED };
    static const uint32_t ins[] = { 0, BIT1, BIT3, BIT1 | BIT3, 0xFFFFFFFFu };
    unsigned s, r, i, t, o;

    for (s = 0; s < 3; s++) {
        for (r = 0; r < 3; r++) {
            for (i = 0; i < 5; i++) {
                for (t = 0; t < 2; t++) {
                    for (o = 0; o < 2; o++) {
                        /* bootinfo_build() keeps received flags when trusted, writes bit 2 when cold. */
                        uint32_t built = t ? ins[i] : ROBCO_BOOT_COLD;
                        uint32_t got = mfb_boot_flags(built, (int)t, ins[i], (int)o, reqs[r],
                                                      subtypes[s]);
                        int found = reqs[r] != MFB_REQ_NONE;
                        int screen = o || (t && (ins[i] & BIT1) != 0);
                        int bit1 = found || screen || subtypes[s] == RIMG_BOOT_MAINT;

                        CHECK_EQ((got & BIT1) != 0, bit1);
                        CHECK_EQ((got & BIT3) != 0, found);
                        CHECK_EQ(got & ~(BIT1 | BIT3), built & ~(BIT1 | BIT3));
                    }
                }
            }
        }
    }
}

TEST(bits_named_cases)
{
    /* No request, UOS from the menu: both clear, even when received set. */
    CHECK_EQ(mfb_boot_flags(BIT3, 1, BIT3, 0, MFB_REQ_NONE, RIMG_BOOT_UOS), 0);
    /* A request, then UOS: bits 1 and 3 stay set whatever entry. */
    CHECK_EQ(mfb_boot_flags(0, 1, 0, 1, MFB_REQ_FOUND, RIMG_BOOT_UOS), BIT1 | BIT3);
    CHECK_EQ(mfb_boot_flags(0, 1, 0, 1, MFB_REQ_CLEAR_FAILED, RIMG_BOOT_DIAG), BIT1 | BIT3);
    /* MAINT from the menu with no request: bit 1 set, bit 3 clear. */
    CHECK_EQ(mfb_boot_flags(0, 1, 0, 0, MFB_REQ_NONE, RIMG_BOOT_MAINT), BIT1);
    CHECK_EQ(mfb_boot_flags(ROBCO_BOOT_COLD, 0, 0, 0, MFB_REQ_NONE, RIMG_BOOT_MAINT),
             ROBCO_BOOT_COLD | BIT1);
    /* The screen opened, then UOS: bit 1 only. */
    CHECK_EQ(mfb_boot_flags(0, 1, 0, 1, MFB_REQ_NONE, RIMG_BOOT_UOS), BIT1);
    /* Received bit 1 opens the screen: it stays. */
    CHECK_EQ(mfb_boot_flags(BIT1, 1, BIT1, 0, MFB_REQ_NONE, RIMG_BOOT_UOS), BIT1);
    /* Received bit 1 from an untrusted block counts for nothing. */
    CHECK_EQ(mfb_boot_flags(ROBCO_BOOT_COLD | BIT1, 0, BIT1, 0, MFB_REQ_NONE, RIMG_BOOT_UOS),
             ROBCO_BOOT_COLD);
}

static void add_entry(mfb_cfg_t *cfg, const char *name, const char *path)
{
    mfb_entry_t *e = &cfg->entry[cfg->count++];

    memset(e, 0, sizeof(*e));
    strcpy(e->name, name);
    strcpy(e->path, path);
}

TEST(maint_entry_or_fixed_path)
{
    mfb_cfg_t cfg;
    mfb_entry_t fixed;
    const mfb_entry_t *e;
    unsigned index = 99;

    memset(&cfg, 0, sizeof(cfg));
    mfb_cfg_default(&cfg);
    CHECK(mfb_maint_entry(&cfg, &index) == NULL);
    CHECK_EQ(index, 0);
    memset(&cfg, 0, sizeof(cfg));
    add_entry(&cfg, "UOS", "/ROBCO/UOS.SYS");
    add_entry(&cfg, "MAINTX", "/ROBCO/X.SYS");
    add_entry(&cfg, "MAINT", "/TOOLS/M.SYS");
    e = mfb_maint_entry(&cfg, &index);
    CHECK(e == &cfg.entry[2]);
    CHECK_EQ(index, 3);
    CHECK_STR(e->path, "/TOOLS/M.SYS");
    strcpy(cfg.entry[2].name, "maint");
    CHECK(mfb_maint_entry(&cfg, &index) == &cfg.entry[2]);
    strcpy(cfg.entry[2].name, "MAIN");
    CHECK(mfb_maint_entry(&cfg, &index) == NULL);
    CHECK_EQ(index, 0);
    mfb_maint_fixed(&fixed);
    CHECK_STR(fixed.name, "MAINT");
    CHECK_STR(fixed.path, "/ROBCO/MAINT.SYS");
    CHECK_STR(fixed.cmdline, "");
}
