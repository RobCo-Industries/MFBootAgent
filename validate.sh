#!/bin/bash
# validate.sh - Basic validation without requiring ARM toolchain

echo "MFBootAgent Validation Script"
echo "=============================="
echo ""

# Check directory structure
echo "Checking directory structure..."
for dir in src include config payloads tools src/drivers; do
    if [ -d "$dir" ]; then
        echo "  ✓ $dir exists"
    else
        echo "  ✗ $dir missing"
        exit 1
    fi
done
echo ""

# Check required files
echo "Checking required files..."
required_files=(
    "Makefile"
    "linker.ld"
    "README.md"
    "BUILD.md"
    "LICENSE"
    ".gitignore"
)

for file in "${required_files[@]}"; do
    if [ -f "$file" ]; then
        echo "  ✓ $file exists"
    else
        echo "  ✗ $file missing"
        exit 1
    fi
done
echo ""

# Check source files
echo "Checking source files..."
src_files=(
    "src/stage2.S"
    "src/main.c"
    "src/terminal.c"
    "src/hardware.c"
    "src/memory_mgr.c"
    "src/filesystem.c"
    "src/loader.c"
    "src/menu.c"
    "src/maintenance.c"
    "src/utils.c"
    "src/crypto.c"
)

for file in "${src_files[@]}"; do
    if [ -f "$file" ]; then
        echo "  ✓ $file exists"
    else
        echo "  ✗ $file missing"
        exit 1
    fi
done
echo ""

# Check header files
echo "Checking header files..."
header_files=(
    "include/mfboot.h"
    "include/protocols.h"
    "include/termlink.h"
    "include/terminal.h"
    "include/memory_mgr.h"
    "include/filesystem.h"
    "include/loader.h"
    "include/hardware.h"
)

for file in "${header_files[@]}"; do
    if [ -f "$file" ]; then
        echo "  ✓ $file exists"
    else
        echo "  ✗ $file missing"
        exit 1
    fi
done
echo ""

# Check drivers
echo "Checking driver files..."
driver_files=(
    "src/drivers/mmc.c"
    "src/drivers/usb.c"
    "src/drivers/tftp.c"
    "src/drivers/holotape.c"
)

for file in "${driver_files[@]}"; do
    if [ -f "$file" ]; then
        echo "  ✓ $file exists"
    else
        echo "  ✗ $file missing"
        exit 1
    fi
done
echo ""

# Check payloads
echo "Checking payload files..."
payload_files=(
    "payloads/emergency_shell.c"
    "payloads/diagnostics.c"
)

for file in "${payload_files[@]}"; do
    if [ -f "$file" ]; then
        echo "  ✓ $file exists"
    else
        echo "  ✗ $file missing"
        exit 1
    fi
done
echo ""

# Check tools
echo "Checking tool files..."
tool_files=(
    "tools/mkbootimg.py"
    "tools/sign_payload.py"
)

for file in "${tool_files[@]}"; do
    if [ -f "$file" ]; then
        echo "  ✓ $file exists"
        if [ -x "$file" ]; then
            echo "    (executable)"
        else
            echo "    ⚠ not executable (run: chmod +x $file)"
        fi
    else
        echo "  ✗ $file missing"
        exit 1
    fi
done
echo ""

# Check config files
echo "Checking config files..."
config_files=(
    "config/bootmenu.conf"
    "config/devices.conf"
)

for file in "${config_files[@]}"; do
    if [ -f "$file" ]; then
        echo "  ✓ $file exists"
    else
        echo "  ✗ $file missing"
        exit 1
    fi
done
echo ""

# Count lines of code
echo "Code statistics:"
c_lines=$(find src payloads -name "*.c" -exec wc -l {} + | tail -1 | awk '{print $1}')
h_lines=$(find include -name "*.h" -exec wc -l {} + | tail -1 | awk '{print $1}')
s_lines=$(find src -name "*.S" -exec wc -l {} + | tail -1 | awk '{print $1}')
total_lines=$((c_lines + h_lines + s_lines))
echo "  C source files: $c_lines lines"
echo "  Header files: $h_lines lines"
echo "  Assembly files: $s_lines lines"
echo "  Total: $total_lines lines"
echo ""

# Python syntax check
echo "Checking Python tool syntax..."
for tool in tools/*.py; do
    if python3 -m py_compile "$tool" 2>/dev/null; then
        echo "  ✓ $tool syntax OK"
    else
        echo "  ✗ $tool has syntax errors"
        exit 1
    fi
done
echo ""

echo "=============================="
echo "✓ All validation checks passed!"
echo ""
echo "Note: Full compilation requires ARM cross-compiler (arm-none-eabi-gcc)"
echo "See BUILD.md for installation instructions."
