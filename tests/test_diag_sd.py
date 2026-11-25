"""The SD test: a good card, bad images, other files, no /ROBCO, a broken directory,
a large card, no card. Never a write."""

import hashlib
import struct

import pytest

import mfboot
from diag import (TIMEOUT, alone, command, diag_image, diag_test_image, dump, events,  # noqa: F401
                  good_card, one, run)
from harness.sdimage import PART_LBA, SECTOR, corrupt, make_sd_image


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
        assert (m["errors"], m["files"], m["images"], m["bad_images"], m["others"],
                m["writes"]) == ("0", "4", "3", "0", "1", "0"), m
        found = images(vm)
        assert set(found) == {mfboot.UOS, mfboot.MAINT, mfboot.DIAG}, found
        assert all(x["rc"] == "ok" for x in found.values()), found
        _, rows = dump(vm)
        assert any(r.startswith("DIAG.SYS") and " OK      DIAG subtype 3" in r for r in rows), rows
    assert digest(good_card) == before


def test_write_attempt_is_counted_and_refused(board, diag_test_image, good_card):
    """The test build sends one write through the guard: counted, refused, card unchanged."""
    before = digest(good_card)
    with alone(diag_test_image, board, sd=good_card) as vm:
        assert one(vm, "write_hook")["name"] == "sd"
        m = run(vm, "sd")
        assert (m["result"], m["writes"]) == ("ok", "0"), m
        command(vm, "sdwrite")
        m = run(vm, "sd")
        assert m["result"] == "fail", vm.serial
        assert (m["probe_rc"], m["writes"]) == ("-5", "1"), m
        # sdwrite is one-shot: the next run sends no write and passes.
        m = run(vm, "sd")
        assert (m["result"], m["writes"]) == ("ok", "0"), m
        assert "probe_rc" not in m, m
    assert digest(good_card) == before


def test_failed_chunk_reads_fail_even_when_every_block_reads_alone(board, diag_test_image,
                                                                    good_card):
    """The test build fails every multi-block read once: the run fails with the count."""
    with alone(diag_test_image, board, sd=good_card) as vm:
        command(vm, "sdmulti")
        m = run(vm, "sd")
        assert m["result"] == "fail", vm.serial
        assert m["errors"] == "0" and "first_bad" not in m, m
        assert int(m["chunk_errors"]) > 0, m
        # sdmulti is one-shot: the next run reads normally and passes.
        m = run(vm, "sd")
        assert (m["result"], m["chunk_errors"], m["errors"]) == ("ok", "0", "0"), m


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
        assert rows[17] == "SD: FAILED. UOS.SYS: crc", rows


def test_short_diag_fails(board, diag_image, good_card_files, need_mtools, tmp_path):
    """DIAG.SYS cut to 16 bytes is a bad image, never "not an image"."""
    files = good_card_files
    files[mfboot.DIAG] = files[mfboot.DIAG][:16]
    sd = mfboot.card(tmp_path, files)
    with alone(diag_image, board, sd=sd) as vm:
        m = run(vm, "sd")
        assert m["result"] == "fail", vm.serial
        assert (m["images"], m["bad_images"], m["bad_path"], m["bad_rc"]) == \
            ("3", "1", mfboot.DIAG, "short"), m
        found = images(vm)
        assert (found[mfboot.DIAG]["rc"], found[mfboot.DIAG]["size"]) == ("short", "16"), found
        _, rows = dump(vm)
        assert any(r.startswith("DIAG.SYS") and "FAILED  short" in r for r in rows), rows
        assert rows[17] == "SD: FAILED. DIAG.SYS: short", rows


def test_bad_magic_fails(board, diag_image, good_card_files, need_mtools, tmp_path):
    files = good_card_files
    files[mfboot.UOS] = mfboot.flip(files[mfboot.UOS], 0x20)
    sd = mfboot.card(tmp_path, files)
    with alone(diag_image, board, sd=sd) as vm:
        m = run(vm, "sd")
        assert m["result"] == "fail", vm.serial
        assert (m["images"], m["bad_images"], m["bad_path"], m["bad_rc"]) == \
            ("3", "1", mfboot.UOS, "magic"), m
        assert images(vm)[mfboot.UOS]["rc"] == "magic", vm.serial


def test_other_file_is_listed(board, diag_image, good_card_files, need_mtools, tmp_path):
    files = good_card_files
    files["/ROBCO/NOTES.TXT"] = b"not an image\n"
    sd = mfboot.card(tmp_path, files)
    with alone(diag_image, board, sd=sd) as vm:
        m = run(vm, "sd")
        assert m["result"] == "ok", vm.serial
        assert (m["files"], m["images"], m["bad_images"], m["others"]) == ("5", "3", "0", "2"), m
        assert "/ROBCO/NOTES.TXT" not in images(vm)
        _, rows = dump(vm)
        assert any(r.startswith("NOTES.TXT") and r.endswith("-       not an image")
                   for r in rows), rows


def test_no_robco_folder(board, diag_image, need_mtools, tmp_path):
    sd = tmp_path / "nodir.img"
    make_sd_image(sd, 16, {"OTHER/README.TXT": b"x"})
    with alone(diag_image, board, sd=sd) as vm:
        m = run(vm, "sd")
        assert (m["result"], m["dir"], m["files"]) == ("fail", "missing", "0"), m
        # The block reads still ran and report.
        assert (m["errors"], m["read"]) == ("0", "30720"), m
        _, rows = dump(vm)
        assert rows[17] == "SD: FAILED. No /ROBCO folder", rows


def _break_robco_chain(path):
    """Points the FAT entry of the first cluster of /ROBCO at cluster 1: an invalid link."""
    base = PART_LBA * SECTOR
    with open(path, "rb") as f:
        f.seek(base)
        bs = f.read(SECTOR)
        per_sec, = struct.unpack_from("<H", bs, 11)
        rsvd, = struct.unpack_from("<H", bs, 14)
        nfats = bs[16]
        root_ents, = struct.unpack_from("<H", bs, 17)
        fatsz, = struct.unpack_from("<H", bs, 22)
        assert per_sec == SECTOR and fatsz != 0, "a FAT12/16 volume is expected"
        root = base + (rsvd + nfats * fatsz) * SECTOR
        f.seek(root)
        ents = f.read(root_ents * 32)
    for i in range(0, len(ents), 32):
        e = ents[i:i + 32]
        if e[:11] == b"ROBCO      " and e[11] & 0x10:
            first, = struct.unpack_from("<H", e, 26)
            break
    else:
        raise AssertionError("no /ROBCO entry")
    for k in range(nfats):
        corrupt(path, base + (rsvd + k * fatsz) * SECTOR + first * 2, struct.pack("<H", 1))


def test_directory_read_error_fails(board, diag_image, good_card_files, need_mtools, tmp_path):
    """/ROBCO spans two clusters and its chain is broken: readdir fails after the first."""
    files = good_card_files
    for i in range(20):
        files[f"/ROBCO/PAD{i:02d}.TXT"] = b"x"
    sd = mfboot.card(tmp_path, files)
    _break_robco_chain(sd)
    with alone(diag_image, board, sd=sd) as vm:
        m = run(vm, "sd")
        assert m["result"] == "fail", vm.serial
        assert m["dir_rc"] == "-17", m
        _, rows = dump(vm)
        assert rows[17] == "SD: FAILED. /ROBCO read error -17", rows


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


@pytest.fixture
def good_card_files(diag_image, test_os):
    """The files of good_card, to change before the card is made."""
    with open(diag_image, "rb") as f:
        diag = f.read()
    return {mfboot.UOS: test_os["uos"], mfboot.MAINT: test_os["maint"], mfboot.DIAG: diag}
