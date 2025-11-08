#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <stdint.h>
#include <stddef.h>

// File handle structure
typedef struct {
    uint32_t start_sector;
    uint32_t size;
    uint32_t position;
    uint8_t valid;
} file_handle_t;

// Function declarations
int fs_init(void);
file_handle_t* fs_open(const char* path);
int fs_read(file_handle_t* fh, void* buffer, size_t size);
void fs_close(file_handle_t* fh);
int fs_exists(const char* path);

#endif // FILESYSTEM_H
