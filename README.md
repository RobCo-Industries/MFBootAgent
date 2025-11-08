# MF Boot Agent

## Overview

MFBootAgent is the critical link between RETROS-BIOS and UOS/Pip-OS, serving as the second-stage bootloader that manages advanced system initialization and operating system loading on RobCo terminal hardware.

### Boot Chain

```
Power On → RETROS-BIOS → MFBootAgent → UOS/Pip-OS
         (bare metal)   (bootloader)   (operating system)
         First Stage    Second Stage
```

**RETROS-BIOS** (RobCo Industries™ Terminal BIOS) is the first-stage bootloader that:
- Runs directly on bare metal at power-on
- Initializes core hardware (UART, framebuffer, PWM audio)
- Provides Fallout-style terminal aesthetics with boot beep and scanline effects
- Performs basic SD card initialization
- Loads MFBootAgent from SD card to memory address `0x8000`
- Transfers control to MFBootAgent's entry point

### Platform Support

MFBootAgent runs on Raspberry Pi hardware initialized by RETROS-BIOS:
- **BCM2835**: Raspberry Pi Zero, Zero W, 1A, 1B, 1B+
- **BCM2836**: Raspberry Pi 2B
- **BCM2837**: Raspberry Pi 3B

The peripheral base address is automatically configured by RETROS-BIOS based on the detected platform.

## RETROS-BIOS Dependency

MFBootAgent is designed to work exclusively with [RETROS-BIOS](https://github.com/RobCo-Industries/RETROS-BIOS), the Fallout-inspired first-stage bootloader.

### What RETROS-BIOS Provides

**Core Initialization**:
- Bare-metal hardware bring-up from power-on
- UART serial console at 115200 baud
- HDMI framebuffer with 8x16 VGA font rendering
- System timer and delay functions
- Basic SD card support for chain-loading

**Terminal Aesthetics**:
- Authentic Fallout terminal experience
- PWM boot beep (three-tone sequence)
- CRT-style scanline effects
- Animated boot messages with typewriter effect
- Random memory test patterns and bad sector warnings

**Binary Size**: RETROS-BIOS is extremely compact (~10KB), leaving maximum space for MFBootAgent and the OS.

### Why Second-Stage Loading?

The two-stage boot architecture provides:

1. **Size Constraints**: RETROS-BIOS stays minimal to fit firmware requirements
2. **Modularity**: Bootloader features can be upgraded independently
3. **Flexibility**: MFBootAgent can be replaced without reflashing RETROS-BIOS
4. **Complexity Management**: Advanced features (filesystems, networking, crypto) in second stage
5. **Recovery**: If MFBootAgent fails, RETROS-BIOS diagnostic mode remains accessible

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

MFBootAgent receives control from RETROS-BIOS after first-stage initialization. RETROS-BIOS provides:

#### Hardware State
- **UART0**: Initialized at 115200 baud (GPIO 14 TX, GPIO 15 RX)
- **Framebuffer**: 640x480 @ 32-bit color, ready for rendering
- **System Timer**: Available for delays and timing operations
- **PWM Audio**: Configured on GPIO 40 (boot beep complete)
- **SD Card**: Basic EMMC initialization performed
- **GPIO**: Core pins configured for peripherals

#### Memory Layout
- **Entry Address**: `0x8000` (kernel load address)
- **Stack**: Set up with 32KB allocation below kernel entry
- **Peripheral Base**: 
  - BCM2835 (RPi0/1): `0x20000000`
  - BCM2836/7 (RPi2/3): `0x3F000000`
- **Exception Vectors**: `0x00000000` (GPU-managed)

#### Available Routines
MFBootAgent can leverage RETROS-BIOS routines if needed:
- UART output functions (`uart_puts`, `uart_printf`)
- Framebuffer drawing functions (`fb_draw_string`, `fb_draw_char`)
- Hardware delay functions (`delay_ms`, `delay_us`)
- System timer access

#### Register State on Entry
- **r0**: Board type identifier (e.g., 0x00 for RPi1)
- **r1**: ARM Linux machine type (typically 0)
- **r2**: ATAGS/Device Tree pointer (physical address)

RETROS-BIOS completes in approximately 5-9 seconds, including the Fallout-style boot sequence with animated messages, memory test patterns, and optional bad sector warnings.

### To UOS/Pip-OS

MFBootAgent prepares the system for OS execution by extending the initialization started by RETROS-BIOS:

#### Bootloader Responsibilities
- **Filesystem Access**: Implements full FAT32/ext4 support for locating kernel images
- **Device Enumeration**: Scans and identifies available boot devices (SD/MMC, USB, network, holotape)
- **Boot Menu**: Presents device selection interface if multiple options available
- **Kernel Loading**: Loads ELF or binary kernel images from selected boot device
- **Memory Management**: Allocates and manages upper 64KB memory region
- **Signature Verification**: Validates OS image signatures if secure boot is enabled

#### OS Handoff
- **Kernel Command Line**: Sets up boot arguments and parameters
- **Device Tree/ATAGS**: Prepares hardware description structures for OS
- **Memory Map**: Configures initial memory layout with bootloader exclusion zones
- **Boot Parameters**: Passes device information, root filesystem location, and boot flags
- **Hardware State**: Ensures all initialized hardware is in a known state for OS takeover

#### Boot Protocol Support
- **Linux ARM boot protocol**: Standard ATAGS/Device Tree handoff
- **RobCo Termlink Protocol**: Terminal-specific initialization and capabilities
- **Emergency Shell**: Fallback environment if OS kernel is unavailable or fails to load

The complete boot chain from power-on to OS kernel entry typically completes in 10-15 seconds, with RETROS-BIOS taking 5-9 seconds and MFBootAgent adding 5-6 seconds for device enumeration and kernel loading.

---

**RobCo Industries™** - *"Engineering a Better Future, Today!"*
