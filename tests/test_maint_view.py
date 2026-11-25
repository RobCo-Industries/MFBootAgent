"""View file: BOOT.CFG in the pager, paging, a missing, an empty, an oversized and a
binary file, a cancel. Never a write."""

import hashlib

import pytest

import mfboot as mf
from maintimg import (alone, bad_cluster, card, command, dump, events, good_files, key, maint_image,  # noqa: F401
                      maint_test_image, start, state, wait_result)

# Work rows 14 to 29: the header, 14 text rows, the footer.
HEAD, TEXT0, FOOT = 14, 15, 29


def digest(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def open_cfg(vm):
    start(vm, "view_file")
    _, rows = dump(vm)
    assert rows[HEAD] == "Choose a file. UP/DOWN: move   ENTER: view   ESC: back", rows
    assert rows[HEAD + 1] == "> " + "BOOT.CFG".ljust(26) + "/ROBCO/BOOT.CFG", rows
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


# The chooser: rows 15 to 27 list the files, row 28 holds the note.
NOTE = 28


def chooser(vm):
    start(vm, "view_file")
    return events(vm, "view_list")[-1], dump(vm)[1]


def open_nth(vm, n):
    """Moves to file n, from 0, opens it, and returns the screen."""
    for _ in range(n):
        key(vm, "down")
    key(vm, "select")
    state(vm)
    return dump(vm)[1]


@pytest.mark.parametrize("n, path", [(1, "/UOS/UOS.CFG"), (2, "/UOS/DEVICES.CFG")],
                         ids=["uos", "devices"])
def test_fixed_paths(board, maint_image, good_files, need_mtools, tmp_path, n, path):
    files = dict(good_files)
    files["/UOS/UOS.CFG"] = b"[uos]\nname = uos\n"
    files["/UOS/DEVICES.CFG"] = b"[devices]\nprinter = none\n"
    sd = card(tmp_path, files)
    with alone(maint_image, board, sd=sd) as vm:
        lst, rows = chooser(vm)
        assert (lst["files"], lst["users"], lst["home"]) == ("3", "0", "missing"), lst
        assert rows[HEAD + 2] == "  " + "UOS.CFG".ljust(26) + "/UOS/UOS.CFG", rows
        assert rows[HEAD + 3] == "  " + "DEVICES.CFG".ljust(26) + "/UOS/DEVICES.CFG", rows
        assert rows[NOTE] == "No user TERMINAL.CFG: no /UOS/HOME folder.", rows
        rows = open_nth(vm, n)
        assert rows[HEAD] == f"{path}: {len(files[path])} bytes", rows
        assert rows[TEXT0] == files[path].decode().splitlines()[0], rows
        m = close(vm)
        assert (m["result"], m["path"], m["writes"]) == ("ok", path, "0"), m


def test_two_users_one_terminal_cfg(board, maint_image, good_files, need_mtools, tmp_path):
    files = dict(good_files)
    files["/UOS/HOME/ALICE/TERMINAL.CFG"] = b"[terminal]\ncolour = amber\n"
    files["/UOS/HOME/BOB/NOTES.TXT"] = b"no config here\n"
    sd = card(tmp_path, files)
    before = digest(sd)
    with alone(maint_image, board, sd=sd) as vm:
        lst, rows = chooser(vm)
        assert (lst["files"], lst["users"], lst["cut"], lst["home"]) == ("4", "2", "0", "ok"), lst
        assert rows[HEAD + 4] == "  " + "ALICE/TERMINAL.CFG".ljust(26) + \
            "/UOS/HOME/ALICE/TERMINAL.CFG", rows
        assert rows[HEAD + 5] == "" and rows[NOTE] == "", rows
        rows = open_nth(vm, 3)
        assert rows[TEXT0:TEXT0 + 2] == ["[terminal]", "colour = amber"], rows
        m = close(vm)
        assert (m["result"], m["path"]) == ("ok", "/UOS/HOME/ALICE/TERMINAL.CFG"), m
    assert digest(sd) == before


def test_seventeen_users(board, maint_image, good_files, need_mtools, tmp_path):
    files = dict(good_files)
    for i in range(1, 18):
        files[f"/UOS/HOME/U{i:02d}/TERMINAL.CFG"] = b"user %d\n" % i
    sd = card(tmp_path, files)
    with alone(maint_image, board, sd=sd) as vm:
        lst, rows = chooser(vm)
        assert (lst["files"], lst["users"], lst["cut"]) == ("19", "16", "1"), lst
        assert rows[NOTE] == "The list is cut: only the first 16 user folders of /UOS/HOME.", rows
        # 13 rows show; DOWN scrolls to the last offered file, U16.
        rows = open_nth(vm, 18)
        assert rows[HEAD] == "/UOS/HOME/U16/TERMINAL.CFG: 8 bytes", rows
        names = [m["name"] for m in events(vm, "view_choose")]
        assert names[-1] == "U16/TERMINAL.CFG" and "U17/TERMINAL.CFG" not in names, names
        assert close(vm)["result"] == "ok"


def test_home_with_no_terminal_cfg(board, maint_image, good_files, need_mtools, tmp_path):
    files = dict(good_files)
    files["/UOS/HOME/BOB/NOTES.TXT"] = b"x"
    sd = card(tmp_path, files)
    with alone(maint_image, board, sd=sd) as vm:
        lst, rows = chooser(vm)
        assert (lst["files"], lst["users"], lst["home"]) == ("3", "1", "ok"), lst
        assert rows[NOTE] == "No user TERMINAL.CFG: none found in /UOS/HOME.", rows
        key(vm, "back")
        assert wait_result(vm, "view_file", 1)["why"] == "cancel"


def test_home_does_not_read(board, maint_image, good_files, need_mtools, tmp_path):
    """A /UOS/HOME the card cannot read is an error with its code, never "no folder"."""
    files = dict(good_files)
    files["/UOS/HOME/ALICE/TERMINAL.CFG"] = b"[terminal]\n"
    sd = card(tmp_path, files)
    bad_cluster(sd, b"HOME       ")
    before = digest(sd)
    with alone(maint_image, board, sd=sd) as vm:
        lst, rows = chooser(vm)
        assert (lst["home"], lst["files"]) == ("error", "3"), lst
        assert int(lst["home_rc"]) < 0, lst
        assert rows[NOTE] == "/UOS/HOME does not read: error %s. Users may be missing." % \
            lst["home_rc"], rows
        # The fixed files still open; the tool fails on the fault it saw.
        rows = open_nth(vm, 0)
        assert rows[HEAD].startswith("/ROBCO/BOOT.CFG: "), rows
        m = close(vm)
        assert (m["result"], m["list"], m["list_rc"], m["writes"]) == \
            ("fail", "error", lst["home_rc"], "0"), m
        state(vm)
        assert dump(vm)[1][12] == "View file: FAILED. /UOS/HOME read error %s." % lst["home_rc"]
    assert digest(sd) == before


def test_user_name_with_a_space(board, maint_image, good_files, need_mtools, tmp_path):
    """A user folder whose long name has a space: one value in view_choose and path=."""
    files = dict(good_files)
    files["/UOS/HOME/Mary Ann/TERMINAL.CFG"] = b"[terminal]\ncolour = green\n"
    sd = card(tmp_path, files)
    with alone(maint_image, board, sd=sd) as vm:
        lst, rows = chooser(vm)
        assert (lst["files"], lst["users"], lst["home"]) == ("4", "1", "ok"), lst
        assert rows[HEAD + 4] == "  " + "Mary Ann/TERMINAL.CFG".ljust(26) + \
            "/UOS/HOME/Mary Ann/TERMINAL.CFG", rows
        rows = open_nth(vm, 3)
        assert rows[TEXT0:TEXT0 + 2] == ["[terminal]", "colour = green"], rows
        names = [mf.mval(m["name"]) for m in events(vm, "view_choose")]
        assert names[-1] == ("Mary Ann/TERMINAL.CFG", False), names
        m = close(vm)
        assert m["result"] == "ok", m
        assert mf.mval(m["path"]) == ("/UOS/HOME/Mary Ann/TERMINAL.CFG", False), m
