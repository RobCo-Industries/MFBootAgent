"""An operating image whose bytes are the same on every host, for the goldens.

The compiled test OS (tests/os) changes size and CRC with the toolchain.
The menu screen shows neither, but the golden of the handoff screen must
not depend on which image ran. This one is a few fixed ARM words sealed
by RobCo-Core's tools/rimg.py, so its size and CRC are constants.

Run as a script to write the image: python3 tests/fixed_os.py OUT
"""

import hashlib
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "external", "robco-core", "tools"))

import rimg  # noqa: E402

# ARM (A32) code, ARMv6 and later. It prints nothing, so the screen stays
# as MFBootAgent left it.
WORDS = [
    0xF10C00C0,  # cpsid if    mask IRQ and FIQ
    0xE320F003,  # wfi         wait for an interrupt (ARMv6K hint)
    0xEAFFFFFD,  # b   wfi     back to the wfi, forever
]
NAME = "fixedos"
LOAD_ADDR = 0x00400000  # OS slot base
SUBTYPE = 0             # RIMG_BOOT_UOS
ABI_VERSION = 2 << 16   # BOOT ABI major 2

# Pinned: a change in WORDS, in the fields above or in rimg.py shows here.
SIZE = 108
CRC = 0x88BEC7A4
SHA256 = "c82c02207f56bd3f71631ae186cf0fbf5fa97b8a477575e3738b67ade9946791"


def build():
    payload = struct.pack(f"<{len(WORDS)}I", *WORDS)
    return rimg.pack(payload, "BOOT", SUBTYPE, LOAD_ADDR, name=NAME,
                     abi_version=ABI_VERSION)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


if __name__ == "__main__":
    data = build()
    with open(sys.argv[1], "wb") as f:
        f.write(data)
    print(f"size={len(data)} crc={struct.unpack_from('<I', data, 0x3C)[0]:#010x} "
          f"sha256={sha256(data)}")
