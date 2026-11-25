"""Set clock: no chip, typed digits and a yes, buttons and a no, an invalid date, a cancel."""

import re

from maintimg import (RTC, alone, char, command, dump, events, good_card, good_files,  # noqa: F401
                      key, maint_image, run, start, state, wait_result)

ISO = re.compile(r"20\d\d-\d\d-\d\dT\d\d:\d\d:\d\d")


def test_no_clock(board, maint_image, good_card):
    with alone(maint_image, board, sd=good_card) as vm:
        m = run(vm, "set_clock")
        assert (m["result"], m["addr"]) == ("absent", "0x68"), m
        assert "writes" not in m, m
        state(vm)
        _, rows = dump(vm)
        assert rows[14] == "No clock chip at 0x68. Nothing to set.", rows
        assert rows[12] == "Set clock: ABSENT. No clock chip at 0x68.", rows


def test_typed_and_yes(board, maint_image, good_card):
    with alone(maint_image, board, sd=good_card, devices=RTC) as vm:
        start(vm, "set_clock")
        now = events(vm, "clock_form")[0]["now"]
        assert ISO.fullmatch(now), now
        command(vm, "text 20300615120000")
        state(vm)
        _, rows = dump(vm)
        assert rows[18] == "New time:  2030-06-15 12:00:[00]", rows
        key(vm, "select")
        state(vm)
        _, rows = dump(vm)
        assert rows[20] == ("Write 2030-06-15T12:00:00 to the clock chip? ENTER or Y: yes. "
                            "Other keys: no."), rows
        char(vm, "y")
        m = wait_result(vm, "set_clock", 1)
        assert (m["result"], m["chip"], m["set"]) == ("ok", "ds1307", "2030-06-15T12:00:00"), m
        assert re.fullmatch(r"2030-06-15T12:00:0[0-2]", m["read"]), m
        state(vm)
        _, rows = dump(vm)
        assert rows[21] == f"Wrote 2030-06-15T12:00:00, read back {m['read']}.", rows
        # The chip keeps the new time: the next run starts from it.
        start(vm, "set_clock")
        assert events(vm, "clock_form")[1]["now"].startswith("2030-06-15T12:0"), vm.serial
        key(vm, "back")
        assert wait_result(vm, "set_clock", 2)["result"] == "refused"


def test_buttons_and_no(board, maint_image, good_card):
    with alone(maint_image, board, sd=good_card, devices=RTC) as vm:
        start(vm, "set_clock")
        before = events(vm, "clock_form")[0]["now"]
        command(vm, "text 2040")
        key(vm, "up")       # month 01 to 02, or the chip's month plus one
        key(vm, "right")
        key(vm, "left")
        key(vm, "left")     # back on the year
        key(vm, "down")
        state(vm)
        _, rows = dump(vm)
        assert rows[18].startswith("New time:  [2039]-"), rows
        key(vm, "select")
        key(vm, "back")     # ESC at the question is a no
        m = wait_result(vm, "set_clock", 1)
        assert (m["result"], m["why"]) == ("refused", "no"), m
        assert "set" not in m, m
        start(vm, "set_clock")
        after = events(vm, "clock_form")[1]["now"]
        assert not after.startswith("2039"), (before, after)
        key(vm, "back")
        assert wait_result(vm, "set_clock", 2)["why"] == "cancel"


def test_invalid_date_is_refused(board, maint_image, good_card):
    with alone(maint_image, board, sd=good_card, devices=RTC) as vm:
        start(vm, "set_clock")
        command(vm, "text 20270229101010")
        key(vm, "select")
        state(vm)
        assert events(vm, "clock_invalid")[0]["value"] == "2027-02-29T10:10:10", vm.serial
        assert events(vm, "ask") == [], vm.serial
        _, rows = dump(vm)
        assert rows[20] == "Not a valid date and time. Change it, or ESC.", rows
        st = state(vm)
        assert (st["phase"], st["tool"]) == ("tool", "set_clock"), st
        # The year 2100 is past the chip's range.
        for _ in range(5):
            key(vm, "left")
        command(vm, "text 21000101000000")
        key(vm, "select")
        state(vm)
        assert events(vm, "clock_invalid")[1]["value"] == "2100-01-01T00:00:00", vm.serial
        vm.send("\x03")     # Ctrl-C on the serial console cancels
        m = wait_result(vm, "set_clock", 1)
        assert (m["result"], m["why"]) == ("refused", "cancel"), m
