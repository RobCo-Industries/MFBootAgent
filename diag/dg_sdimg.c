/* The check of one image file on the card: see dg_sdimg.h. */

#include <stdint.h>

#include <robco/alloc.h>
#include <robco/fs.h>
#include <robco/klibc.h>
#include <robco/rimg.h>

#include "dg_sd.h"
#include "dg_sdimg.h"

/* Largest image file the check reads: the OS slot is 12 MiB. */
#define FILE_MAX (16u << 20)

int dg_sd_check_image(const char *path, uint32_t size, char name[DG_SDIMG_NAME + 1],
                      unsigned *subtype, int *fs_rc)
{
    static fs_file_t f;
    uint8_t head[RIMG_META_OFFSET];
    const rimg_header_t *h = 0;
    void *base = 0;
    size_t room = robco_heap_high(&base);
    int n = 0;
    int rc;

    if (size == 0 || size < RIMG_META_OFFSET) {
        return dg_sd_head(size, 0, 0);
    }
    *fs_rc = 0;
    rc = fs_open(&f, path, FS_READ);
    if (rc != 0) {
        *fs_rc = rc;
        return DG_SDF_OPEN;
    }
    n = fs_read(&f, head, sizeof(head));
    rc = dg_sd_head(size, head, n);
    if (rc == DG_SDF_OK) {
        if (room > FILE_MAX) {
            room = FILE_MAX;
        }
        if (size > room) {
            rc = RIMG_E_SIZE;
        } else if (fs_seek(&f, 0, FS_SEEK_SET) != 0 ||
                   fs_read(&f, base, size) != (int)size) {
            rc = RIMG_E_IO;
        } else {
            rc = rimg_validate(base, size, 0, &h);
        }
        if (rc == RIMG_OK) {
            k_memcpy(name, h->name, DG_SDIMG_NAME);
            name[DG_SDIMG_NAME] = '\0';
            *subtype = (unsigned)h->subtype;
        }
    }
    (void)fs_close(&f);
    return rc;
}
