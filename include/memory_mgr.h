#ifndef MEMORY_MGR_H
#define MEMORY_MGR_H

#include <stdint.h>
#include <stddef.h>

// Memory regions
#define MEM_LOWER_START     0x00000000
#define MEM_KERNEL_START    0x00008000
#define MEM_UPPER_START     0x00100000

// Function declarations
void memory_init(void);
void* memory_allocate_upper(size_t size);
void memory_free_upper(void* ptr);
void* memory_alloc(size_t size);
void memory_free(void* ptr);

#endif // MEMORY_MGR_H
