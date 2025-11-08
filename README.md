# MF Boot Agent v2.3.0

**Copyright 2201-2203 RobCo Industries**

## Overview

MFBootAgent is the critical link between RETROS-BIOS and UOS/Pip-OS, serving as the bootloader that manages system initialization and operating system loading on RobCo terminal hardware.

### Boot Chain

```
Power On → RETROS-BIOS → MFBootAgent → UOS/Pip-OS
         (bare metal)   (bootloader)   (operating system)
```

## Project Structure

```
MFBootAgent/
├── README.md                 # MF Boot Agent v2.3.0 - Copyright 2201-2203 Robco Ind.
├── Makefile
├── config/
│   ├── bootmenu.conf        # Boot menu configuration
│   └── devices.conf         # Supported boot devices
├── src/
│   ├── stage2.S             # Entry from RETROS-BIOS
│   ├── main.c               # Core bootloader logic
│   ├── memory_mgr.c         # Upper memory allocation (64KB)
│   ├── filesystem.c         # FAT32/ext4 support
│   ├── loader.c             # ELF/binary loading
│   ├── menu.c               # Boot device selection menu
│   ├── maintenance.c        # Maintenance mode utilities
│   ├── terminal.c           # Terminal protocol init
│   ├── crypto.c             # Signature verification (if secure boot)
│   └── drivers/
│       ├── mmc.c            # SD/MMC driver (enhanced from RETROS)
│       ├── usb.c            # USB mass storage
│       ├── tftp.c           # Network boot support
│       └── holotape.c       # External media support
├── include/
│   ├── mfboot.h
│   ├── protocols.h          # Boot protocols
│   └── termlink.h           # RobCo Termlink definitions
├── payloads/
│   ├── emergency_shell.c    # Fallback shell
│   └── diagnostics.c        # Hardware diagnostics
└── tools/
    ├── mkbootimg.py         # Create boot images
    └── sign_payload.py      # Sign OS images
```

## Entry Point

The bootloader's main entry point is located in `src/main.c`:

```c
void mfboot_main(uint32_t r0, uint32_t r1, uint32_t atags) { ... }
```

This function receives control from RETROS-BIOS with hardware initialization parameters.

## Features

- **Multi-stage Loading**: Efficient two-stage boot process for reliable system startup
- **Maintenance Mode**: Comprehensive diagnostic and recovery tools
- **Holotape Boot Support**: Legacy external media compatibility
- **Network Boot (PXE-style)**: Remote boot capabilities via TFTP
- **Signature Verification**: Secure boot with cryptographic payload verification

## Integration Points

### From RETROS-BIOS

MFBootAgent receives control from RETROS-BIOS with the following handoff:

- **Entry Address**: `0x8000`
- **Hardware State**: Inherits basic hardware initialization
- **Peripheral Access**: Uses established UART/display routines
- **Register State**: Receives boot parameters in r0, r1, and atags pointer

### To UOS/Pip-OS

MFBootAgent prepares the system for OS execution:

- **Kernel Command Line**: Sets up boot arguments and parameters
- **Device Tree/ATAGS**: Prepares hardware description structures
- **Memory Map**: Configures initial memory layout (64KB upper memory allocation)
- **Boot Parameters**: Passes necessary system information to the OS kernel

---

*MFBootAgent - Bridging bare metal and operating system since 2201*
