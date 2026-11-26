"""The whole chain to the diagnostics image: RETROS-BIOS loads the real agent from the
card, the menu picks DIAG, and `diag all` runs to its result line. Needs --retros-img."""

import re

import mfboot as mf
from diag import (RTC, TESTS, TIMEOUT, X0, X1, diag_image, dump, events,  # noqa: F401
                  results, run_all, skip_if_timer_starved, state)
from test_chain import BIOS_WAIT_CFG, TITLE
from test_diag_all import FULL
from test_diag_chain import chain_files, check_diag_booted

DIAG_ROW = "  3  DIAG                     /ROBCO/DIAG.SYS"


def test_whole_chain_diag(board, retros_image, mfboot, diag_image, test_os, need_mtools,
                          tmp_path):
    files = chain_files(test_os, diag_image)
    files["/ROBCO/MFBOOT.SYS"] = mfboot
    sd = mf.card(tmp_path, files, cfg=BIOS_WAIT_CFG)
    with mf.after(retros_image, board, sd, extra_args=RTC + X0 + X1) as vm:
        vm.wait_for(mf.READY, timeout=TIMEOUT)
        assert mf.one(vm, "handoff")["trusted"] == "1"
        _, rows = mf.dump(vm)
        assert TITLE in rows, rows
        assert DIAG_ROW in rows, rows
        mf.char(vm, "3")
        mf.key(vm, "select")
        check_diag_booted(vm)
        done = run_all(vm)
        skip_if_timer_starved(vm, [(t, FULL.get(t, "ok")) for t in TESTS])
        assert (done["ok"], done["fail"], done["absent"], done["skipped"]) == \
            ("7", "0", "1", "1"), vm.serial
        state(vm)
        _, rows = dump(vm)
        assert rows[17] == "Run all: 7 OK, 0 FAILED, 1 ABSENT, 1 SKIPPED.", rows
        sd_result = results(vm, "sd")
        assert len(sd_result) == 1 and sd_result[0]["writes"] == "0", sd_result
        assert len(re.findall(r"@RC stage=bios ev=jump", vm.serial)) == 1, vm.serial
        assert not re.search(r"@RC stage=\w+ ev=exception", vm.serial), vm.serial
