/*
 * The decision table of the restore users tool. Pure: the host tests
 * link it. The tool copies /UOS/SYSUAF.DEF over /UOS/SYSUAF.DAT. It
 * knows nothing of the format.
 */
#ifndef MT_USERS_H
#define MT_USERS_H

#include <stdint.h>

#define MT_USERS_DEF    "/UOS/SYSUAF.DEF"
#define MT_USERS_DAT    "/UOS/SYSUAF.DAT"
/* The largest default file the tool copies. */
#define MT_USERS_MAX    (1u << 20)

/* mt_users_check() results, before the question. */
#define MT_US_OFF       0   /* restore_users is not on: do nothing */
#define MT_US_MISSING   1   /* no default file, or a folder of that name */
#define MT_US_EMPTY     2   /* the default file has 0 bytes */
#define MT_US_LARGE     3   /* the default file is larger than MT_USERS_MAX */
#define MT_US_STAT      4   /* the default file does not stat */
#define MT_US_ASK       5   /* all good: ask the question */
#define MT_US_CONFIG    6   /* BOOT.CFG did not read or parse: off, the setting not known */

/*
 * on: the [maint] setting. cfg_known: mt_cfg_known(), 0 when the start
 * could not tell what [maint] says. stat_rc: fs_stat() of the default file, with
 * missing_rc the code for a missing file. is_dir and size: its stat.
 */
int mt_users_check(int on, int cfg_known, int stat_rc, int missing_rc, int is_dir,
                   uint32_t size);

/* Answers to the question, as mt_ui.h gives them. */
#define MT_US_ANS_NONE  0   /* not asked */

/*
 * The result of the tool, an MT_* result of mt_ui.h, and the why key in
 * *why: "setting", "config", "missing", "empty", "large", "stat", "no", "read",
 * "write" or "" for OK. answer is MT_ANS_YES or MT_ANS_NO of mt_ui.h, or
 * MT_US_ANS_NONE when check did not ask. read_rc and write_rc are 0 or
 * the error of the read of the default and of the atomic write; they
 * count only after a yes.
 */
int mt_users_result(int check, int answer, int read_rc, int write_rc, const char **why);

/* 1 when the tool may write: check gave MT_US_ASK and the answer is yes. */
int mt_users_may_write(int check, int answer);

#endif
