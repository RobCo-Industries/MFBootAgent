"""The menu alone, with the default card: markers, golden, keys and the countdown."""

import re
import time

import pytest

from mfboot import (JUMP, OS_READY, READY, TIMEOUT, UOS, WAIT_CFG, alone, card, char,
                    check_golden, dump, events, key, one, order, os_checks, os_files,
                    screenshot, state)

# Time between a marker in the guest and the host seeing it, at most. The
# guest runs on its own timer; this only absorbs output latency.
LATENCY = 0.1
# What a busy host can add to a guest wait: other QEMU instances share the CPU.
HOST_ALLOWANCE = 3.0

MENU_ROWS = [
    "ROBCO MFBOOT AGENT V0.1.0",
    "COPYRIGHT 2075-2077 ROBCO INDUSTRIES",
    "MULTI-FUNCTION BOOT AGENT. SELECT AN OPERATING IMAGE.",
    "========================================",
    "",
    "> 1  UOS             DEFAULT  /ROBCO/UOS.SYS",
    "  2  MAINT                    /ROBCO/MAINT.SYS",
    "  3  DIAG                     /ROBCO/DIAG.SYS",
    "",
    "UP/DOWN or 1-8: select   ENTER: boot   M: maintenance   R: reboot",
]

TWO_BOOTS = re.compile(r"(@RC stage=bootagent ev=boot [^\n]*\n[\s\S]*){2}")

# What each entry of WAIT_CFG hands over: (kind, path, cmdline marker value).
ENTRIES = {
    1: ("uos", "/ROBCO/UOS.SYS", "console=tty0_quiet"),
    2: ("maint", "/ROBCO/MAINT.SYS", "none"),
    3: ("diag", "/ROBCO/DIAG.SYS", "level=2"),
}


def cfg_with(extra):
    return WAIT_CFG.replace(b"default = UOS\n", b"default = UOS\n" + extra)


def check_booted(vm, index, via):
    """The entry `index` booted: the markers, the boot info and every check of the test OS."""
    kind, path, cmdline = ENTRIES[index]
    vm.wait_for(OS_READY, timeout=TIMEOUT)
    choose = one(vm, "choose")
    assert (choose["index"], choose["path"], choose["via"]) == (str(index), path, via), choose
    load = one(vm, "load")
    assert (load["path"], load["addr"], load["subtype"]) == (path, "0x00400000",
                                                             str({"uos": 0, "maint": 2,
                                                                  "diag": 3}[kind]))
    assert one(vm, "boot", "testos")["kind"] == kind
    os_checks(vm)
    info = one(vm, "bootinfo", "testos")
    assert (info["prev_stage"], info["path"], info["cmdline"]) == ("2", path, cmdline), info
    assert info["part_lba"] == "2048"
    # Alone, MFBootAgent cold-booted and built the block itself.
    assert int(info["flags"], 16) == 1 << 2
    assert not events(vm, "error")


def test_markers_and_golden(board, mfboot_image, test_os, need_mtools, tmp_path):
    with alone(mfboot_image, board, card(tmp_path, os_files(test_os))) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        assert order(vm) == ["boot", "handoff", "screen", "input", "selfcheck", "mount",
                             "config", "entries", "ready"], vm.serial
        cfg = one(vm, "config")
        assert (cfg["result"], cfg["entries"], cfg["default_found"]) == ("ok", "3", "1")
        ent = one(vm, "entries")
        assert (ent["count"], ent["default"], ent["timeout"]) == ("3", "UOS", "none")
        mount = one(vm, "mount")
        assert mount["lba"] == "2048"
        _, rows = dump(vm)
        assert rows[:len(MENU_ROWS)] == MENU_ROWS, rows
        assert all(r == "" for r in rows[len(MENU_ROWS):]), rows
        # The PL011 shows the same menu, line by line.
        lines = vm.lines()
        start = lines.index(MENU_ROWS[0])
        shown = [r for r in MENU_ROWS if r]
        assert lines[start:start + len(shown)] == shown, lines
        st = state(vm)
        assert (st["phase"], st["sel"], st["counting"], st["error"]) == ("menu", "1", "0",
                                                                         "none"), st
        img = screenshot(vm, tmp_path)
    check_golden(img, f"menu_{board}")


def test_default_boots_after_timeout(board, mfboot_image, test_os, need_mtools, tmp_path):
    seconds = 2
    sd = card(tmp_path, os_files(test_os), cfg=cfg_with(f"timeout = {seconds}\n".encode()))
    with alone(mfboot_image, board, sd) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        t0 = time.monotonic()
        vm.wait_for(JUMP, timeout=TIMEOUT)
        elapsed = time.monotonic() - t0
        # The design: the boot starts no sooner than the timeout after the
        # menu shows, and as soon as the timer says so after that.
        assert seconds - LATENCY <= elapsed <= seconds + HOST_ALLOWANCE, elapsed
        assert one(vm, "countdown")["state"] == "start"
        assert "Booting UOS in 2 s. Press any key to stop." in vm.lines()
        assert "Booting UOS in 1 s. Press any key to stop." in vm.lines()
        check_booted(vm, 1, "timeout")


def test_key_stops_the_countdown(board, mfboot_image, test_os, need_mtools, tmp_path):
    seconds = 2
    sd = card(tmp_path, os_files(test_os), cfg=cfg_with(f"timeout = {seconds}\n".encode()))
    with alone(mfboot_image, board, sd) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        vm.send("x")
        vm.wait_for("ev=countdown state=stopped", timeout=TIMEOUT)
        time.sleep(seconds + 1)
        assert not events(vm, "choose"), vm.serial
        st = state(vm)
        assert (st["phase"], st["counting"], st["left"]) == ("menu", "0", "0"), st
        _, rows = dump(vm)
        assert "Countdown stopped. Press ENTER to boot." in rows, rows
        vm.send("\r")
        check_booted(vm, 1, "key")


@pytest.mark.parametrize("keys,index", [
    (["key down", "key select"], 2),
    (["key down", "key down", "key down", "key up", "key select"], 2),
    (["key up", "key select"], 1),
    (["char 3", "key select"], 3),
    (["char 3", "char 9", "char 4", "key select"], 3),
], ids=["down", "down_up", "up_at_top", "digit", "digit_beyond"])
def test_pick_an_entry(board, mfboot_image, test_os, need_mtools, tmp_path, keys, index):
    with alone(mfboot_image, board, card(tmp_path, os_files(test_os))) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        for k in keys:
            assert vm.script(k, timeout=TIMEOUT).status == "ok"
        check_booted(vm, index, "key")
        sel = events(vm, "select")
        assert (sel[-1]["index"] if sel else "1") == str(index)


def test_serial_arrows(board, mfboot_image, test_os, need_mtools, tmp_path):
    """The console backend: ANSI arrows and ENTER typed on the PL011."""
    with alone(mfboot_image, board, card(tmp_path, os_files(test_os))) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        vm.send("\x1b[B")
        vm.wait_for("ev=select index=2", timeout=TIMEOUT)
        vm.send("\x1b[B")
        vm.wait_for("ev=select index=3", timeout=TIMEOUT)
        vm.send("\r")
        check_booted(vm, 3, "key")


def test_timeout_zero_boots_at_once(board, mfboot_image, test_os, need_mtools, tmp_path):
    cfg = WAIT_CFG.replace(b"default = UOS\n", b"default = maint\ntimeout = 0\n")
    with alone(mfboot_image, board, card(tmp_path, os_files(test_os), cfg=cfg)) as vm:
        check_booted(vm, 2, "timeout")
        assert not events(vm, "countdown")
        evs = order(vm)
        assert evs[evs.index("ready"):evs.index("load")] == ["ready", "choose"], evs


def test_no_timeout_waits(board, mfboot_image, test_os, need_mtools, tmp_path):
    with alone(mfboot_image, board, card(tmp_path, os_files(test_os))) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        time.sleep(3)
        assert not events(vm, "choose") and not events(vm, "countdown"), vm.serial
        assert state(vm)["phase"] == "menu"
        key(vm, "select")
        check_booted(vm, 1, "key")


def test_reboot_key(board, mfboot_image, test_os, need_mtools, tmp_path):
    with alone(mfboot_image, board, card(tmp_path, os_files(test_os))) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        char(vm, "r")
        vm.wait_for("ev=reboot source=key", timeout=TIMEOUT)
        # The watchdog restarts the board: the firmware stub starts again.
        vm.wait_for(TWO_BOOTS, timeout=TIMEOUT)


def test_golden_after_boot(board, mfboot_image, fixed_os, need_mtools, tmp_path):
    """timeout = 0 with the fixed OS: the screen as handed over, with no size or CRC."""
    cfg = b"[boot]\ntimeout = 0\n[entry UOS]\npath = /ROBCO/UOS.SYS\n"
    with alone(mfboot_image, board, card(tmp_path, {UOS: fixed_os}, cfg=cfg)) as vm:
        vm.wait_for(JUMP, timeout=TIMEOUT)
        # The jump marker comes after the last drawing.
        img = screenshot(vm, tmp_path)
    check_golden(img, f"boot_{board}")
