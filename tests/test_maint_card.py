"""Check card: a good card, a damaged file, a full card, a stop. Never a write."""

import hashlib

import mfboot as mf
from maintimg import (TIMEOUT, alone, card, command, dump, events, good_card,  # noqa: F401
                      good_files, maint_image, maint_test_image, run, state, wait_result)


def digest(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def errors(vm):
    return [(m["path"], m["what"]) for m in events(vm, "card_error")]


def test_good_card(board, maint_image, good_files, need_mtools, tmp_path):
    files = dict(good_files)
    files["/UOS/HOLO/A.HTP"] = b"tape" * 100
    files["/NOTES.TXT"] = b"hello\n"
    sd = card(tmp_path, files)
    before = digest(sd)
    with alone(maint_image, board, sd=sd) as vm:
        m = run(vm, "check_card")
        assert m["result"] == "ok", vm.serial
        # /, /ROBCO, /UOS, /UOS/HOLO; BOOT.CFG, three images, the tape, the notes.
        assert (m["dirs"], m["files"], m["errors"], m["writes"]) == ("4", "6", "0", "0"), m
        size = sum(len(v) for v in files.values()) + len(mf.WAIT_CFG)
        assert m["bytes"] == str(size), m
        assert 0 < int(m["free_bytes"]) < int(m["total_bytes"]), m
        assert errors(vm) == []
        st = state(vm)
        assert st["phase"] == "menu", st
        _, rows = dump(vm)
        assert rows[4].startswith("> 1  CHECK CARD    Read every file of the card    OK"), rows
        assert rows[15].startswith("4 folders, 6 files, 0 errors. Free "), rows
    assert digest(sd) == before


def dir_entry(data, name83):
    """The offset of the short directory entry with this 11-byte name."""
    off = data.find(name83)
    assert off >= 0 and off % 32 == 0, name83
    return off


def test_damaged_file(board, maint_image, good_files, need_mtools, tmp_path):
    """A file whose directory entry claims more bytes than its clusters hold."""
    files = dict(good_files)
    files["/UOS/BAD.DAT"] = b"x" * 1000
    files["/UOS/GOOD.DAT"] = b"y" * 1000
    sd = card(tmp_path, files)
    data = bytearray(sd.read_bytes())
    off = dir_entry(data, b"BAD     DAT")
    data[off + 28:off + 32] = (200000).to_bytes(4, "little")
    sd.write_bytes(bytes(data))
    before = digest(sd)
    with alone(maint_image, board, sd=sd) as vm:
        m = run(vm, "check_card")
        assert m["result"] == "fail", vm.serial
        assert (m["errors"], m["first_path"], m["first_what"], m["writes"]) == \
            ("1", "/UOS/BAD.DAT", "file", "0"), m
        # The walk went on after the damaged file.
        assert m["files"] == "6", m
        assert [p for p, _ in errors(vm)] == ["/UOS/BAD.DAT"], vm.serial
        st = state(vm)
        _, rows = dump(vm)
        assert any(r.startswith("ERROR /UOS/BAD.DAT: file ") for r in rows), rows
        assert rows[12] == "Check card: FAILED. Errors: 1.", rows
    assert digest(sd) == before


def test_full_card(board, maint_image, good_files, need_mtools, tmp_path):
    """A card filled to its last cluster: the walk shows progress and the port answers."""
    files = dict(good_files)
    used = sum(len(v) for v in files.values()) + len(mf.WAIT_CFG)
    probe = card(tmp_path, files, name="probe.img")
    with alone(maint_image, board, sd=probe) as vm:
        free = int(run(vm, "check_card")["free_bytes"])
    files["/FILL.DAT"] = bytes(range(256)) * ((free - 64 * 1024) // 256)
    sd = card(tmp_path, files)
    before = digest(sd)
    with alone(maint_image, board, sd=sd) as vm:
        command(vm, "tool check_card")
        # The port answers while the walk runs.
        reply = vm.script("state", timeout=TIMEOUT)
        assert reply.status == "ok", reply
        st = dict(f.split("=", 1) for f in reply.lines[0].split())
        assert (st["phase"], st["tool"]) == ("tool", "check_card"), st
        m = wait_result(vm, "check_card", 1)
        assert (m["result"], m["errors"], m["writes"]) == ("ok", "0", "0"), vm.serial
        assert int(m["free_bytes"]) < 128 * 1024, m
        assert int(m["bytes"]) == used + len(files["/FILL.DAT"]), m
        # About 4 s of reads: progress every 500 ms.
        assert len(events(vm, "progress")) >= 2, vm.serial
    assert digest(sd) == before


def test_stop(board, maint_image, good_files, need_mtools, tmp_path):
    files = dict(good_files)
    files["/FILL.DAT"] = b"z" * (12 << 20)
    sd = card(tmp_path, files)
    with alone(maint_image, board, sd=sd) as vm:
        command(vm, "tool check_card")
        command(vm, "key back")
        m = wait_result(vm, "check_card", 1)
        assert (m["result"], m["why"], m["writes"]) == ("fail", "stopped", "0"), m
        assert state(vm)["phase"] == "menu"


def test_write_is_counted_and_refused(board, maint_test_image, good_card):
    """The test build sends one write through the guard: counted, refused, the tool fails."""
    before = digest(good_card)
    with alone(maint_test_image, board, sd=good_card) as vm:
        command(vm, "wprobe")
        m = run(vm, "check_card")
        assert (m["result"], m["probe_rc"], m["writes"]) == ("fail", "-5", "1"), m
        # The probe is one-shot.
        m = run(vm, "check_card")
        assert (m["result"], m["writes"]) == ("ok", "0"), m
        assert "probe_rc" not in m
    assert digest(good_card) == before
