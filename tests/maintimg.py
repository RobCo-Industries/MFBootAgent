"""Helpers for the maintenance image's target tests, tests/test_maint_*.py."""

import contextlib
import os
import re
import struct
import tempfile

import pytest

import mfboot
from harness.boot import boot
from harness.sdimage import minimal_env
# The image's port shares the agent's state and dump protocol: state()
# waits for pending=0, batch() sends raw lines and reads every reply.
from mfboot import batch, dump, state  # noqa: F401

SLOT = 0x00400000
# ldr pc, [pc, #-4]; .word 0x00400000: a first stage that hands over nothing.
TRAMPOLINE = struct.pack("<II", 0xE51FF004, SLOT)
TIMEOUT = 60

READY = re.compile(r"@RC stage=maint ev=ready\r?\n")
BOOT = re.compile(r"@RC stage=maint ev=boot [^\n]*\n")

# The clock chip of the terminal controller board on I2C1.
RTC = ["-device", "ds1338,bus=i2c-bus.1,address=0x68"]

TOOLS = ["check_card", "check_images", "view_file", "set_clock", "restore_users", "restart"]

USERS_DEF = "/UOS/SYSUAF.DEF"
USERS_DAT = "/UOS/SYSUAF.DAT"

# The menu rows before any tool, as `dump` shows them.
MENU_ROWS = [
    "TERMINAL MAINTENANCE V0.1.0",
    "Card, clock and user file tools. Each result also goes to the serial port.",
    "========================================",
    "",
    "> 1  CHECK CARD    Read every file of the card",
    "  2  CHECK IMAGES  Validate each image in /ROBCO",
    "  3  VIEW FILE     Show a configuration file",
    "  4  SET CLOCK     Set the clock chip",
    "  5  RESTORE USERS Copy the default user file",
    "  6  RESTART       Restart the terminal",
    "",
    "UP/DOWN, 1-6: select   ENTER: start   ESC: stop or cancel",
    "Choose a tool.",
    "========================================",
]


def build_path(build_dir, test_build=False):
    if test_build:
        return os.path.join(build_dir, "mainttest", "maint.img")
    return os.path.join(build_dir, "maint.sys")


def elf_path(image):
    base = os.path.dirname(image)
    if os.path.basename(image) == "maint.sys":
        return os.path.join(base, "maint", "maint.elf")
    return os.path.join(base, "maint.elf")


@contextlib.contextmanager
def alone(image, board, sd=None, devices=(), wait=True, qtest=False, gpio_high=()):
    """Boots the maintenance image with no earlier stage: QEMU loads it in the OS slot."""
    with tempfile.TemporaryDirectory(prefix="maint") as tmp:
        tramp = os.path.join(tmp, "tramp.img")
        with open(tramp, "wb") as f:
            f.write(TRAMPOLINE)
        args = ["-device", f"loader,file={image},addr={SLOT:#x},force-raw=on"]
        with boot(tramp, board, sd=sd, serial2=True, extra_args=args + list(devices),
                  elf=elf_path(image), qtest=qtest, gpio_high=gpio_high) as vm:
            if wait:
                vm.wait_for(READY, timeout=TIMEOUT)
            yield vm


def events(vm, name):
    return [m for m in vm.markers() if m.get("stage") == "maint" and m.get("ev") == name]


def one(vm, name):
    found = events(vm, name)
    assert len(found) == 1, f"maint {name}: {len(found)} found\n{vm.serial}"
    return found[0]


def results(vm, name):
    return [m for m in events(vm, "tool") if m.get("name") == name]


def command(vm, line):
    reply = vm.script(line, timeout=TIMEOUT)
    assert reply.status == "ok", (line, reply)
    return reply


def wait_result(vm, name, count, timeout=TIMEOUT):
    """Waits for the count-th tool marker of `name` and returns it."""
    vm.wait_for(re.compile(r"(?:@RC stage=maint ev=tool name=%s [^\n]*\n[\s\S]*?){%d}"
                           % (name, count)), timeout=timeout)
    return results(vm, name)[count - 1]


def run(vm, name, timeout=TIMEOUT):
    """Starts `tool <name>` through the maintenance port and returns its tool marker."""
    before = len(results(vm, name))
    command(vm, f"tool {name}")
    return wait_result(vm, name, before + 1, timeout)


def start(vm, name):
    """Starts an interactive tool through the port and waits until it runs."""
    command(vm, f"tool {name}")
    st = state(vm)
    assert (st["phase"], st["tool"]) == ("tool", name), st
    return st


def key(vm, name):
    command(vm, f"key {name}")


def char(vm, c):
    command(vm, f"char {c}")


def card(tmp_path, files, cfg=mfboot.WAIT_CFG, name="sd.img"):
    """A card with `files` ({"/DIR/X": bytes}) and BOOT.CFG (None: none)."""
    return mfboot.card(tmp_path, files, cfg=cfg, name=name)


def read_card_file(sd, path):
    """The bytes of `path` on the card image, or None when it is missing."""
    import subprocess
    out = subprocess.run(["mtype", "-i", f"{sd}@@{mfboot.PART_LBA * 512}", "::" + path],
                         capture_output=True, env=minimal_env(MTOOLS_SKIP_CHECK="1"))
    if out.returncode != 0:
        return None
    return out.stdout


def card_names(sd, folder):
    """The names in `folder` of the card image."""
    import subprocess
    out = subprocess.run(["mdir", "-b", "-i", f"{sd}@@{mfboot.PART_LBA * 512}", "::" + folder],
                         capture_output=True, text=True, env=minimal_env(MTOOLS_SKIP_CHECK="1"))
    assert out.returncode == 0, out.stderr
    return sorted(os.path.basename(p.rstrip("/")) for p in out.stdout.split())


def maint_cfg(extra):
    """WAIT_CFG with `extra` text after it, such as a [maint] section."""
    return mfboot.WAIT_CFG + extra


# ---- Fixtures: import them into each test file ----------------------------


@pytest.fixture(scope="session")
def maint_image(mfboot_image, build_dir):
    """build/maint.sys, built with the agent by `make all`."""
    path = build_path(build_dir)
    assert os.path.isfile(path), path
    return path


@pytest.fixture(scope="session")
def maint_test_image(mfboot_image, build_dir):
    """build/mainttest/maint.img: the test build with the write probe."""
    path = build_path(build_dir, test_build=True)
    assert os.path.isfile(path), path
    return path


@pytest.fixture
def good_files(maint_image, test_os):
    """The files of a good card: the test UOS and DIAG, the real MAINT.SYS."""
    with open(maint_image, "rb") as f:
        maint = f.read()
    return {mfboot.UOS: test_os["uos"], mfboot.MAINT: maint, mfboot.DIAG: test_os["diag"]}


@pytest.fixture
def good_card(good_files, need_mtools, tmp_path):
    return card(tmp_path, good_files)
