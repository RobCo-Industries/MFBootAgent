"""GPIO buttons driven through qtest with harness.pigpio, on the agent and on DIAG.SYS.

QEMU models no pulls, so an undriven line reads 0, which an active-low
button reads as pressed. Each test that wants working buttons drives
every button line high before the guest starts.

No check here depends on host timing. A press is held until the guest
reports its effect, then released, so a host stall can only make the
test slower. UP and DOWN repeat after 500 ms held: the menus here have
two entries, so a repeat at an end changes nothing, and the tests count
the moves.
"""

import re
import time

import diag
from diag import diag_image  # noqa: F401
from harness.pigpio import PiGpio
from mfboot import (OS_READY, READY, TIMEOUT, alone, card, events, key, one, os_files,
                    state)

# src/maint.c and diag/main.c, BCM numbers. The dial is DIAG.SYS only.
PINS = {"up": 5, "down": 6, "left": 17, "right": 22, "select": 26, "back": 27}
DIAG_PINS = dict(PINS, dial_push=16, dial_a=23, dial_b=24)

TWO_CFG = b"""[boot]
default = UOS
%s
[entry UOS]
path = /ROBCO/UOS.SYS

[entry MAINT]
path = /ROBCO/MAINT.SYS
"""


def two(tmp_path, test_os, extra=b""):
    return card(tmp_path, os_files(test_os), cfg=TWO_CFG % extra)


def wait_count(vm, name, n, stage="bootagent"):
    deadline = time.monotonic() + TIMEOUT
    while len(events(vm, name, stage)) < n:
        assert time.monotonic() < deadline, vm.serial
        time.sleep(0.02)
    return events(vm, name, stage)


def hold_until(io, vm, button, name, stage="bootagent"):
    """Presses `button` until one more `name` marker shows, then releases it."""
    n = len(events(vm, name, stage))
    io.press(button)
    try:
        return wait_count(vm, name, n + 1, stage)[n]
    finally:
        io.release(button)


def test_menu_up_down_select(board, mfboot_image, test_os, need_mtools, tmp_path):
    """The buttons alone move the bar and boot the entry under it."""
    with alone(mfboot_image, board, two(tmp_path, test_os), qtest=True,
               gpio_high=PINS.values()) as vm:
        io = PiGpio(vm, board, names=PINS)
        vm.wait_for(READY, timeout=TIMEOUT)
        inp = one(vm, "input")
        assert (inp["buttons"], inp["live"]) == ("active", "serial,gpio,script"), inp
        assert hold_until(io, vm, "down", "select")["index"] == "2"
        assert state(vm)["sel"] == "2"
        assert hold_until(io, vm, "up", "select")["index"] == "1"
        assert hold_until(io, vm, "down", "select")["index"] == "2"
        assert state(vm)["sel"] == "2"
        n = len(events(vm, "choose"))
        io.press("select")
        try:
            choose = wait_count(vm, "choose", n + 1)[n]
        finally:
            io.release("select")
        assert (choose["index"], choose["via"]) == ("2", "key"), choose
        vm.wait_for(OS_READY, timeout=TIMEOUT)


def test_back_held_at_start_opens_maintenance(board, mfboot_image, test_os, need_mtools,
                                              tmp_path):
    """BACK held through the guard comes as a press the menu takes: the screen opens.

    BOOT.md: the presses the guard reports for held buttons stay queued
    for the menu, and BACK from the buttons opens the maintenance screen.
    """
    high = [p for name, p in PINS.items() if name != "back"]
    with alone(mfboot_image, board, two(tmp_path, test_os, b"timeout = 5\n"), qtest=True,
               gpio_high=high) as vm:
        io = PiGpio(vm, board, names=PINS)
        vm.wait_for(READY, timeout=TIMEOUT)
        assert one(vm, "input")["buttons"] == "active"
        m = wait_count(vm, "maint_open", 1)[0]
        assert m["why"] == "button", m
        io.release("back")
        st = state(vm)
        assert (st["phase"], st["maint"]) == ("menu", "1"), st
        assert not events(vm, "choose")


def test_down_held_at_start_stops_the_countdown(board, mfboot_image, test_os, need_mtools,
                                                tmp_path):
    """DOWN held at start moves the bar and stops the countdown, like any key."""
    high = [p for name, p in PINS.items() if name != "down"]
    with alone(mfboot_image, board, two(tmp_path, test_os, b"timeout = 3\n"), qtest=True,
               gpio_high=high) as vm:
        io = PiGpio(vm, board, names=PINS)
        vm.wait_for(READY, timeout=TIMEOUT)
        sel = wait_count(vm, "select", 1)[0]
        io.release("down")
        assert sel["index"] == "2", sel
        vm.wait_for("ev=countdown state=stopped", timeout=TIMEOUT)
        # Past the countdown's 3 s: nothing boots.
        time.sleep(4)
        st = state(vm)
        assert (st["phase"], st["sel"]) == ("menu", "2"), st
        assert not events(vm, "choose")


def test_select_held_at_start_boots_the_default(board, mfboot_image, test_os, need_mtools,
                                                tmp_path):
    """USER_MANUAL.md: hold SELECT at power-on to boot the default entry at once."""
    high = [p for name, p in PINS.items() if name != "select"]
    with alone(mfboot_image, board, two(tmp_path, test_os, b"timeout = 30\n"), qtest=True,
               gpio_high=high) as vm:
        io = PiGpio(vm, board, names=PINS)
        choose = wait_count(vm, "choose", 1)[0]
        io.release("select")
        assert (choose["index"], choose["via"]) == ("1", "key"), choose
        vm.wait_for(OS_READY, timeout=TIMEOUT)


def test_all_lines_low_disables_the_buttons(board, mfboot_image, test_os, need_mtools,
                                            tmp_path):
    """No line driven high: every button reads pressed, the guard disables the
    backend, and later levels on the pins do nothing. The console still works."""
    with alone(mfboot_image, board, two(tmp_path, test_os), qtest=True) as vm:
        io = PiGpio(vm, board, names=PINS)
        vm.wait_for(READY, timeout=TIMEOUT)
        inp = one(vm, "input")
        assert (inp["gpio"], inp["buttons"], inp["live"]) == \
            ("0", "disabled_stuck", "serial,script"), inp
        assert not events(vm, "maint_open")
        # Release every line, then press DOWN: the backend no longer samples.
        for pin in PINS.values():
            io.release(pin)
        io.press("down")
        time.sleep(0.3)
        io.release("down")
        # The port's DOWN is queued behind any button event, so state() sees both.
        key(vm, "up")
        st = state(vm)
        assert st["sel"] == "1", st
        assert len(events(vm, "select")) == 0, vm.serial
        vm.send("\x1b[B")
        vm.wait_for("ev=select index=2", timeout=TIMEOUT)
        vm.send("\r")
        vm.wait_for(OS_READY, timeout=TIMEOUT)
        assert one(vm, "choose")["index"] == "2"


def test_diag_input_view_shows_two_buttons(board, diag_image):
    """Two buttons held together show as gpio events and as PRESSED rows."""
    with diag.alone(diag_image, board, qtest=True, gpio_high=DIAG_PINS.values()) as vm:
        io = PiGpio(vm, board, names=DIAG_PINS)
        diag.command(vm, "diag input")
        # DIAG.SYS does not wait for the guard, so the view can open while it checks.
        vm.wait_for(re.compile(r"ev=input_view state=open gpio=(checking|active) "),
                    timeout=TIMEOUT)

        def gpio_events():
            return [(e["code"], e["value"]) for e in diag.events(vm, "input_event")
                    if e["source"] == "gpio"]

        def wait_gpio(want):
            deadline = time.monotonic() + TIMEOUT
            while want not in gpio_events():
                assert time.monotonic() < deadline, vm.serial
                time.sleep(0.02)

        io.press("up")
        io.press("select")
        wait_gpio(("up", "1"))
        wait_gpio(("select", "1"))
        diag.state(vm)
        _, rows = diag.dump(vm)
        assert any(r.startswith("up       PRESSED") for r in rows), rows
        assert any(r.startswith("select   PRESSED") for r in rows), rows
        io.release("up")
        io.release("select")
        wait_gpio(("up", "0"))
        wait_gpio(("select", "0"))
        got = gpio_events()
        # A press held past 500 ms may repeat; only these two keys may show.
        assert {c for c, _ in got} == {"up", "select"}, got
        diag.state(vm)
        _, rows = diag.dump(vm)
        assert not any("PRESSED" in r for r in rows), rows
        # ESC twice on the console leaves the view; the result names the backend state.
        vm.send("\x1b")
        time.sleep(0.3)
        vm.send("\x1b")
        deadline = time.monotonic() + TIMEOUT
        while not diag.results(vm, "input"):
            assert time.monotonic() < deadline, vm.serial
            time.sleep(0.05)
        m = diag.results(vm, "input")[-1]
        assert (m["result"], m["gpio"]) == ("ok", "active"), m
