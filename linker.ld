/* Linker script for MFBootAgent
 * Load address: 0x8000 (standard ARM kernel entry)
 * Compatible with RETROS-BIOS handoff
 */

ENTRY(_start)

SECTIONS
{
    /* Code loaded at 0x8000 by RETROS-BIOS */
    . = 0x8000;
    
    .text : {
        KEEP(*(.text.boot))
        *(.text)
        *(.text.*)
    }
    
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }
    
    .data : {
        *(.data)
        *(.data.*)
    }
    
    .bss : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }
    
    /* Stack grows down from 0x8000 (set by RETROS-BIOS) */
    /* Upper memory allocation at runtime */
    
    /DISCARD/ : {
        *(.comment)
        *(.gnu*)
        *(.note*)
        *(.eh_frame*)
    }
}

__bss_size = (__bss_end - __bss_start) >> 3;
