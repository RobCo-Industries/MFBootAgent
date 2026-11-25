#include "maintreq.h"

#include <robco/bootinfo.h>
#include <robco/fs.h>
#include <robco/klibc.h>
#include <robco/rimg.h>

#include "boot.h"
#include "keymap.h"

const char *mfb_req_name(int result)
{
    switch (result) {
    case MFB_REQ_NONE:         return "none";
    case MFB_REQ_FOUND:        return "found";
    case MFB_REQ_CLEAR_FAILED: return "clear_failed";
    default:                   return "unknown";
    }
}

int mfb_req_present(int stat_rc, int is_dir)
{
    return stat_rc == 0 && !is_dir;
}

int mfb_req_line(int read_rc, const char *data, size_t len, char *out, size_t size)
{
    size_t i;

    out[0] = '\0';
    if (read_rc != 0 || data == NULL) {
        return 0;
    }
    /* One line end closes the line: CRLF, LF or CR. */
    if (len >= 2 && data[len - 2] == '\r' && data[len - 1] == '\n') {
        len -= 2;
    } else if (len >= 1 && (data[len - 1] == '\n' || data[len - 1] == '\r')) {
        len -= 1;
    }
    if (len == 0 || len > MFB_REQ_LINE_MAX || len + 1 > size) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)data[i];

        if (c < 0x20 || c > 0x7E) {
            return 0;
        }
    }
    k_memcpy(out, data, len);
    out[len] = '\0';
    return 1;
}

int mfb_req_none_rc(int stat_rc)
{
    return stat_rc == FS_ERR_NOENT ? 0 : stat_rc;
}

int mfb_req_result(int unlink_rc)
{
    return unlink_rc == 0 ? MFB_REQ_FOUND : MFB_REQ_CLEAR_FAILED;
}

int mfb_start_screen(int req, int trusted, uint32_t received, int *why)
{
    if (req != MFB_REQ_NONE) {
        *why = MFB_WHY_REQUEST;
        return 1;
    }
    if (mfb_maint_at_start(trusted, received)) {
        *why = MFB_WHY_FLAG;
        return 1;
    }
    return 0;
}

uint32_t mfb_boot_flags(uint32_t flags, int trusted, uint32_t received, int opened, int req,
                        uint32_t subtype)
{
    int found = req != MFB_REQ_NONE;

    flags = mfb_maint_flag(flags, trusted, received,
                           opened || found || subtype == RIMG_BOOT_MAINT);
    return found ? flags | ROBCO_BOOT_MAINT_REQ : flags & ~ROBCO_BOOT_MAINT_REQ;
}

/* Entry names compare without case, as BOOT.CFG's duplicate rule does. */
static int is_maint_name(const char *s)
{
    const char *m = MFB_MAINT_NAME;

    for (; *m != '\0'; s++, m++) {
        char c = *s >= 'a' && *s <= 'z' ? (char)(*s - 'a' + 'A') : *s;

        if (c != *m) {
            return 0;
        }
    }
    return *s == '\0';
}

const mfb_entry_t *mfb_maint_entry(const mfb_cfg_t *cfg, unsigned *index)
{
    unsigned i;

    for (i = 0; i < cfg->count && i < MFB_MAX_ENTRIES; i++) {
        if (is_maint_name(cfg->entry[i].name)) {
            *index = i + 1;
            return &cfg->entry[i];
        }
    }
    *index = 0;
    return NULL;
}

void mfb_maint_fixed(mfb_entry_t *e)
{
    k_memset(e, 0, sizeof(*e));
    k_memcpy(e->name, MFB_MAINT_NAME, sizeof(MFB_MAINT_NAME));
    k_memcpy(e->path, MFB_MAINT_PATH, sizeof(MFB_MAINT_PATH));
}
