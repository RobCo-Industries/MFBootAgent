/*
 * Results, marker tails, screen lines, the progress rate and the timer
 * and RNG verdicts. Pure: the host tests link them.
 */
#ifndef DG_FMT_H
#define DG_FMT_H

#include <stddef.h>
#include <stdint.h>

#include <robco/input.h>

/* Test results, as BOOT_PROTOCOL.md gives them for the diag marker. */
enum {
    DG_OK = 0,
    DG_FAIL,
    DG_ABSENT,
    DG_SKIPPED,
    DG_RESULTS,
    DG_NONE = DG_RESULTS    /* not run yet */
};

/* "ok", "fail", "absent", "skipped"; "none" otherwise. */
const char *dg_result_name(int r);
/* "OK", "FAILED", "ABSENT", "SKIPPED"; "" otherwise. */
const char *dg_result_word(int r);

typedef struct {
    unsigned n[DG_RESULTS];
} dg_counts_t;

void dg_counts_init(dg_counts_t *c);
/* Counts one result; ignores values outside DG_OK to DG_SKIPPED. */
void dg_counts_add(dg_counts_t *c, int r);

/* The screen grid the menu needs. */
#define DG_MIN_COLS     80u
#define DG_MIN_ROWS     30u

/* Screen rows. The test work area runs from DG_ROW_WORK to the last row. */
#define DG_ROW_TITLE    0u
#define DG_ROW_SUB      1u
#define DG_ROW_RULE     2u
#define DG_ROW_ITEM0    4u      /* DG_ITEMS rows */
#define DG_ROW_HELP     16u
#define DG_ROW_STATUS   17u
#define DG_ROW_RULE2    18u
#define DG_ROW_WORK     19u
#define DG_WORK_ROWS    11u     /* rows 19 to 29 of a 30-row grid */

/* A screen or serial line, without its end. Sized for any formatted line. */
#define DG_LINE_MAX     128u
/* The detail text of a result on the screen. */
#define DG_TEXT_MAX     64u
/* The test keys of a result marker. */
#define DG_KEYS_MAX     320u

/*
 * The detail texts of the menu rows, set by dg_text(). A row with a result
 * has DG_ITEM_FIXED columns before its text, and the console leaves the
 * last of the DG_MIN_COLS columns blank. So a text holds DG_TEXT_COLS
 * characters at most, with its worst-case values. test_diag_fmt.c builds
 * every row with them. The full detail goes to the work rows.
 */
#define DG_ITEM_FIXED   53u
#define DG_TEXT_COLS    (DG_MIN_COLS - 1u - DG_ITEM_FIXED)

#define DG_TX_STOPPED       "Stopped."
#define DG_TX_PERSON        "Needs a person: run alone"
#define DG_TX_I2C           "I2C does not start."
#define DG_TX_ALL           "%u ok, %u failed"
/* mem: MiB, patterns, seconds rounded up. */
#define DG_TX_MEM_NORAM     "No RAM to test."
#define DG_TX_MEM_FAULT     "Bad word at 0x%08x"
#define DG_TX_MEM_OK        "%u MiB, %u patterns, %us"
/* sd */
#define DG_TX_SD_NOCARD     "No card."
#define DG_TX_SD_NOSTART    "The card does not start."
#define DG_TX_SD_NOVOL      "No boot volume."
#define DG_TX_SD_NODIR      "No /ROBCO folder"
#define DG_TX_SD_DIRERR     "/ROBCO read error %d"
#define DG_TX_SD_BLOCKS     "%u of %u blocks bad"
#define DG_TX_SD_BAD        "%.12s: %s"
#define DG_TX_SD_WRITES     "%u writes refused"
#define DG_TX_SD_OK         "%u blocks, %u images OK"
/* video */
#define DG_TX_VID_NOFB      "No screen."
#define DG_TX_VID_MBOX      "Mailbox does not answer."
#define DG_TX_VID_MODE      "Mode differs from screen"
#define DG_TX_VID_OK        "%ux%u, %u to judge"
/* audio */
#define DG_TX_AUD_INIT      "Audio does not start: %d."
#define DG_TX_AUD_LONG      "The %s ran too long."
#define DG_TX_AUD_TONE      "Tone %u Hz failed: %d."
#define DG_TX_AUD_TUNE      "The tune failed: %d."
#define DG_TX_AUD_OK        "%s, %u tones, %u notes"
/* input */
#define DG_TX_IN_OK         "%u events, exit %s"
/* timer */
#define DG_TX_TMR_STOP      "Tick stopped at tick %u"
#define DG_TX_TMR_OK        "%d ppm, %u missed"
/* rng */
#define DG_TX_RNG_NONE      "No generator."
#define DG_TX_RNG_READ      "No data after %u words."
#define DG_TX_RNG_OK        "%u repeats, %u ones"
/* rtc */
#define DG_TX_RTC_ABSENT    "No clock chip at 0x%02x."
#define DG_TX_RTC_INIT      "Clock chip error %d."
#define DG_TX_RTC_HALTED    "Clock halted: time invalid"
#define DG_TX_RTC_READ      "Time read error %d."
#define DG_TX_RTC_OK        "%s UTC"
/* expander */
#define DG_TX_X_OK          "%u of 2 chips present"
#define DG_TX_X_ERR         "%u of 2 chips, read errors"

void dg_line_title(char *buf, size_t size, const char *version);
const char *dg_line_sub(void);
const char *dg_line_rule(void);
const char *dg_line_help(void);

/*
 * One menu row: the selection mark, the key, the name, what it does, then
 * the last result and its detail when the row ran. item is 0 to
 * DG_ITEMS - 1; result DG_NONE for a row not run.
 */
void dg_line_item(char *buf, size_t size, unsigned item, int selected, int result,
                  const char *text);

/* The status row after one test: "MEM: OK. <text>". */
void dg_line_status_one(char *buf, size_t size, unsigned test, int result, const char *text);
/* The status row after "run all". */
void dg_line_status_all(char *buf, size_t size, const dg_counts_t *c);
/* The status row before any test. */
const char *dg_line_status_idle(void);

/* After "ev=": "diag name=<name> result=<r>" and the keys, if any. */
void dg_line_diag(char *buf, size_t size, const char *name, int result, const char *keys);
/* After "ev=": "diag_done ok=<n> fail=<n> absent=<n> skipped=<n>". */
void dg_line_done(char *buf, size_t size, const dg_counts_t *c);

/* Upper-case name of a menu row, "MEM" to "REBOOT". */
const char *dg_item_title(unsigned item);

/* ---- Live views ---- */

/*
 * One row of the expander view: pin `pin` of both chips. have0 and have1
 * say whether each chip answered; bits0 and bits1 are their input ports.
 */
void dg_line_xrow(char *buf, size_t size, unsigned pin, int have0, uint8_t bits0, int have1,
                  uint8_t bits1);
/* The registers of one chip, or that it is absent. */
void dg_line_xregs(char *buf, size_t size, unsigned chip, unsigned addr, int present,
                   uint8_t in, uint8_t out, uint8_t pol, uint8_t cfg);

/* One button of the GPIO backend: its key name and its state. */
void dg_line_button(char *buf, size_t size, const char *key, int pressed);
/* One input event: its source, type, code and value. */
void dg_line_event(char *buf, size_t size, const input_event_t *ev);
/* Name of a key code as the script backend's key command spells it, "unknown" past the table. */
const char *dg_key_name(unsigned code);
/* Source of an event for markers and the screen: "gpio", "serial", "script" or "other". */
const char *dg_source_name(unsigned source);

/* ---- Progress ---- */

/* At most one progress line per period. The first call is due. */
typedef struct {
    uint32_t period_ms;
    uint32_t last_ms;
    int started;
} dg_rate_t;

void dg_rate_init(dg_rate_t *r, uint32_t period_ms);
/* 1 when a line is due at now_ms, and starts a new period. Survives timer wrap. */
int dg_rate_due(dg_rate_t *r, uint32_t now_ms);

/* Percent of done in total, 0 to 100, with no overflow. */
unsigned dg_percent(uint64_t done, uint64_t total);

/* ---- Verdicts ---- */

/*
 * The timer test. `ticks` tick periods at `hz` took `timer_us` on the
 * system timer. Stores the error in parts per million in *ppm. The limit
 * is one tick period over the window: 1000000 / ticks ppm. A missed tick
 * fails. Returns DG_OK or DG_FAIL, or DG_SKIPPED when ticks or hz is 0.
 */
int dg_timer_judge(uint64_t timer_us, uint32_t ticks, uint32_t hz, uint32_t missed,
                   int32_t *ppm, uint32_t *limit_ppm);

/*
 * The RNG sanity check on n words: no word equals the one before, and
 * the count of set bits is within n / 2 of half the bits. Not a test of
 * randomness. Stores the repeats and the set bits. Returns DG_OK or
 * DG_FAIL; DG_SKIPPED for n below 2.
 */
int dg_rng_judge(const uint32_t *w, uint32_t n, uint32_t *repeats, uint32_t *bits);

#endif
