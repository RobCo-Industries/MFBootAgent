// stage2.S - Entry point for MFBootAgent
// Called by RETROS-BIOS at address 0x8000
// r0 = board type, r1 = machine type, r2 = ATAGS pointer

.section ".text.boot"

.global _start

_start:
    // Save boot parameters passed from RETROS-BIOS
    mov r4, r0          // Save r0 (board type)
    mov r5, r1          // Save r1 (machine type)
    mov r6, r2          // Save r2 (ATAGS pointer)
    
    // Set up stack (already configured by RETROS-BIOS, but ensure it's correct)
    ldr sp, =0x8000
    
    // Clear BSS section
    ldr r0, =__bss_start
    ldr r1, =__bss_end
    mov r2, #0
    
bss_clear_loop:
    cmp r0, r1
    strlt r2, [r0], #4
    blt bss_clear_loop
    
    // Restore boot parameters and call main
    mov r0, r4          // Restore r0
    mov r1, r5          // Restore r1
    mov r2, r6          // Restore r2
    bl mfboot_main
    
    // If main returns, halt
halt:
    wfe
    b halt

.global jump_to_kernel_asm
jump_to_kernel_asm:
    // r0 = kernel address, r1 = r0 param, r2 = r1 param, r3 = r2 param (atags)
    mov r4, r0          // Save kernel address
    mov r0, r1          // Set r0
    mov r1, r2          // Set r1
    mov r2, r3          // Set r2
    bx r4               // Jump to kernel
