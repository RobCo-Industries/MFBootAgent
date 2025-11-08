#ifndef TERMLINK_H
#define TERMLINK_H

#include <stdint.h>

// RobCo Termlink Protocol definitions
#define TERMLINK_MAGIC      0x524F4243  // "ROBC"
#define TERMLINK_VERSION    0x00010000  // v1.0

// Terminal capabilities
#define TERM_CAP_SERIAL     (1 << 0)
#define TERM_CAP_FRAMEBUF   (1 << 1)
#define TERM_CAP_AUDIO      (1 << 2)
#define TERM_CAP_GPIO       (1 << 3)

// Terminal state structure
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t capabilities;
    uint32_t fb_addr;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_pitch;
    uint32_t fb_depth;
    uint32_t peripheral_base;
} termlink_info_t;

// Function declarations
void termlink_init(termlink_info_t* info);
void termlink_handoff(termlink_info_t* info);

#endif // TERMLINK_H
