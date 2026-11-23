/*
 * The video test. Reads the mode from the mailbox, then draws full-screen
 * patterns. Alone, a key steps to the next pattern and ESC stops; each
 * pattern waits at most STEP_MAX_MS. In "run all" each pattern shows for
 * ALL_MS. Only a person can judge the picture: "ok" means the mode reads
 * back and every pattern was drawn.
 */

#include <stdint.h>

#include <robco/fb.h>
#include <robco/klibc.h>
#include <robco/mbox.h>
#include <robco/timer.h>

#include "dg.h"

#define ALL_MS      300u
#define STEP_MAX_MS 30000u
#define GRID_STEP   40u

enum { P_BARS, P_GRID, P_RAMP, P_RED, P_GREEN, P_BLUE, P_WHITE, P_BLACK, P_COUNT };

static const char *const names[P_COUNT] = {
    "bars", "grid", "ramp", "red", "green", "blue", "white", "black",
};

/* Mailbox property tags. */
#define TAG_PHYS_WH 0x00040003u
#define TAG_DEPTH   0x00040005u
#define TAG_ORDER   0x00040006u
#define TAG_PITCH   0x00040008u

typedef struct {
    uint32_t w, h, depth, order, pitch;
} mode_t_;

static int read_mode(mode_t_ *m)
{
    static uint32_t b[20] __attribute__((aligned(64)));
    unsigned i = 0;

    b[i++] = sizeof(b);
    b[i++] = 0;
    b[i++] = TAG_PHYS_WH; b[i++] = 8; b[i++] = 0; b[i++] = 0; b[i++] = 0;
    b[i++] = TAG_DEPTH;   b[i++] = 4; b[i++] = 0; b[i++] = 0;
    b[i++] = TAG_ORDER;   b[i++] = 4; b[i++] = 0; b[i++] = 0;
    b[i++] = TAG_PITCH;   b[i++] = 4; b[i++] = 0; b[i++] = 0;
    b[i++] = 0;
    if (mbox_call(b) != 0) {
        return -1;
    }
    m->w = b[5];
    m->h = b[6];
    m->depth = b[10];
    m->order = b[14];
    m->pitch = b[18];
    return 0;
}

static void draw(unsigned p, uint32_t w, uint32_t h)
{
    static const uint8_t bars[8][3] = {
        { 255, 255, 255 }, { 255, 255, 0 }, { 0, 255, 255 }, { 0, 255, 0 },
        { 255, 0, 255 }, { 255, 0, 0 }, { 0, 0, 255 }, { 0, 0, 0 },
    };
    uint32_t white = fb_rgb(255, 255, 255);
    uint32_t x, y;

    switch (p) {
    case P_BARS:
        for (x = 0; x < 8; x++) {
            uint32_t x0 = w * x / 8u;
            uint32_t x1 = w * (x + 1u) / 8u;

            fb_fill_rect(x0, 0, x1 - x0, h, fb_rgb(bars[x][0], bars[x][1], bars[x][2]));
        }
        break;
    case P_GRID:
        fb_clear(fb_rgb(0, 0, 0));
        for (x = 0; x < w; x += GRID_STEP) {
            fb_fill_rect(x, 0, 1, h, white);
        }
        for (y = 0; y < h; y += GRID_STEP) {
            fb_fill_rect(0, y, w, 1, white);
        }
        /* The one-pixel border on all four edges. */
        fb_fill_rect(0, 0, w, 1, white);
        fb_fill_rect(0, h - 1u, w, 1, white);
        fb_fill_rect(0, 0, 1, h, white);
        fb_fill_rect(w - 1u, 0, 1, h, white);
        break;
    case P_RAMP:
        for (x = 0; x < w; x++) {
            uint8_t g = (uint8_t)(w > 1 ? x * 255u / (w - 1u) : 0);

            fb_fill_rect(x, 0, 1, h, fb_rgb(g, g, g));
        }
        break;
    case P_RED:   fb_clear(fb_rgb(255, 0, 0)); break;
    case P_GREEN: fb_clear(fb_rgb(0, 255, 0)); break;
    case P_BLUE:  fb_clear(fb_rgb(0, 0, 255)); break;
    case P_WHITE: fb_clear(white); break;
    default:      fb_clear(fb_rgb(0, 0, 0)); break;
    }
}

/* Waits for the next step. Returns 1 when ESC stopped the test. */
static int step_wait(int all)
{
    input_event_t ev;
    uint32_t t0 = timer_now_ms();
    uint32_t limit = all ? ALL_MS : STEP_MAX_MS;

    while (timer_now_ms() - t0 < limit) {
        if (!dg_wait_event(&ev, 20)) {
            continue;
        }
        if (dg_is_abort(&ev)) {
            return 1;
        }
        if (!all && ((ev.type == INPUT_EV_KEY && ev.value == INPUT_PRESS) ||
                     ev.type == INPUT_EV_CHAR)) {
            return 0;
        }
    }
    return 0;
}

void dg_test_video(dg_run_t *r)
{
    const fb_info_t *fb = fb_get();
    mode_t_ m;
    unsigned p;
    int rc;

    if (!dg.fb_ok || fb == 0) {
        r->result = DG_ABSENT;
        dg_keys(r, "why=nofb");
        dg_text(r, "No screen.");
        return;
    }
    rc = read_mode(&m);
    if (rc != 0) {
        r->result = DG_FAIL;
        dg_keys(r, "why=mbox");
        dg_text(r, "The mailbox does not answer.");
        return;
    }
    dg_keys(r, "width=%u height=%u depth=%u pitch=%u order=%s", (unsigned)m.w, (unsigned)m.h,
            (unsigned)m.depth, (unsigned)m.pitch, m.order == 1 ? "rgb" : "bgr");
    if (m.w != fb->width || m.h != fb->height || m.depth != fb->depth || m.pitch != fb->pitch) {
        r->result = DG_FAIL;
        dg_keys(r, "why=mismatch fb_width=%u fb_height=%u", (unsigned)fb->width,
                (unsigned)fb->height);
        dg_text(r, "Mode differs from the screen in use.");
        return;
    }
    for (p = 0; p < P_COUNT; p++) {
        draw(p, fb->width, fb->height);
        dg_mark("video_pattern name=%s width=%u height=%u", names[p], (unsigned)fb->width,
                (unsigned)fb->height);
        if (step_wait(r->mode == DG_MODE_ALL)) {
            r->result = DG_SKIPPED;
            dg_keys(r, "why=abort pattern=%s", names[p]);
            dg_text(r, "Stopped.");
            return;
        }
    }
    r->result = DG_OK;
    dg_keys(r, "patterns=%u judge=person", P_COUNT);
    dg_text(r, "%ux%u, %u patterns. A person judges them.", (unsigned)fb->width,
            (unsigned)fb->height, P_COUNT);
}
