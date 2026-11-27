"""The whole chain when the agent's image is broken: RETROS-BIOS falls back to
/ROBCO/MFBOOT.BAK, or opens its emergency shell when both files are broken.
Each case breaks the image one way the image format validates. Needs --retros-img."""

import re

import pytest
from harness.boot import parse_markers

import mfboot as mf
from test_chain import BIOS_WAIT_CFG

PRIMARY = "/ROBCO/MFBOOT.SYS"
BACKUP = "/ROBCO/MFBOOT.BAK"
SHELL_BANNER = "RETROS emergency shell. Type HELP for the commands."
SHELL_OPEN = re.compile(r"@RC stage=bios ev=shell [^\n]*available=1[^\n]*\n")

ERROR_LINE = {
    "magic": "ERROR E03 MAGIC: {path}: not a boot image",
    "size": "ERROR E05 SIZE: {path}: size does not match the file",
    "crc": "ERROR E08 CRC: {path}: image CRC does not match",
}


def broken(image, way):
    """The agent's image broken one way, and the code the BIOS reports for it."""
    if way == "crc":
        return mf.flip(image), "crc"
    if way == "magic":
        return mf.patched(image, mf.OFF_MAGIC, "8s", b"ROM1\0\0\0\0"), "magic"
    if way == "truncated":
        # The header still names the full size: the FAT size does not match it.
        return image[:-4], "size"
    raise ValueError(way)


WAYS = ["crc", "magic", "truncated"]


# dump() and state() are copied from RETROS-BIOS tests/retros.py. The BIOS state
# line has no pending field, so mfboot.state() does not fit it.
def dump(vm):
    """The screen text from the maintenance port: (header marker, rows)."""
    reply = vm.script("dump", timeout=mf.TIMEOUT)
    assert reply.status == "ok", reply
    head = parse_markers(reply.lines[0] + "\n")[0]
    rows = [line[1:] for line in reply.lines if line.startswith("|")]
    return head, rows


def state(vm):
    reply = vm.script("state", timeout=mf.TIMEOUT)
    assert reply.status == "ok" and len(reply.lines) == 1, reply
    return dict(field.split("=", 1) for field in reply.lines[0].split())


def bios_order(vm):
    return [m["ev"] for m in vm.markers() if m.get("stage") == "bios"]


def fallback_card(tmp_path, test_os, primary, backup):
    files = mf.os_files(test_os)
    files[PRIMARY] = primary
    files[BACKUP] = backup
    return mf.card(tmp_path, files, cfg=BIOS_WAIT_CFG)


@pytest.mark.parametrize("way", WAYS)
def test_whole_chain_fallback_bak(board, retros_image, mfboot, test_os, need_mtools, tmp_path,
                                  way):
    bad, code = broken(mfboot, way)
    sd = fallback_card(tmp_path, test_os, bad, mfboot)
    with mf.after(retros_image, board, sd) as vm:
        vm.wait_for(mf.READY, timeout=mf.TIMEOUT)
        err = mf.one(vm, "error", "bios")
        assert (err["code"], err["path"]) == (code, PRIMARY), err
        fb = mf.one(vm, "fallback", "bios")
        assert (fb["path"], fb["from"], fb["to"], fb["reason"]) == \
            (BACKUP, PRIMARY, BACKUP, code), fb
        assert mf.one(vm, "load", "bios")["path"] == BACKUP
        bios_flags = int(mf.one(vm, "bootinfo", "bios")["flags"], 16)
        assert bios_flags & mf.BOOT_FALLBACK, vm.serial
        order = bios_order(vm)
        assert order.index("error") < order.index("fallback") < order.index("load") \
            < order.index("jump"), order
        assert mf.one(vm, "handoff")["trusted"] == "1"
        mf.key(vm, "select")
        vm.wait_for(mf.OS_READY, timeout=mf.TIMEOUT)
        mf.os_checks(vm)
        info = mf.one(vm, "bootinfo", "testos")
        assert info["path"] == mf.UOS, info
        # BOOT_PROTOCOL.md: the agent clears bit 0, since it loaded no fallback file.
        assert not int(info["flags"], 16) & mf.BOOT_FALLBACK, info


@pytest.mark.parametrize("way", WAYS)
def test_whole_chain_fallback_shell(board, retros_image, mfboot, test_os, need_mtools, tmp_path,
                                    way):
    bad, code = broken(mfboot, way)
    sd = fallback_card(tmp_path, test_os, bad, bad)
    with mf.after(retros_image, board, sd) as vm:
        vm.wait_for(SHELL_OPEN, timeout=mf.TIMEOUT)
        errors = [(e["code"], e.get("path")) for e in mf.events(vm, "error", "bios")]
        assert errors == [(code, PRIMARY), (code, BACKUP)], vm.serial
        assert mf.one(vm, "fallback", "bios")["reason"] == code
        assert mf.one(vm, "shell", "bios")["reason"] == code
        assert not mf.events(vm, "jump", "bios"), vm.serial
        assert "stage=bootagent" not in vm.serial, vm.serial
        st = state(vm)
        assert (st["phase"], st["error"]) == ("shell", code), st
        _, rows = dump(vm)
        for path in (PRIMARY, BACKUP):
            assert ERROR_LINE[code].format(path=path) in rows, rows
        assert SHELL_BANNER in rows, rows
