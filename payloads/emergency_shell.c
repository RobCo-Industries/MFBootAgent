// payloads/emergency_shell.c - Fallback emergency shell

#include "mfboot.h"
#include "terminal.h"
#include "hardware.h"

// Emergency shell implementation is in maintenance.c (enter_emergency_mode)
// This file is a placeholder for additional emergency tools

void emergency_memory_dump(uint32_t addr, size_t len) {
    term_printf("Memory dump at 0x%08X:\n", addr);
    
    uint32_t* ptr = (uint32_t*)addr;
    for (size_t i = 0; i < len / 4 && i < 64; i++) {
        if (i % 4 == 0) {
            term_printf("\n0x%08X: ", addr + i * 4);
        }
        term_printf("%08X ", ptr[i]);
    }
    term_print("\n");
}

void emergency_reboot(void) {
    term_print("Initiating emergency reboot...\n");
    delay_ms(1000);
    
    // Trigger watchdog or reset
    // For now, just halt
    while (1) {
        __asm__ volatile("wfe");
    }
}
