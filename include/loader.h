#ifndef LOADER_H
#define LOADER_H

#include <stdint.h>
#include "mfboot.h"

// ELF header magic
#define ELF_MAGIC 0x464C457F  // "\x7FELF"

// Function declarations
int load_kernel(boot_entry_t* entry);
int verify_signature(boot_entry_t* entry);
void jump_to_kernel(uint32_t addr, uint32_t r0, uint32_t r1, uint32_t atags);

#endif // LOADER_H
