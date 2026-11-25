"""The diagnostics image alone: markers, the menu golden, keys and the port."""

import re

from diag import (MENU_ROWS, READY, TIMEOUT, alone, command, diag_image, dump,  # noqa: F401
                  events, key, one, results, run, state)
from mfboot import check_golden, screenshot

ORDER = ["boot", "handoff", "screen", "mmu", "input", "selfcheck", "ready"]


def test_markers_and_golden(board, diag_image, tmp_path):
    with alone(diag_image, board) as vm:
        got = [m["ev"] for m in vm.markers() if m.get("stage") == "diag"]
        assert got == ORDER, vm.serial
        boot = one(vm, "boot")
        assert boot["version"] == "0.1.0" and boot["entry_mode"] in ("svc", "hyp"), boot
        assert one(vm, "handoff")["trusted"] == "0"
        scr = one(vm, "screen")
        assert (scr["fb"], scr["console"], scr["path"], scr["scheme"]) == ("0", "0", "init", "0")
        assert (scr["cols"], scr["rows"]) == ("80", "30"), scr
        assert one(vm, "mmu")["on"] == "1"
        inp = one(vm, "input")
        assert (inp["serial"], inp["mini"], inp["script"]) == ("0", "0", "0"), inp
        assert one(vm, "selfcheck")["rc"] == "ok"
        _, rows = dump(vm)
        assert rows[:len(MENU_ROWS)] == MENU_ROWS, rows
        assert all(r == "" for r in rows[len(MENU_ROWS):]), rows
        # The PL011 shows the menu too, line by line.
        lines = vm.lines()
        for row in MENU_ROWS:
            if row and row != MENU_ROWS[-1]:
                assert row in lines, row
        st = state(vm)
        assert (st["phase"], st["trusted"], st["fb"], st["sel"], st["test"]) == \
            ("menu", "0", "1", "1", "none"), st
        img = screenshot(vm, tmp_path)
    check_golden(img, f"diag_menu_{board}")


def test_keys_move_and_start(board, diag_image):
    with alone(diag_image, board) as vm:
        key(vm, "down")
        key(vm, "down")
        vm.wait_for("ev=select index=3 name=video", timeout=TIMEOUT)
        key(vm, "up")
        # The first DOWN printed this marker too: wait for the second one.
        vm.wait_for(re.compile(r"(?:ev=select index=2 name=sd\r?\n[\s\S]*?){2}"), timeout=TIMEOUT)
        assert state(vm)["sel"] == "2"
        _, rows = dump(vm)
        assert rows[5] == "> 2  SD        Card reads, image checks", rows
        assert rows[4] == "  1  MEM       RAM write and read", rows
        # A digit starts its test: 7 is rng.
        command(vm, "char 7")
        vm.wait_for(re.compile(r"ev=diag name=rng result=[^\n]*\n"), timeout=TIMEOUT)
        assert events(vm, "select")[-1]["name"] == "rng"
        # ENTER on the selected row runs it again, and the row shows the result.
        key(vm, "select")
        vm.wait_for(re.compile(r"(ev=diag name=rng [^\n]*\n[\s\S]*){2}"), timeout=TIMEOUT)
        assert len(results(vm, "rng")) == 2
        _, rows = dump(vm)
        assert rows[10].startswith("> 7  RNG       Random number generator      OK       "), rows
        assert rows[17].startswith("RNG: OK. 0 repeats, "), rows


def test_port_commands(board, diag_image):
    with alone(diag_image, board) as vm:
        assert vm.script("diag bogus", timeout=TIMEOUT).status == "err unknown test"
        assert vm.script("diag", timeout=TIMEOUT).status == "err unknown command"
        assert vm.script("bogus", timeout=TIMEOUT).status == "err unknown command"
        assert vm.script("fault 0x1000", timeout=TIMEOUT).status == "err unknown command"
        # A long test runs in the main loop: the port answers meanwhile and refuses a second.
        command(vm, "diag mem")
        vm.wait_for("ev=diag_progress name=mem", timeout=TIMEOUT)
        st = state(vm)
        assert (st["phase"], st["test"]) == ("test", "mem"), st
        assert vm.script("diag rng", timeout=TIMEOUT).status == "err busy"
        vm.wait_for(re.compile(r"ev=diag name=mem result=ok"), timeout=TIMEOUT)
        assert state(vm)["phase"] == "menu"
        assert not results(vm, "rng")


def test_reboot_from_menu_and_port(board, diag_image):
    with alone(diag_image, board) as vm:
        command(vm, "char r")
        vm.wait_for("ev=reboot source=key", timeout=TIMEOUT)
        vm.wait_for(re.compile(r"(@RC stage=diag ev=ready\n[\s\S]*){2}"), timeout=TIMEOUT)
        vm.send("reboot\n", port=1)
        vm.wait_for("ev=reboot source=maint", timeout=TIMEOUT)
        vm.wait_for(re.compile(r"(@RC stage=diag ev=ready\n[\s\S]*){3}"), timeout=TIMEOUT)
