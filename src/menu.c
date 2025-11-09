// src/menu.c - Interactive Boot Selection

#include "mfboot.h"
#include "terminal.h"
#include "hardware.h"

void display_boot_menu(boot_entry_t* entries) {
    int selection = 0;
    int num_entries = count_boot_entries(entries);
    
    while (1) {
        term_clear();
        term_print("═══════════════════════════════════════\n");
        term_print("    RobCo Industries Boot Selection    \n");
        term_print("═══════════════════════════════════════\n\n");
        
        for (int i = 0; i < num_entries; i++) {
            if (i == selection) {
                term_print("> ");
                term_set_color(COLOR_AMBER);
            } else {
                term_print("  ");
            }
            
            term_printf("[%d] %s", i+1, entries[i].name);
            
            // Show device type
            switch(entries[i].type) {
                case BOOT_TYPE_UOS:
                    term_print(" (Unified Operating System)");
                    break;
                case BOOT_TYPE_PIPOS:
                    term_print(" (PIP-OS v7.1.0.8)");
                    break;
                case BOOT_TYPE_MAINTENANCE:
                    term_print(" (Maintenance Mode)");
                    break;
                case BOOT_TYPE_DIAGNOSTIC:
                    term_print(" (Hardware Diagnostics)");
                    break;
            }
            
            term_set_color(COLOR_GREEN);
            term_print("\n");
        }
        
        term_print("\n[M] Maintenance Mode\n");
        term_print("[H] Holotape Boot\n");
        term_print("[R] Reboot\n");
        term_print("\nUse Arrow Keys to select, Enter to boot\n");
        
        char key = wait_for_key();
        
        switch(key) {
            case KEY_UP:
                selection = (selection - 1 + num_entries) % num_entries;
                break;
            case KEY_DOWN:
                selection = (selection + 1) % num_entries;
                break;
            case KEY_ENTER:
            case '\r':
            case '\n':
                boot_selected(&entries[selection]);
                break;
            case 'M':
            case 'm':
                enter_maintenance_mode();
                break;
            case 'H':
            case 'h':
                load_holotape_boot();
                break;
            case 'R':
            case 'r':
                term_print("\nRebooting...\n");
                delay_ms(1000);
                // Would trigger reboot here
                break;
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
                {
                    int idx = key - '1';
                    if (idx >= 0 && idx < num_entries) {
                        boot_selected(&entries[idx]);
                    }
                }
                break;
        }
    }
}
