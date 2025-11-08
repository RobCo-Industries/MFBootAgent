// src/filesystem.c - Basic FAT32 filesystem support

#include "filesystem.h"
#include "mfboot.h"

// For now, this is a stub implementation
// A full FAT32 driver would be quite complex

static int fs_initialized = 0;

int fs_init(void) {
    // Initialize SD card and read FAT32 structures
    // For now, just stub
    fs_initialized = 1;
    return 0;
}

file_handle_t* fs_open(const char* path) {
    (void)path;
    
    if (!fs_initialized) {
        return NULL;
    }
    
    // Would search directory entries for file
    // For now, return NULL (file not found)
    return NULL;
}

int fs_read(file_handle_t* fh, void* buffer, size_t size) {
    (void)fh;
    (void)buffer;
    (void)size;
    
    if (!fs_initialized) {
        return -1;
    }
    
    // Would read sectors from SD card
    return 0;
}

void fs_close(file_handle_t* fh) {
    (void)fh;
}

int fs_exists(const char* path) {
    (void)path;
    
    if (!fs_initialized) {
        return 0;
    }
    
    // For testing purposes, pretend some files exist
    if (strcmp(path, "/boot/uos.img") == 0) {
        return 1;
    }
    
    return 0;
}
