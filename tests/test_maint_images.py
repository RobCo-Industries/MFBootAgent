"""Check images: a good card, a truncated image, a wrong CRC, other names. Never a write."""

import hashlib

import mfboot as mf
from maintimg import (alone, card, dump, events, good_card, good_files,  # noqa: F401
                      maint_image, maint_test_image, command, run, state)


def digest(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def images(vm):
    return {m["path"]: (m["rc"], m["size"]) for m in events(vm, "image")}


def test_good_card(board, maint_image, good_files, need_mtools, tmp_path):
    files = dict(good_files)
    files["/ROBCO/NOTES.TXT"] = b"not an image"
    sd = card(tmp_path, files)
    before = digest(sd)
    with alone(maint_image, board, sd=sd) as vm:
        m = run(vm, "check_images")
        assert (m["result"], m["images"], m["bad"], m["writes"]) == ("ok", "3", "0", "0"), m
        found = images(vm)
        assert found == {p: ("ok", str(len(files[p]))) for p in (mf.UOS, mf.MAINT, mf.DIAG)}, found
        state(vm)
        _, rows = dump(vm)
        assert any(r.startswith("MAINT.SYS") and " OK      MAINT subtype 2" in r for r in rows), rows
        assert not any("NOTES.TXT" in r for r in rows), rows
    assert digest(sd) == before


def test_truncated_and_wrong_crc(board, maint_image, good_files, need_mtools, tmp_path):
    files = dict(good_files)
    files[mf.UOS] = files[mf.UOS][:len(files[mf.UOS]) // 2]
    files[mf.DIAG] = mf.flip(files[mf.DIAG])
    sd = card(tmp_path, files)
    before = digest(sd)
    with alone(maint_image, board, sd=sd) as vm:
        m = run(vm, "check_images")
        assert (m["result"], m["images"], m["bad"], m["writes"]) == ("fail", "3", "2", "0"), m
        found = images(vm)
        assert found[mf.UOS][0] == "size", found
        assert found[mf.DIAG][0] == "crc", found
        assert found[mf.MAINT][0] == "ok", found
        state(vm)
        _, rows = dump(vm)
        assert any(r.startswith("UOS.SYS") and "FAILED  size" in r for r in rows), rows
        assert any(r.startswith("DIAG.SYS") and "FAILED  crc" in r for r in rows), rows
        assert rows[12].startswith("Check images: FAILED. "), rows
    assert digest(sd) == before


def test_short_and_empty(board, maint_image, good_files, need_mtools, tmp_path):
    files = dict(good_files)
    files["/ROBCO/MFBOOT.BAK"] = b""
    files["/ROBCO/OLD.IMG"] = b"x" * 16
    sd = card(tmp_path, files)
    with alone(maint_image, board, sd=sd) as vm:
        m = run(vm, "check_images")
        assert (m["result"], m["images"], m["bad"]) == ("fail", "5", "2"), m
        found = images(vm)
        assert found["/ROBCO/MFBOOT.BAK"] == ("empty", "0"), found
        assert found["/ROBCO/OLD.IMG"] == ("short", "16"), found


def test_no_robco_folder(board, maint_image, need_mtools, tmp_path):
    sd = card(tmp_path, {"/UOS/X.DAT": b"x"}, cfg=None)
    with alone(maint_image, board, sd=sd) as vm:
        m = run(vm, "check_images")
        assert (m["result"], m["dir"], m["writes"]) == ("fail", "missing", "0"), m


def test_write_is_counted_and_refused(board, maint_test_image, good_card):
    before = digest(good_card)
    with alone(maint_test_image, board, sd=good_card) as vm:
        command(vm, "wprobe")
        m = run(vm, "check_images")
        assert (m["result"], m["probe_rc"], m["writes"]) == ("fail", "-5", "1"), m
        m = run(vm, "check_images")
        assert (m["result"], m["writes"]) == ("ok", "0"), m
    assert digest(good_card) == before


def test_name_with_a_space(board, maint_image, good_files, need_mtools, tmp_path):
    """A long name with a space stays one marker value and parses back whole."""
    files = dict(good_files)
    files["/ROBCO/My Image.SYS"] = b"x" * 16
    sd = card(tmp_path, files)
    with alone(maint_image, board, sd=sd) as vm:
        m = run(vm, "check_images")
        assert (m["result"], m["images"], m["bad"]) == ("fail", "4", "1"), m
        assert m["bad_path"] == "/ROBCO/My%20Image.SYS", m
        assert mf.mval(m["bad_path"]) == ("/ROBCO/My Image.SYS", False), m
        found = {mf.mval(p)[0]: v for p, v in images(vm).items()}
        assert found["/ROBCO/My Image.SYS"] == ("short", "16"), found
        state(vm)
        _, rows = dump(vm)
        assert any(r.startswith("My Image.SYS") and "FAILED  short" in r for r in rows), rows
