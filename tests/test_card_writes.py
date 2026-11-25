"""MFBootAgent writes one thing on the card: the delete of /ROBCO/MAINT.REQ.

The boot volume is mounted with write support, so these checks hold the
rest of the stage to reads: no writing call of the file system API is
linked in, fs_unlink() has one caller with the request's path, and every
fs_open() opens for reading only.
"""

import os
import re
import shutil
import subprocess

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")

# Calls of RobCo-Core's fs.h that change the card, besides fs_unlink().
WRITERS = ("fs_write", "fs_write_file", "fs_write_file_atomic", "fs_rename", "fs_mkdir",
           "fs_rmdir", "fs_truncate", "fs_mkfs", "fs_atomic_recover", "fs_read_file_atomic",
           "fs_sync")


def sources():
    for name in sorted(os.listdir(SRC)):
        if name.endswith(".c"):
            with open(os.path.join(SRC, name)) as f:
                yield name, f.read()


def test_no_writing_call_is_linked(mfboot_image):
    nm = shutil.which("arm-none-eabi-nm")
    if nm is None:
        pytest.skip("arm-none-eabi-nm not installed")
    elf = os.path.splitext(mfboot_image)[0] + ".elf"
    out = subprocess.run([nm, elf], check=True, capture_output=True, text=True).stdout
    defined = {line.split()[-1] for line in out.splitlines() if " T " in line}
    assert "fs_unlink" in defined
    assert not defined & set(WRITERS), sorted(defined & set(WRITERS))


def test_one_delete_and_read_only_opens():
    unlinks = []
    for name, text in sources():
        unlinks += [(name, m.group(1)) for m in re.finditer(r"\bfs_unlink\(([^)]+)\)", text)]
        for m in re.finditer(r"\bfs_open\(([^;]*)\)\s*[!=;]", text):
            flags = m.group(1).rsplit(",", 1)[-1].strip()
            assert flags == "FS_READ", (name, m.group(0))
        for w in WRITERS:
            assert not re.search(rf"\b{w}\([^)]", text), (name, w)
    assert unlinks == [("main.c", "MFB_REQ_PATH")], unlinks
