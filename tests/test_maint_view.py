"""View file: BOOT.CFG in the pager, paging, a missing, an empty, an oversized and a
binary file, a cancel. Never a write."""

import hashlib

import mfboot as mf
from maintimg import (alone, card, command, dump, events, good_files, key, maint_image,  # noqa: F401
                      maint_test_image, start, state, wait_result)

# Work rows 14 to 29: the header, 14 text rows, the footer.
HEAD, TEXT0, FOOT = 14, 15, 29


def digest(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def open_cfg(vm):
    start(vm, "view_file")
    _, rows = dump(vm)
    assert rows[HEAD] == "Choose a file. ENTER: view   ESC: back", rows
    assert rows[HEAD + 1] == "> 1  BOOT.CFG      /ROBCO/BOOT.CFG", rows
    key(vm, "select")
    state(vm)
    return dump(vm)[1]


def close(vm, n=1):
    key(vm, "back")
    return wait_result(vm, "view_file", n)


def test_boot_cfg_pages(board, maint_image, good_files, need_mtools, tmp_path):
    cfg = mf.WAIT_CFG + b"".join(b"; comment line %d\n" % i for i in range(20))
    sd = card(tmp_path, good_files, cfg=cfg)
    before = digest(sd)
    lines = cfg.decode().splitlines()
    with alone(maint_image, board, sd=sd) as vm:
        rows = open_cfg(vm)
        assert rows[HEAD] == f"/ROBCO/BOOT.CFG: {len(cfg)} bytes", rows
        assert rows[TEXT0:TEXT0 + 14] == lines[:14], rows
        assert rows[FOOT] == "Lines 1-14 of 33.   UP/DOWN: page   ESC or ENTER: close", rows
        key(vm, "down")
        state(vm)
        rows = dump(vm)[1]
        assert rows[TEXT0:TEXT0 + 14] == lines[14:28], rows
        key(vm, "down")
        state(vm)
        rows = dump(vm)[1]
        assert rows[TEXT0:TEXT0 + 5] == lines[28:33] and rows[TEXT0 + 5] == "", rows
        assert rows[FOOT].startswith("Lines 29-33 of 33."), rows
        # The last page is the last: DOWN stays.
        key(vm, "down")
        key(vm, "up")
        state(vm)
        assert [(m["first"], m["last"]) for m in events(vm, "view_page")] == \
            [("1", "14"), ("15", "28"), ("29", "33"), ("15", "28")], vm.serial
        m = close(vm)
        assert (m["result"], m["path"], m["size"], m["lines"], m["binary"], m["truncated"],
                m["writes"]) == ("ok", mf.CFG, str(len(cfg)), "33", "0", "0", "0"), m
        assert state(vm)["phase"] == "menu"
    assert digest(sd) == before


def test_missing(board, maint_image, good_files, need_mtools, tmp_path):
    sd = card(tmp_path, good_files, cfg=None)
    with alone(maint_image, board, sd=sd) as vm:
        start(vm, "view_file")
        key(vm, "select")
        m = wait_result(vm, "view_file", 1)
        assert (m["result"], m["why"], m["path"], m["writes"]) == \
            ("absent", "missing", mf.CFG, "0"), m
        state(vm)
        assert dump(vm)[1][12] == "View file: ABSENT. No BOOT.CFG.", vm.serial


def test_empty(board, maint_image, good_files, need_mtools, tmp_path):
    sd = card(tmp_path, good_files, cfg=b"")
    with alone(maint_image, board, sd=sd) as vm:
        rows = open_cfg(vm)
        assert rows[HEAD] == "/ROBCO/BOOT.CFG: 0 bytes", rows
        assert rows[TEXT0] == "(empty file)", rows
        assert rows[FOOT] == "No lines.   ESC or ENTER: close", rows
        m = close(vm)
        assert (m["result"], m["size"], m["lines"], m["truncated"]) == ("ok", "0", "0", "0"), m


def test_oversized(board, maint_image, good_files, need_mtools, tmp_path):
    cfg = b"".join(b"line %05d\n" % i for i in range(2000))     # 22000 bytes
    sd = card(tmp_path, good_files, cfg=cfg)
    with alone(maint_image, board, sd=sd) as vm:
        rows = open_cfg(vm)
        assert rows[HEAD] == "/ROBCO/BOOT.CFG: 22000 bytes, the first 16384 shown", rows
        assert rows[TEXT0] == "line 00000", rows
        # 16384 bytes: 1489 whole lines of 11 bytes and the start of the next.
        assert rows[FOOT].startswith("Lines 1-14 of 1490."), rows
        m = close(vm)
        assert (m["result"], m["size"], m["shown"], m["lines"], m["truncated"]) == \
            ("ok", "22000", "16384", "1490", "1"), m


def test_binary(board, maint_image, good_files, need_mtools, tmp_path):
    cfg = b"[boot]\x00\x01\x02\xff\ndefault = UOS\n"
    sd = card(tmp_path, good_files, cfg=cfg)
    with alone(maint_image, board, sd=sd) as vm:
        rows = open_cfg(vm)
        assert rows[HEAD] == "/ROBCO/BOOT.CFG: 25 bytes, 4 bytes shown as '.'", rows
        assert rows[TEXT0:TEXT0 + 2] == ["[boot]....", "default = UOS"], rows
        m = close(vm)
        assert (m["result"], m["binary"], m["lines"]) == ("ok", "4", "2"), m


def test_enter_closes_and_cancel(board, maint_image, good_files, need_mtools, tmp_path):
    sd = card(tmp_path, good_files)
    with alone(maint_image, board, sd=sd) as vm:
        open_cfg(vm)
        key(vm, "select")
        assert wait_result(vm, "view_file", 1)["result"] == "ok"
        # ESC at the chooser cancels: nothing was shown.
        start(vm, "view_file")
        key(vm, "back")
        m = wait_result(vm, "view_file", 2)
        assert (m["result"], m["why"], m["writes"]) == ("refused", "cancel", "0"), m


def test_write_is_counted_and_refused(board, maint_test_image, good_files, need_mtools,
                                      tmp_path):
    sd = card(tmp_path, good_files)
    before = digest(sd)
    with alone(maint_test_image, board, sd=sd) as vm:
        command(vm, "wprobe")
        open_cfg(vm)
        m = close(vm)
        assert (m["result"], m["probe_rc"], m["writes"]) == ("fail", "-5", "1"), m
    assert digest(sd) == before
