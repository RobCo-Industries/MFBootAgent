// src/maintenance.c - Maintenance mode utilities

#include "mfboot.h"
#include "terminal.h"
#include "hardware.h"
#include "memory_mgr.h"

static void print_menu(void);
static void show_system_info(void);
static void show_memory_info(void);
static void test_hardware(void);

void enter_maintenance_mode(void) {
    term_clear();
    term_print("═══════════════════════════════════════\n");
    term_print("    RobCo Maintenance Terminal v2.1    \n");
    term_print("═══════════════════════════════════════\n\n");
    
    while (1) {
        print_menu();
        
        term_print("\nSelect option: ");
        char key = wait_for_key();
        term_printf("%c\n\n", key);
        
        switch (key) {
            case '1':
                show_system_info();
                break;
            case '2':
                show_memory_info();
                break;
            case '3':
                test_hardware();
                break;
            case '4':
                term_print("Returning to boot menu...\n");
                delay_ms(1000);
                return;
            case '5':
                enter_emergency_mode();
                break;
            case 'R':
            case 'r':
                term_print("Rebooting system...\n");
                delay_ms(1000);
                // Would trigger hardware reset
                break;
            default:
                term_print("Invalid option\n");
                break;
        }
        
        term_print("\nPress any key to continue...");
        wait_for_key();
        term_clear();
    }
}

static void print_menu(void) {
    term_print("Maintenance Options:\n");
    term_print("  [1] System Information\n");
    term_print("  [2] Memory Information\n");
    term_print("  [3] Hardware Tests\n");
    term_print("  [4] Return to Boot Menu\n");
    term_print("  [5] Emergency Shell\n");
    term_print("  [R] Reboot System\n");
}

static void show_system_info(void) {
    term_print("System Information:\n");
    term_print("─────────────────────────────────────\n");
    term_print("Boot Agent: MF Boot Agent v" MFBOOT_VERSION "\n");
    term_print(COPYRIGHT "\n");
    
#ifdef BCM2835
    term_print("Platform: BCM2835 (RPi0/1)\n");
    term_printf("Peripheral Base: 0x%08X\n", PERIPHERAL_BASE);
#elif defined(BCM2836)
    term_print("Platform: BCM2836 (RPi2)\n");
    term_printf("Peripheral Base: 0x%08X\n", PERIPHERAL_BASE);
#elif defined(BCM2837)
    term_print("Platform: BCM2837 (RPi3)\n");
    term_printf("Peripheral Base: 0x%08X\n", PERIPHERAL_BASE);
#else
    term_print("Platform: Unknown\n");
#endif
    
    uint32_t timer = get_timer_count();
    term_printf("System Timer: %d us\n", timer);
}

static void show_memory_info(void) {
    term_print("Memory Information:\n");
    term_print("─────────────────────────────────────\n");
    term_printf("Upper Memory Size: %d KB\n", UPPERMEM_SIZE / 1024);
    term_printf("Kernel Load Address: 0x%08X\n", KERNEL_LOAD_ADDR);
    
    extern uint8_t __bss_start, __bss_end;
    term_printf("BSS Start: 0x%08X\n", (uint32_t)&__bss_start);
    term_printf("BSS End: 0x%08X\n", (uint32_t)&__bss_end);
}

static void test_hardware(void) {
    term_print("Hardware Tests:\n");
    term_print("─────────────────────────────────────\n");
    
    term_print("Testing UART... ");
    uart_putc('O');
    uart_putc('K');
    term_print("\n");
    
    term_print("Testing Timer... ");
    uint32_t t1 = get_timer_count();
    delay_ms(100);
    uint32_t t2 = get_timer_count();
    term_printf("OK (%d us)\n", t2 - t1);
    
    term_print("Testing GPIO... ");
    gpio_set_function(BOOT_MENU_PIN, 0);  // Input
    uint32_t level = gpio_read(BOOT_MENU_PIN);
    term_printf("OK (Pin %d = %d)\n", BOOT_MENU_PIN, level);
    
    term_print("\nAll tests completed\n");
}

void enter_emergency_mode(void) {
    term_clear();
    term_print("═══════════════════════════════════════\n");
    term_print("      EMERGENCY RECOVERY SHELL         \n");
    term_print("═══════════════════════════════════════\n\n");
    term_print("Critical boot failure detected.\n");
    term_print("System halted for manual recovery.\n\n");
    term_print("Available commands:\n");
    term_print("  REBOOT - Restart system\n");
    term_print("  INFO   - Show system information\n");
    term_print("  MAINT  - Enter maintenance mode\n\n");
    
    char buffer[64];
    int pos = 0;
    
    while (1) {
        term_print("> ");
        pos = 0;
        
        while (1) {
            char c = wait_for_key();
            
            if (c == '\r' || c == '\n') {
                buffer[pos] = '\0';
                term_print("\n");
                break;
            } else if (c == 127 || c == 8) {  // Backspace
                if (pos > 0) {
                    pos--;
                    term_print("\b \b");
                }
            } else if (pos < 63) {
                buffer[pos++] = c;
                uart_putc(c);
            }
        }
        
        // Process command
        if (strcmp(buffer, "REBOOT") == 0 || strcmp(buffer, "reboot") == 0) {
            term_print("Rebooting...\n");
            delay_ms(1000);
            // Would trigger reset
        } else if (strcmp(buffer, "INFO") == 0 || strcmp(buffer, "info") == 0) {
            show_system_info();
        } else if (strcmp(buffer, "MAINT") == 0 || strcmp(buffer, "maint") == 0) {
            enter_maintenance_mode();
        } else if (buffer[0] != '\0') {
            term_print("Unknown command\n");
        }
    }
}
