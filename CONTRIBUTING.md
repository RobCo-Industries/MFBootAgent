# Contributing to MFBootAgent

Thank you for your interest in contributing to MFBootAgent! This document provides guidelines for contributing to this second-stage bootloader project.

## Code of Conduct

Be respectful and constructive. This is a technical project focused on bare-metal ARM bootloader development.

## Getting Started

1. **Fork the repository**
2. **Clone your fork**
   ```bash
   git clone https://github.com/YOUR_USERNAME/MFBootAgent.git
   cd MFBootAgent
   ```
3. **Set up the development environment** (see BUILD.md)
4. **Run validation**
   ```bash
   ./validate.sh
   ```

## Development Process

### 1. Pick an Issue or Enhancement

Look for:
- Open issues labeled `good first issue` or `help wanted`
- TODO items in SUMMARY.md (Future Enhancements)
- Driver implementations (USB, TFTP, Holotape)
- Security features (crypto, secure boot)

### 2. Create a Branch

```bash
git checkout -b feature/your-feature-name
```

Use prefixes:
- `feature/` - New features
- `fix/` - Bug fixes
- `docs/` - Documentation
- `driver/` - Driver implementations

### 3. Make Your Changes

#### Code Style

**C Code:**
- Use K&R style indentation (4 spaces)
- No tabs, use spaces
- Lines should be ≤100 characters
- Functions should have descriptive names
- Add comments for complex logic

**Example:**
```c
// Good
void initialize_hardware(uint32_t peripheral_base) {
    // Configure GPIO pins for output
    gpio_set_function(LED_PIN, GPIO_FUNC_OUTPUT);
    
    // Set initial state to off
    gpio_clear(LED_PIN);
}

// Bad
void init(uint32_t p){gpio_set_function(17,1);gpio_clear(17);}
```

**Assembly:**
- Use consistent indentation
- Add comments for each section
- Label important code blocks

**Python:**
- Follow PEP 8
- Use type hints where appropriate
- Add docstrings to functions

#### File Organization

Place new code in the appropriate directory:
- `src/` - Core bootloader code
- `src/drivers/` - Device drivers
- `payloads/` - Standalone utilities
- `include/` - Header files
- `tools/` - Build and deployment tools
- `config/` - Configuration files

### 4. Test Your Changes

```bash
# Run validation
./validate.sh

# If you have ARM toolchain:
make clean
make bcm2835
make bcm2836
make bcm2837

# Test on real hardware if possible
```

### 5. Commit Your Changes

Write clear, descriptive commit messages:

```bash
# Good
git commit -m "Add USB mass storage driver initialization"
git commit -m "Fix memory leak in filesystem read function"

# Bad
git commit -m "fixed stuff"
git commit -m "update"
```

### 6. Push and Create Pull Request

```bash
git push origin feature/your-feature-name
```

Then create a PR on GitHub with:
- Clear title describing the change
- Description of what was changed and why
- Link to related issues
- Testing notes

## Specific Contribution Areas

### Implementing Driver Stubs

Current stubs that need implementation:
- `src/drivers/usb.c` - USB mass storage
- `src/drivers/tftp.c` - Network boot
- `src/drivers/holotape.c` - External media

Steps:
1. Study the existing MMC driver
2. Follow the same pattern
3. Add initialization to `main.c`
4. Update configuration in `config/devices.conf`
5. Test thoroughly

### Security Enhancements

Cryptographic features need real implementations:
- `src/crypto.c` - Currently stubs
- Implement SHA-256 hashing
- Add RSA or ECDSA signature verification
- Integrate with `tools/sign_payload.py`

### Filesystem Support

The filesystem layer is basic:
- Implement full FAT32 support
- Add ext2/ext3/ext4 support
- Improve error handling
- Add directory traversal

### Documentation

Help is always needed with:
- Code comments
- README improvements
- Tutorial creation
- Example configurations

## Coding Guidelines

### Memory Management

- Always check allocation success
- Free resources when done
- Avoid memory leaks
- Keep allocations minimal (bare metal constraints)

### Error Handling

```c
// Good
if (init_hardware() != 0) {
    term_print("ERROR: Hardware initialization failed\n");
    return -1;
}

// Avoid silent failures
init_hardware();  // Bad - no error check
```

### Portability

Support all platforms:
```c
#ifdef BCM2835
    #define PERIPHERAL_BASE 0x20000000
#elif defined(BCM2836) || defined(BCM2837)
    #define PERIPHERAL_BASE 0x3F000000
#endif
```

### Size Awareness

MFBootAgent should stay compact:
- Avoid large static buffers
- Use const for read-only data
- Optimize for size when possible
- Check build output size regularly

## Testing Requirements

### Before Submitting PR

- [ ] Code compiles for all targets (BCM2835, BCM2836, BCM2837)
- [ ] No compiler warnings
- [ ] Validation script passes
- [ ] Python tools syntax check passes
- [ ] Tested on real hardware (if possible)
- [ ] Documentation updated
- [ ] SUMMARY.md updated if adding features

### Platform Testing

If possible, test on:
- Raspberry Pi Zero/1 (BCM2835)
- Raspberry Pi 2 (BCM2836)
- Raspberry Pi 3 (BCM2837)

## Review Process

1. Automated checks run on PR
2. Code review by maintainers
3. Testing on available hardware
4. Merge when approved

## Questions?

- Open an issue for questions
- Check existing documentation
- Review RETROS-BIOS integration docs

## License

By contributing, you agree that your contributions will be licensed under the MIT License (see LICENSE file).

---

**RobCo Industries™** - *"Engineering a Better Future, Today!"*
