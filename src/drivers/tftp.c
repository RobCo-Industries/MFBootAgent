// src/drivers/tftp.c - Network boot support (stub)

#include "mfboot.h"

// TFTP network boot driver stub
// Full network stack would be needed for real implementation

int tftp_init(void) {
    // Initialize network interface
    return -1;  // Not implemented
}

int tftp_download(const char* server, const char* filename, void* buffer, size_t max_size) {
    (void)server;
    (void)filename;
    (void)buffer;
    (void)max_size;
    return -1;  // Not implemented
}
