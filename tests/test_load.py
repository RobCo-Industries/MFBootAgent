"""Load failures: each code shows its line, the menu stays and another entry then boots.

Bad images are the test OS with bytes patched, resealed where the case
needs a valid CRC. Set ROBCO_UPDATE_GOLDEN=1 to rewrite
tests/golden/error_<board>.png.
"""

import re
import time

import pytest

from mfboot import (IDLE, MAINT, OFF_ABI, OFF_FILE_SIZE, OFF_HDR_VERSION, OFF_LOAD_ADDR,
                    OFF_MAGIC, OFF_SUBTYPE, OS_READY, READY, SUBTYPE, TIMEOUT, UOS, WAIT_CFG,
                    alone, card, check_golden, dump, events, flip, key, one, os_checks, patched,
                    screenshot, state)

ERROR_LINE = {
    "nofile": "ERROR E01 NOFILE: UOS: file not found",
    "magic": "ERROR E03 MAGIC: UOS: not a boot image",
    "version": "ERROR E04 VERSION: UOS: unknown header version",
    "size": "ERROR E05 SIZE: UOS: size does not match the file",
    "bounds": "ERROR E06 BOUNDS: UOS: image does not fit the OS slot",
    "abi": "ERROR E07 ABI: UOS: wrong ABI version",
    "crc": "ERROR E08 CRC: UOS: image CRC does not match",
    "agent": "ERROR E06 BOUNDS: UOS: not an operating image",
    "agent_crc": "ERROR E06 BOUNDS: UOS: not an operating image",
    "agent_abi": "ERROR E06 BOUNDS: UOS: not an operating image",
    "subtype1": "ERROR E06 BOUNDS: UOS: not an operating image",
}
CODE = {"agent": "bounds", "agent_crc": "bounds", "agent_abi": "bounds", "subtype1": "bounds"}


def bad_image(good, case):
    if case == "crc":
        return flip(good)
    if case == "magic":
        return patched(good, OFF_MAGIC, "8s", b"ROM1\0\0\0\0")
    if case == "version":
        return patched(good, OFF_HDR_VERSION, "<H", 2)
    if case == "size":
        return patched(good, OFF_FILE_SIZE, "<I", len(good) + 4)
    if case == "bounds":
        return patched(good, OFF_LOAD_ADDR, "<I", 0x00410000)
    if case == "abi":
        return patched(good, OFF_ABI, "<I", 3 << 16)
    if case == "agent":
        # A sealed boot agent image: MFBootAgent never loads another boot agent.
        return patched(good, OFF_SUBTYPE, "<I", 4)
    if case == "agent_crc":
        # The subtype check comes before the CRC: bounds, not crc.
        return patched(good, OFF_SUBTYPE, "<I", 4, seal=False)
    if case == "agent_abi":
        # And before the ABI step.
        return patched(patched(good, OFF_ABI, "<I", 3 << 16), OFF_SUBTYPE, "<I", 4)
    if case == "subtype1":
        return patched(good, OFF_SUBTYPE, "<I", 1)
    raise ValueError(case)


def files_for(test_os, case):
    files = {MAINT: test_os["maint"]}
    if case != "nofile":
        files[UOS] = bad_image(test_os["uos"], case)
    return files


@pytest.mark.parametrize("case", list(ERROR_LINE))
def test_failed_entry(board, mfboot_image, test_os, need_mtools, tmp_path, case):
    code = CODE.get(case, case)
    with alone(mfboot_image, board, card(tmp_path, files_for(test_os, case))) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        key(vm, "select")
        vm.wait_for(re.compile(r"@RC stage=bootagent ev=error [^\n]*\n"), timeout=TIMEOUT)
        err = one(vm, "error")
        assert (err["code"], err["path"], err["name"]) == (code, UOS, "UOS"), err
        assert not events(vm, "load") and not events(vm, "jump"), vm.serial
        st = state(vm)
        assert (st["phase"], st["error"], st["sel"]) == ("menu", code, "1"), st
        _, rows = dump(vm)
        assert rows[11] == ERROR_LINE[case], rows
        assert ERROR_LINE[case] in vm.lines()
        assert "> 1  UOS             DEFAULT  /ROBCO/UOS.SYS" in rows, rows
        # One error per attempt.
        key(vm, "select")
        deadline = time.monotonic() + TIMEOUT
        while len(events(vm, "error")) < 2:
            assert time.monotonic() < deadline, vm.serial
            time.sleep(0.05)
        assert len(events(vm, "error")) == 2
        # The menu still works: another entry boots.
        key(vm, "down")
        key(vm, "select")
        vm.wait_for(OS_READY, timeout=TIMEOUT)
        assert one(vm, "load")["path"] == MAINT
        assert one(vm, "boot", "testos")["kind"] == "maint"
        os_checks(vm)
        assert one(vm, "bootinfo", "testos")["path"] == MAINT


def test_countdown_does_not_restart(board, mfboot_image, test_os, need_mtools, tmp_path):
    """The default fails after the countdown: the error shows and nothing boots by itself."""
    cfg = WAIT_CFG.replace(b"default = UOS\n", b"default = UOS\ntimeout = 1\n")
    sd = card(tmp_path, {MAINT: test_os["maint"]}, cfg=cfg)
    with alone(mfboot_image, board, sd) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        vm.wait_for("ev=error code=nofile", timeout=TIMEOUT)
        time.sleep(2.5)
        assert len(events(vm, "choose")) == 1, vm.serial
        assert one(vm, "choose")["via"] == "timeout"
        st = state(vm)
        assert (st["counting"], st["error"]) == ("0", "nofile"), st
        _, rows = dump(vm)
        assert ERROR_LINE["nofile"] in rows, rows
        assert not any(r.startswith("Booting ") for r in rows), rows


def test_golden_error_screen(board, mfboot_image, test_os, need_mtools, tmp_path):
    with alone(mfboot_image, board, card(tmp_path, files_for(test_os, "crc"))) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        key(vm, "select")
        vm.wait_for("ev=error code=crc", timeout=TIMEOUT)
        # The state reply comes from the menu's input wait: drawing is done.
        state(vm)
        img = screenshot(vm, tmp_path)
    check_golden(img, f"error_{board}")


HALTED = "BOOT HALTED. Fix the card and reboot."


def test_no_card(board, mfboot_image):
    with alone(mfboot_image, board) as vm:
        vm.wait_for(IDLE, timeout=TIMEOUT)
        err = one(vm, "error")
        assert (err["code"], err["path"], err["what"]) == ("io", "/ROBCO/BOOT.CFG", "sd"), err
        assert not events(vm, "mount") and not events(vm, "ready"), vm.serial
        assert state(vm)["phase"] == "halted"
        _, rows = dump(vm)
        assert rows[:2] == ["ERROR E02 IO: SD card or FAT volume not usable", HALTED], rows
        # The maintenance port still answers.
        assert vm.script("bogus", timeout=TIMEOUT).status == "err unknown command"


def test_nothing_mountable(board, mfboot_image, tmp_path):
    sd = tmp_path / "blank.img"
    sd.write_bytes(bytes(16 << 20))
    with alone(mfboot_image, board, sd) as vm:
        vm.wait_for(IDLE, timeout=TIMEOUT)
        err = one(vm, "error")
        assert (err["code"], err["what"]) == ("io", "fat"), err
        assert HALTED in vm.lines()


def test_reboot_from_maintenance_port(board, mfboot_image):
    with alone(mfboot_image, board) as vm:
        vm.wait_for(IDLE, timeout=TIMEOUT)
        vm.send("reboot\n", port=1)
        vm.wait_for("ev=reboot source=maint", timeout=TIMEOUT)
        vm.wait_for(re.compile(r"(@RC stage=bootagent ev=boot [^\n]*\n[\s\S]*){2}"),
                    timeout=TIMEOUT)


@pytest.mark.parametrize("kind", ["uos", "maint", "diag"])
def test_each_operating_subtype_boots(board, mfboot_image, test_os, need_mtools, tmp_path,
                                      kind):
    """The test OS built as subtype 0, 2 and 3, each in the first entry, boots."""
    with alone(mfboot_image, board, card(tmp_path, {UOS: test_os[kind]})) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        key(vm, "select")
        vm.wait_for(OS_READY, timeout=TIMEOUT)
        assert one(vm, "load")["subtype"] == str(SUBTYPE[kind])
        assert one(vm, "boot", "testos")["kind"] == kind
        assert not events(vm, "error"), vm.serial
        os_checks(vm)
