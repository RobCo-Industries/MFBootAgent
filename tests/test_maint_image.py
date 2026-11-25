"""MAINT.SYS: its start alone and from the agent's menu, its markers, its menu,
its port and the [maint] section of BOOT.CFG."""

import pytest

import mfboot as mf
from maintimg import (MENU_ROWS, READY, TIMEOUT, alone, card, command, dump, events,  # noqa: F401
                      good_card, good_files, maint_cfg, maint_image, maint_test_image, one,
                      state)

ORDER = ["boot", "handoff", "screen", "input", "selfcheck", "mount", "config", "ready"]


def order(vm):
    return [m["ev"] for m in vm.markers() if m.get("stage") == "maint"]


def test_alone_markers_and_menu(board, maint_image, good_card):
    with alone(maint_image, board, sd=good_card) as vm:
        assert order(vm) == ORDER, vm.serial
        boot = one(vm, "boot")
        assert boot["version"] == "0.1.0" and boot["entry_mode"] in ("svc", "hyp"), boot
        assert one(vm, "handoff")["trusted"] == "0"
        scr = one(vm, "screen")
        assert (scr["path"], scr["fb"], scr["console"]) == ("init", "0", "0"), scr
        assert one(vm, "selfcheck")["rc"] == "ok"
        assert one(vm, "mount")["lba"] == "2048"
        cfg = one(vm, "config")
        assert (cfg["result"], cfg["path"], cfg["restore_users"], cfg["bad"], cfg["unknown"]) == \
            ("ok", mf.CFG, "off", "0", "0"), cfg
        st = state(vm)
        assert (st["phase"], st["trusted"], st["fb"], st["tool"], st["card"]) == \
            ("menu", "0", "1", "none", "1"), st
        _, rows = dump(vm)
        assert rows[:len(MENU_ROWS)] == MENU_ROWS, rows


def test_from_the_agent_menu(board, mfboot_image, maint_image, good_card):
    with mf.alone(mfboot_image, board, good_card) as vm:
        vm.wait_for(mf.READY, timeout=TIMEOUT)
        mf.char(vm, "2")
        mf.key(vm, "select")
        vm.wait_for(READY, timeout=TIMEOUT)
        choose = mf.one(vm, "choose")
        assert (choose["name"], choose["path"]) == ("MAINT", mf.MAINT), choose
        load = mf.one(vm, "load")
        assert (load["subtype"], load["name"], load["addr"]) == ("2", "MAINT", "0x00400000"), load
        assert order(vm) == ORDER, vm.serial
        assert one(vm, "handoff")["trusted"] == "1"
        scr = one(vm, "screen")
        assert (scr["path"], scr["cols"], scr["rows"]) == ("adopt", "80", "30"), scr
        st = state(vm)
        assert (st["phase"], st["trusted"], st["card"]) == ("menu", "1", "1"), st
        _, rows = dump(vm)
        # The menu entry: boot_flags bit 1 shows as a fact on the status row.
        want = list(MENU_ROWS)
        want[12] = "Maintenance asked. Choose a tool."
        assert rows[:len(MENU_ROWS)] == want, rows
        assert not any("MFBOOT" in r for r in rows), rows


def test_no_card(board, maint_image):
    with alone(maint_image, board) as vm:
        assert one(vm, "error")["what"] == "sd", vm.serial
        cfg = one(vm, "config")
        assert (cfg["result"], cfg["why"], cfg["restore_users"]) == ("bad", "read", "off"), cfg
        st = state(vm)
        assert (st["phase"], st["card"]) == ("menu", "0"), st


def test_port(board, maint_image, good_card):
    with alone(maint_image, board, sd=good_card) as vm:
        assert vm.script("tool", timeout=TIMEOUT).status == "err unknown command"
        assert vm.script("tool bogus", timeout=TIMEOUT).status == "err unknown tool"
        assert vm.script("bogus", timeout=TIMEOUT).status == "err unknown command"
        # The test build's write probe is not in the shipped image.
        assert vm.script("wprobe", timeout=TIMEOUT).status == "err unknown command"
        assert events(vm, "write_hook") == [], vm.serial
        # Keys move the selection.
        mf.key(vm, "down")
        mf.key(vm, "down")
        mf.key(vm, "up")
        st = state(vm)
        assert st["sel"] == "2", st
        assert [m["name"] for m in events(vm, "select")] == ["check_images", "view_file",
                                                            "check_images"], vm.serial


def test_test_build_has_the_hook(board, maint_test_image, good_card):
    with alone(maint_test_image, board, sd=good_card) as vm:
        assert one(vm, "write_hook")["name"] == "guard"
        command(vm, "wprobe")


@pytest.mark.parametrize("extra, want", [
    (b"", ("ok", "off", "0", "0")),
    (b"\n[maint]\nrestore_users = on\n", ("ok", "on", "0", "0")),
    (b"\n[MAINT]\nRestore_Users = ON\n", ("ok", "on", "0", "0")),
    (b"\n[maint]\nrestore_users = off\n", ("ok", "off", "0", "0")),
    (b"\n[maint]\nrestore_users = yes\n", ("ok", "off", "1", "0")),
    (b"\n[maint]\nrestore_users = on\ncolour = red\n[maint]\nrestore_users = off\n",
     ("ok", "off", "0", "1")),
    (b"\n[maint]\nrestore_users = off\n[boot]\ntimeout = 3\n[maint]\nrestore_users = on\n",
     ("ok", "on", "0", "0")),
], ids=["absent", "on", "case", "off", "bad", "twice_off", "twice_on"])
def test_config(board, maint_image, good_files, need_mtools, tmp_path, extra, want):
    sd = card(tmp_path, good_files, cfg=maint_cfg(extra))
    with alone(maint_image, board, sd=sd) as vm:
        cfg = one(vm, "config")
        assert (cfg["result"], cfg["restore_users"], cfg["bad"], cfg["unknown"]) == want, cfg
        assert state(vm)["restore_users"] == want[1]


def test_config_missing_and_syntax(board, maint_image, good_files, need_mtools, tmp_path):
    sd = card(tmp_path, good_files, cfg=None)
    with alone(maint_image, board, sd=sd) as vm:
        cfg = one(vm, "config")
        assert (cfg["result"], cfg["restore_users"]) == ("missing", "off"), cfg
    sd = card(tmp_path, good_files, name="bad.img",
              cfg=maint_cfg(b"\n[maint]\nrestore_users = on\nthis line does not parse\n"))
    with alone(maint_image, board, sd=sd) as vm:
        cfg = one(vm, "config")
        assert (cfg["result"], cfg["why"], cfg["restore_users"]) == ("bad", "syntax", "off"), cfg
        assert cfg["line"] == "17", cfg
        assert state(vm)["restore_users"] == "off"
