"""MFBootAgent after a first stage: RobCo-Core's examples/chain1, and RETROS-BIOS when
RETROS_IMG is set. The first stage loads /ROBCO/MFBOOT.SYS from the card."""

import re

import pytest

from mfboot import (MACH, OS_READY, READY, STAGE_BIOS, TIMEOUT, UOS, WAIT_CFG, WAIT_N1, after,
                    card, dump, events, key, one, os_checks, os_files, render_rows,
                    same_pixels, screenshot, text_rows)

COLS, ROWS = 80, 30
MENU_ROWS = 13  # the menu with three entries
TITLE = "ROBCO MFBOOT AGENT V0.1.0"

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
        # chain1 hands over an amber console. Both adopt calls succeed.
        assert (screen["kept"], screen["path"], screen["adopt_fb"], screen["adopt_console"],
                screen["scheme"], screen["unknown"], screen["grid"]) == \
            ("1", "adopt", "0", "0", "1", str(COLS * ROWS), "ok"), screen
        # chain1's text stays as pixels above the menu, after a blank row.
        img = screenshot(vm, tmp_path)
        assert same_pixels(text_rows(img, 0, 2), render_rows(["chain1", ""], 1)), vm.serial
        # The console does not know chain1's cells; it knows every cell of the menu.
        _, rows = dump(vm)
        assert rows[:3] == ["#" * COLS, "#" * COLS, TITLE], rows
        assert rows[7] == "> 1  UOS             DEFAULT  /ROBCO/UOS.SYS", rows
        assert all("#" not in r for r in rows[2:2 + MENU_ROWS]), rows
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


def test_after_chain1_without_fb(board, chain1_nofb_image, mfboot, test_os, need_mtools,
                                 tmp_path):
    """A trusted handoff with caps bit 0 clear: no adopt call, a fresh screen."""
    with after(chain1_nofb_image, board, chain_card(tmp_path, mfboot, test_os)) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        assert one(vm, "handoff")["trusted"] == "1"
        screen = one(vm, "screen")
        assert (screen["fb"], screen["console"], screen["kept"], screen["path"],
                screen["adopt_fb"], screen["adopt_console"], screen["grid"]) == \
            ("0", "0", "0", "init", "none", "none", "none"), screen
        _, rows = dump(vm)
        assert rows[0] == TITLE, rows


# The smallest grid the menu fits: 79 characters and a blank last column,
# and 18 rows for the menu with eight entries.
MIN_COLS, MIN_ROWS = 80, 18

# Eight entries: the tallest menu, MIN_ROWS rows.
EIGHT_CFG = b"[boot]\ndefault = E1\n" + b"".join(
    b"\n[entry E%d]\npath = /ROBCO/UOS.SYS\n" % i for i in range(1, 9))


def menu_rows_ok(rows, top, count):
    """The menu text in the console dump from row `top`, every cell known."""
    assert rows[top] == TITLE, rows
    for r in rows[top:top + 5 + count + 5]:
        assert "#" not in r, rows
    return rows[top + 5:top + 5 + count]


@pytest.mark.parametrize("cols,grid_rows,why", [(MIN_COLS, MIN_ROWS - 1, "rows"),
                                                (MIN_COLS - 1, ROWS, "cols")])
def test_small_grid_falls_back(board, chain1_grid_image, mfboot, test_os, need_mtools,
                               tmp_path, cols, grid_rows, why):
    """A first stage hands over a valid grid too small for the menu: a fresh screen."""
    image = chain1_grid_image(cols, grid_rows)
    with after(image, board, chain_card(tmp_path, mfboot, test_os)) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        assert one(vm, "grid", "chain1")["rc"] == "0", vm.serial
        assert one(vm, "handoff")["trusted"] == "1"
        screen = one(vm, "screen")
        assert (screen["fb"], screen["console"], screen["kept"], screen["path"],
                screen["adopt_fb"], screen["adopt_console"], screen["grid"],
                screen["unknown"]) == ("0", "0", "0", "init", "0", "0", why, "0"), screen
        # The fresh 80 by 30 console: the menu from row 0, every cell known.
        _, rows = dump(vm)
        assert len(rows) == ROWS, rows
        entries = menu_rows_ok(rows, 0, 3)
        assert entries[0] == "> 1  UOS             DEFAULT  /ROBCO/UOS.SYS", rows
        assert entries[2] == "  3  DIAG                     /ROBCO/DIAG.SYS", rows
        key(vm, "select")
        vm.wait_for(OS_READY, timeout=TIMEOUT)
        os_checks(vm)
        assert one(vm, "choose")["name"] == "UOS"


def test_minimum_grid_adopted(board, chain1_grid_image, mfboot, test_os, need_mtools,
                              tmp_path):
    """A grid exactly the minimum, with eight entries: adopted, and the menu fills it."""
    image = chain1_grid_image(MIN_COLS, MIN_ROWS)
    with after(image, board, chain_card(tmp_path, mfboot, test_os, cfg=EIGHT_CFG)) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        screen = one(vm, "screen")
        assert (screen["kept"], screen["path"], screen["adopt_console"], screen["grid"],
                screen["unknown"]) == \
            ("1", "adopt", "0", "ok", str(MIN_COLS * MIN_ROWS)), screen
        # chain1's line and the blank row scrolled off: the menu takes every row.
        _, rows = dump(vm)
        assert len(rows) == MIN_ROWS, rows
        entries = menu_rows_ok(rows, 0, 8)
        assert entries[0].startswith("> 1  E1 ") and entries[0].endswith("/ROBCO/UOS.SYS"), rows
        assert entries[7].startswith("  8  E8 "), rows
        assert rows[MIN_ROWS - 1] == "", rows
        key(vm, "select")
        vm.wait_for(OS_READY, timeout=TIMEOUT)
        assert one(vm, "choose")["name"] == "E1"
        # The test OS expects 80 by 30; the stage hands on the 80 by 18 grid it adopted,
        # with the cursor on the last row after the menu scrolled up one more row.
        checks = {m["name"]: m for m in events(vm, "check", "testos")}
        assert [n for n, m in checks.items() if m["result"] != "pass"] == ["console"], vm.serial
        info = one(vm, "bootinfo", "testos")
        assert (info["cx"], info["cy"]) == ("0", str(MIN_ROWS - 1)), info


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
        assert (screen["kept"], screen["path"], screen["scheme"]) == ("1", "adopt", "0"), screen
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


BIOS_WAIT_CFG = WAIT_CFG + b"\n[bios]\ndelay_ms = 0\nbeep = off\n"


def test_whole_chain_keeps_bios_text(board, retros_image, chain2_wait_image, mfboot, test_os,
                                     need_mtools, tmp_path):
    """The BIOS text above the menu is the BIOS screen at the handoff, pixel for pixel."""
    # The BIOS screen at the handoff: chain2 adopts it, writes nothing and waits.
    with open(chain2_wait_image, "rb") as f:
        waiter = f.read()
    (tmp_path / "a").mkdir()
    sd = chain_card(tmp_path / "a", waiter, test_os, cfg=BIOS_WAIT_CFG)
    with after(retros_image, board, sd) as vm:
        vm.wait_for(WAIT_N1, timeout=TIMEOUT)
        rb = one(vm, "rebuild", "chain2")
        before = screenshot(vm, tmp_path, "before")
    assert one(vm, "video", "chain2")["path"] == "adopt", vm.serial
    cy = int(rb["cy"])
    assert 0 < cy < ROWS, rb
    # The menu starts after a blank row, or scrolls the screen up to fit.
    top = min(cy + 1, ROWS - MENU_ROWS)
    shift = cy + 1 - top

    (tmp_path / "b").mkdir()
    sd = chain_card(tmp_path / "b", mfboot, test_os, cfg=BIOS_WAIT_CFG)
    with after(retros_image, board, sd) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        img = screenshot(vm, tmp_path, "menu")
        _, rows = dump(vm)
    assert one(vm, "screen")["path"] == "adopt", vm.serial
    assert rows[top] == TITLE, rows
    # Unknown cells above the menu, known cells in it.
    assert all(set(r) == {"#"} for r in rows[:top]), rows
    assert all("#" not in r for r in rows[top:top + MENU_ROWS]), rows
    # The BIOS rows that name the image it loaded differ between the two
    # runs: compare them with their text from the BIOS serial output.
    loaded = re.findall(r"^(Loaded MFBOOT: [^\r\n]*|Starting MFBOOT at [^\r\n]*)\r?$",
                        vm.serial, re.M)
    assert len(loaded) == 2, vm.serial
    same = top - 3
    checks = [("same", text_rows(img, 0, same), text_rows(before, shift, same)),
              ("loaded", text_rows(img, same, 3), render_rows(loaded + [""], 0))]
    for name, got, want in checks:
        if not same_pixels(got, want):
            got.save(tmp_path / f"{name}.got.png")
            want.save(tmp_path / f"{name}.want.png")
            raise AssertionError(f"BIOS text differs above the menu ({name}), see {tmp_path}")
