"""The video, audio, input, timer, rng and rtc tests through the maintenance port."""

import re
import time

from diag import (MENU_ROWS, RTC, TIMEOUT, alone, command, diag_image, dump,  # noqa: F401
                  events, key, run, skip_if_timer_alone_starved, state)
from mfboot import screenshot

WHITE, BLACK = (255, 255, 255), (0, 0, 0)
# Pixels each pattern must show, (x, y): colour. The screen is 640 by 480.
PIXELS = {
    "bars": {(40, 240): WHITE, (120, 240): (255, 255, 0), (200, 100): (0, 255, 255),
             (280, 10): (0, 255, 0), (360, 470): (255, 0, 255), (440, 240): (255, 0, 0),
             (520, 240): (0, 0, 255), (600, 240): BLACK},
    "grid": {(0, 0): WHITE, (639, 0): WHITE, (0, 479): WHITE, (639, 479): WHITE,
             (639, 210): WHITE, (330, 479): WHITE, (638, 210): BLACK, (330, 478): BLACK,
             (40, 20): WHITE, (20, 40): WHITE, (20, 20): BLACK, (41, 41): BLACK},
    "ramp": {(0, 240): BLACK, (639, 240): WHITE, (320, 0): (127, 127, 127)},
    "red": {(0, 0): (255, 0, 0), (639, 479): (255, 0, 0)},
    "green": {(320, 240): (0, 255, 0)},
    "blue": {(320, 240): (0, 0, 255)},
    "white": {(320, 240): WHITE, (0, 479): WHITE},
    "black": {(320, 240): BLACK, (639, 0): BLACK},
}
# Audio: every call the trace backend saw, in order.
SWEEP = ["250", "500", "1000", "2000", "4000"]
TUNE = ["523", "659", "784", "784", "1047"]


def pattern_marker(vm, name, n):
    vm.wait_for(re.compile(r"(?:ev=video_pattern name=%s [^\n]*\n[\s\S]*?){%d}" % (name, n)),
                timeout=TIMEOUT)


def test_video_patterns_step_by_key(board, diag_image, tmp_path):
    with alone(diag_image, board) as vm:
        command(vm, "diag video")
        for name, pixels in PIXELS.items():
            pattern_marker(vm, name, 1)
            img = screenshot(vm, tmp_path, name)
            for (x, y), want in pixels.items():
                assert img.getpixel((x, y)) == want, (name, x, y, img.getpixel((x, y)))
            # The pattern stays until a key.
            time.sleep(0.4)
            assert len(events(vm, "video_pattern")) == list(PIXELS).index(name) + 1
            key(vm, "select")
        m = run_result(vm, "video")
        assert m["result"] == "ok", vm.serial
        assert (m["width"], m["height"], m["depth"], m["pitch"]) == ("640", "480", "32", "2560")
        assert (m["patterns"], m["judge"]) == ("8", "person"), m
        # The pixel order and offset are part of the verdict; virt and alpha only report.
        assert (m["order"], m.get("offset"), m.get("virt"), m.get("alpha")) == \
            ("rgb", "0,0", "640x480", "2"), m
        # The menu comes back in full.
        state(vm)
        _, rows = dump(vm)
        assert rows[:4] == MENU_ROWS[:4], rows
        assert rows[6].startswith("  3  VIDEO     Screen patterns              OK       640x480"), rows


def run_result(vm, name):
    vm.wait_for(re.compile(r"ev=diag name=%s result=[^\n]*\n" % name), timeout=TIMEOUT)
    return [m for m in events(vm, "diag") if m["name"] == name][-1]


def test_video_abort(board, diag_image):
    with alone(diag_image, board) as vm:
        command(vm, "diag video")
        pattern_marker(vm, "bars", 1)
        key(vm, "back")
        m = run_result(vm, "video")
        assert (m["result"], m["why"], m["pattern"]) == ("skipped", "abort", "bars"), m
        assert state(vm)["phase"] == "menu"
        _, rows = dump(vm)
        assert rows[0] == MENU_ROWS[0], rows


def test_audio_through_trace(board, diag_image):
    with alone(diag_image, board) as vm:
        m = run(vm, "audio")
        assert m["result"] == "ok", vm.serial
        # QEMU does not model the PWM: the library falls back to the trace backend.
        assert (m["backend"], m["tones"], m["notes"]) == ("trace", "5", "6"), m
        tones = [a["hz"] for a in events(vm, "audio") if a["op"] == "tone"]
        assert tones == SWEEP + TUNE, tones
        stops = [a for a in events(vm, "audio") if a["op"] == "stop"]
        assert len(stops) >= len(SWEEP) + 1, stops


def test_timer_and_rng(board, diag_image):
    with alone(diag_image, board) as vm:
        # rng first: a timer skip on a busy host must not hide it.
        m = run(vm, "rng")
        assert m["result"] == "ok", vm.serial
        assert (m["words"], m["repeats"]) == ("1024", "0"), m
        assert abs(int(m["bits"]) - 16384) <= 512, m
        m = run(vm, "timer")
        skip_if_timer_alone_starved(vm, m)
        assert m["result"] == "ok", vm.serial
        assert (m["ticks"], m["asked_hz"], m["hz"], m["limit"], m["missed"]) == \
            ("100", "100", "100", "10000", "0"), m
        assert abs(int(m["ppm"])) <= 10000


def test_rtc_present(board, diag_image):
    with alone(diag_image, board, devices=RTC) as vm:
        m = run(vm, "rtc")
        assert m["result"] == "ok", vm.serial
        assert (m["addr"], m["chip"], m["halted"]) == ("0x68", "ds1307", "0"), m
        assert re.fullmatch(r"20\d\d-\d\d-\d\dT\d\d:\d\d:\d\d", m["time"]), m
        # Never sets the time: no write to the clock chip's time registers.
        assert "rtc_set" not in vm.serial


def test_rtc_absent(board, diag_image):
    with alone(diag_image, board) as vm:
        m = run(vm, "rtc")
        assert (m["result"], m["addr"]) == ("absent", "0x68"), m
        state(vm)
        _, rows = dump(vm)
        assert rows[11].endswith("ABSENT   No clock chip at 0x68."), rows


def input_events(vm):
    return [(e["source"], e["type"], e["code"], e["value"]) for e in events(vm, "input_event")]


def open_view(vm):
    command(vm, "diag input")
    vm.wait_for("ev=input_view state=open", timeout=TIMEOUT)


def test_input_live_view(board, diag_image):
    with alone(diag_image, board) as vm:
        open_view(vm)
        key(vm, "up")
        command(vm, "char x")
        key(vm, "select", 1)
        vm.wait_for(re.compile(r"ev=input_event source=script type=key code=select value=1\n"),
                    timeout=TIMEOUT)
        state(vm)
        _, rows = dump(vm)
        assert rows[22].startswith("select   PRESSED"), rows
        assert rows[29] == "script  key   select    press", rows
        assert rows[28] == "script  char  0x78 'x'", rows
        key(vm, "select", 0)
        # One BACK alone stays in the view.
        key(vm, "back")
        time.sleep(1.2)
        assert state(vm)["test"] == "input"
        # ESC twice on the console leaves: each ESC becomes BACK.
        vm.send("\x1b")
        time.sleep(0.3)
        vm.send("\x1b")
        m = run_result(vm, "input")
        assert (m["result"], m["why"]) == ("ok", "back2"), m
        got = input_events(vm)
        assert got[:4] == [("script", "key", "up", "1"), ("script", "key", "up", "0"),
                           ("script", "char", "0x78", "0"), ("script", "key", "select", "1")], got
        assert got[-1][:3] == ("serial", "key", "back"), got


def test_input_long_back_and_idle(board, diag_image):
    with alone(diag_image, board) as vm:
        open_view(vm)
        key(vm, "back", 1)
        m = run_result(vm, "input")
        assert (m["result"], m["why"], m["events"]) == ("ok", "long", "1"), m
        key(vm, "back", 0)
        # Nothing at all for 20 s leaves too.
        open_view(vm)
        t0 = time.monotonic()
        vm.wait_for(re.compile(r"(?:ev=diag name=input [^\n]*\n[\s\S]*?){2}"), timeout=TIMEOUT)
        took = time.monotonic() - t0
        m = [x for x in events(vm, "diag") if x["name"] == "input"][-1]
        assert (m["why"], m["events"]) == ("idle", "0"), m
        assert 19 < took < 20 + 5, took
