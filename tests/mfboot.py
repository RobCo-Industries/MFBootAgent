"""Helpers for the MFBootAgent target tests."""

import contextlib
import os
import re
import struct
import tempfile
import time
import zlib

from PIL import Image

from harness.boot import boot, parse_markers
from harness.sdimage import make_sd_image

SLOT = 0x00200000
OS_SLOT = 0x00400000
CFG = "/ROBCO/BOOT.CFG"
UOS = "/ROBCO/UOS.SYS"
MAINT = "/ROBCO/MAINT.SYS"
DIAG = "/ROBCO/DIAG.SYS"
PART_LBA = 2048
MACH = {"raspi0": 0xC42, "raspi1ap": 0xC42, "raspi2b": 0xC43, "raspi3b": 0xC43}
STAGE_BIOS = 1
STAGE_BOOTAGENT = 2
BOOT_FALLBACK = 1 << 0
BOOT_COLD = 1 << 2
SUBTYPE = {"uos": 0, "maint": 2, "diag": 3}
TIMEOUT = 60
GOLDEN_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "golden")

# rimg header offsets, IMAGE_FORMAT.md.
OFF_MAGIC = 0x20
OFF_HDR_VERSION = 0x28
OFF_LOAD_ADDR = 0x30
OFF_FILE_SIZE = 0x34
OFF_CRC = 0x3C
OFF_ABI = 0x40
OFF_SUBTYPE = 0x48

# A tiny image for 0x8000 that jumps to MFBootAgent: ldr pc, [pc, #-4]; .word 0x00200000.
# It stands in for a first stage that hands over nothing: r3 holds no boot info.
TRAMPOLINE = struct.pack("<II", 0xE51FF004, SLOT)

# The default card: three entries, no timeout, so the menu waits for a key.
WAIT_CFG = b"""[boot]
default = UOS

[entry UOS]
path = /ROBCO/UOS.SYS
cmdline = console=tty0 quiet

[entry MAINT]
path = /ROBCO/MAINT.SYS

[entry DIAG]
path = /ROBCO/DIAG.SYS
cmdline = level=2
"""

READY = re.compile(r"@RC stage=bootagent ev=ready\r?\n")
IDLE = re.compile(r"@RC stage=bootagent ev=idle\r?\n")
JUMP = re.compile(r"@RC stage=bootagent ev=jump [^\n]*\n")
OS_READY = re.compile(r"@RC stage=testos ev=ready\r?\n")
MENU = re.compile(r"@RC stage=bootagent ev=menu [^\n]*\n")


def events(vm, name, stage="bootagent"):
    return [m for m in vm.markers() if m.get("stage") == stage and m.get("ev") == name]


def one(vm, name, stage="bootagent"):
    found = events(vm, name, stage)
    assert len(found) == 1, f"{stage} {name}: {len(found)} found\n{vm.serial}"
    return found[0]


def order(vm, stage="bootagent"):
    return [m["ev"] for m in vm.markers() if m.get("stage") == stage]


def card(tmp_path, files, cfg=WAIT_CFG, name="sd.img"):
    """An SD card image with `files` ({"/ROBCO/X": bytes}) and BOOT.CFG (None: none)."""
    content = {k.lstrip("/"): v for k, v in files.items()}
    if cfg is not None:
        content[CFG.lstrip("/")] = cfg
    if not content:
        content["ROBCO/"] = b""
    sd = tmp_path / name
    make_sd_image(sd, 16, content)
    return sd


def os_files(test_os):
    return {UOS: test_os["uos"], MAINT: test_os["maint"], DIAG: test_os["diag"]}


def reseal(data):
    data = bytearray(data)
    struct.pack_into("<I", data, OFF_CRC, 0)
    struct.pack_into("<I", data, OFF_CRC, zlib.crc32(data) & 0xFFFFFFFF)
    return bytes(data)


def patched(image, offset, fmt, value, seal=True):
    data = bytearray(image)
    struct.pack_into(fmt, data, offset, value)
    return reseal(data) if seal else bytes(data)


def flip(image, offset=0x200):
    data = bytearray(image)
    data[offset] ^= 0x01
    return bytes(data)


@contextlib.contextmanager
def alone(mfboot_image, board, sd=None, extra_args=None):
    """Boots MFBootAgent with no first stage: QEMU loads it at its link address."""
    with tempfile.TemporaryDirectory(prefix="mfb") as tmp:
        tramp = os.path.join(tmp, "tramp.img")
        with open(tramp, "wb") as f:
            f.write(TRAMPOLINE)
        args = ["-device", f"loader,file={mfboot_image},addr={SLOT:#x},force-raw=on"]
        with boot(tramp, board, sd=sd, serial2=True,
                  extra_args=args + list(extra_args or []),
                  elf=os.path.splitext(mfboot_image)[0] + ".elf") as vm:
            yield vm


@contextlib.contextmanager
def after(first_image, board, sd, extra_args=None):
    """Boots a first stage from 0x8000 that loads MFBootAgent from the card."""
    with boot(first_image, board, sd=sd, serial2=True, extra_args=extra_args) as vm:
        yield vm


def dump(vm):
    """The screen text from the maintenance port: (header marker, rows)."""
    reply = vm.script("dump", timeout=TIMEOUT)
    assert reply.status == "ok", reply
    head = parse_markers(reply.lines[0] + "\n")[0]
    rows = [line[1:] for line in reply.lines if line.startswith("|")]
    return head, rows


def state(vm):
    """The state line, once the stage has taken every queued input event.

    The port answers between input events, so a reply can come while
    keys sent before it still wait in the queue. Asks again until
    pending=0: then the stage handled and drew every earlier key.
    """
    deadline = time.monotonic() + TIMEOUT
    while True:
        reply = vm.script("state", timeout=TIMEOUT)
        assert reply.status == "ok" and len(reply.lines) == 1, reply
        st = dict(field.split("=", 1) for field in reply.lines[0].split())
        if st["pending"] == "0":
            return st
        assert time.monotonic() < deadline, reply


def key(vm, name):
    """Presses and releases one key through the maintenance port."""
    reply = vm.script(f"key {name}", timeout=TIMEOUT)
    assert reply.status == "ok", reply


def char(vm, c):
    reply = vm.script(f"char {c}", timeout=TIMEOUT)
    assert reply.status == "ok", reply


def screenshot(vm, tmp_path, name="screen"):
    ppm = tmp_path / f"{name}.ppm"
    vm.screendump(ppm)
    with Image.open(ppm) as img:
        return img.convert("RGB")


# The console font and schemes from RobCo-Core's own console tests.
from test_console import FH, FW, SCHEMES, load_font  # noqa: E402

WAIT_N1 = "@RC stage=chain2 ev=wait n=1\n"


def render_rows(rows, scheme, width=640):
    """The pixels of `rows` of text in the normal colour of console scheme
    `scheme` (the boot info's id, from 0), as the console draws them."""
    _, bg, _dim, normal, _bright = SCHEMES[str(scheme + 1)]
    font = load_font()
    img = Image.new("RGB", (width, len(rows) * FH), bg)
    px = img.load()
    for r, text in enumerate(rows):
        for c, ch in enumerate(text):
            glyph = font[ord(ch)]
            for y in range(FH):
                for x in range(FW):
                    if glyph[y] & (0x80 >> x):
                        px[c * FW + x, r * FH + y] = normal
    return img


def text_rows(img, first, count):
    """Text rows first..first+count-1 of a screenshot, as an image."""
    return img.crop((0, first * FH, img.size[0], (first + count) * FH))


def same_pixels(a, b):
    return a.size == b.size and a.tobytes() == b.tobytes()


def check_golden(img, name):
    """Compares with tests/golden/<name>.png. ROBCO_UPDATE_GOLDEN=1 rewrites it."""
    path = os.path.join(GOLDEN_DIR, f"{name}.png")
    if os.environ.get("ROBCO_UPDATE_GOLDEN") == "1":
        os.makedirs(GOLDEN_DIR, exist_ok=True)
        img.save(path)
    with Image.open(path) as golden:
        assert golden.size == img.size, f"size differs from {path}"
        assert golden.convert("RGB").tobytes() == img.tobytes(), f"screen differs from {path}"


def os_checks(vm):
    """The test OS's check results as {name: marker}; asserts every check passed."""
    checks = {m["name"]: m for m in events(vm, "check", "testos")}
    failed = [n for n, m in checks.items() if m["result"] != "pass"]
    assert not failed, f"failed checks {failed}\n{vm.serial}"
    summary = one(vm, "checks", "testos")
    assert summary["failed"] == "0" and int(summary["passed"]) == len(checks), vm.serial
    assert len(checks) > 0, vm.serial
    return checks
