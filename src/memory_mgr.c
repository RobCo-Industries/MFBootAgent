// src/memory_mgr.c - Memory management

#include "memory_mgr.h"
#include "mfboot.h"

// Simple memory allocator for upper memory
static uint8_t upper_memory[UPPERMEM_SIZE] __attribute__((aligned(16)));
static size_t upper_used = 0;

void memory_init(void) {
    upper_used = 0;
    memset(upper_memory, 0, UPPERMEM_SIZE);
}

void* memory_allocate_upper(size_t size) {
    if (upper_used + size > UPPERMEM_SIZE) {
        return NULL;
    }
    
    void* ptr = &upper_memory[upper_used];
    upper_used += size;
    
    // Align to 16 bytes
    upper_used = (upper_used + 15) & ~15;
    
    return ptr;
}

void memory_free_upper(void* ptr) {
    // Simple allocator doesn't support free
    (void)ptr;
}

void* memory_alloc(size_t size) {
    return memory_allocate_upper(size);
}

void memory_free(void* ptr) {
    memory_free_upper(ptr);
}
