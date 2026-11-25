/*
 * The video test. Reads the mode from the mailbox, then draws full-screen
 * patterns. Alone, a key steps to the next pattern and ESC stops; each
 * pattern waits at most STEP_MAX_MS. In "run all" each pattern shows for
 * ALL_MS. The test reads no pixel back. Only a person can judge the
 * picture: "ok" means the mode reads back, matches the framebuffer in
 * use in size, depth, pitch and pixel order, shows the buffer from its
 * start, and every pattern was drawn.
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

/* Mailbox property tags, all "get": none changes the mode. */
#define TAG_PHYS_WH 0x00040003u
#define TAG_VIRT_WH 0x00040004u
#define TAG_DEPTH   0x00040005u
#define TAG_ORDER   0x00040006u
#define TAG_ALPHA   0x00040007u
#define TAG_PITCH   0x00040008u
#define TAG_OFFSET  0x00040009u
/* Set in a tag's code word when the firmware answered that tag. */
#define TAG_ANSWERED 0x80000000u

typedef struct {
    dg_vmode_t v;
    int has_virt, has_alpha;
    uint32_t vw, vh, alpha;
} mode_t_;

/* Appends a tag with n value words. Returns the index of its first value. */
static unsigned tag(uint32_t *b, unsigned *i, uint32_t id, unsigned n)
{
    unsigned at;

    b[(*i)++] = id;
    b[(*i)++] = n * 4u;
    b[(*i)++] = 0;
    at = *i;
    while (n-- > 0) {
        b[(*i)++] = 0;
    }
    return at;
}

static int answered(const uint32_t *b, unsigned at)
{
    return (b[at - 1u] & TAG_ANSWERED) != 0;
}

static int read_mode(mode_t_ *m)
{
    static uint32_t b[48] __attribute__((aligned(64)));
    unsigned i = 2;
    unsigned wh, virt, depth, order, alpha, pitch, off;

    k_memset(b, 0, sizeof(b));
    b[0] = sizeof(b);
    wh = tag(b, &i, TAG_PHYS_WH, 2);
    virt = tag(b, &i, TAG_VIRT_WH, 2);
    depth = tag(b, &i, TAG_DEPTH, 1);
    order = tag(b, &i, TAG_ORDER, 1);
    alpha = tag(b, &i, TAG_ALPHA, 1);
    pitch = tag(b, &i, TAG_PITCH, 1);
    off = tag(b, &i, TAG_OFFSET, 2);
    b[i] = 0;
    if (mbox_call(b) != 0) {
        return -1;
    }
    k_memset(m, 0, sizeof(*m));
    m->v.w = b[wh];
    m->v.h = b[wh + 1u];
    m->v.depth = b[depth];
    m->v.order = b[order];
    m->v.pitch = b[pitch];
    m->v.has_offset = answered(b, off);
    m->v.x_off = b[off];
    m->v.y_off = b[off + 1u];
    m->has_virt = answered(b, virt);
    m->vw = b[virt];
    m->vh = b[virt + 1u];
    m->has_alpha = answered(b, alpha);
    m->alpha = b[alpha];
    return 0;
}

static const char *order_name(uint32_t order)
{
    return order == FB_ORDER_RGB ? "rgb" : order == FB_ORDER_BGR ? "bgr" : "other";
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
    dg_vmode_t want;
    unsigned p;
    int rc;

    if (!dg.fb_ok || fb == 0) {
        r->result = DG_ABSENT;
        dg_keys(r, "why=nofb");
        dg_text(r, DG_TX_VID_NOFB);
        return;
    }
    rc = read_mode(&m);
    if (rc != 0) {
        r->result = DG_FAIL;
        dg_keys(r, "why=mbox");
        dg_text(r, DG_TX_VID_MBOX);
        return;
    }
    dg_keys(r, "width=%u height=%u depth=%u pitch=%u order=%s", (unsigned)m.v.w,
            (unsigned)m.v.h, (unsigned)m.v.depth, (unsigned)m.v.pitch, order_name(m.v.order));
    /* Information only: see "What each test checks" in docs/DIAG.md. */
    if (m.has_virt) {
        dg_keys(r, "virt=%ux%u", (unsigned)m.vw, (unsigned)m.vh);
    }
    if (m.v.has_offset) {
        dg_keys(r, "offset=%u,%u", (unsigned)m.v.x_off, (unsigned)m.v.y_off);
    }
    if (m.has_alpha) {
        dg_keys(r, "alpha=%u", (unsigned)m.alpha);
    }
    k_memset(&want, 0, sizeof(want));
    want.w = fb->width;
    want.h = fb->height;
    want.depth = fb->depth;
    want.pitch = fb->pitch;
    want.order = fb->order;
    switch (dg_video_judge(&m.v, &want)) {
    case DG_VID_MODE:
        r->result = DG_FAIL;
        dg_keys(r, "why=mismatch fb_width=%u fb_height=%u fb_depth=%u fb_pitch=%u",
                (unsigned)fb->width, (unsigned)fb->height, (unsigned)fb->depth,
                (unsigned)fb->pitch);
        dg_text(r, DG_TX_VID_MODE);
        return;
    case DG_VID_ORDER:
        r->result = DG_FAIL;
        dg_keys(r, "why=order mode_order=%u fb_order=%u", (unsigned)m.v.order,
                (unsigned)fb->order);
        dg_text(r, DG_TX_VID_ORDER);
        return;
    case DG_VID_OFFSET:
        r->result = DG_FAIL;
        dg_keys(r, "why=offset");
        dg_text(r, DG_TX_VID_OFFSET);
        return;
    default:
        break;
    }
    for (p = 0; p < P_COUNT; p++) {
        draw(p, fb->width, fb->height);
        dg_mark("video_pattern name=%s width=%u height=%u", names[p], (unsigned)fb->width,
                (unsigned)fb->height);
        if (step_wait(r->mode == DG_MODE_ALL)) {
            r->result = DG_SKIPPED;
            dg_keys(r, "why=abort pattern=%s", names[p]);
            dg_text(r, DG_TX_STOPPED);
            return;
        }
    }
    r->result = DG_OK;
    dg_keys(r, "patterns=%u judge=person", P_COUNT);
    dg_text(r, DG_TX_VID_OK, (unsigned)fb->width, (unsigned)fb->height, P_COUNT);
}
