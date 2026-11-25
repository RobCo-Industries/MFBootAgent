"""MAINT.SYS driven by GPIO buttons through qtest: the menu and the restore users question.

QEMU models no pulls, so every button line is driven high before the
guest starts. A press is held until the image reports its effect, then
released, so no check depends on host timing. The image's buttons do not
repeat; the rule that a held key answers once is a host test.
"""

import time

from harness.pigpio import PiGpio
from maintimg import (TIMEOUT, USERS_DAT, USERS_DEF, alone, card, command, events,  # noqa: F401
                      good_files, maint_cfg, maint_image, read_card_file, state)

PINS = {"up": 5, "down": 6, "left": 17, "right": 22, "select": 26, "back": 27}
DEFAULT = b"default users\n" * 10
OLD = b"old users\n" * 10


def wait_count(vm, name, n):
    deadline = time.monotonic() + TIMEOUT
    while len(events(vm, name)) < n:
        assert time.monotonic() < deadline, vm.serial
        time.sleep(0.02)
    return events(vm, name)


def hold_until(io, vm, button, name):
    """Presses `button` until one more `name` marker shows, then releases it."""
    n = len(events(vm, name))
    io.press(button)
    try:
        return wait_count(vm, name, n + 1)[n]
    finally:
        io.release(button)


def test_menu_with_buttons(board, maint_image, good_files, need_mtools, tmp_path):
    sd = card(tmp_path, good_files)
    with alone(maint_image, board, sd=sd, qtest=True, gpio_high=PINS.values()) as vm:
        io = PiGpio(vm, board, names=PINS)
        # No button is pressed twice in a row: the guest sees no release
        # shorter than its debounce, and nothing here waits for one.
        assert hold_until(io, vm, "down", "select")["index"] == "2"
        assert hold_until(io, vm, "up", "select")["index"] == "1"
        assert hold_until(io, vm, "down", "select")["index"] == "2"
        assert state(vm)["sel"] == "2"
        m = hold_until(io, vm, "select", "tool")
        assert (m["name"], m["result"]) == ("check_images", "ok"), m
        # View file, then BACK at its chooser cancels it.
        assert hold_until(io, vm, "down", "select")["index"] == "3"
        hold_until(io, vm, "select", "view_choose")
        m = hold_until(io, vm, "back", "tool")
        assert (m["name"], m["result"], m["why"]) == ("view_file", "refused", "cancel"), m
        assert state(vm)["phase"] == "menu"


def test_question_with_buttons(board, maint_image, good_files, need_mtools, tmp_path):
    files = dict(good_files)
    files[USERS_DEF] = DEFAULT
    files[USERS_DAT] = OLD
    sd = card(tmp_path, files, cfg=maint_cfg(b"\n[maint]\nrestore_users = on\n"))
    with alone(maint_image, board, sd=sd, qtest=True, gpio_high=PINS.values()) as vm:
        io = PiGpio(vm, board, names=PINS)
        command(vm, "tool restore_users")
        wait_count(vm, "ask", 1)
        m = hold_until(io, vm, "back", "tool")
        assert (m["name"], m["result"], m["why"]) == ("restore_users", "refused", "no"), m
        assert read_card_file(sd, USERS_DAT) == OLD
        command(vm, "tool restore_users")
        wait_count(vm, "ask", 2)
        m = hold_until(io, vm, "select", "tool")
        assert (m["name"], m["result"]) == ("restore_users", "ok"), m
        # One answer per question: no second tool marker from the held SELECT.
        assert state(vm)["phase"] == "menu"
        assert len(events(vm, "tool")) == 2
    assert read_card_file(sd, USERS_DAT) == DEFAULT
