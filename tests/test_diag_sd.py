"""The SD test: a good card, a corrupted image, a large card, no card. Never a write."""

import hashlib

import mfboot
from diag import (TIMEOUT, alone, diag_image, dump, events, good_card, run)  # noqa: F401
from harness.sdimage import make_sd_image


def digest(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def images(vm):
    return {m["path"]: m for m in events(vm, "sd_image")}


def test_good_card(board, diag_image, good_card):
    before = digest(good_card)
    with alone(diag_image, board, sd=good_card) as vm:
        m = run(vm, "sd")
        assert m["result"] == "ok", vm.serial
        assert (m["card"], m["oid"], m["pnm"], m["blocks"]) == ("sdsc", "XY", "QEMU_", "32768"), m
        # A 16 MiB card: the volume is read whole.
        assert (m["vol_lba"], m["vol_blocks"], m["read"], m["sampled"]) == \
            ("2048", "30720", "30720", "0"), m
        assert (m["errors"], m["files"], m["images"], m["bad_images"], m["writes"]) == \
            ("0", "4", "3", "0", "0"), m
        found = images(vm)
        assert set(found) == {mfboot.UOS, mfboot.MAINT, mfboot.DIAG}, found
        assert all(x["rc"] == "ok" for x in found.values()), found
        _, rows = dump(vm)
        assert any(r.startswith("DIAG.SYS") and " OK      DIAG subtype 3" in r for r in rows), rows
    assert digest(good_card) == before


def test_corrupted_image(board, diag_image, test_os, need_mtools, tmp_path):
    files = {mfboot.UOS: mfboot.flip(test_os["uos"]), mfboot.MAINT: test_os["maint"]}
    sd = mfboot.card(tmp_path, files)
    with alone(diag_image, board, sd=sd) as vm:
        m = run(vm, "sd")
        assert m["result"] == "fail", vm.serial
        assert (m["errors"], m["images"], m["bad_images"], m["bad_path"]) == \
            ("0", "2", "1", mfboot.UOS), m
        found = images(vm)
        assert found[mfboot.UOS]["rc"] == "crc" and found[mfboot.MAINT]["rc"] == "ok", found
        _, rows = dump(vm)
        assert any(r.startswith("UOS.SYS") and "FAILED  crc" in r for r in rows), rows
        assert rows[17] == "SD: FAILED. 0 read errors, 1 bad images", rows


def test_large_card_is_sampled(board, diag_image, test_os, need_mtools, tmp_path):
    sd = tmp_path / "big.img"
    make_sd_image(sd, 64, {"ROBCO/UOS.SYS": test_os["uos"], "ROBCO/BOOT.CFG": mfboot.WAIT_CFG})
    with alone(diag_image, board, sd=sd) as vm:
        m = run(vm, "sd")
        assert m["result"] == "ok", vm.serial
        assert int(m["vol_blocks"]) > 65536 and m["sampled"] == "1", m
        assert m["read"] == "65536", m


def test_no_card(board, diag_image):
    with alone(diag_image, board) as vm:
        m = run(vm, "sd")
        assert (m["result"], m["why"]) == ("absent", "init"), m
        _, rows = dump(vm)
        assert rows[5].endswith("ABSENT   No card."), rows
