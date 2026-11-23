"""BOOT.CFG on the card: missing, bad and partly bad files."""

from mfboot import (OS_READY, READY, TIMEOUT, UOS, alone, card, dump, key, one, os_checks,
                    os_files)

ONE_ENTRY = "> 1  UOS             DEFAULT  /ROBCO/UOS.SYS"


def check_default_menu(vm):
    ent = one(vm, "entries")
    assert (ent["count"], ent["default"], ent["timeout"]) == ("1", "UOS", "none"), ent
    _, rows = dump(vm)
    assert rows[5] == ONE_ENTRY and rows[6] == "", rows
    key(vm, "select")
    vm.wait_for(OS_READY, timeout=TIMEOUT)
    os_checks(vm)
    info = one(vm, "bootinfo", "testos")
    assert (info["path"], info["cmdline"]) == (UOS, "none"), info


def test_missing_file(board, mfboot_image, test_os, need_mtools, tmp_path):
    with alone(mfboot_image, board, card(tmp_path, os_files(test_os), cfg=None)) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        cfg = one(vm, "config")
        assert (cfg["result"], cfg["path"]) == ("missing", "/ROBCO/BOOT.CFG"), cfg
        check_default_menu(vm)


def test_syntax_error(board, mfboot_image, test_os, need_mtools, tmp_path):
    bad = b"[boot]\ntimeout = 1\n[entry MAINT]\npath = /ROBCO/MAINT.SYS\nthis is not ini\n"
    with alone(mfboot_image, board, card(tmp_path, os_files(test_os), cfg=bad)) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        cfg = one(vm, "config")
        assert (cfg["result"], cfg["why"], cfg["line"]) == ("bad", "syntax", "5"), cfg
        check_default_menu(vm)


def test_no_usable_entry(board, mfboot_image, test_os, need_mtools, tmp_path):
    bad = b"[boot]\ntimeout = 1\n[entry A]\npath = no/slash\n"
    with alone(mfboot_image, board, card(tmp_path, os_files(test_os), cfg=bad)) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        cfg = one(vm, "config")
        assert (cfg["result"], cfg["why"], cfg["bad"]) == ("bad", "empty", "2"), cfg
        check_default_menu(vm)


def test_counts_and_limits(board, mfboot_image, test_os, need_mtools, tmp_path):
    text = "[boot]\ndefault = E9\ntimeout = 250\ncolour = red\n[bios]\ndelay_ms = 0\n"
    for i in range(1, 10):
        text += f"[entry E{i}]\npath = /ROBCO/UOS.SYS\ncmdline = n={i}\n"
    text += "[entry E2]\npath = /ROBCO/DIAG.SYS\n[entry TOO_LONG_A_NAME_X]\npath = /A\n"
    with alone(mfboot_image, board, card(tmp_path, os_files(test_os), cfg=text.encode())) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        cfg = one(vm, "config")
        assert (cfg["result"], cfg["entries"], cfg["ignored"], cfg["dup"], cfg["unknown"],
                cfg["bad"], cfg["clamped"], cfg["default_found"]) == \
            ("ok", "8", "1", "1", "1", "1", "1", "0"), cfg
        ent = one(vm, "entries")
        # E9 is past the eighth entry, so the default is the first.
        assert (ent["count"], ent["default"], ent["timeout"]) == ("8", "E1", "99"), ent
        _, rows = dump(vm)
        assert rows[12] == "  8  E8                       /ROBCO/UOS.SYS", rows
        assert rows[15] == "Booting E1 in 99 s. Press any key to stop.", rows
        key(vm, "up")
        vm.send("8")
        vm.wait_for("ev=select index=8", timeout=TIMEOUT)
        key(vm, "select")
        vm.wait_for(OS_READY, timeout=TIMEOUT)
        assert one(vm, "bootinfo", "testos")["cmdline"] == "n=8"
