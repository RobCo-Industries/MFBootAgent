"""MFBootAgent after a first stage: RobCo-Core's examples/chain1, and RETROS-BIOS when
RETROS_IMG is set. The first stage loads /ROBCO/MFBOOT.SYS from the card."""

from mfboot import (MACH, OS_READY, READY, STAGE_BIOS, TIMEOUT, UOS, WAIT_CFG, after, card,
                    dump, events, key, one, os_checks, os_files, screenshot)

BOOT_COLD = 1 << 2


def chain_card(tmp_path, mfboot, test_os, cfg=WAIT_CFG):
    files = os_files(test_os)
    files["/ROBCO/MFBOOT.SYS"] = mfboot
    return card(tmp_path, files, cfg=cfg)


def stage_order(vm):
    return [(m["stage"], m["ev"]) for m in vm.markers()
            if (m["stage"], m["ev"]) in {("chain1", "jump"), ("bios", "jump"),
                                         ("bootagent", "boot"), ("bootagent", "ready"),
                                         ("bootagent", "jump"), ("testos", "boot"),
                                         ("testos", "ready")}]


def test_after_chain1(board, chain1_image, mfboot, test_os, need_mtools, tmp_path):
    with after(chain1_image, board, chain_card(tmp_path, mfboot, test_os)) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        assert one(vm, "handoff")["trusted"] == "1"
        screen = one(vm, "screen")
        # chain1 hands over an amber console; its text stays on the screen.
        assert (screen["kept"], screen["unknown"], screen["scheme"]) == ("1", "0", "1"), screen
        _, rows = dump(vm)
        assert rows[:3] == ["chain1", "", "ROBCO MFBOOT AGENT V0.1.0"], rows
        assert rows[7] == "> 1  UOS             DEFAULT  /ROBCO/UOS.SYS", rows
        key(vm, "select")
        vm.wait_for(OS_READY, timeout=TIMEOUT)
        assert stage_order(vm) == [("chain1", "jump"), ("bootagent", "boot"),
                                   ("bootagent", "ready"), ("bootagent", "jump"),
                                   ("testos", "boot"), ("testos", "ready")], vm.serial
        os_checks(vm)
        fb = one(vm, "fb", "chain1")["addr"]
        info = one(vm, "bootinfo", "testos")
        # Kept from chain1: firmware registers, framebuffer, the cold flag, the scheme.
        assert info["fw_r1"] == f"{MACH[board]:#010x}"
        assert info["fb"] == fb
        assert int(info["flags"], 16) == BOOT_COLD
        assert info["scheme"] == "1"
        # Set by MFBootAgent: the stage, the path, the cmdline and the cursor.
        assert (info["prev_stage"], info["path"], info["cmdline"]) == \
            ("2", UOS, "console=tty0_quiet"), info
        assert info["part_lba"] == "2048"
        # The row after the menu: chain1 text and a blank row, then 13 menu rows.
        assert int(info["cy"]) == 2 + 13, info


def test_after_chain1_screen(board, chain1_image, mfboot, test_os, need_mtools, tmp_path):
    """The screenshot of the menu after a first stage; looked at, not compared."""
    with after(chain1_image, board, chain_card(tmp_path, mfboot, test_os)) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        img = screenshot(vm, tmp_path)
    assert img.size == (640, 480)


RETROS_CFG = WAIT_CFG.replace(b"default = UOS\n", b"default = UOS\ntimeout = 0\n") + \
    b"\n[bios]\ndelay_ms = 0\nbeep = off\n"


def test_whole_chain(board, retros_image, mfboot, test_os, need_mtools, tmp_path):
    sd = chain_card(tmp_path, mfboot, test_os, cfg=RETROS_CFG)
    with after(retros_image, board, sd) as vm:
        vm.wait_for(OS_READY, timeout=TIMEOUT)
        assert stage_order(vm) == [("bios", "jump"), ("bootagent", "boot"),
                                   ("bootagent", "ready"), ("bootagent", "jump"),
                                   ("testos", "boot"), ("testos", "ready")], vm.serial
        assert one(vm, "handoff")["trusted"] == "1"
        screen = one(vm, "screen")
        assert (screen["kept"], screen["unknown"], screen["scheme"]) == ("1", "0", "0"), screen
        os_checks(vm)
        info = one(vm, "bootinfo", "testos")
        bios = one(vm, "bootinfo", "bios")
        assert (info["prev_stage"], info["path"]) == ("2", UOS), info
        assert info["part_lba"] == bios["part_lba"]
        assert int(info["flags"], 16) == int(bios["flags"], 16)
        assert not events(vm, "error")
        assert int(one(vm, "load", "bios")["size"]) == len(mfboot)
        assert one(vm, "boot", "bios")["midr"] == one(vm, "boot", "bootagent")["midr"]
        assert one(vm, "handoff", "testos")["trusted"] == "1"
        assert STAGE_BIOS == 1
