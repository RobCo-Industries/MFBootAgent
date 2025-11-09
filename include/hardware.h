#ifndef HARDWARE_H
#define HARDWARE_H

#include <stdint.h>

// Peripheral base addresses (set by RETROS-BIOS)
#ifdef BCM2835
    #define PERIPHERAL_BASE 0x20000000
#elif defined(BCM2836) || defined(BCM2837)
    #define PERIPHERAL_BASE 0x3F000000
#else
    #define PERIPHERAL_BASE 0x20000000  // Default to BCM2835
#endif

// GPIO registers
#define GPIO_BASE       (PERIPHERAL_BASE + 0x200000)
#define GPFSEL1         ((volatile uint32_t*)(GPIO_BASE + 0x04))
#define GPSET0          ((volatile uint32_t*)(GPIO_BASE + 0x1C))
#define GPCLR0          ((volatile uint32_t*)(GPIO_BASE + 0x28))
#define GPLEV0          ((volatile uint32_t*)(GPIO_BASE + 0x34))

// UART registers
#define UART0_BASE      (PERIPHERAL_BASE + 0x201000)
#define UART0_DR        ((volatile uint32_t*)(UART0_BASE + 0x00))
#define UART0_FR        ((volatile uint32_t*)(UART0_BASE + 0x18))

// System Timer registers
#define TIMER_BASE      (PERIPHERAL_BASE + 0x3000)
#define TIMER_CLO       ((volatile uint32_t*)(TIMER_BASE + 0x04))

// Function declarations
void delay_ms(uint32_t ms);
void delay_us(uint32_t us);
uint32_t get_timer_count(void);
void gpio_set_function(uint32_t pin, uint32_t func);
void gpio_set(uint32_t pin);
void gpio_clear(uint32_t pin);
uint32_t gpio_read(uint32_t pin);
void uart_putc(char c);
char uart_getc(void);
int uart_readable(void);

#endif // HARDWARE_H
