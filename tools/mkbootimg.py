#!/usr/bin/env python3
"""
mkbootimg.py - Create boot images for MFBootAgent
Copyright 2201-2203 Robco Ind.
"""

import sys
import struct
import argparse
from pathlib import Path

# Boot image magic number
BOOT_MAGIC = 0x544F4F42  # "BOOT"

def create_boot_image(kernel_path, output_path, load_addr=0x8000, boot_type=0):
    """
    Create a boot image from a kernel file.
    
    Args:
        kernel_path: Path to kernel binary
        output_path: Path to output boot image
        load_addr: Load address for kernel (default 0x8000)
        boot_type: Boot type (0=UOS, 1=PipOS, 2=Maint, 3=Diag)
    """
    try:
        # Read kernel file
        with open(kernel_path, 'rb') as f:
            kernel_data = f.read()
        
        kernel_size = len(kernel_data)
        
        # Create boot image header
        # Format: magic (4), version (4), type (4), load_addr (4), size (4), checksum (4)
        header = struct.pack('<IIIIII', 
                           BOOT_MAGIC,      # Magic
                           0x00010000,      # Version 1.0
                           boot_type,       # Boot type
                           load_addr,       # Load address
                           kernel_size,     # Kernel size
                           0)               # Checksum (placeholder)
        
        # Calculate simple checksum
        checksum = sum(kernel_data) & 0xFFFFFFFF
        header = struct.pack('<IIIIII',
                           BOOT_MAGIC,
                           0x00010000,
                           boot_type,
                           load_addr,
                           kernel_size,
                           checksum)
        
        # Write boot image
        with open(output_path, 'wb') as f:
            f.write(header)
            f.write(kernel_data)
        
        print(f"Boot image created: {output_path}")
        print(f"  Kernel size: {kernel_size} bytes")
        print(f"  Load address: 0x{load_addr:08X}")
        print(f"  Checksum: 0x{checksum:08X}")
        
        return 0
        
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1

def main():
    parser = argparse.ArgumentParser(description='Create boot images for MFBootAgent')
    parser.add_argument('kernel', help='Kernel binary file')
    parser.add_argument('-o', '--output', required=True, help='Output boot image file')
    parser.add_argument('-a', '--addr', type=lambda x: int(x, 0), default=0x8000,
                       help='Load address (default: 0x8000)')
    parser.add_argument('-t', '--type', type=int, choices=[0, 1, 2, 3], default=0,
                       help='Boot type: 0=UOS, 1=PipOS, 2=Maintenance, 3=Diagnostic')
    
    args = parser.parse_args()
    
    if not Path(args.kernel).exists():
        print(f"Error: Kernel file not found: {args.kernel}", file=sys.stderr)
        return 1
    
    return create_boot_image(args.kernel, args.output, args.addr, args.type)

if __name__ == '__main__':
    sys.exit(main())
