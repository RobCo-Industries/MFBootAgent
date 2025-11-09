# Building MFBootAgent

## Prerequisites

MFBootAgent is built for ARM bare-metal systems (Raspberry Pi). You'll need:

1. **ARM Cross-Compilation Toolchain**
   - GCC ARM Embedded Toolchain
   - Install on Ubuntu/Debian: `sudo apt-get install gcc-arm-none-eabi`
   - Install on macOS: `brew install arm-none-eabi-gcc`
   - Download from: https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/gnu-rm

2. **Build Tools**
   - GNU Make
   - Python 3.x (for tools)

## Building

### Quick Start

Build for Raspberry Pi Zero/1 (BCM2835):
```bash
make
```

Build for specific platforms:
```bash
make bcm2835  # Raspberry Pi Zero, Zero W, 1A, 1B, 1B+
make bcm2836  # Raspberry Pi 2B
make bcm2837  # Raspberry Pi 3B
```

### Build Output

The build process creates:
- `build/mfbootagent.elf` - ELF executable with debug symbols
- `build/mfbootagent.img` - Raw binary image (ready to deploy)
- `build/mfbootagent.list` - Disassembly listing

### Clean Build

```bash
make clean
```

## Installation

1. Build the bootloader for your target platform
2. Copy `build/mfbootagent.img` to your SD card
3. RETROS-BIOS should be configured to load `mfbootagent.img` from the boot partition
4. The boot chain will be: RETROS-BIOS → MFBootAgent → Your OS

## Creating Boot Images

Use the `mkbootimg.py` tool to create bootable OS images:

```bash
# Create a UOS boot image
./tools/mkbootimg.py kernel.bin -o boot/uos.img -t 0

# Create a PIP-OS boot image
./tools/mkbootimg.py kernel.bin -o boot/pipos.img -t 1 -a 0x8000
```

## Signing Boot Images (Secure Boot)

For secure boot, sign your OS images:

```bash
# Sign an image
./tools/sign_payload.py sign boot/uos.img -k private_key.pem -o boot/uos.img.signed

# Verify a signed image
./tools/sign_payload.py verify boot/uos.img.signed -k public_key.pem
```

**Note**: The signing tools currently contain stub implementations. Real cryptographic signing should be implemented for production use.

## Configuration

Edit configuration files in the `config/` directory:

- `bootmenu.conf` - Boot menu behavior and appearance
- `devices.conf` - Boot device configuration and search paths

## Integration with RETROS-BIOS

MFBootAgent is designed to work exclusively with RETROS-BIOS:

1. RETROS-BIOS loads MFBootAgent to address `0x8000`
2. MFBootAgent receives control with hardware initialized:
   - UART at 115200 baud
   - Framebuffer ready
   - System timer running
   - SD card initialized
3. MFBootAgent scans for OS kernels and presents boot menu
4. Selected OS is loaded and receives control

## Memory Map

```
0x00000000  Exception vectors (GPU-managed)
0x00008000  MFBootAgent entry point
0x00100000  Upper memory region (64KB, dynamically allocated)
0x20000000  Peripherals (BCM2835)
0x3F000000  Peripherals (BCM2836/2837)
```

## Troubleshooting

### Build Errors

**Error: `arm-none-eabi-gcc: command not found`**
- Install the ARM cross-compilation toolchain (see Prerequisites)

**Error: `undefined reference to ...`**
- Check that all source files are included in the Makefile
- Verify that the linker script is correct

### Runtime Issues

**Bootloader doesn't start**
- Verify RETROS-BIOS is correctly configured
- Check that the image is copied to the correct location
- Ensure the target platform matches the build (BCM2835/2836/2837)

**Boot menu doesn't appear**
- Check GPIO pin 17 configuration (boot menu trigger)
- Verify UART output at 115200 baud

## Development

### Adding New Drivers

1. Create driver file in `src/drivers/`
2. Add header declarations
3. Update Makefile to include the new driver
4. Implement initialization in `main.c`

### Adding New Boot Devices

1. Implement driver in `src/drivers/`
2. Add device scanning in `scan_boot_devices()` in `main.c`
3. Update `config/devices.conf` with device configuration

## Testing

Basic functionality testing can be done with QEMU:

```bash
qemu-system-arm -M raspi0 -kernel build/mfbootagent.img -serial stdio
```

Note: Full testing requires actual Raspberry Pi hardware with RETROS-BIOS installed.

## Size Constraints

MFBootAgent should remain compact to allow maximum space for the OS:
- Target size: < 50 KB
- Current size: See build output
- If size exceeds limits, consider reducing features or optimizing code

## License

See LICENSE file for licensing information.

---

**RobCo Industries™** - *"Engineering a Better Future, Today!"*
