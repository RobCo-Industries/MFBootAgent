/*
 * The [maint] section of BOOT.CFG, by the key table of BOOT_PROTOCOL.md.
 * Pure: the host tests link it.
 */
#ifndef MT_CFG_H
#define MT_CFG_H

#include <stddef.h>

#define MT_CFG_PATH     "/ROBCO/BOOT.CFG"
/* The largest BOOT.CFG read, as the boot agent reads it. */
#define MT_CFG_FILE_MAX 4096u

/* mt_cfg_parse() results. */
#define MT_CFG_OK       0
#define MT_CFG_SYNTAX   1   /* a line does not parse: the tool stays off */

typedef struct {
    int restore_users;  /* 1 only for a last value of "on" */
    unsigned bad;       /* values other than on and off */
    unsigned unknown;   /* other keys in [maint] */
    int read_rc;        /* BOOT.CFG could not be read: the card, mount or fs code, else 0 */
    unsigned syntax_line; /* the line that does not parse, else 0 */
} mt_cfg_t;

/* restore_users off, no count, no error. */
void mt_cfg_default(mt_cfg_t *c);

/*
 * 1 when the start knows the setting: BOOT.CFG read and parsed, or it is
 * missing, which means off. 0 when a read error or a bad line left the
 * tool off without knowing what the file says.
 */
int mt_cfg_known(const mt_cfg_t *c);

/*
 * The status row at start when the setting is not known, else "":
 * "BOOT.CFG does not read: error -1. Restore users is off."
 */
void mt_cfg_status(char *out, size_t size, const mt_cfg_t *c);

/*
 * Reads [maint] from `len` bytes of BOOT.CFG text. Section and key names
 * and the words are case-insensitive. Every [maint] header adds to one
 * section, and a key given twice takes its last value. Other sections are
 * skipped. On MT_CFG_SYNTAX, *err_line gets the line and restore_users is
 * off, whatever came before the bad line.
 */
int mt_cfg_parse(const char *text, size_t len, mt_cfg_t *c, unsigned *err_line);

#endif
