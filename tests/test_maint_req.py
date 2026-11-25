"""The maintenance request of BOOT_PROTOCOL.md: /ROBCO/MAINT.REQ is read,
deleted and honoured once, boot_flags bits 1 and 3 reach the next image,
and the maintenance screen starts the maintenance image.

The test OS sealed with subtype 2 stands in for MAINT.SYS.
"""

import os
import re
import subprocess
import time

from harness.pigpio import PiGpio
from harness.sdimage import PART_LBA, SECTOR, minimal_env
from mfboot import (MAINT, OS_READY, READY, TIMEOUT, UOS, WAIT_CFG, after, alone, card, char, dump,
                    events, key, one, os_checks, os_files, state)

REQ = "/ROBCO/MAINT.REQ"
BIT1 = 1 << 1
COLD = 1 << 2
BIT3 = 1 << 3
OPEN = re.compile(r"@RC stage=bootagent ev=maint_open [^\n]*\n")
CLOSE = re.compile(r"@RC stage=bootagent ev=maint_close\r?\n")
HOLD = re.compile(r"@RC stage=bootagent ev=test_hold\r?\n")
REQ_LINE = re.compile(r"@RC stage=bootagent ev=maint_req [^\n]*\n")
NOFILE = re.compile(r"@RC stage=bootagent ev=error code=nofile [^\n]*\n")
PINS = {"up": 5, "down": 6, "left": 17, "right": 22, "select": 26, "back": 27}
TITLE = "MFBOOT AGENT V0.1.0 MAINTENANCE"
NOTICE = "The request comes back at each start until the file is removed."
# The screen's actions from the top: Start the maintenance image is the fourth.
START_ITEM = 3

# A card whose BOOT.CFG has no MAINT entry.
NO_MAINT_CFG = b"""[boot]
default = UOS

[entry UOS]
path = /ROBCO/UOS.SYS

[entry DIAG]
path = /ROBCO/DIAG.SYS
"""


def req_card(tmp_path, test_os, req, cfg=WAIT_CFG, name="sd.img", drop=()):
    """The usual card with `req` at /ROBCO/MAINT.REQ: bytes, "dir" for a folder, or None."""
    files = os_files(test_os)
    for path in drop:
        del files[path]
    if req == "dir":
        files[REQ + "/"] = b""
    elif req is not None:
        files[REQ] = req
    return card(tmp_path, files, cfg=cfg, name=name)


def mtools(sd, tool, *args):
    """Runs an mtools command on the card's FAT volume. Returns its output."""
    return subprocess.run([tool, "-i", f"{os.fspath(sd)}@@{PART_LBA * SECTOR}", *args],
                          check=True, capture_output=True, text=True,
                          env=minimal_env(MTOOLS_SKIP_CHECK="1")).stdout


def card_names(sd):
    """The names in /ROBCO on the card image, upper case."""
    out = mtools(sd, "mdir", "-b", "::/ROBCO")
    return {line.rstrip("/").rsplit("/", 1)[-1].upper() for line in out.split() if line}


def req_marker(vm):
    vm.wait_for(REQ_LINE, timeout=TIMEOUT)
    return one(vm, "maint_req")


def screen_rows(vm):
    """The 18 rows of the last maintenance screen on the console."""
    _, rows = dump(vm)
    top = len(rows) - 1 - rows[::-1].index(TITLE)
    return rows[top:top + 18]


def output_rows(vm):
    """The output rows of the screen, without the blank ones at the end."""
    rows = screen_rows(vm)[8:]
    while rows and rows[-1] == "":
        rows.pop()
    return rows


def opened_by_request(vm):
    vm.wait_for(OPEN, timeout=TIMEOUT)
    st = state(vm)
    assert (st["phase"], st["maint"], st["counting"]) == ("menu", "1", "0"), st
    assert one(vm, "maint_open")["why"] == "request"


def os_flags(vm):
    """boot_flags as the test OS read them from its boot info."""
    vm.wait_for(OS_READY, timeout=TIMEOUT)
    os_checks(vm)
    flags = int(one(vm, "bootinfo", "testos")["flags"], 16)
    assert int(one(vm, "bootinfo")["flags"], 16) == flags
    return flags


def close_and_boot(vm, index):
    """Closes the screen with Q and boots menu entry `index`, 1-based."""
    char(vm, "q")
    vm.wait_for(CLOSE, timeout=TIMEOUT)
    char(vm, str(index))
    key(vm, "select")


def start_maint_image(vm):
    for _ in range(START_ITEM):
        key(vm, "down")
    key(vm, "select")


def test_no_file(board, mfboot_image, test_os, need_mtools, tmp_path):
    """No request: none, the countdown runs, the default boots with bits 1 and 3 clear."""
    cfg = WAIT_CFG.replace(b"default = UOS\n", b"default = UOS\ntimeout = 1\n")
    with alone(mfboot_image, board, req_card(tmp_path, test_os, None, cfg=cfg)) as vm:
        m = req_marker(vm)
        assert (m["result"], m["rc"]) == ("none", "0"), m
        assert os_flags(vm) == COLD
        assert one(vm, "choose")["via"] == "timeout"
        assert not events(vm, "maint_open")
        ev = [m["ev"] for m in vm.markers() if m.get("stage") == "bootagent"]
        assert ev.index("mount") < ev.index("maint_req") < ev.index("config") < ev.index("ready")


def test_folder_is_no_request(board, mfboot_image, test_os, need_mtools, tmp_path):
    """A folder named MAINT.REQ is no request, and the agent leaves it alone."""
    cfg = WAIT_CFG.replace(b"default = UOS\n", b"default = UOS\ntimeout = 1\n")
    sd = req_card(tmp_path, test_os, "dir", cfg=cfg)
    with alone(mfboot_image, board, sd) as vm:
        m = req_marker(vm)
        assert (m["result"], m["rc"]) == ("none", "0"), m
        assert os_flags(vm) == COLD
        assert not events(vm, "maint_open")
    assert "MAINT.REQ" in card_names(sd)


def test_line_shows_and_file_goes(board, mfboot_image, test_os, need_mtools, tmp_path):
    """A request with a line: found, deleted, the screen with the line, no countdown."""
    cfg = WAIT_CFG.replace(b"default = UOS\n", b"default = UOS\ntimeout = 1\n")
    sd = req_card(tmp_path, test_os, b"OPER SYSTEM at 2077-10-23 09:47\r\n", cfg=cfg)
    with alone(mfboot_image, board, sd) as vm:
        m = req_marker(vm)
        assert (m["result"], m["rc"]) == ("found", "0"), m
        opened_by_request(vm)
        assert not events(vm, "countdown")
        assert output_rows(vm) == ["MAINTENANCE REQUESTED",
                                   "Request:      OPER SYSTEM at 2077-10-23 09:47"]
        time.sleep(1.5)
        assert not events(vm, "choose"), vm.serial
    assert "MAINT.REQ" not in card_names(sd)


def test_empty_file(board, mfboot_image, test_os, need_mtools, tmp_path):
    sd = req_card(tmp_path, test_os, b"")
    with alone(mfboot_image, board, sd) as vm:
        m = req_marker(vm)
        assert (m["result"], m["rc"]) == ("found", "0"), m
        opened_by_request(vm)
        assert output_rows(vm) == ["MAINTENANCE REQUESTED"]
    assert "MAINT.REQ" not in card_names(sd)


def test_binary_content(board, mfboot_image, test_os, need_mtools, tmp_path):
    sd = req_card(tmp_path, test_os, b"OPER\x00\x1b[2J\xff")
    with alone(mfboot_image, board, sd) as vm:
        m = req_marker(vm)
        assert (m["result"], m["rc"]) == ("found", "0"), m
        opened_by_request(vm)
        assert output_rows(vm) == ["MAINTENANCE REQUESTED"]
    assert "MAINT.REQ" not in card_names(sd)


def test_line_of_64_bytes(board, mfboot_image, test_os, need_mtools, tmp_path):
    """63 bytes is the longest line: 64 shows no line, and the request still counts."""
    sd = req_card(tmp_path, test_os, b"A" * 64 + b"\n")
    with alone(mfboot_image, board, sd) as vm:
        m = req_marker(vm)
        assert (m["result"], m["rc"]) == ("found", "0"), m
        opened_by_request(vm)
        assert output_rows(vm) == ["MAINTENANCE REQUESTED"]
    assert "MAINT.REQ" not in card_names(sd)
    sd = req_card(tmp_path, test_os, b"A" * 63 + b"\n", name="sd63.img")
    with alone(mfboot_image, board, sd) as vm:
        opened_by_request(vm)
        assert output_rows(vm) == ["MAINTENANCE REQUESTED", "Request:      " + "A" * 63]


def test_request_and_held_back(board, mfboot_image, test_os, need_mtools, tmp_path):
    """A request and BACK held through the guard: one screen, why=request, still open."""
    high = [p for name, p in PINS.items() if name != "back"]
    sd = req_card(tmp_path, test_os, b"held\n")
    with alone(mfboot_image, board, sd, qtest=True, gpio_high=high) as vm:
        io = PiGpio(vm, board, names=PINS)
        vm.wait_for(READY, timeout=TIMEOUT)
        assert one(vm, "input")["buttons"] == "active"
        opened_by_request(vm)
        io.release("back")
        st = state(vm)
        assert (st["phase"], st["maint"]) == ("menu", "1"), st
        assert not events(vm, "maint_close"), vm.serial
        assert len(events(vm, "maint_open")) == 1
        # The next BACK closes it as usual.
        key(vm, "back")
        vm.wait_for(CLOSE, timeout=TIMEOUT)
        assert state(vm)["maint"] == "0"


def test_delete_fails_on_a_locked_file(board, mfboot_image, test_os, need_mtools, tmp_path):
    """A locked file, with the FAT read-only attribute, does not delete: clear_failed
    with the code, the screen with the notice, once, and the file stays. The next
    start shows it again. QEMU refuses a read-only drive as an SD card, so the
    lock stands in for a write-protected card."""
    sd = req_card(tmp_path, test_os, b"locked\n")
    mtools(sd, "mattrib", "+r", "::" + REQ)
    for boot_no in (1, 2):
        with alone(mfboot_image, board, sd) as vm:
            m = req_marker(vm)
            assert m["result"] == "clear_failed", m
            assert int(m["rc"]) < 0, m
            opened_by_request(vm)
            rc = m["rc"]
            assert output_rows(vm) == [
                "MAINTENANCE REQUESTED",
                "Request:      locked",
                f"NOTICE: {REQ} could not be deleted, code {rc}.",
                NOTICE,
            ]
            # It never loops: closing the screen leaves the menu, with no countdown.
            char(vm, "q")
            vm.wait_for(CLOSE, timeout=TIMEOUT)
            time.sleep(1.5)
            st = state(vm)
            assert (st["phase"], st["maint"], st["counting"]) == ("menu", "0", "0"), st
            assert len(events(vm, "maint_open")) == 1
            assert len(events(vm, "maint_req")) == 1
            assert not events(vm, "choose")
        assert "MAINT.REQ" in card_names(sd), boot_no


def test_one_shot_over_two_boots(board, mfboot_image, test_os, need_mtools, tmp_path):
    cfg = WAIT_CFG.replace(b"default = UOS\n", b"default = UOS\ntimeout = 1\n")
    sd = req_card(tmp_path, test_os, b"once\n", cfg=cfg)
    with alone(mfboot_image, board, sd) as vm:
        assert req_marker(vm)["result"] == "found"
        opened_by_request(vm)
    assert "MAINT.REQ" not in card_names(sd)
    with alone(mfboot_image, board, sd) as vm:
        m = req_marker(vm)
        assert (m["result"], m["rc"]) == ("none", "0"), m
        assert os_flags(vm) == COLD
        assert not events(vm, "maint_open")


def test_request_then_uos(board, mfboot_image, test_os, need_mtools, tmp_path):
    """The bits say what was asked: UOS gets bits 1 and 3."""
    with alone(mfboot_image, board, req_card(tmp_path, test_os, b"x\n")) as vm:
        opened_by_request(vm)
        close_and_boot(vm, 1)
        assert os_flags(vm) == COLD | BIT1 | BIT3
        c = one(vm, "choose")
        assert (c["name"], c["via"]) == ("UOS", "key"), c
        assert one(vm, "load")["subtype"] == "0"


def test_request_then_maint_from_the_screen(board, mfboot_image, test_os, need_mtools,
                                            tmp_path):
    with alone(mfboot_image, board, req_card(tmp_path, test_os, b"")) as vm:
        opened_by_request(vm)
        start_maint_image(vm)
        assert os_flags(vm) == COLD | BIT1 | BIT3
        c = one(vm, "choose")
        assert (c["index"], c["name"], c["path"], c["via"]) == ("2", "MAINT", MAINT, "key"), c
        assert events(vm, "maint_action")[-1]["name"] == "start"


def test_maint_from_the_menu(board, mfboot_image, test_os, need_mtools, tmp_path):
    """No request: the MAINT entry from the menu gets bit 1, bit 3 clear."""
    with alone(mfboot_image, board, req_card(tmp_path, test_os, None)) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        char(vm, "2")
        key(vm, "select")
        assert os_flags(vm) == COLD | BIT1
        assert one(vm, "load")["subtype"] == "2"
        assert not events(vm, "maint_open")


def screen_starts_fixed_path(vm):
    vm.wait_for(READY, timeout=TIMEOUT)
    char(vm, "m")
    vm.wait_for(OPEN, timeout=TIMEOUT)
    start_maint_image(vm)


def test_screen_starts_fixed_path_without_maint_entry(board, mfboot_image, test_os,
                                                      need_mtools, tmp_path):
    sd = req_card(tmp_path, test_os, None, cfg=NO_MAINT_CFG)
    with alone(mfboot_image, board, sd) as vm:
        screen_starts_fixed_path(vm)
        assert os_flags(vm) == COLD | BIT1
        c = one(vm, "choose")
        assert (c["index"], c["name"], c["path"], c["via"]) == ("-1", "MAINT", MAINT, "key"), c


def test_screen_starts_fixed_path_without_boot_cfg(board, mfboot_image, test_os, need_mtools,
                                                   tmp_path):
    sd = req_card(tmp_path, test_os, None, cfg=None)
    with alone(mfboot_image, board, sd) as vm:
        screen_starts_fixed_path(vm)
        assert one(vm, "config")["result"] == "missing"
        assert os_flags(vm) == COLD | BIT1
        c = one(vm, "choose")
        assert (c["index"], c["path"]) == ("-1", MAINT), c


def test_missing_maint_sys(board, mfboot_image, test_os, need_mtools, tmp_path):
    """No MAINT.SYS: the usual load error under the menu, and the menu stays."""
    sd = req_card(tmp_path, test_os, None, cfg=None, drop=(MAINT,))
    with alone(mfboot_image, board, sd) as vm:
        screen_starts_fixed_path(vm)
        vm.wait_for(NOFILE, timeout=TIMEOUT)
        err = one(vm, "error")
        assert (err["path"], err["name"]) == (MAINT, "MAINT"), err
        st = state(vm)
        assert (st["phase"], st["maint"], st["error"]) == ("menu", "0", "nofile"), st
        _, rows = dump(vm)
        assert "ERROR E01 NOFILE: MAINT: file not found" in rows, rows
        # The menu still boots UOS, which never gets bit 3.
        key(vm, "select")
        assert os_flags(vm) == COLD | BIT1
        assert [c["path"] for c in events(vm, "choose")] == [MAINT, UOS]


def test_request_with_bit_1_received(board, chain1_maint_image, mfboot, test_os, need_mtools,
                                     tmp_path):
    """A first stage hands on bit 1 and a request waits: one screen, why=request."""
    files = os_files(test_os)
    files["/ROBCO/MFBOOT.SYS"] = mfboot
    files[REQ] = b"both\n"
    with after(chain1_maint_image, board, card(tmp_path, files)) as vm:
        assert req_marker(vm)["result"] == "found"
        assert one(vm, "handoff")["trusted"] == "1"
        opened_by_request(vm)
        close_and_boot(vm, 1)
        # chain1's cold flag as received, bit 1, and bit 3 for the request.
        assert os_flags(vm) == COLD | BIT1 | BIT3
        assert len(events(vm, "maint_open")) == 1


def test_bit_1_received_and_held_back(board, chain1_maint_image, mfboot, test_os, need_mtools,
                                      tmp_path):
    """A received bit 1 and BACK held through the guard: why=flag, and the screen
    stays open. The guard's BACK press does not close it."""
    files = os_files(test_os)
    files["/ROBCO/MFBOOT.SYS"] = mfboot
    high = [p for name, p in PINS.items() if name != "back"]
    with after(chain1_maint_image, board, card(tmp_path, files), qtest=True,
               gpio_high=high) as vm:
        io = PiGpio(vm, board, names=PINS)
        vm.wait_for(OPEN, timeout=TIMEOUT)
        assert one(vm, "handoff")["trusted"] == "1"
        assert one(vm, "input")["buttons"] == "active"
        assert one(vm, "maint_open")["why"] == "flag"
        io.release("back")
        st = state(vm)
        assert (st["phase"], st["maint"]) == ("menu", "1"), st
        assert not events(vm, "maint_close"), vm.serial
        key(vm, "back")
        vm.wait_for(CLOSE, timeout=TIMEOUT)
        assert state(vm)["maint"] == "0"


# A button held through the guard and released before the start screen
# opens. The test build holds the boot while LEFT is held: the test
# releases the button, then LEFT, so the release is queued before the
# screen opens. LEFT does nothing on the screen.


def released_in_window(vm, board, button):
    """Waits for the test build's hold, then releases `button` and LEFT in that order."""
    io = PiGpio(vm, board, names=PINS)
    vm.wait_for(HOLD, timeout=TIMEOUT)
    assert one(vm, "input")["buttons"] == "active"
    assert not events(vm, "maint_open"), vm.serial
    io.release(button)
    io.release("left")


def held_low(*names):
    return [p for name, p in PINS.items() if name not in names]


def check_still_open(vm):
    vm.wait_for(OPEN, timeout=TIMEOUT)
    st = state(vm)
    assert (st["phase"], st["maint"]) == ("menu", "1"), st
    assert not events(vm, "maint_close"), vm.serial
    assert not events(vm, "maint_action"), vm.serial
    assert len(events(vm, "maint_open")) == 1


def test_shipped_image_has_no_hold(mfboot, mfboot_test_image):
    """The hold is in the test build only."""
    with open(mfboot_test_image, "rb") as f:
        assert b"test_hold" in f.read()
    assert b"test_hold" not in mfboot


def test_request_and_back_released_before_the_screen(board, mfboot_test_image, test_os,
                                                     need_mtools, tmp_path):
    """BACK held through the guard and released before the screen opens: the queued
    press does not close it. The next BACK does."""
    sd = req_card(tmp_path, test_os, b"held\n")
    with alone(mfboot_test_image, board, sd, qtest=True, gpio_high=held_low("back", "left")) as vm:
        released_in_window(vm, board, "back")
        check_still_open(vm)
        assert one(vm, "maint_open")["why"] == "request"
        key(vm, "back")
        vm.wait_for(CLOSE, timeout=TIMEOUT)
        assert state(vm)["maint"] == "0"


def test_bit_1_and_back_released_before_the_screen(board, chain1_maint_image, mfboot_test_image,
                                                   test_os, need_mtools, tmp_path):
    """The same with a received bit 1: why=flag, the screen stays open."""
    files = os_files(test_os)
    with open(mfboot_test_image, "rb") as f:
        files["/ROBCO/MFBOOT.SYS"] = f.read()
    with after(chain1_maint_image, board, card(tmp_path, files), qtest=True,
               gpio_high=held_low("back", "left")) as vm:
        released_in_window(vm, board, "back")
        check_still_open(vm)
        assert one(vm, "handoff")["trusted"] == "1"
        assert one(vm, "maint_open")["why"] == "flag"
        key(vm, "back")
        vm.wait_for(CLOSE, timeout=TIMEOUT)
        assert state(vm)["maint"] == "0"


def test_request_and_select_released_before_the_screen(board, mfboot_test_image, test_os,
                                                       need_mtools, tmp_path):
    """SELECT held through the guard with a request: no action runs, no boot. The
    next SELECT runs the selected action."""
    sd = req_card(tmp_path, test_os, b"held\n")
    with alone(mfboot_test_image, board, sd, qtest=True,
               gpio_high=held_low("select", "left")) as vm:
        released_in_window(vm, board, "select")
        check_still_open(vm)
        assert not events(vm, "jump"), vm.serial
        key(vm, "select")
        state(vm)
        assert one(vm, "maint_action")["name"] == "verify", vm.serial
