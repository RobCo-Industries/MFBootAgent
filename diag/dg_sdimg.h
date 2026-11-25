/*
 * The check of one image file on the mounted card, with the loader's own
 * steps. Hardware side: it reads the card. DIAG.SYS's SD test and
 * MAINT.SYS's check images tool both use it.
 */
#ifndef DG_SDIMG_H
#define DG_SDIMG_H

#include <stdint.h>

/* Bytes of the header name, without its end. */
#define DG_SDIMG_NAME 16u

/*
 * Checks the image file at `path` of `size` bytes: the first bytes by
 * dg_sd_head(), then the whole file in RAM above the OS slot through
 * rimg_validate(). Returns DG_SDF_OK or the outcome of dg_sd.h. With
 * DG_SDF_OK, `name` gets the header name and `subtype` the subtype. With
 * DG_SDF_OPEN, *fs_rc gets the fs_open() code, else 0.
 */
int dg_sd_check_image(const char *path, uint32_t size, char name[DG_SDIMG_NAME + 1],
                      unsigned *subtype, int *fs_rc);

#endif
