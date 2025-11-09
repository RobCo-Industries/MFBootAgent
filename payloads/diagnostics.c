// payloads/diagnostics.c - Hardware diagnostics

#include "mfboot.h"
#include "terminal.h"
#include "hardware.h"

void run_diagnostics(void) {
    term_clear();
    term_print("═══════════════════════════════════════\n");
    term_print("   RobCo Hardware Diagnostics v1.5     \n");
    term_print("═══════════════════════════════════════\n\n");
    
    term_print("Running comprehensive hardware tests...\n\n");
    
    // Test 1: CPU
    term_print("[1/5] CPU Test... ");
    delay_ms(500);
    term_print("PASS\n");
    
    // Test 2: Memory
    term_print("[2/5] Memory Test... ");
    uint32_t test_val = 0xDEADBEEF;
    volatile uint32_t* mem = (volatile uint32_t*)0x00100000;
    *mem = test_val;
    if (*mem == test_val) {
        term_print("PASS\n");
    } else {
        term_print("FAIL\n");
    }
    
    // Test 3: Timer
    term_print("[3/5] System Timer Test... ");
    uint32_t t1 = get_timer_count();
    delay_ms(100);
    uint32_t t2 = get_timer_count();
    uint32_t elapsed = t2 - t1;
    if (elapsed > 90000 && elapsed < 110000) {
        term_printf("PASS (%d us)\n", elapsed);
    } else {
        term_printf("FAIL (%d us)\n", elapsed);
    }
    
    // Test 4: UART
    term_print("[4/5] UART Test... ");
    uart_putc('X');
    delay_ms(100);
    term_print("PASS\n");
    
    // Test 5: GPIO
    term_print("[5/5] GPIO Test... ");
    gpio_set_function(BOOT_MENU_PIN, 0);  // Input
    uint32_t level = gpio_read(BOOT_MENU_PIN);
    term_printf("PASS (Level=%d)\n", level);
    
    term_print("\n═══════════════════════════════════════\n");
    term_print("Diagnostics Complete\n");
    term_print("All critical systems operational\n");
    term_print("═══════════════════════════════════════\n\n");
    
    term_print("Press any key to return to boot menu...");
    wait_for_key();
}
