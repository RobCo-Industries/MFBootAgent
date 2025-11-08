#ifndef PROTOCOLS_H
#define PROTOCOLS_H

#include <stdint.h>

// Boot protocol types
#define PROTOCOL_LINUX_ARM  0x01
#define PROTOCOL_TERMLINK   0x02

// ATAGS structures (simplified for ARM Linux boot protocol)
#define ATAG_NONE       0x00000000
#define ATAG_CORE       0x54410001
#define ATAG_MEM        0x54410002
#define ATAG_VIDEOTEXT  0x54410003
#define ATAG_RAMDISK    0x54410004
#define ATAG_INITRD2    0x54420005
#define ATAG_SERIAL     0x54410006
#define ATAG_REVISION   0x54410007
#define ATAG_VIDEOLFB   0x54410008
#define ATAG_CMDLINE    0x54410009

typedef struct {
    uint32_t size;
    uint32_t tag;
} atag_header_t;

typedef struct {
    uint32_t flags;
    uint32_t pagesize;
    uint32_t rootdev;
} atag_core_t;

typedef struct {
    uint32_t size;
    uint32_t start;
} atag_mem_t;

typedef struct {
    char cmdline[1];
} atag_cmdline_t;

typedef struct {
    atag_header_t hdr;
    union {
        atag_core_t core;
        atag_mem_t mem;
        atag_cmdline_t cmdline;
    } u;
} atag_t;

// Boot parameter structure
typedef struct {
    uint32_t machine_type;
    uint32_t boot_device;
    char cmdline[256];
    uint32_t initrd_start;
    uint32_t initrd_size;
} boot_params_t;

// Function declarations
void setup_boot_params(boot_params_t* params);
atag_t* create_atags(uint32_t mem_size, const char* cmdline);

#endif // PROTOCOLS_H
