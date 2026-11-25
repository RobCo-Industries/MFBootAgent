"""The whole road with the real MAINT.SYS: a request, the agent's screen, its
start action, the image with boot_flags bits 1 and 3, its restart, and the
next boot. Also MAINT from the menu, the fixed path, restore users through
the chain, and a button held from power-on into the image.

test_maint_req.py keeps the test OS as a stand-in for MAINT.SYS where it
reads the flags from the image it boots: the test OS prints them. Here the
real image shows them in its handoff marker and on its status row.
"""

import re

import pytest
from harness.pigpio import PiGpio

import mfboot as mf
import maintimg as mt
from maintimg import (USERS_DAT, USERS_DEF, good_files, maint_cfg, maint_image,  # noqa: F401
                      read_card_file)

REQ = "/ROBCO/MAINT.REQ"
BIT1 = 1 << 1
BIT3 = 1 << 3
TIMEOUT = mf.TIMEOUT
OPEN = re.compile(r"@RC stage=bootagent ev=maint_open [^\n]*\n")
SECOND_READY = re.compile(r"(?:@RC stage=bootagent ev=ready\r?\n[\s\S]*?){2}")
PINS = {"up": 5, "down": 6, "left": 17, "right": 22, "select": 26, "back": 27}
# The screen's actions from the top: Start the maintenance image is the fourth.
START_ITEM = 3
STATUS_ROW = 12

NO_MAINT_CFG = b"""[boot]
default = UOS

[entry UOS]
path = /ROBCO/UOS.SYS

[entry DIAG]
path = /ROBCO/DIAG.SYS
"""

USERS_ON = b"\n[maint]\nrestore_users = on\n"
DEFAULT = b"default users\n" * 30
OLD = b"old users, every password lost\n" * 20


def road_card(tmp_path, files, req=None, cfg=mf.WAIT_CFG):
    files = dict(files)
    if req is not None:
        files[REQ] = req
    return mf.card(tmp_path, files, cfg=cfg)


def start_from_the_screen(vm):
    for _ in range(START_ITEM):
        mf.key(vm, "down")
    mf.key(vm, "select")


def image_flags(vm):
    """The real image is up from the agent: its trusted handoff, its adopted
    screen, and boot_flags as its handoff marker prints them."""
    vm.wait_for(mt.READY, timeout=TIMEOUT)
    hand = mt.one(vm, "handoff")
    assert hand["trusted"] == "1", hand
    scr = mt.one(vm, "screen")
    assert scr["path"] == "adopt", scr
    assert mf.one(vm, "load")["subtype"] == "2"
    flags = int(hand["flags"], 16)
    assert int(mf.one(vm, "bootinfo")["flags"], 16) == flags
    return flags


def status_row(vm):
    st = mt.state(vm)
    assert (st["phase"], st["trusted"], st["tool"]) == ("menu", "1", "none"), st
    _, rows = mt.dump(vm)
    return rows[STATUS_ROW]


def request_to_image(vm):
    vm.wait_for(OPEN, timeout=TIMEOUT)
    assert mf.one(vm, "maint_req")["result"] == "found"
    assert mf.one(vm, "maint_open")["why"] == "request"
    st = mf.state(vm)
    assert (st["phase"], st["maint"]) == ("menu", "1"), st
    start_from_the_screen(vm)
    flags = image_flags(vm)
    assert flags & (BIT1 | BIT3) == BIT1 | BIT3, hex(flags)
    c = mf.one(vm, "choose")
    assert (c["name"], c["path"]) == ("MAINT", mf.MAINT), c


def test_request_to_the_real_image(board, mfboot_image, good_files, need_mtools, tmp_path):
    sd = road_card(tmp_path, good_files, req=b"OPER SYSTEM\n")
    with mf.alone(mfboot_image, board, sd) as vm:
        request_to_image(vm)
        assert status_row(vm) == "Started by a request. Choose a tool."
    assert read_card_file(sd, REQ) is None


def test_maint_from_the_menu(board, mfboot_image, good_files, need_mtools, tmp_path):
    with mf.alone(mfboot_image, board, road_card(tmp_path, good_files)) as vm:
        vm.wait_for(mf.READY, timeout=TIMEOUT)
        mf.char(vm, "2")
        mf.key(vm, "select")
        flags = image_flags(vm)
        assert flags & (BIT1 | BIT3) == BIT1, hex(flags)
        assert mf.one(vm, "maint_req")["result"] == "none"
        assert status_row(vm) == "Maintenance asked. Choose a tool."


def fixed_path(vm):
    vm.wait_for(mf.READY, timeout=TIMEOUT)
    mf.char(vm, "m")
    vm.wait_for(OPEN, timeout=TIMEOUT)
    start_from_the_screen(vm)
    flags = image_flags(vm)
    assert flags & (BIT1 | BIT3) == BIT1, hex(flags)
    c = mf.one(vm, "choose")
    assert (c["index"], c["path"]) == ("-1", mf.MAINT), c


def test_fixed_path_without_maint_entry(board, mfboot_image, good_files, need_mtools, tmp_path):
    sd = road_card(tmp_path, good_files, cfg=NO_MAINT_CFG)
    with mf.alone(mfboot_image, board, sd) as vm:
        fixed_path(vm)
        assert mt.one(vm, "config")["result"] == "ok"


def test_fixed_path_without_boot_cfg(board, mfboot_image, good_files, need_mtools, tmp_path):
    sd = road_card(tmp_path, good_files, cfg=None)
    with mf.alone(mfboot_image, board, sd) as vm:
        fixed_path(vm)
        assert mf.one(vm, "config")["result"] == "missing"
        assert mt.one(vm, "config")["result"] == "missing"


def after_restart(vm):
    """Restarts from the image, then the agent's second boot: no request, the
    normal menu. Returns the agent's markers of that boot."""
    mt.command(vm, "tool restart")
    vm.wait_for(SECOND_READY, timeout=TIMEOUT)
    ms = [m for m in vm.markers() if m.get("stage") == "bootagent"]
    boots = [i for i, m in enumerate(ms) if m["ev"] == "boot"]
    assert len(boots) == 2, vm.serial
    second = ms[boots[1]:]
    req = [m for m in second if m["ev"] == "maint_req"]
    assert [(m["result"], m["rc"]) for m in req] == [("none", "0")], req
    assert not [m for m in second if m["ev"] == "maint_open"], vm.serial
    st = mf.state(vm)
    assert (st["phase"], st["maint"]) == ("menu", "0"), st
    return second


def test_restart_gives_a_normal_boot(board, mfboot_image, good_files, need_mtools, tmp_path):
    sd = road_card(tmp_path, good_files, req=b"once\n")
    with mf.alone(mfboot_image, board, sd) as vm:
        request_to_image(vm)
        after_restart(vm)
        assert [m["name"] for m in mt.events(vm, "tool")] == ["restart"]
    assert read_card_file(sd, REQ) is None


def test_restore_users_through_the_chain(board, mfboot_image, good_files, need_mtools, tmp_path):
    """BOOT.CFG turns restore users on, UOS asked for maintenance, the person
    says yes in the real image, then restarts. The card holds the default."""
    if board != "raspi2b":
        pytest.skip("the long road runs on raspi2b; its pieces run on every board")
    files = dict(good_files)
    files[USERS_DEF] = DEFAULT
    files[USERS_DAT] = OLD
    sd = road_card(tmp_path, files, req=b"OPER lost every password\n",
                   cfg=maint_cfg(USERS_ON))
    with mf.alone(mfboot_image, board, sd) as vm:
        request_to_image(vm)
        assert mt.one(vm, "config")["restore_users"] == "on"
        mt.command(vm, "tool restore_users")
        st = mt.state(vm)
        assert (st["phase"], st["tool"]) == ("tool", "restore_users"), st
        mt.char(vm, "y")
        m = mt.wait_result(vm, "restore_users", 1)
        assert (m["result"], m["size"]) == ("ok", str(len(DEFAULT))), m
        after_restart(vm)
    assert read_card_file(sd, USERS_DAT) == DEFAULT
    assert read_card_file(sd, USERS_DEF) == DEFAULT
    assert read_card_file(sd, REQ) is None


def test_select_held_into_the_image_runs_no_tool(board, mfboot_image, good_files, need_mtools,
                                                 tmp_path):
    """SELECT held from power-on, a request: the agent's screen drops it, the
    port starts the image, and the image's own guard queues SELECT as a press
    before its menu. The menu drops it, its release too. The next press runs
    the tool under the bar."""
    sd = road_card(tmp_path, good_files, req=b"held\n")
    high = [p for name, p in PINS.items() if name != "select"]
    with mf.alone(mfboot_image, board, sd, qtest=True, gpio_high=high) as vm:
        io = PiGpio(vm, board, names=PINS)
        request_to_image(vm)
        inp = mt.one(vm, "input")
        assert inp["buttons"] == "active", inp
        assert int(inp["pending"]) >= 1, inp
        st = mt.state(vm)
        assert (st["phase"], st["tool"]) == ("menu", "none"), st
        assert not mt.events(vm, "tool"), vm.serial
        # The release is dropped too. DOWN, pressed after it, shows the
        # guest saw the release: the poll reads every line in one pass.
        io.release("select")
        io.press("down")
        vm.wait_for(re.compile(r"@RC stage=maint ev=select index=2 [^\n]*\n"), timeout=TIMEOUT)
        io.release("down")
        st = mt.state(vm)
        assert (st["phase"], st["sel"], st["tool"]) == ("menu", "2", "none"), st
        assert not mt.events(vm, "tool"), vm.serial
        # A new press acts: the tool under the bar runs.
        io.press("select")
        m = mt.wait_result(vm, "check_images", 1)
        io.release("select")
        assert [t["name"] for t in mt.events(vm, "tool")] == ["check_images"], vm.serial
