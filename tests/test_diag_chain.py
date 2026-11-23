"""The chain: MFBootAgent's menu, choose DIAG, the diagnostics image boots,
trusts the boot info and reaches ready. Alone, and after examples/chain1."""

import re

import mfboot as mf
from diag import READY, TIMEOUT, diag_image, events, one, run, state  # noqa: F401


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
