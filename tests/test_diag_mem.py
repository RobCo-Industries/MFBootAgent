"""The memory test: the full range, a planted fault and an abort by key."""

import re
import time

from diag import (TIMEOUT, alone, command, diag_image, diag_test_image, events,  # noqa: F401
                  key, one, run, state)

# QEMU's ARM RAM from the mailbox, per board, and the image end the map gives.
RAM_TOP = {"raspi0": 0x1C000000, "raspi1ap": 0x1C000000, "raspi2b": 0x3C000000,
           "raspi3b": 0x3C000000}
# A full pass takes about 4 s on 448 MiB and 9 s on 960 MiB in QEMU on an idle host.
# A busy host gets this much more.
HOST_ALLOWANCE = 20


def test_full_range(board, diag_image):
    with alone(diag_image, board) as vm:
        t0 = time.monotonic()
        m = run(vm, "mem", timeout=TIMEOUT)
        took = time.monotonic() - t0
        assert m["result"] == "ok", vm.serial
        start, end = int(m["start"], 16), int(m["end"], 16)
        assert (m["ranges"], m["patterns"]) == ("1", "4"), m
        # From the end of the image, page aligned, to the top of ARM RAM.
        assert 0x00400000 < start < 0x00500000 and start % 0x1000 == 0, m
        assert end == RAM_TOP[board], m
        assert int(m["bytes"]) == end - start
        # Every byte read back was cleaned and invalidated first, with the caches on.
        assert int(m["flushed"]) == int(m["bytes"]) and m["cache"] == "1", m
        assert int(m["ms"]) / 1000 < 10 + HOST_ALLOWANCE and took < 10 + HOST_ALLOWANCE, m
        prog = [p for p in events(vm, "diag_progress") if p["name"] == "mem"]
        assert prog and {p["step"] for p in prog} <= {"addr", "walk", "check", "inverse"}
        # At most one progress line per 500 ms, give or take one line.
        assert len(prog) <= int(m["ms"]) / 500 + 2, (len(prog), m["ms"])


def test_planted_fault_is_reported(board, diag_test_image):
    with alone(diag_test_image, board) as vm:
        assert one(vm, "fault_hook")["name"] == "mem"
        m = run(vm, "mem")
        assert m["result"] == "ok", m
        # The test build stops at 16 MiB.
        assert int(m["bytes"]) == 16 << 20, m
        bad = int(m["start"], 16) + 0x123450
        command(vm, f"fault {bad:#x}")
        m = run(vm, "mem")
        assert m["result"] == "fail", m
        assert (m["pattern"], int(m["addr"], 16)) == ("addr", bad), m
        assert int(m["expected"], 16) == bad
        assert int(m["found"], 16) == bad ^ 0x100
        assert vm.script("fault zz", timeout=TIMEOUT).status == "err bad address"


def test_abort_by_key(board, diag_image):
    with alone(diag_image, board) as vm:
        command(vm, "diag mem")
        vm.wait_for(re.compile(r"ev=diag_progress name=mem step=addr pct=\d+"), timeout=TIMEOUT)
        t0 = time.monotonic()
        key(vm, "back")
        vm.wait_for(re.compile(r"ev=diag name=mem result=[^\n]*\n"), timeout=TIMEOUT)
        assert time.monotonic() - t0 < 2 + 3, "the abort took too long"
        m = [x for x in events(vm, "diag") if x["name"] == "mem"][0]
        assert (m["result"], m["why"]) == ("skipped", "abort"), m
        assert state(vm)["phase"] == "menu"
