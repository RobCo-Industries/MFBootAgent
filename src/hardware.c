// src/hardware.c - Hardware abstraction layer

#include "hardware.h"

void delay_ms(uint32_t ms) {
    uint32_t start = *TIMER_CLO;
    uint32_t target = start + (ms * 1000);
    
    // Handle timer wraparound
    if (target < start) {
        while (*TIMER_CLO >= start) {}
    }
    
    while (*TIMER_CLO < target) {}
}

void delay_us(uint32_t us) {
    uint32_t start = *TIMER_CLO;
    uint32_t target = start + us;
    
    // Handle timer wraparound
    if (target < start) {
        while (*TIMER_CLO >= start) {}
    }
    
    while (*TIMER_CLO < target) {}
}

uint32_t get_timer_count(void) {
    return *TIMER_CLO;
}

void gpio_set_function(uint32_t pin, uint32_t func) {
    uint32_t reg = pin / 10;
    uint32_t shift = (pin % 10) * 3;
    volatile uint32_t* fsel = (volatile uint32_t*)(GPIO_BASE + (reg * 4));
    
    uint32_t val = *fsel;
    val &= ~(7 << shift);
    val |= (func << shift);
    *fsel = val;
}

void gpio_set(uint32_t pin) {
    uint32_t reg = pin / 32;
    uint32_t bit = pin % 32;
    volatile uint32_t* gpset = (volatile uint32_t*)(GPIO_BASE + 0x1C + (reg * 4));
    *gpset = (1 << bit);
}

void gpio_clear(uint32_t pin) {
    uint32_t reg = pin / 32;
    uint32_t bit = pin % 32;
    volatile uint32_t* gpclr = (volatile uint32_t*)(GPIO_BASE + 0x28 + (reg * 4));
    *gpclr = (1 << bit);
}

uint32_t gpio_read(uint32_t pin) {
    uint32_t reg = pin / 32;
    uint32_t bit = pin % 32;
    volatile uint32_t* gplev = (volatile uint32_t*)(GPIO_BASE + 0x34 + (reg * 4));
    return (*gplev & (1 << bit)) ? 1 : 0;
}

void uart_putc(char c) {
    // Wait for UART to be ready
    while (*UART0_FR & (1 << 5)) {}
    *UART0_DR = c;
}

char uart_getc(void) {
    // Wait for data
    while (*UART0_FR & (1 << 4)) {}
    return (char)(*UART0_DR & 0xFF);
}

int uart_readable(void) {
    // Check if data is available (RX FIFO not empty)
    return !(*UART0_FR & (1 << 4));
}
