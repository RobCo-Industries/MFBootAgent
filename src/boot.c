#include "boot.h"

#include <robco/klibc.h>
#include <robco/rimg.h>

typedef struct {
    int err;
    int code;
    const char *name;
    const char *text;
} err_info_t;

/* Names match rimg_strerror() and IMAGE_FORMAT.md. */
static const err_info_t errors[] = {
    { MFB_ERR_NOFILE,  1, "nofile",  "file not found" },
    { MFB_ERR_IO,      2, "io",      "SD card or FAT volume not usable" },
    { MFB_ERR_MAGIC,   3, "magic",   "not a boot image" },
    { MFB_ERR_VERSION, 4, "version", "unknown header version" },
    { MFB_ERR_SIZE,    5, "size",    "size does not match the file" },
    { MFB_ERR_BOUNDS,  6, "bounds",  "image does not fit the OS slot" },
    { MFB_ERR_ABI,     7, "abi",     "wrong ABI version" },
    { MFB_ERR_CRC,     8, "crc",     "image CRC does not match" },
    { MFB_ERR_SUBTYPE, 6, "bounds",  "not an operating image" },
};

static const err_info_t *find(int err)
{
    size_t i;

    for (i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
        if (errors[i].err == err) {
            return &errors[i];
        }
    }
    return NULL;
}

int mfb_err_code(int err)
{
    const err_info_t *e = find(err);

    return e != NULL ? e->code : err;
}

const char *mfb_err_name(int err)
{
    const err_info_t *e = find(err);

    return e != NULL ? e->name : "unknown";
}

const char *mfb_err_text(int err)
{
    const err_info_t *e = find(err);

    return e != NULL ? e->text : "unknown error";
}

void mfb_error_line(char *buf, size_t size, int err, const char *name)
{
    char upper[8];
    const char *n = mfb_err_name(err);
    int code = mfb_err_code(err);
    size_t i;
    /* Codes print as two digits. */
    unsigned shown = code >= 0 && code <= 99 ? (unsigned)code : 99u;

    for (i = 0; i + 1 < sizeof(upper) && n[i] != '\0'; i++) {
        upper[i] = (n[i] >= 'a' && n[i] <= 'z') ? (char)(n[i] - 'a' + 'A') : n[i];
    }
    upper[i] = '\0';
    if (name != NULL && name[0] != '\0') {
        k_snprintf(buf, size, "ERROR E%02u %s: %s: %s", shown, upper, name, mfb_err_text(err));
    } else {
        k_snprintf(buf, size, "ERROR E%02u %s: %s", shown, upper, mfb_err_text(err));
    }
}

int mfb_subtype_ok(uint32_t subtype)
{
    return subtype == RIMG_BOOT_UOS || subtype == RIMG_BOOT_MAINT || subtype == RIMG_BOOT_DIAG;
}

static void put_string(char *dst, size_t size, const char *src)
{
    size_t n = src != NULL ? k_strlen(src) : 0;

    if (n > size - 1) {
        n = size - 1;
    }
    k_memset(dst, 0, size);
    if (n > 0) {
        k_memcpy(dst, src, n);
    }
}

void mfb_bootinfo_set_boot(robco_bootinfo_t *bi, uint32_t boot_dev, uint32_t part_lba,
                           const char *path, const char *cmdline)
{
    bi->boot_dev = boot_dev;
    bi->boot_part_lba = part_lba;
    bi->boot_flags &= ~ROBCO_BOOT_FALLBACK;
    put_string(bi->boot_path, sizeof(bi->boot_path), path);
    put_string(bi->cmdline, sizeof(bi->cmdline), cmdline);
    bootinfo_seal(bi);
}
