"""Helpers for the diagnostics image's target tests, tests/test_diag*.py."""

import contextlib
import os
import re
import struct
import tempfile

import timing_skips

from harness.boot import boot, parse_markers

DIAG_SLOT = 0x00400000
# ldr pc, [pc, #-4]; .word 0x00400000: a first stage that hands over nothing.
TRAMPOLINE = struct.pack("<II", 0xE51FF004, DIAG_SLOT)
TIMEOUT = 60

READY = re.compile(r"@RC stage=diag ev=ready\r?\n")
DONE = re.compile(r"@RC stage=diag ev=diag_done [^\n]*\n")

# The clock chip and the two expanders of the terminal controller board on I2C1.
RTC = ["-device", "ds1338,bus=i2c-bus.1,address=0x68"]
X0 = ["-device", "pca9554,bus=i2c-bus.1,address=0x20,id=gx0"]
X1 = ["-device", "pca9554,bus=i2c-bus.1,address=0x21,id=gx1"]

TESTS = ["mem", "sd", "video", "audio", "input", "timer", "rng", "rtc", "expander"]

# The menu rows before any test, as `dump` shows them.
MENU_ROWS = [
    "TERMINAL DIAGNOSTICS V0.1.0",
    "Tests for this terminal. Each result also goes to the serial port.",
    "========================================",
    "",
    "> 1  MEM       RAM write and read",
    "  2  SD        Card reads, image checks",
    "  3  VIDEO     Screen patterns",
    "  4  AUDIO     Tones and a short tune",
    "  5  INPUT     Live view of every key",
    "  6  TIMER     Timer against the tick",
    "  7  RNG       Random number generator",
    "  8  RTC       Clock chip, read only",
    "  9  EXPANDER  I/O expanders, read only",
    "  A  RUN ALL   Every test without a person",
    "  R  REBOOT    Restart the board",
    "",
    "UP/DOWN, 1-9, A, R: select   ENTER: start   ESC: stop a test",
    "Choose a test.",
    "========================================",
]


def build_path(build_dir, test_build=False):
    if test_build:
        return os.path.join(build_dir, "diagtest", "diag.img")
    return os.path.join(build_dir, "diag.sys")


def elf_path(image):
    base = os.path.dirname(image)
    if os.path.basename(image) == "diag.sys":
        return os.path.join(base, "diag", "diag.elf")
    return os.path.join(base, "diag.elf")


@contextlib.contextmanager
def alone(image, board, sd=None, devices=(), qtest=False):
    """Boots the diagnostics image with no earlier stage: QEMU loads it in the OS slot."""
    with tempfile.TemporaryDirectory(prefix="diag") as tmp:
        tramp = os.path.join(tmp, "tramp.img")
        with open(tramp, "wb") as f:
            f.write(TRAMPOLINE)
        args = ["-device", f"loader,file={image},addr={DIAG_SLOT:#x},force-raw=on"]
        with boot(tramp, board, sd=sd, serial2=True, extra_args=args + list(devices),
                  elf=elf_path(image), qtest=qtest) as vm:
            vm.wait_for(READY, timeout=TIMEOUT)
            yield vm


def events(vm, name):
    return [m for m in vm.markers() if m.get("stage") == "diag" and m.get("ev") == name]


def one(vm, name):
    found = events(vm, name)
    assert len(found) == 1, f"diag {name}: {len(found)} found\n{vm.serial}"
    return found[0]


def results(vm, name):
    return [m for m in events(vm, "diag") if m.get("name") == name]


def command(vm, line):
    reply = vm.script(line, timeout=TIMEOUT)
    assert reply.status == "ok", (line, reply)
    return reply


def run(vm, name, timeout=TIMEOUT):
    """Starts `diag <name>` through the maintenance port and returns its diag marker."""
    before = len(results(vm, name))
    command(vm, f"diag {name}")
    vm.wait_for(re.compile(r"(?:@RC stage=diag ev=diag name=%s [^\n]*\n[\s\S]*?){%d}"
                           % (name, before + 1)), timeout=timeout)
    return results(vm, name)[before]


def run_all(vm, timeout=TIMEOUT):
    before = len(events(vm, "diag_done"))
    command(vm, "diag all")
    vm.wait_for(re.compile(r"(?:@RC stage=diag ev=diag_done [^\n]*\n[\s\S]*?){%d}" % (before + 1)),
                timeout=timeout)
    return events(vm, "diag_done")[before]


def timer_starved(m):
    """The numbers of a failed timer run that missed ticks alone explain, else None.

    On a busy host QEMU can stop the guest for longer than a tick period, so
    the image counts missed ticks and fails, as it must on a real board. The
    run is explained when ticks were missed and the timer time covers the
    counted and the missed periods within one period: the timer itself ran
    at the right rate.
    """
    keys = ("ticks", "hz", "us", "missed", "window_missed")
    if m.get("result") != "fail" or any(k not in m for k in keys):
        return None
    ticks, hz, us, missed, window = (int(m[k]) for k in keys)
    if missed == 0 or hz == 0:
        return None
    period = 1000000 // hz
    want = (ticks + window) * period
    if abs(us - want) > period:
        return None
    return (f"timer missed {missed} ticks on a busy host, {window} inside the window: "
            f"us={us} for {ticks}+{window} periods of {period} us, ppm={m['ppm']} "
            f"limit={m['limit']}")


def skip_if_timer_starved(vm, want):
    """Skips a run all whose only failure is a timer that missed ticks on a busy host.

    want is the expected list of (test, result). Any other difference fails.
    """
    got = [(m["name"], m["result"]) for m in events(vm, "diag")]
    if got == want:
        return
    timer = [m for m in events(vm, "diag") if m["name"] == "timer"]
    starved = timer_starved(timer[0]) if len(timer) == 1 else None
    expect = [(n, "fail" if n == "timer" else r) for n, r in want]
    assert starved is not None and got == expect, vm.serial
    timing_skips.skip(starved)


def skip_if_timer_alone_starved(vm, m):
    """Skips a timer run started alone that failed only by missed ticks on a busy host.

    m is the timer's diag marker. A pass returns. Any other failure fails.
    """
    if m.get("result") == "ok":
        return
    starved = timer_starved(m)
    assert starved is not None, vm.serial
    timing_skips.skip(starved)


def wait_state(vm, phase, timeout=TIMEOUT):
    """Polls `state` until the phase is `phase`."""
    import time
    deadline = time.monotonic() + timeout
    while True:
        st = state(vm)
        if st["phase"] == phase:
            return st
        assert time.monotonic() < deadline, st
        time.sleep(0.1)


def key(vm, name, value=None):
    command(vm, f"key {name}" + ("" if value is None else f" {value}"))


# ---- Fixtures: import them into each test file ----------------------------

import pytest  # noqa: E402

import mfboot  # noqa: E402
# The image's port shares the agent's state and dump protocol: state()
# waits for pending=0, batch() sends raw lines and reads every reply.
from mfboot import batch, dump, state  # noqa: E402,F401


@pytest.fixture(scope="session")
def diag_image(mfboot_image, build_dir):
    """build/diag.sys, built with the agent by `make all`."""
    path = build_path(build_dir)
    assert os.path.isfile(path), path
    return path


@pytest.fixture(scope="session")
def diag_test_image(mfboot_image, build_dir):
    """build/diagtest/diag.img: the test build with the fault command."""
    path = build_path(build_dir, test_build=True)
    assert os.path.isfile(path), path
    return path


@pytest.fixture
def good_card(diag_image, test_os, need_mtools, tmp_path):
    """A card with BOOT.CFG, UOS.SYS, MAINT.SYS and the real DIAG.SYS."""
    with open(diag_image, "rb") as f:
        diag = f.read()
    files = {mfboot.UOS: test_os["uos"], mfboot.MAINT: test_os["maint"], mfboot.DIAG: diag}
    return mfboot.card(tmp_path, files)
