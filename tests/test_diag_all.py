"""`diag all`: every test that needs no person, its counts, and an abort."""

import re
import time

from diag import (RTC, TESTS, TIMEOUT, X0, X1, alone, command, diag_image, dump,  # noqa: F401
                  events, good_card, key, run_all, skip_if_timer_starved, timer_starved)

# The whole run on a busy host stays under this.
LIMIT_S = 30


def results(vm):
    return [(m["name"], m["result"]) for m in events(vm, "diag")]


def test_all_with_every_device(board, diag_image, good_card):
    with alone(diag_image, board, sd=good_card, devices=RTC + X0 + X1) as vm:
        t0 = time.monotonic()
        done = run_all(vm)
        assert time.monotonic() - t0 < LIMIT_S
        skip_if_timer_starved(vm, [(t, "skipped" if t == "input" else "ok") for t in TESTS])
        assert (done["ok"], done["fail"], done["absent"], done["skipped"]) == \
            ("8", "0", "0", "1"), vm.serial
        assert results(vm) == [(t, "skipped" if t == "input" else "ok") for t in TESTS]
        why = {m["name"]: m.get("why") for m in events(vm, "diag")}
        assert why["input"] == "interactive"
        mem = [m for m in events(vm, "diag") if m["name"] == "mem"][0]
        # "Run all" tests the first 16 MiB.
        assert mem["bytes"] == str(16 << 20), mem
        # Video showed each pattern for a short time, with no key.
        assert len(events(vm, "video_pattern")) == 8
        _, rows = dump(vm)
        assert rows[17] == "Run all: 8 OK, 0 FAILED, 0 ABSENT, 1 SKIPPED.", rows
        assert rows[8].startswith("  5  INPUT     Live view of every key       SKIPPED  "), rows
        assert rows[13].startswith("  A  RUN ALL   Every test without a person  OK       8 ok"), rows


def test_all_on_a_bare_board(board, diag_image):
    with alone(diag_image, board) as vm:
        done = run_all(vm)
        bare = {"sd": "absent", "rtc": "absent", "expander": "absent", "input": "skipped"}
        skip_if_timer_starved(vm, [(t, bare.get(t, "ok")) for t in TESTS])
        assert (done["ok"], done["fail"], done["absent"], done["skipped"]) == \
            ("5", "0", "3", "1"), vm.serial
        got = dict(results(vm))
        assert (got["sd"], got["rtc"], got["expander"]) == ("absent", "absent", "absent"), got


def test_abort_stops_the_rest(board, diag_image, good_card):
    with alone(diag_image, board, sd=good_card) as vm:
        command(vm, "diag all")
        vm.wait_for(re.compile(r"ev=diag_progress name=sd step=blocks pct=\d+\n"), timeout=TIMEOUT)
        key(vm, "back")
        vm.wait_for(re.compile(r"ev=diag_done [^\n]*\n"), timeout=TIMEOUT)
        got = results(vm)
        assert got[0] == ("mem", "ok") and got[1] == ("sd", "skipped"), got
        assert all(r == "skipped" for _, r in got[1:]), got
        assert {m.get("why") for m in events(vm, "diag")[1:]} == {"abort"}, vm.serial
        done = events(vm, "diag_done")[0]
        assert (done["ok"], done["skipped"]) == ("1", "8"), done
        # A stopped run is never OK.
        _, rows = dump(vm)
        assert "SKIPPED  1 ok, 0 failed" in rows[13], rows
        # The tests after the abort never started.
        assert not events(vm, "video_pattern") and not events(vm, "audio"), vm.serial


def test_timer_starved_rule():
    """Only missed ticks that explain the whole error excuse a failed timer run."""
    base = {"result": "fail", "ticks": "100", "hz": "100", "ppm": "0", "limit": "10000"}
    # Seen on raspi2b at load 22: 20 missed ticks, the timer ran at its rate.
    assert timer_starved(dict(base, us="1201762", missed="20", window_missed="20"))
    # 16 missed, 1 of them before the first tick: us=1147000 was seen at load 52.
    assert timer_starved(dict(base, us="1147000", missed="16", window_missed="15"))
    # The same run judged by every missed tick is not explained.
    assert timer_starved(dict(base, us="1147000", missed="16", window_missed="16")) is None
    # No missed tick: a wrong rate is a timer fault.
    assert timer_starved(dict(base, us="1201762", missed="0", window_missed="0")) is None
    # Missed ticks that do not explain the time: the rate is wrong too.
    assert timer_starved(dict(base, us="1500000", missed="20", window_missed="20")) is None
    # An image without window_missed, a passing run or a run with no tick is never excused.
    assert timer_starved(dict(base, us="1201762", missed="20")) is None
    assert timer_starved(dict(base, result="ok", us="1000000", missed="0",
                              window_missed="0")) is None
    assert timer_starved({"result": "fail", "why": "notick", "ticks": "3"}) is None
