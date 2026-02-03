#!/usr/bin/env python3
import sys
import struct
import hashlib
from pathlib import Path

def add_firmware_header(input_file, output_file, version):
    """
    Add header to firmware binary:
    - Magic (4 bytes): 0xDEADBEEF
    - Version (4 bytes): major.minor.patch as uint32
    - Size (4 bytes): firmware size
    - SHA256 (32 bytes): firmware hash
    """
    
    # Read original firmware
    with open(input_file, 'rb') as f:
        firmware_data = f.read()
    
    # Calculate hash
    sha256 = hashlib.sha256(firmware_data).digest()
    
    # Parse version (e.g., "1.0.0" -> 0x010000)
    major, minor, patch = map(int, version.split('.'))
    version_uint = (major << 16) | (minor << 8) | patch
    
    # Build header
    magic = 0xDEADBEEF
    size = len(firmware_data)
    
    header = struct.pack('<III', magic, version_uint, size) + sha256
    
    # Write output
    with open(output_file, 'wb') as f:
        f.write(header + firmware_data)
    
    print(f"✓ Firmware prepared:")
    print(f"  Version: {version}")
    print(f"  Size: {size} bytes")
    print(f"  SHA256: {sha256.hex()}")
    print(f"  Output: {output_file}")

if __name__ == '__main__':
    if len(sys.argv) != 4:
        print("Usage: prepare-firmware.py <input.bin> <output.bin> <version>")
        print("Example: prepare-firmware.py app.bin app_signed.bin 1.0.0")
        sys.exit(1)
    
    add_firmware_header(sys.argv[1], sys.argv[2], sys.argv[3])