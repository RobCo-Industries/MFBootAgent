#ifndef MFBOOT_H
#define MFBOOT_H

#include <stdint.h>
#include <stddef.h>

// Version information
#define MFBOOT_VERSION "2.3.0"
#define COPYRIGHT "Copyright 2201-2203 Robco Ind."

// Memory configuration
#define UPPERMEM_SIZE 0x10000  // 64KB upper memory
#define KERNEL_LOAD_ADDR 0x8000

// Boot device types
typedef enum {
    BOOT_TYPE_UOS = 0,          // Unified Operating System
    BOOT_TYPE_PIPOS,            // PIP-OS v7.1.0.8
    BOOT_TYPE_MAINTENANCE,      // Maintenance Mode
    BOOT_TYPE_DIAGNOSTIC        // Hardware Diagnostics
} boot_type_t;

// Boot entry structure
typedef struct {
    char name[32];
    char path[256];
    uint32_t load_addr;
    uint32_t size;
    uint8_t signature[64];
    boot_type_t type;
} boot_entry_t;

// GPIO pins
#define BOOT_MENU_PIN 17

// Function declarations
void mfboot_main(uint32_t r0, uint32_t r1, uint32_t atags);
void enter_emergency_mode(void);
void enter_maintenance_mode(void);
boot_entry_t* scan_boot_devices(void);
int count_boot_entries(boot_entry_t* entries);
void display_boot_menu(boot_entry_t* entries);
void boot_selected(boot_entry_t* entry);
void auto_boot_primary(void);
int check_holotape_present(void);
void load_holotape_boot(void);
uint32_t get_boot_count(void);

// Standard library replacements
void* memset(void* s, int c, size_t n);
void* memcpy(void* dest, const void* src, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);
size_t strlen(const char* s);
char* strcpy(char* dest, const char* src);
int strcmp(const char* s1, const char* s2);

#endif // MFBOOT_H
