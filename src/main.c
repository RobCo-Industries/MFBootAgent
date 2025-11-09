// src/main.c - MF Boot Agent Core

#include "mfboot.h"
#include "terminal.h"
#include "memory_mgr.h"
#include "filesystem.h"
#include "loader.h"
#include "hardware.h"

// Boot entry storage
static boot_entry_t boot_entries[8];
static int num_boot_entries = 0;

void mfboot_main(uint32_t r0, uint32_t r1, uint32_t atags) {
    (void)r0;
    (void)r1;
    (void)atags;
    
    // Initialize terminal from RETROS-BIOS state
    terminal_init();
    
    // Display boot agent header
    term_clear();
    term_print("MF Boot Agent v" MFBOOT_VERSION "\n");
    term_print(COPYRIGHT "\n");
    term_print("LOADER v1.1\n");
    term_print("EXEC VERSION 41.10\n");
    delay_ms(200);
    
    // Initialize memory management
    memory_init();
    
    // Allocate upper memory
    term_print("Initializing Upper Memory: ");
    void* upper_mem = memory_allocate_upper(UPPERMEM_SIZE);
    if (upper_mem) {
        term_print("64 KB\n");
        term_printf("Upper Memory Address: 0x%08X\n", (uint32_t)upper_mem);
    } else {
        term_print("FAILED\n");
        enter_emergency_mode();
    }
    delay_ms(150);
    
    // Initialize filesystem
    term_print("Initializing Filesystem: ");
    if (fs_init() == 0) {
        term_print("OK\n");
    } else {
        term_print("FAILED\n");
        enter_emergency_mode();
    }
    delay_ms(100);
    
    // Scan for boot devices
    term_print("\nScanning for boot devices...\n");
    boot_entry_t* entries = scan_boot_devices();
    
    // Check for holotape override
    if (check_holotape_present()) {
        term_print("*** HOLOTAPE DETECTED ***\n");
        term_print("Loading from external media...\n");
        load_holotape_boot();
        return;
    }
    
    // Display boot menu or auto-boot
    if (gpio_read(BOOT_MENU_PIN) == 0 || get_boot_count() > 1) {
        display_boot_menu(entries);
    } else {
        // Auto-boot primary OS
        term_print("Auto-booting primary OS...\n");
        delay_ms(500);
        auto_boot_primary();
    }
    
    // Should never reach here
    enter_emergency_mode();
}

boot_entry_t* scan_boot_devices(void) {
    num_boot_entries = 0;
    
    // Scan for UOS kernel
    if (fs_exists("/boot/uos.img")) {
        boot_entry_t* entry = &boot_entries[num_boot_entries++];
        strcpy(entry->name, "Unified Operating System");
        strcpy(entry->path, "/boot/uos.img");
        entry->type = BOOT_TYPE_UOS;
        entry->load_addr = 0x8000;
        term_print("  Found: UOS\n");
    }
    
    // Scan for PIP-OS kernel
    if (fs_exists("/boot/pipos.img")) {
        boot_entry_t* entry = &boot_entries[num_boot_entries++];
        strcpy(entry->name, "PIP-OS v7.1.0.8");
        strcpy(entry->path, "/boot/pipos.img");
        entry->type = BOOT_TYPE_PIPOS;
        entry->load_addr = 0x8000;
        term_print("  Found: PIP-OS\n");
    }
    
    // Always add maintenance mode
    boot_entry_t* maint = &boot_entries[num_boot_entries++];
    strcpy(maint->name, "Maintenance Mode");
    strcpy(maint->path, "");
    maint->type = BOOT_TYPE_MAINTENANCE;
    maint->load_addr = 0;
    
    // Always add diagnostics
    boot_entry_t* diag = &boot_entries[num_boot_entries++];
    strcpy(diag->name, "Hardware Diagnostics");
    strcpy(diag->path, "");
    diag->type = BOOT_TYPE_DIAGNOSTIC;
    diag->load_addr = 0;
    
    return boot_entries;
}

int count_boot_entries(boot_entry_t* entries) {
    (void)entries;
    return num_boot_entries;
}

void auto_boot_primary(void) {
    // Try to boot the first available OS
    for (int i = 0; i < num_boot_entries; i++) {
        if (boot_entries[i].type == BOOT_TYPE_UOS || 
            boot_entries[i].type == BOOT_TYPE_PIPOS) {
            term_printf("Loading %s...\n", boot_entries[i].name);
            boot_selected(&boot_entries[i]);
            return;
        }
    }
    
    // No OS found
    term_print("ERROR: No bootable OS found\n");
    enter_emergency_mode();
}

void boot_selected(boot_entry_t* entry) {
    if (entry->type == BOOT_TYPE_MAINTENANCE) {
        enter_maintenance_mode();
        return;
    }
    
    if (entry->type == BOOT_TYPE_DIAGNOSTIC) {
        term_print("\nStarting Hardware Diagnostics...\n");
        delay_ms(500);
        // Diagnostics would be implemented here
        extern void run_diagnostics(void);
        run_diagnostics();
        return;
    }
    
    // Load and boot OS kernel
    term_printf("\nLoading %s\n", entry->name);
    term_printf("Path: %s\n", entry->path);
    
    if (load_kernel(entry) != 0) {
        term_print("ERROR: Failed to load kernel\n");
        delay_ms(2000);
        enter_emergency_mode();
    }
}

int check_holotape_present(void) {
    // Check GPIO or USB for holotape presence
    // For now, always return false (no holotape)
    return 0;
}

void load_holotape_boot(void) {
    term_print("Loading from holotape...\n");
    // Holotape boot implementation would go here
    delay_ms(1000);
    enter_emergency_mode();
}

uint32_t get_boot_count(void) {
    // For now, return 0 (first boot)
    // Could be stored in a reserved memory location
    return 0;
}
