"""The chain: MFBootAgent's menu, choose DIAG, the diagnostics image boots,
trusts the boot info and reaches ready. Alone, and after examples/chain1."""

import re

import mfboot as mf
from diag import MENU_ROWS, READY, TIMEOUT, diag_image, events, one, run, state  # noqa: F401


def chain_files(test_os, diag_image):
    with open(diag_image, "rb") as f:
        diag = f.read()
    return {mf.UOS: test_os["uos"], mf.MAINT: test_os["maint"], mf.DIAG: diag}


def check_diag_booted(vm):
    vm.wait_for(READY, timeout=TIMEOUT)
    choose = mf.one(vm, "choose")
    assert (choose["index"], choose["name"], choose["path"]) == ("3", "DIAG", mf.DIAG), choose
    load = mf.one(vm, "load")
    assert (load["subtype"], load["name"], load["addr"]) == ("3", "DIAG", "0x00400000"), load
    assert one(vm, "handoff")["trusted"] == "1"
    order = [m["ev"] for m in vm.markers() if m.get("stage") == "diag"]
    assert order == ["boot", "handoff", "screen", "mmu", "input", "selfcheck", "ready"], order
    assert one(vm, "selfcheck")["rc"] == "ok"
    st = state(vm)
    assert (st["phase"], st["trusted"]) == ("menu", "1"), st


def test_from_the_agent_menu(board, mfboot_image, diag_image, test_os, need_mtools, tmp_path):
    sd = mf.card(tmp_path, chain_files(test_os, diag_image))
    with mf.alone(mfboot_image, board, sd) as vm:
        vm.wait_for(mf.READY, timeout=TIMEOUT)
        mf.char(vm, "3")
        mf.key(vm, "select")
        check_diag_booted(vm)
        # The agent cold-booted with its own green screen: adopted as it is, then cleared.
        scr = one(vm, "screen")
        assert (scr["path"], scr["scheme"], scr["cols"], scr["rows"]) == \
            ("adopt", "0", "80", "30"), scr
        mf.state(vm)
        _, rows = mf.dump(vm)
        assert rows[0] == "TERMINAL DIAGNOSTICS V0.1.0", rows
        assert not any("MFBOOT" in r for r in rows), rows
        # The card the agent booted from works in the image.
        m = run(vm, "sd")
        assert (m["result"], m["images"], m["writes"]) == ("ok", "3", "0"), m


def test_after_chain1_keeps_the_scheme(board, chain1_image, mfboot, diag_image, test_os,
                                       need_mtools, tmp_path):
    files = chain_files(test_os, diag_image)
    files["/ROBCO/MFBOOT.SYS"] = mfboot
    with mf.after(chain1_image, board, mf.card(tmp_path, files)) as vm:
        vm.wait_for(mf.READY, timeout=TIMEOUT)
        mf.char(vm, "3")
        mf.key(vm, "select")
        check_diag_booted(vm)
        # chain1 handed over an amber console; the agent passed it on.
        scr = one(vm, "screen")
        assert (scr["path"], scr["scheme"]) == ("adopt", "1"), scr
        assert re.search(r"@RC stage=chain1 ev=jump", vm.serial)
        assert not events(vm, "exception")


MAINT_OPEN = re.compile(r"@RC stage=bootagent ev=maint_open [^\n]*\n")
MAINT_CLOSE = re.compile(r"@RC stage=bootagent ev=maint_close\r?\n")
VERIFY_DONE = re.compile(r"@RC stage=bootagent ev=verify_done [^\n]*\n")
BOOT_MAINT = 1 << 1


def test_verify_then_boot_from_the_maintenance_screen(board, mfboot_image, diag_image, test_os,
                                                       need_mtools, tmp_path):
    """Both features on one card: Verify entries passes the real DIAG.SYS,
    then DIAG boots from the menu with boot_flags bit 1 set and reaches ready."""
    sd = mf.card(tmp_path, chain_files(test_os, diag_image))
    with mf.alone(mfboot_image, board, sd) as vm:
        vm.wait_for(mf.READY, timeout=TIMEOUT)
        mf.char(vm, "m")
        vm.wait_for(MAINT_OPEN, timeout=TIMEOUT)
        # The bar starts on Verify entries.
        mf.key(vm, "select")
        vm.wait_for(VERIFY_DONE, timeout=TIMEOUT)
        assert mf.one(vm, "maint_action")["name"] == "verify"
        found = [(m["name"], m["path"], m["result"]) for m in mf.events(vm, "verify_entry")]
        assert found == [("UOS", mf.UOS, "ok"), ("MAINT", mf.MAINT, "ok"),
                         ("DIAG", mf.DIAG, "ok")], vm.serial
        done = mf.one(vm, "verify_done")
        assert (done["result"], done["good"], done["failed"]) == ("ok", "3", "0"), done
        st = mf.state(vm)
        assert st["maint"] == "1", st
        mf.char(vm, "q")
        vm.wait_for(MAINT_CLOSE, timeout=TIMEOUT)
        mf.char(vm, "3")
        mf.key(vm, "select")
        check_diag_booted(vm)
        flags = int(mf.one(vm, "bootinfo")["flags"], 0)
        assert flags & BOOT_MAINT, flags
        # The image ignores bit 1: the same menu as without it.
        _, rows = mf.dump(vm)
        assert rows[:len(MENU_ROWS)] == MENU_ROWS, rows
