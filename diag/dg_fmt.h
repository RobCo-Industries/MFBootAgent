/*
 * Results, marker tails, screen lines, the progress rate and the timer
 * and RNG verdicts. Pure: the host tests link them.
 */
#ifndef DG_FMT_H
#define DG_FMT_H

#include <stdarg.h>
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
/*
 * The result of a whole run: DG_FAIL when a test failed, else DG_SKIPPED
 * when ESC stopped the run, else DG_ABSENT when no test gave OK, else DG_OK.
 */
int dg_counts_result(const dg_counts_t *c, int stopped);

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
/* The test keys of a result marker: its short keys, then one encoded text. */
#define DG_KEYS_MAX     448u

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
#define DG_TX_MEM_TOP       "Boot info RAM past mailbox"
#define DG_TX_MEM_FAULT     "Bad word at 0x%08x"
#define DG_TX_MEM_OK        "%u MiB, %u patterns, %us"
/* sd */
#define DG_TX_SD_NOCARD     "No card."
#define DG_TX_SD_NOSTART    "The card does not start."
#define DG_TX_SD_NOVOL      "No boot volume."
#define DG_TX_SD_CID        "Card ID all 0s or all 1s"
#define DG_TX_SD_NODIR      "No /ROBCO folder"
#define DG_TX_SD_DIRERR     "/ROBCO read error %d"
#define DG_TX_SD_BLOCKS     "Bad %u blk, %u chunks"
#define DG_TX_SD_CHUNKS     "%u chunk reads failed"
#define DG_TX_SD_BAD        "%.12s: %s"
#define DG_TX_SD_WRITES     "%u writes refused"
#define DG_TX_SD_OK         "%u blocks, %u images OK"
/* video */
#define DG_TX_VID_NOFB      "No screen."
#define DG_TX_VID_MBOX      "Mailbox does not answer."
#define DG_TX_VID_MODE      "Mode differs from screen"
#define DG_TX_VID_ORDER     "Pixel order differs"
#define DG_TX_VID_OFFSET    "Screen offset is not 0"
#define DG_TX_VID_OK        "%ux%u, %u to judge"
/* audio */
#define DG_TX_AUD_INIT      "Audio does not start: %d."
#define DG_TX_AUD_PWM       "No PWM, trace only: %d."
#define DG_TX_AUD_LONG      "The %s ran too long."
#define DG_TX_AUD_TONE      "Tone %u Hz failed: %d."
#define DG_TX_AUD_TUNE      "The tune failed: %d."
#define DG_TX_AUD_OK        "%s, %u tones, %u notes"
#define DG_TX_AUD_NONE      "No sound output found."
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

/*
 * The expander test's attach step for a chip whose four registers read
 * back. gpiox_attach() writes the polarity register when it is not 0, so
 * a chip with polarity not 0 is never attached: DG_X_RAW, read raw, not
 * an error. Else attach(chip, addr) runs: DG_X_ATTACHED on 0, else
 * DG_X_ATTACH_ERR with its result in *rc, a bus error the test counts.
 */
#define DG_X_RAW            0
#define DG_X_ATTACHED       1
#define DG_X_ATTACH_ERR     2
int dg_x_attach(uint8_t pol, int (*attach)(unsigned, uint8_t), unsigned chip, uint8_t addr,
                int *rc);

/*
 * 1 when all four registers of a chip that answered read 0xff. A fresh
 * PCA9554 holds polarity 0 and gpiox_attach() writes 0 there, so a
 * polarity of 0xff with every other register 0xff means a data line held
 * high after the address byte: a bus fault, not pin levels. All zeros
 * stays information: every pin an output driven low reads that way.
 */
int dg_x_stuck(const uint8_t reg[4]);

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

/* ---- 64-bit numbers ---- */

/*
 * The library's %u takes 32 bits, so a size, a byte count or a block
 * count that can pass 4 GiB goes through dg_u64() and %s. DG_U64_SIZE
 * holds the 20 digits of UINT64_MAX and the end.
 */
#define DG_U64_SIZE 21u
/* Writes v in decimal into out and returns out. */
const char *dg_u64(char out[DG_U64_SIZE], uint64_t v);

/* ---- Result keys ---- */

/*
 * The key a result marker ends with when keys did not fit its buffer.
 * dg_keys_add() always keeps room for it, so a marker that lost keys
 * says so and never looks whole.
 */
#define DG_KEYS_CUT     "keys_cut=1"

/*
 * Appends one key, or several, formatted from fmt, to the keys in buf of
 * `size` bytes, with a space between keys. A key that does not fit with
 * room left for " " DG_KEYS_CUT is dropped whole and DG_KEYS_CUT ends
 * the keys; every later key is dropped too. size must exceed 2 +
 * sizeof(DG_KEYS_CUT).
 */
void dg_keys_add(char *buf, size_t size, const char *fmt, va_list ap);

/* ---- Text from the card or a person ---- */

/*
 * A marker value is one run of printable characters with no space:
 * BOOT_PROTOCOL.md's VCHAR except SP. Every marker value that carries
 * text from the card or a person, such as a path or a file name, goes
 * through dg_mval(), the one encoder of both images:
 *   - bytes 0x21 to 0x7E stay, except '%' and '=';
 *   - every other byte, '%', '=', a space, a control byte or a byte
 *     above 0x7E, becomes %XX, two upper-case hex digits;
 *   - an empty text is "%00";
 *   - a value that does not fit is cut after a whole character or
 *     escape and ends with a lone '%', which no whole value can.
 * A reader splits the line at spaces, each key at its first '=', and
 * decodes each %XX. DG_MVAL_SIZE holds an encoded value and its end.
 */
#define DG_MVAL_SIZE    128u
/* Encodes `in` into out, of size at least 4, and returns out. */
const char *dg_mval(char *out, size_t size, const char *in);

/* ---- Marker sizes ---- */

/*
 * The one rule of every marker with text from the card:
 *   - the short keys come first: rc, fs_rc, result, why, counts;
 *   - the one encoded text comes last, so a cut can only shorten it;
 *   - a cut is marked: a lone '%' ends a cut text, DG_KEYS_CUT ends keys
 *     that lost a key;
 *   - buffers are sized from these constants, and a _Static_assert ties
 *     each one to the longest text it can get.
 */
#define DG_STR_LEN(s)       (sizeof(s) - 1u)
#define DG_INT_CHARS        11u     /* "-2147483648" */
#define DG_UINT_CHARS       10u     /* "4294967295" */
#define DG_MVAL_MAX         (DG_MVAL_SIZE - 1u)
/* " <key>=" and a number or an encoded text: the most a key adds. */
#define DG_KEY_INT(key)     (DG_STR_LEN(" " key "=") + DG_INT_CHARS)
#define DG_KEY_UINT(key)    (DG_STR_LEN(" " key "=") + DG_UINT_CHARS)
#define DG_KEY_TEXT(key)    (DG_STR_LEN(" " key "=") + DG_MVAL_MAX)
/*
 * Raw text for dg_keys_text(): one byte more than an encoded value holds,
 * so a text cut to this size still encodes past DG_MVAL_MAX and dg_mval()
 * marks the cut.
 */
#define DG_TEXT_RAW_SIZE    (DG_MVAL_SIZE + 1u)

/*
 * Appends " <key>=<text>" to the keys in buf of `size` bytes, the text
 * encoded by dg_mval() into the room left: a text cut there ends with a
 * lone '%'. When not even "<key>=" and one escape fit, DG_KEYS_CUT ends
 * the keys, if it fits. Keys that already end with DG_KEYS_CUT stay.
 * The text key goes last.
 */
void dg_keys_text(char *buf, size_t size, const char *key, const char *text);

/*
 * A marker line formatted into buf of `size` bytes, `len` the length
 * k_vsnprintf() returned. A line that did not fit loses whole keys from
 * its end, and DG_KEYS_CUT ends it. The sizes make this a backstop:
 * no marker reaches it.
 */
void dg_mark_fit(char *buf, size_t size, int len);

/*
 * One marker after "ev=". k_printf() streams it to the console and the
 * UART byte by byte with no buffer of its own, so this buffer is the
 * only limit of a marker line. It holds the result marker with the
 * longest name and result, and every key.
 */
#define DG_MARK_SIZE        (DG_LINE_MAX + DG_KEYS_MAX)
#define DG_DIAG_HEAD_MAX    DG_STR_LEN("diag name=expander result=skipped ")
_Static_assert(DG_MARK_SIZE >= DG_DIAG_HEAD_MAX + DG_KEYS_MAX, "diag marker buffer");

/*
 * A name for a screen field of `width` columns, 4 or more: bytes 0x20 to
 * 0x7E stay, every other byte shows as '?'. A longer name shows its
 * first width - 3 characters and "...". Returns out, of size > width.
 */
const char *dg_sname(char *out, size_t size, const char *in, unsigned width);

/* Every byte of `text` outside 0x20 to 0x7E becomes '?', in place: a row stays one row. */
void dg_printable(char *text);

/* ---- Verdicts ---- */

/*
 * The timer test. `ticks` tick periods at `hz` took `timer_us` on the
 * system timer. Stores the error in parts per million in *ppm. The limit
 * is one tick period over the window: 1000000 / ticks ppm. A missed tick
 * fails, and so does a tick rate hz other than the rate asked: a tick that
 * keeps the wrong rate is a fault the ppm alone cannot see. Returns DG_OK
 * or DG_FAIL, or DG_SKIPPED when ticks or hz is 0.
 */
int dg_timer_judge(uint64_t timer_us, uint32_t ticks, uint32_t asked_hz, uint32_t hz,
                   uint32_t missed, int32_t *ppm, uint32_t *limit_ppm);

/*
 * The RNG sanity check on n words: no word equals the one before, and
 * the count of set bits is within n / 2 of half the bits. Not a test of
 * randomness. Stores the repeats and the set bits. Returns DG_OK or
 * DG_FAIL; DG_SKIPPED for n below 2.
 */
int dg_rng_judge(const uint32_t *w, uint32_t n, uint32_t *repeats, uint32_t *bits);

/*
 * The video test: the mode the mailbox reads against the framebuffer in
 * use. Width, height, depth and pitch must match, then the pixel order:
 * fb_rgb() draws with the framebuffer's order, so a different firmware
 * order swaps red and blue. Then the virtual offset must be 0, when the
 * mailbox gave it (has_offset), or the screen shows another part of the
 * buffer than the test draws. Returns DG_VID_*, the first that differs.
 */
typedef struct {
    uint32_t w, h, depth, pitch, order;
    int has_offset;
    uint32_t x_off, y_off;
} dg_vmode_t;

#define DG_VID_OK       0
#define DG_VID_MODE     1
#define DG_VID_ORDER    2
#define DG_VID_OFFSET   3
int dg_video_judge(const dg_vmode_t *mode, const dg_vmode_t *fb);

/*
 * The audio test. dg_audio_start() reads the audio_init() code before any
 * sound: DG_AUD_NOINIT when audio cannot play at all, else DG_AUD_PLAY.
 * dg_audio_judge() gives the verdict of a run whose tones and tune all
 * played in time. trace is 1 when audio_backend() is "trace".
 *   DG_AUD_OK       init 0 and the PWM backend played: OK.
 *   DG_AUD_FELLBACK init AUDIO_ERR_NO_PWM or AUDIO_ERR_PIN: the PWM was
 *                   asked for and failed, the trace played: FAILED.
 *   DG_AUD_TRACE    init 0 and the trace backend played: no PWM was found,
 *                   so no sound could come out: ABSENT.
 * dg_audio_result() maps a DG_AUD_* to DG_OK, DG_FAIL or DG_ABSENT.
 */
#define DG_AUD_OK       0
#define DG_AUD_PLAY     DG_AUD_OK
#define DG_AUD_NOINIT   1
#define DG_AUD_FELLBACK 2
#define DG_AUD_TRACE    3
int dg_audio_start(int init_rc);
int dg_audio_judge(int init_rc, int trace);
int dg_audio_result(int verdict);

#endif
