/*
 * The expander test: the two PCA9554 of the terminal controller board at
 * 0x20 and 0x21 on I2C1. STRICTLY READ-ONLY: relays and door locks may
 * hang on these pins. This file never calls gpiox_config(),
 * gpiox_write() or gpiox_write_port(), and never writes a register.
 *
 * Each chip's four registers are read first, with a one-byte register
 * pointer and a read. gpiox_attach() writes the polarity register when
 * it is not 0, so the test attaches a chip only when its polarity reads
 * 0: then the attach writes nothing. With polarity set it keeps to the
 * raw reads. Alone, a live view shows each input line until ESC, or
 * IDLE_MS with no change and no key.
 */

#include <stdint.h>

#include <robco/gpiox.h>
#include <robco/i2c.h>
#include <robco/klibc.h>
#include <robco/timer.h>

#include "dg.h"

#define CHIPS       2u
#define IDLE_MS     60000u
#define POLL_MS     50u
#define ROW_PINS    2u

static const uint8_t addrs[CHIPS] = { 0x20, 0x21 };

typedef struct {
    int present;
    int attached;
    int attach;         /* DG_X_RAW, DG_X_ATTACHED or DG_X_ATTACH_ERR */
    int rc;             /* the first failed read's result, else 0 */
    int errors;         /* reads that failed after the chip answered, or not with a NACK */
    int stuck;          /* every register read 0xff: dg_x_stuck() */
    uint8_t reg[4];     /* input, output, polarity, configuration */
} chip_t;

static chip_t chips[CHIPS];

/* Reads one register: the pointer write selects it, nothing is stored. */
static int read_reg(uint8_t addr, uint8_t reg, uint8_t *val)
{
    return i2c_write_read(addr, &reg, 1, val, 1);
}

static void probe(unsigned c)
{
    chip_t *x = &chips[c];
    unsigned i;
    int rc = 0;

    k_memset(x, 0, sizeof(*x));
    for (i = 0; i < 4 && rc == 0; i++) {
        rc = read_reg(addrs[c], (uint8_t)i, &x->reg[i]);
    }
    x->rc = rc;
    if (rc != 0) {
        /* Only a NACK of the first read means no chip. Anything else is a fault. */
        if (i != 1 || rc != I2C_ERR_NACK) {
            x->errors++;
        }
        return;
    }
    x->present = 1;
    if (dg_x_stuck(x->reg)) {
        x->stuck = 1;
        x->errors++;
        dg_mark("expander_err chip=%u at=stuck", c);
        return;
    }
    /* A failed attach is a bus error. Polarity not 0 is the raw-read path, not an error. */
    x->attach = dg_x_attach(x->reg[GPIOX_REG_POLARITY], gpiox_attach, c, addrs[c], &rc);
    x->attached = x->attach == DG_X_ATTACHED;
    if (x->attach == DG_X_ATTACH_ERR) {
        x->rc = rc;
        x->errors++;
        dg_mark("expander_err chip=%u at=attach rc=%d", c, rc);
    }
}

/* The input port: through the driver when attached, else a raw read. */
static int read_in(unsigned c, uint8_t *bits)
{
    if (chips[c].attached) {
        return gpiox_read_port(c, bits);
    }
    return read_reg(addrs[c], GPIOX_REG_INPUT, bits);
}

static void draw_pins(void)
{
    char line[DG_LINE_MAX];
    unsigned p;

    for (p = 0; p < GPIOX_PINS; p++) {
        dg_line_xrow(line, sizeof(line), p, chips[0].present, chips[0].reg[0], chips[1].present,
                     chips[1].reg[0]);
        dg_work_line(ROW_PINS + p, line, 0);
    }
}

/* The live view. Returns its exit reason for the marker. */
static const char *live(void)
{
    input_event_t ev;
    uint32_t last = timer_now_ms();
    unsigned c;

    dg_work_line(DG_WORK_ROWS - 1, "ESC leaves. One minute with no change also leaves.", 1);
    draw_pins();
    for (;;) {
        int changed = 0;

        if (dg_wait_event(&ev, POLL_MS)) {
            if (dg_is_abort(&ev)) {
                return "back";
            }
            last = timer_now_ms();
        }
        for (c = 0; c < CHIPS; c++) {
            uint8_t bits;

            if (!chips[c].present) {
                continue;
            }
            if (read_in(c, &bits) != 0) {
                /* A chip that stops answering is a fault, never a quiet line. */
                if (chips[c].errors++ == 0) {
                    dg_mark("expander_err chip=%u", c);
                }
                continue;
            }
            if (bits == chips[c].reg[0]) {
                continue;
            }
            chips[c].reg[0] = bits;
            changed = 1;
            dg_mark("expander_in chip=%u bits=0x%02x", c, bits);
        }
        if (changed) {
            draw_pins();
            last = timer_now_ms();
        }
        if (timer_now_ms() - last >= IDLE_MS) {
            return "idle";
        }
    }
}

void dg_test_expander(dg_run_t *r)
{
    char line[DG_LINE_MAX];
    unsigned present = 0;
    unsigned errors = 0;
    unsigned c;
    int rc = dg_i2c();

    if (rc != 0) {
        r->result = DG_FAIL;
        dg_keys(r, "why=i2c rc=%d", rc);
        dg_text(r, DG_TX_I2C);
        return;
    }
    for (c = 0; c < CHIPS; c++) {
        chip_t *x = &chips[c];

        probe(c);
        dg_line_xregs(line, sizeof(line), c, addrs[c], x->present, x->reg[0], x->reg[1],
                      x->reg[2], x->reg[3]);
        dg_work_line(c, line, 1);
        if (x->present) {
            present++;
            dg_mark("expander chip=%u addr=0x%02x present=1 in=0x%02x out=0x%02x pol=0x%02x "
                    "cfg=0x%02x attached=%d", c, addrs[c], x->reg[0], x->reg[1], x->reg[2],
                    x->reg[3], x->attached);
            dg_keys(r, "x%u=0x%02x in%u=0x%02x out%u=0x%02x pol%u=0x%02x cfg%u=0x%02x", c,
                    addrs[c], c, x->reg[0], c, x->reg[1], c, x->reg[2], c, x->reg[3]);
            if (x->stuck) {
                dg_keys(r, "stuck%u=1", c);
            }
            if (x->attach == DG_X_ATTACH_ERR) {
                dg_keys(r, "attach%u=error rc%u=%d", c, c, x->rc);
            }
        } else {
            dg_mark("expander chip=%u addr=0x%02x present=0 rc=%d", c, addrs[c], x->rc);
            if (x->errors == 0) {
                dg_keys(r, "x%u=absent", c);
            } else {
                dg_keys(r, "x%u=error rc%u=%d", c, c, x->rc);
            }
        }
    }
    if (present != 0 && r->mode == DG_MODE_ONE) {
        dg_keys(r, "view=%s", live());
    }
    for (c = 0; c < CHIPS; c++) {
        if (chips[c].attached) {
            /* Forgets the chip; its pins stay as they are. */
            (void)gpiox_detach(c);
        }
    }
    for (c = 0; c < CHIPS; c++) {
        errors += (unsigned)chips[c].errors;
    }
    if (errors != 0) {
        r->result = DG_FAIL;
        dg_keys(r, "errors=%u", errors);
        dg_text(r, DG_TX_X_ERR, present);
        return;
    }
    r->result = present != 0 ? DG_OK : DG_ABSENT;
    dg_text(r, DG_TX_X_OK, present);
}
