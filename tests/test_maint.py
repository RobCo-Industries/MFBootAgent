"""The maintenance screen: how it opens and closes, each action, the verify table,
boot_flags bit 1 and the input backends.

Set ROBCO_UPDATE_GOLDEN=1 to rewrite tests/golden/maint_<board>.png and
tests/golden/verify_<board>.png.
"""

import os
import re
import struct
import time

from mfboot import (DIAG, MAINT, OFF_ABI, OFF_CRC, OFF_SUBTYPE, OS_READY, READY, TIMEOUT, UOS,
                    WAIT_CFG, after, alone, batch, card, char, check_golden, dump, events, flip, key, one,
                    os_checks, os_files, patched, screenshot, state)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BOOT_MAINT = 1 << 1
BOOT_COLD = 1 << 2

OPEN = re.compile(r"@RC stage=bootagent ev=maint_open [^\n]*\n")
CLOSE = re.compile(r"@RC stage=bootagent ev=maint_close\r?\n")
VERIFY_DONE = re.compile(r"@RC stage=bootagent ev=verify_done [^\n]*\n")

TITLE = "MFBOOT AGENT V0.1.0 MAINTENANCE"
SCREEN = [
    TITLE,
    "> Verify entries",
    "  System",
    "  Configuration",
    "  Start the maintenance image",
    "  Reboot",
    "  Back to the boot menu",
    "UP/DOWN: move   ENTER: run   ESC or Q: back to the boot menu",
]
MENU_TITLE = "ROBCO MFBOOT AGENT V0.1.0"


def core_pin(build_dir):
    """The library commit compiled into the image of build_dir: the Makefile
    writes it to build_dir/core_commit at each build."""
    path = os.path.join(build_dir, "core_commit")
    assert os.path.isfile(path), f"{path} is missing: build first, make BUILD={build_dir}"
    with open(path) as f:
        return f.read().strip()


def wait_count(vm, name, n):
    deadline = time.monotonic() + TIMEOUT
    while len(events(vm, name)) < n:
        assert time.monotonic() < deadline, vm.serial
        time.sleep(0.05)


def screen_rows(vm):
    """The 18 rows of the last maintenance screen on the console."""
    _, rows = dump(vm)
    top = len(rows) - 1 - rows[::-1].index(TITLE)
    return rows[top:top + 18]


def open_by_key(vm):
    char(vm, "m")
    vm.wait_for(OPEN, timeout=TIMEOUT)
    # state() waits until the stage took every queued key: drawing is done.
    assert state(vm)["maint"] == "1"


def run_item(vm, index, done=None):
    """Moves the bar to action `index` from the top and runs it."""
    for _ in range(6):
        key(vm, "up")
    for _ in range(index):
        key(vm, "down")
    n = len(events(vm, "maint_action"))
    key(vm, "select")
    wait_count(vm, "maint_action", n + 1)
    if done is not None:
        vm.wait_for(done, timeout=TIMEOUT)
    state(vm)


def check_os_flags(vm, flags):
    vm.wait_for(OS_READY, timeout=TIMEOUT)
    os_checks(vm)
    info = one(vm, "bootinfo", "testos")
    assert int(info["flags"], 16) == flags, info
    assert int(one(vm, "bootinfo")["flags"], 16) == flags


def test_input_backends(board, mfboot_image, test_os, need_mtools, tmp_path):
    """QEMU reads every GPIO line as 0: the active-low buttons look held, and the
    backend disables itself. The console and the port still drive the menu."""
    with alone(mfboot_image, board, card(tmp_path, os_files(test_os))) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        inp = one(vm, "input")
        assert (inp["serial"], inp["mini"], inp["script"], inp["gpio"], inp["buttons"],
                inp["repeat"], inp["live"]) == \
            ("0", "0", "0", "0", "disabled_stuck", "0", "serial,script"), inp
        vm.send("\x1b[B")
        vm.wait_for("ev=select index=2", timeout=TIMEOUT)
        vm.send("\r")
        vm.wait_for(OS_READY, timeout=TIMEOUT)
        assert one(vm, "choose")["index"] == "2"


def test_open_by_key_golden_and_boot(board, mfboot_image, test_os, need_mtools, tmp_path):
    with alone(mfboot_image, board, card(tmp_path, os_files(test_os))) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        open_by_key(vm)
        assert one(vm, "maint_open")["why"] == "key"
        rows = screen_rows(vm)
        assert rows[:8] == SCREEN, rows
        assert rows[8:] == [""] * 10, rows
        # The PL011 shows the same rows.
        lines = vm.lines()
        start = len(lines) - 1 - lines[::-1].index(TITLE)
        assert lines[start:start + 8] == SCREEN, lines
        img = screenshot(vm, tmp_path)
        check_golden(img, f"maint_{board}")
        # The bar moves and stops at the ends.
        key(vm, "up")
        key(vm, "down")
        key(vm, "down")
        state(vm)
        rows = screen_rows(vm)
        assert rows[1:4] == ["  Verify entries", "  System", "> Configuration"], rows
        # Q closes it: the menu shows again below it and the boot goes on.
        char(vm, "q")
        vm.wait_for(CLOSE, timeout=TIMEOUT)
        assert not events(vm, "maint_action")
        st = state(vm)
        assert (st["phase"], st["maint"], st["sel"]) == ("menu", "0", "1"), st
        _, rows = dump(vm)
        top = rows.index(MENU_TITLE)
        assert rows[top + 5] == "> 1  UOS             DEFAULT  /ROBCO/UOS.SYS", rows
        assert TITLE not in rows[top:], rows
        key(vm, "select")
        # Opened by the user: bit 1 goes to the next stage, with the cold flag.
        check_os_flags(vm, BOOT_COLD | BOOT_MAINT)


def test_open_by_port_countdown_stays_stopped(board, mfboot_image, test_os, need_mtools,
                                              tmp_path):
    cfg = WAIT_CFG.replace(b"default = UOS\n", b"default = UOS\ntimeout = 2\n")
    with alone(mfboot_image, board, card(tmp_path, os_files(test_os), cfg=cfg)) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        assert [x.status for x in batch(vm, ["maint"])] == ["ok"]
        vm.wait_for(OPEN, timeout=TIMEOUT)
        assert one(vm, "maint_open")["why"] == "port"
        assert events(vm, "countdown")[0]["state"] == "start"
        vm.wait_for("ev=countdown state=stopped", timeout=TIMEOUT)
        # A second request while it shows is refused.
        assert vm.script("maint", timeout=TIMEOUT).status == "err already open"
        # BACK closes it.
        key(vm, "back")
        vm.wait_for(CLOSE, timeout=TIMEOUT)
        time.sleep(3)
        assert not events(vm, "choose"), vm.serial
        st = state(vm)
        assert (st["phase"], st["counting"], st["maint"]) == ("menu", "0", "0"), st
        _, rows = dump(vm)
        assert "Countdown stopped. Press ENTER to boot." in rows, rows
        key(vm, "down")
        key(vm, "select")
        check_os_flags(vm, BOOT_COLD | BOOT_MAINT)
        assert one(vm, "choose")["via"] == "key"


def test_port_select_then_maint_boots(board, mfboot_image, test_os, need_mtools, tmp_path):
    """SELECT came first: it boots, and the maint request behind it is dropped."""
    with alone(mfboot_image, board, card(tmp_path, os_files(test_os))) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        replies = batch(vm, ["key select", "maint"])
        assert [x.status for x in replies] == ["ok", "ok"], replies
        check_os_flags(vm, BOOT_COLD)
        assert not events(vm, "maint_open"), vm.serial
        assert one(vm, "maint_drop")["why"] == "boot"


def test_port_select_then_maint_failed_load(board, mfboot_image, test_os, need_mtools,
                                            tmp_path):
    """The default entry fails to load: the dropped request never opens the screen later."""
    files = os_files(test_os)
    del files[UOS]
    with alone(mfboot_image, board, card(tmp_path, files)) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        replies = batch(vm, ["key select", "maint"])
        assert [x.status for x in replies] == ["ok", "ok"], replies
        vm.wait_for("ev=error code=nofile", timeout=TIMEOUT)
        st = state(vm)
        assert (st["phase"], st["maint"]) == ("menu", "0"), st
        key(vm, "down")
        assert state(vm)["sel"] == "2"
        assert not events(vm, "maint_open"), vm.serial
        assert one(vm, "maint_drop")["why"] == "boot"
        # A new request opens it now.
        assert vm.script("maint", timeout=TIMEOUT).status == "ok"
        vm.wait_for(OPEN, timeout=TIMEOUT)
        assert one(vm, "maint_open")["why"] == "port"


def test_port_maint_then_select_acts_inside(board, mfboot_image, test_os, need_mtools,
                                            tmp_path):
    """maint came first: the screen opens and SELECT runs its first action."""
    with alone(mfboot_image, board, card(tmp_path, os_files(test_os))) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        replies = batch(vm, ["maint", "key select"])
        assert [x.status for x in replies] == ["ok", "ok"], replies
        vm.wait_for(VERIFY_DONE, timeout=TIMEOUT)
        st = state(vm)
        assert (st["phase"], st["maint"]) == ("menu", "1"), st
        assert one(vm, "maint_open")["why"] == "port"
        assert one(vm, "maint_action")["name"] == "verify"
        assert not events(vm, "choose") and not events(vm, "maint_drop"), vm.serial
        char(vm, "q")
        vm.wait_for(CLOSE, timeout=TIMEOUT)
        key(vm, "select")
        check_os_flags(vm, BOOT_COLD | BOOT_MAINT)


def test_state_answers_between_queued_keys(board, mfboot_image, test_os, need_mtools,
                                           tmp_path):
    """The port answers a line before the stage takes the keys queued ahead
    of it. pending= says so, and state() waits for pending=0."""
    with alone(mfboot_image, board, card(tmp_path, os_files(test_os))) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        open_by_key(vm)
        # One write: the stage reads all three lines before it takes a key.
        replies = batch(vm, ["key down", "key down", "state"])
        assert [x.status for x in replies] == ["ok", "ok", "ok"], replies
        assert len(replies[2].lines) == 1, replies
        early = replies[2].lines[0]
        assert re.search(r" pending=[1-9]", early), early
        assert state(vm)["pending"] == "0"
        assert screen_rows(vm)[1:4] == ["  Verify entries", "  System", "> Configuration"]


def test_button_back_opens_and_esc_does_not(board, mfboot_image, test_os, need_mtools,
                                            tmp_path):
    cfg = WAIT_CFG.replace(b"default = UOS\n", b"default = UOS\ntimeout = 5\n")
    with alone(mfboot_image, board, card(tmp_path, os_files(test_os), cfg=cfg)) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        # ESC on the console stops the countdown and opens nothing.
        vm.send("\x1b")
        vm.wait_for("ev=countdown state=stopped", timeout=TIMEOUT)
        time.sleep(0.3)
        state(vm)
        assert not events(vm, "maint_open"), vm.serial
        # BACK from the port's injected buttons opens it, ESC on the console closes it.
        key(vm, "back")
        vm.wait_for(OPEN, timeout=TIMEOUT)
        assert one(vm, "maint_open")["why"] == "button"
        vm.send("\x1b")
        vm.wait_for(CLOSE, timeout=TIMEOUT)
        # LEFT and RIGHT do nothing in either place.
        key(vm, "left")
        key(vm, "right")
        char(vm, "M")
        wait_count(vm, "maint_open", 2)
        key(vm, "left")
        key(vm, "right")
        assert state(vm)["maint"] == "1"
        assert screen_rows(vm)[1] == "> Verify entries"
        assert not events(vm, "maint_action")


def test_system_and_config_pages(board, mfboot_image, build_dir, test_os, need_mtools, tmp_path):
    cfg = WAIT_CFG.replace(b"default = UOS\n",
                           b"default = MAINT\ntimeout = 150\n[entry MAINT]\npath = /X\n")
    with alone(mfboot_image, board, card(tmp_path, os_files(test_os), cfg=cfg)) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        open_by_key(vm)
        run_item(vm, 1)
        assert events(vm, "maint_action")[-1]["name"] == "system"
        rows = screen_rows(vm)
        page = rows[8:]
        assert page[0] == "SYSTEM", rows
        assert page[1].startswith("Board:        Raspberry Pi"), rows
        assert re.fullmatch(r"Memory:       \d+ MiB", page[2]), rows
        assert page[3].startswith("Boot volume:  SD card, block 2048, "), rows
        assert page[4:9] == [
            "Boot info:    none trusted, this stage cold-booted",
            "Boot flags:   none received",
            "Version:      0.1.0",
            f"RobCo-Core:   {core_pin(build_dir)}",
            "Screen:       started by this stage",
        ], rows
        assert page[9] == "", rows
        run_item(vm, 2)
        assert events(vm, "maint_action")[-1]["name"] == "config"
        c = one(vm, "config")
        page = screen_rows(vm)[8:]
        assert page == [
            "CONFIGURATION",
            "File:         /ROBCO/BOOT.CFG, in use",
            "Default:      MAINT",
            "Timeout:      99 s",
            f"Entries:      {c['entries']}",
            f"Ignored:      {c['ignored']}",
            f"Duplicate:    {c['dup']}",
            f"Bad:          {c['bad']}",
            f"Clamped:      {c['clamped']}",
            "",
        ], page
        assert (c["dup"], c["clamped"]) == ("1", "1"), c
        # The last action returns to the menu.
        run_item(vm, 5, CLOSE)
        assert events(vm, "maint_action")[-1]["name"] == "back"
        assert [m["name"] for m in events(vm, "maint_action")] == ["system", "config", "back"]
        assert state(vm)["maint"] == "0"
        # Neither page touched the countdown: 99 s stopped by the M key.
        assert state(vm)["counting"] == "0"


def verify_cfg():
    return b"""[boot]
default = UOS

[entry UOS]
path = /ROBCO/UOS.SYS

[entry MAINT]
path = /ROBCO/MAINT.SYS

[entry DIAG]
path = /ROBCO/DIAG.SYS

[entry AGENT]
path = /ROBCO/AGENT.SYS

[entry OLDABI]
path = /ROBCO/OLDABI.SYS
"""


VERIFY_ROWS = [
    "#  ENTRY           RESULT      PATH",
    "1  UOS             OK          /ROBCO/UOS.SYS",
    "2  MAINT           E08 CRC     /ROBCO/MAINT.SYS",
    "3  DIAG            E01 NOFILE  /ROBCO/DIAG.SYS",
    "4  AGENT           E06 BOUNDS  /ROBCO/AGENT.SYS",
    "5  OLDABI          E07 ABI     /ROBCO/OLDABI.SYS",
    "5 entries checked: 1 good, 4 failed.",
]
VERIFY_RESULTS = ["ok", "crc", "nofile", "bounds", "abi"]


def verify_files(test_os):
    good = test_os["uos"]
    return {
        UOS: good,
        MAINT: flip(test_os["maint"]),
        "/ROBCO/AGENT.SYS": patched(good, OFF_SUBTYPE, "<I", 4),
        "/ROBCO/OLDABI.SYS": patched(good, OFF_ABI, "<I", 3 << 16),
    }


def test_verify_entries(board, mfboot_image, test_os, need_mtools, tmp_path):
    sd = card(tmp_path, verify_files(test_os), cfg=verify_cfg())
    with alone(mfboot_image, board, sd) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        open_by_key(vm)
        run_item(vm, 0, VERIFY_DONE)
        assert events(vm, "maint_action")[-1]["name"] == "verify"
        found = events(vm, "verify_entry")
        assert [(m["index"], m["name"], m["result"]) for m in found] == \
            [(str(i + 1), n, r) for i, (n, r) in
             enumerate(zip(["UOS", "MAINT", "DIAG", "AGENT", "OLDABI"], VERIFY_RESULTS))]
        assert [m["path"] for m in found] == [UOS, MAINT, DIAG, "/ROBCO/AGENT.SYS",
                                              "/ROBCO/OLDABI.SYS"]
        done = one(vm, "verify_done")
        assert (done["result"], done["entries"], done["good"], done["failed"]) == \
            ("ok", "5", "1", "4"), done
        rows = screen_rows(vm)
        assert rows[8:8 + len(VERIFY_ROWS)] == VERIFY_ROWS, rows
        assert rows[8 + len(VERIFY_ROWS):] == ["", "", ""], rows
        # The PL011 shows the same rows, with a marker after each entry.
        lines = [line for line in vm.lines() if not line.startswith("@RC ")]
        start = len(lines) - 1 - lines[::-1].index(VERIFY_ROWS[0])
        assert lines[start:start + len(VERIFY_ROWS)] == VERIFY_ROWS, lines
        # No load error and no jump: verify only checks.
        assert not events(vm, "error") and not events(vm, "jump") and not events(vm, "load")
        img = screenshot(vm, tmp_path)
        check_golden(img, f"verify_{board}")
        # A second run gives the same; then the good entry boots from the menu.
        run_item(vm, 0)
        wait_count(vm, "verify_done", 2)
        assert screen_rows(vm)[8:8 + len(VERIFY_ROWS)] == VERIFY_ROWS
        char(vm, "q")
        vm.wait_for(CLOSE, timeout=TIMEOUT)
        key(vm, "select")
        check_os_flags(vm, BOOT_COLD | BOOT_MAINT)
        assert one(vm, "load")["path"] == UOS


def test_verify_without_card(board, mfboot_image, test_os, need_mtools, tmp_path):
    with alone(mfboot_image, board, card(tmp_path, os_files(test_os))) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        open_by_key(vm)
        vm.qmp.hmp("eject -f sd0")
        run_item(vm, 0, VERIFY_DONE)
        done = one(vm, "verify_done")
        assert (done["result"], done["what"], done["good"], done["failed"]) == \
            ("io", "sd", "0", "0"), done
        assert not events(vm, "verify_entry")
        rows = screen_rows(vm)
        assert rows[8:] == ["ERROR E02 IO: SD card or FAT volume not usable"] + [""] * 9, rows
        # The menu still answers, and a boot now fails with the same code.
        char(vm, "q")
        vm.wait_for(CLOSE, timeout=TIMEOUT)
        key(vm, "select")
        vm.wait_for("ev=error code=io", timeout=TIMEOUT)
        assert state(vm)["phase"] == "menu"


def test_reboot_action(board, mfboot_image, test_os, need_mtools, tmp_path):
    with alone(mfboot_image, board, card(tmp_path, os_files(test_os))) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        open_by_key(vm)
        for _ in range(4):
            key(vm, "down")
        key(vm, "select")
        vm.wait_for("ev=reboot source=screen", timeout=TIMEOUT)
        assert events(vm, "maint_action")[-1]["name"] == "reboot"
        vm.wait_for(re.compile(r"(@RC stage=bootagent ev=boot [^\n]*\n[\s\S]*){2}"),
                    timeout=TIMEOUT)


def test_refused_outside_the_menu(board, mfboot_image):
    with alone(mfboot_image, board) as vm:
        vm.wait_for("ev=idle", timeout=TIMEOUT)
        assert vm.script("maint", timeout=TIMEOUT).status == "err not in the menu"
        assert not events(vm, "maint_open")


def test_flag_opens_at_start(board, chain1_maint_image, mfboot, test_os, need_mtools,
                             tmp_path):
    """A first stage hands on bit 1: the screen opens in place of the countdown."""
    files = os_files(test_os)
    files["/ROBCO/MFBOOT.SYS"] = mfboot
    cfg = WAIT_CFG.replace(b"default = UOS\n", b"default = UOS\ntimeout = 1\n")
    with after(chain1_maint_image, board, card(tmp_path, files, cfg=cfg)) as vm:
        vm.wait_for(OPEN, timeout=TIMEOUT)
        assert one(vm, "handoff")["trusted"] == "1"
        assert one(vm, "maint_open")["why"] == "flag"
        assert not events(vm, "countdown"), vm.serial
        state(vm)
        assert screen_rows(vm)[:8] == SCREEN
        run_item(vm, 1)
        page = screen_rows(vm)[8:]
        assert page[4:6] == ["Boot info:    trusted, from the previous stage",
                             f"Boot flags:   0x{BOOT_COLD | BOOT_MAINT:08x} received"], page
        assert page[8] == "Screen:       adopted from the previous stage", page
        char(vm, "q")
        vm.wait_for(CLOSE, timeout=TIMEOUT)
        time.sleep(2)
        assert not events(vm, "choose") and not events(vm, "countdown"), vm.serial
        st = state(vm)
        assert (st["counting"], st["maint"]) == ("0", "0"), st
        key(vm, "select")
        # Kept as received: chain1's cold flag and bit 1.
        check_os_flags(vm, BOOT_COLD | BOOT_MAINT)


# Card swaps. QEMU changes the medium in the SD slot through QMP.
def eject_card(vm):
    vm.qmp.hmp("eject -f sd0")


def insert_card(vm, sd):
    """Puts the card image `sd` in the slot, in place of any card there."""
    vm.qmp.execute("blockdev-change-medium", device="sd0", filename=str(sd), format="raw")


def verify(vm, n):
    """Runs Verify entries, the n-th run since the boot, and returns its verify_done."""
    run_item(vm, 0)
    wait_count(vm, "verify_done", n)
    return events(vm, "verify_done")[-1]


def other_files(test_os):
    """Card B: other images on the same paths, AGENT.SYS gone, and a new file."""
    return {
        UOS: test_os["diag"],
        MAINT: test_os["maint"],
        DIAG: test_os["diag"],
        "/ROBCO/OLDABI.SYS": test_os["uos"],
        "/ROBCO/NEW.SYS": test_os["uos"],
    }


OTHER_CFG = b"""[boot]
default = NEW

[entry NEW]
path = /ROBCO/NEW.SYS
"""

# The list read at start from card A, checked against card B.
OTHER_ROWS = [
    "#  ENTRY           RESULT      PATH",
    "1  UOS             OK          /ROBCO/UOS.SYS",
    "2  MAINT           OK          /ROBCO/MAINT.SYS",
    "3  DIAG            OK          /ROBCO/DIAG.SYS",
    "4  AGENT           E01 NOFILE  /ROBCO/AGENT.SYS",
    "5  OLDABI          OK          /ROBCO/OLDABI.SYS",
    "5 entries checked: 4 good, 1 failed.",
]


def check_loaded(vm, image):
    """The boot loaded `image`: the CRC in its header, on the load marker."""
    vm.wait_for(OS_READY, timeout=TIMEOUT)
    crc = struct.unpack_from("<I", image, OFF_CRC)[0]
    assert int(one(vm, "load")["crc"], 16) == crc, one(vm, "load")


def test_verify_after_reinsert(board, mfboot_image, test_os, need_mtools, tmp_path):
    """Eject, verify, put the same card back: the next verify gives the table."""
    sd = card(tmp_path, verify_files(test_os), cfg=verify_cfg())
    with alone(mfboot_image, board, sd) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        open_by_key(vm)
        eject_card(vm)
        done = verify(vm, 1)
        assert (done["result"], done["what"]) == ("io", "sd"), done
        insert_card(vm, sd)
        done = verify(vm, 2)
        assert (done["result"], done["good"], done["failed"]) == ("ok", "1", "4"), vm.serial
        assert screen_rows(vm)[8:8 + len(VERIFY_ROWS)] == VERIFY_ROWS
        # Twice in a row without a card, then back again.
        eject_card(vm)
        assert verify(vm, 3)["result"] == "io"
        assert verify(vm, 4)["result"] == "io"
        insert_card(vm, sd)
        assert verify(vm, 5)["result"] == "ok", vm.serial
        char(vm, "q")
        vm.wait_for(CLOSE, timeout=TIMEOUT)
        key(vm, "select")
        check_loaded(vm, test_os["uos"])


def test_verify_after_swap(board, mfboot_image, test_os, need_mtools, tmp_path):
    """Swap to card B: verify checks the list read at start against card B, and a
    boot loads card B's file."""
    sd = card(tmp_path, verify_files(test_os), cfg=verify_cfg())
    other = card(tmp_path, other_files(test_os), cfg=OTHER_CFG, name="other.img")
    with alone(mfboot_image, board, sd) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        open_by_key(vm)
        assert verify(vm, 1)["good"] == "1"
        eject_card(vm)
        insert_card(vm, other)
        done = verify(vm, 2)
        assert (done["result"], done["good"], done["failed"]) == ("ok", "4", "1"), vm.serial
        assert screen_rows(vm)[8:8 + len(OTHER_ROWS)] == OTHER_ROWS
        char(vm, "q")
        vm.wait_for(CLOSE, timeout=TIMEOUT)
        key(vm, "select")
        check_loaded(vm, test_os["diag"])


def test_boot_after_swap(board, mfboot_image, test_os, need_mtools, tmp_path):
    """Swap the card with no verify: the boot mounts card B and loads its file."""
    sd = card(tmp_path, os_files(test_os))
    other = card(tmp_path, other_files(test_os), cfg=OTHER_CFG, name="other.img")
    with alone(mfboot_image, board, sd) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        open_by_key(vm)
        eject_card(vm)
        insert_card(vm, other)
        char(vm, "q")
        vm.wait_for(CLOSE, timeout=TIMEOUT)
        key(vm, "select")
        check_loaded(vm, test_os["diag"])


def test_boot_after_failed_verify(board, mfboot_image, test_os, need_mtools, tmp_path):
    """Verify with no card, then card B: the boot mounts card B and loads its file."""
    sd = card(tmp_path, os_files(test_os))
    other = card(tmp_path, other_files(test_os), cfg=OTHER_CFG, name="other.img")
    with alone(mfboot_image, board, sd) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        open_by_key(vm)
        eject_card(vm)
        assert verify(vm, 1)["result"] == "io"
        insert_card(vm, other)
        char(vm, "q")
        vm.wait_for(CLOSE, timeout=TIMEOUT)
        key(vm, "select")
        check_loaded(vm, test_os["diag"])


def system_page(vm):
    run_item(vm, 1)
    assert events(vm, "maint_action")[-1]["name"] == "system"
    return screen_rows(vm)[8:]


def test_system_after_unmountable_card(board, mfboot_image, test_os, need_mtools, tmp_path):
    """A card with no FAT volume: System says not mounted, names the failed step and
    keeps the last good volume. The good card back: System shows it mounted."""
    sd = card(tmp_path, os_files(test_os))
    blank = tmp_path / "blank.img"
    blank.write_bytes(bytes(16 << 20))
    with alone(mfboot_image, board, sd) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        mount = one(vm, "mount")
        good = f"SD card, block {mount['lba']}, {mount['blocks']} blocks"
        open_by_key(vm)
        assert system_page(vm)[3] == f"Boot volume:  {good}"
        eject_card(vm)
        insert_card(vm, blank)
        done = verify(vm, 1)
        assert (done["result"], done["what"]) == ("io", "fat"), done
        page = system_page(vm)
        assert page[3] == f"Boot volume:  not mounted, step fat failed, code {done['rc']}", page
        assert page[4] == f"Last good:    {good}", page
        assert page[5].startswith("Boot info:    "), page
        assert page[9] == "Screen:       started by this stage", page
        eject_card(vm)
        insert_card(vm, sd)
        assert verify(vm, 2)["result"] == "ok", vm.serial
        page = system_page(vm)
        assert page[3] == f"Boot volume:  {good}", page
        assert page[4].startswith("Boot info:    "), page
