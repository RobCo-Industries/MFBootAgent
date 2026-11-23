/* Loader results to error lines, the accepted subtypes and the boot info update. */

#include <string.h>

#include <robco/bootinfo.h>
#include <robco/rimg.h>

#include "boot.h"
#include "rc_test.h"

TEST(codes_match_the_loader)
{
    int c;

    for (c = RIMG_E_NOFILE; c <= RIMG_E_CRC; c++) {
        CHECK_EQ(mfb_err_code(c), c);
        CHECK_STR(mfb_err_name(c), rimg_strerror(c));
    }
    CHECK_EQ(mfb_err_code(MFB_ERR_SUBTYPE), RIMG_E_BOUNDS);
    CHECK_STR(mfb_err_name(MFB_ERR_SUBTYPE), "bounds");
    CHECK_STR(mfb_err_name(0), "unknown");
    CHECK_STR(mfb_err_name(42), "unknown");
    CHECK_STR(mfb_err_text(42), "unknown error");
}

TEST(error_lines)
{
    static const struct {
        int err;
        const char *line;
    } want[] = {
        { MFB_ERR_NOFILE,  "ERROR E01 NOFILE: UOS: file not found" },
        { MFB_ERR_IO,      "ERROR E02 IO: UOS: SD card or FAT volume not usable" },
        { MFB_ERR_MAGIC,   "ERROR E03 MAGIC: UOS: not a boot image" },
        { MFB_ERR_VERSION, "ERROR E04 VERSION: UOS: unknown header version" },
        { MFB_ERR_SIZE,    "ERROR E05 SIZE: UOS: size does not match the file" },
        { MFB_ERR_BOUNDS,  "ERROR E06 BOUNDS: UOS: image does not fit the OS slot" },
        { MFB_ERR_ABI,     "ERROR E07 ABI: UOS: wrong ABI version" },
        { MFB_ERR_CRC,     "ERROR E08 CRC: UOS: image CRC does not match" },
        { MFB_ERR_SUBTYPE, "ERROR E06 BOUNDS: UOS: not an operating image" },
    };
    char b[MFB_LINE_MAX];
    size_t i;

    for (i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
        mfb_error_line(b, sizeof(b), want[i].err, "UOS");
        CHECK_STR(b, want[i].line);
    }
    mfb_error_line(b, sizeof(b), MFB_ERR_IO, NULL);
    CHECK_STR(b, "ERROR E02 IO: SD card or FAT volume not usable");
    mfb_error_line(b, sizeof(b), MFB_ERR_IO, "");
    CHECK_STR(b, "ERROR E02 IO: SD card or FAT volume not usable");
    mfb_error_line(b, sizeof(b), 123, "X");
    CHECK_STR(b, "ERROR E99 UNKNOWN: X: unknown error");
    /* The longest name keeps the line under 80 columns. */
    mfb_error_line(b, sizeof(b), MFB_ERR_IO, "ABCDEFGHIJKLMNO");
    CHECK(strlen(b) < MFB_LINE_MAX);
    CHECK_STR(b, "ERROR E02 IO: ABCDEFGHIJKLMNO: SD card or FAT volume not usable");
    mfb_error_line(b, 10, MFB_ERR_CRC, "UOS");
    CHECK_STR(b, "ERROR E08");
}

TEST(operating_subtypes_only)
{
    CHECK(mfb_subtype_ok(RIMG_BOOT_UOS));
    CHECK(mfb_subtype_ok(RIMG_BOOT_MAINT));
    CHECK(mfb_subtype_ok(RIMG_BOOT_DIAG));
    CHECK(!mfb_subtype_ok(1));
    CHECK(!mfb_subtype_ok(RIMG_BOOT_AGENT));
    CHECK(!mfb_subtype_ok(RIMG_BOOT_BIOS));
    CHECK(!mfb_subtype_ok(RIMG_BOOT_PIPOS));
    CHECK(!mfb_subtype_ok(RIMG_BOOT_EXAMPLE));
    CHECK(!mfb_subtype_ok(0xFFFFFFFFu));
}

TEST(set_boot_fields)
{
    robco_bootinfo_t bi;
    char path[100];
    char cmd[200];

    memset(&bi, 0xA5, sizeof(bi));
    bi.boot_flags = ROBCO_BOOT_FALLBACK | ROBCO_BOOT_COLD | ROBCO_BOOT_MAINT;
    mfb_bootinfo_set_boot(&bi, ROBCO_BOOT_DEV_EMMC, 2048, "/ROBCO/UOS.SYS", "a b");
    CHECK_EQ(bootinfo_validate(&bi), BOOTINFO_OK);
    CHECK_EQ(bi.boot_dev, ROBCO_BOOT_DEV_EMMC);
    CHECK_EQ(bi.boot_part_lba, 2048);
    CHECK_EQ(bi.boot_flags, ROBCO_BOOT_COLD | ROBCO_BOOT_MAINT);
    CHECK_STR(bi.boot_path, "/ROBCO/UOS.SYS");
    CHECK_STR(bi.cmdline, "a b");
    /* NUL-padded to the end of each array. */
    CHECK_EQ(bi.boot_path[sizeof(bi.boot_path) - 1], 0);
    CHECK_EQ(bi.boot_path[14], 0);
    CHECK_EQ(bi.cmdline[sizeof(bi.cmdline) - 1], 0);
    /* Untouched fields stay. */
    CHECK_EQ(bi.fb_width, 0xA5A5A5A5u);
    /* Cut to fit. */
    memset(path, 'p', sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    memset(cmd, 'c', sizeof(cmd) - 1);
    cmd[sizeof(cmd) - 1] = '\0';
    mfb_bootinfo_set_boot(&bi, 0, 0, path, cmd);
    CHECK_EQ(strlen(bi.boot_path), sizeof(bi.boot_path) - 1);
    CHECK_EQ(strlen(bi.cmdline), sizeof(bi.cmdline) - 1);
    CHECK_EQ(bootinfo_validate(&bi), BOOTINFO_OK);
    /* Empty and NULL. */
    mfb_bootinfo_set_boot(&bi, 0, 0, "/A", NULL);
    CHECK_STR(bi.cmdline, "");
    CHECK_EQ(bi.cmdline[5], 0);
}
