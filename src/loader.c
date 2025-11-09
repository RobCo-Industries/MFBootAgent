// src/loader.c - Kernel loading and boot

#include "loader.h"
#include "filesystem.h"
#include "terminal.h"
#include "hardware.h"
#include "protocols.h"

extern void jump_to_kernel_asm(uint32_t addr, uint32_t r0, uint32_t r1, uint32_t r2);

int load_kernel(boot_entry_t* entry) {
    term_printf("Opening file: %s\n", entry->path);
    
    file_handle_t* fh = fs_open(entry->path);
    if (!fh) {
        term_print("ERROR: Cannot open kernel file\n");
        return -1;
    }
    
    // Load kernel to specified address
    uint8_t* load_addr = (uint8_t*)entry->load_addr;
    term_printf("Loading to address: 0x%08X\n", entry->load_addr);
    
    // Read kernel data
    if (fs_read(fh, load_addr, entry->size) != 0) {
        term_print("ERROR: Failed to read kernel\n");
        fs_close(fh);
        return -1;
    }
    
    fs_close(fh);
    
    term_print("Kernel loaded successfully\n");
    delay_ms(500);
    
    // Setup boot parameters
    boot_params_t params;
    params.machine_type = 0;  // Generic ARM
    params.boot_device = 0;   // SD card
    strcpy(params.cmdline, "console=ttyAMA0,115200 root=/dev/mmcblk0p2 rootwait");
    params.initrd_start = 0;
    params.initrd_size = 0;
    
    term_print("Jumping to kernel...\n\n");
    delay_ms(500);
    
    // Jump to kernel
    jump_to_kernel(entry->load_addr, 0, 0, 0);
    
    return 0;
}

int verify_signature(boot_entry_t* entry) {
    (void)entry;
    // Signature verification would be implemented here
    // For now, always return success
    return 0;
}

void jump_to_kernel(uint32_t addr, uint32_t r0, uint32_t r1, uint32_t atags) {
    // Call assembly wrapper
    jump_to_kernel_asm(addr, r0, r1, atags);
}
