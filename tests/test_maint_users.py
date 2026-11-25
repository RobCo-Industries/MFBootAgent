"""Restore users: the setting off, absent, on and bad; no default file, an empty one;
a no, ESC, Ctrl-C and a wrong key at the question; a yes, read back from the card."""

import pytest

from maintimg import (USERS_DAT, USERS_DEF, alone, card, card_names, char, command,  # noqa: F401
                      dump, events, good_files, key, maint_cfg, maint_image, read_card_file, run,
                      state, wait_result)

ON = b"\n[maint]\nrestore_users = on\n"
DEFAULT = bytes(range(256)) * 9 + b"default users\n"
OLD = b"old users, every password lost\n" * 20


def users_card(tmp_path, good_files, cfg_extra=ON, default=DEFAULT, old=OLD):
    files = dict(good_files)
    if default is not None:
        files[USERS_DEF] = default
    if old is not None:
        files[USERS_DAT] = old
    return card(tmp_path, files, cfg=maint_cfg(cfg_extra))


def run_users(vm):
    command(vm, "tool restore_users")


@pytest.mark.parametrize("extra", [b"", b"\n[maint]\nrestore_users = off\n",
                                   b"\n[maint]\nrestore_users = maybe\n"],
                         ids=["absent", "off", "bad"])
def test_off(board, maint_image, good_files, need_mtools, tmp_path, extra):
    sd = users_card(tmp_path, good_files, cfg_extra=extra)
    with alone(maint_image, board, sd=sd) as vm:
        run_users(vm)
        m = wait_result(vm, "restore_users", 1)
        assert (m["result"], m["why"]) == ("off", "setting"), m
        assert events(vm, "ask") == [], vm.serial
        state(vm)
        _, rows = dump(vm)
        assert rows[14] == "Off. BOOT.CFG must have [maint] restore_users = on.", rows
        assert rows[12] == "Restore users: OFF. Off in BOOT.CFG.", rows
    assert read_card_file(sd, USERS_DAT) == OLD


@pytest.mark.parametrize("default, want", [(None, ("absent", "missing")),
                                           (b"", ("fail", "empty"))],
                         ids=["missing", "empty"])
def test_no_default(board, maint_image, good_files, need_mtools, tmp_path, default, want):
    sd = users_card(tmp_path, good_files, default=default)
    with alone(maint_image, board, sd=sd) as vm:
        run_users(vm)
        m = wait_result(vm, "restore_users", 1)
        assert (m["result"], m["why"]) == want, m
        assert events(vm, "ask") == [], vm.serial
    assert read_card_file(sd, USERS_DAT) == OLD


def ask(vm):
    run_users(vm)
    st = state(vm)
    assert (st["phase"], st["tool"]) == ("tool", "restore_users"), st
    a = events(vm, "ask")[-1]
    assert (a["what"], a["from"], a["to"], a["size"]) == \
        ("restore_users", USERS_DEF, USERS_DAT, str(len(DEFAULT))), a
    _, rows = dump(vm)
    assert rows[14] == f"Copy {USERS_DEF} over {USERS_DAT}?", rows
    assert rows[15] == ("Every account goes back to the default. ENTER or Y: yes. "
                        "Other keys: no."), rows


@pytest.mark.parametrize("answer", ["n", "back", "ctrl_c", "q", "down"])
def test_no_at_the_question(board, maint_image, good_files, need_mtools, tmp_path, answer):
    sd = users_card(tmp_path, good_files)
    with alone(maint_image, board, sd=sd) as vm:
        ask(vm)
        if answer == "ctrl_c":
            vm.send("\x03")
        elif answer in ("back", "down"):
            key(vm, answer)
        else:
            char(vm, answer)
        m = wait_result(vm, "restore_users", 1)
        assert (m["result"], m["why"]) == ("refused", "no"), m
    assert read_card_file(sd, USERS_DAT) == OLD
    assert card_names(sd, "/UOS") == ["SYSUAF.DAT", "SYSUAF.DEF"]


@pytest.mark.parametrize("answer", ["y", "select"])
def test_yes(board, maint_image, good_files, need_mtools, tmp_path, answer):
    sd = users_card(tmp_path, good_files)
    with alone(maint_image, board, sd=sd) as vm:
        ask(vm)
        if answer == "y":
            char(vm, "y")
        else:
            key(vm, "select")
        m = wait_result(vm, "restore_users", 1)
        assert (m["result"], m["from"], m["to"], m["size"]) == \
            ("ok", USERS_DEF, USERS_DAT, str(len(DEFAULT))), m
        state(vm)
        _, rows = dump(vm)
        assert rows[16] == f"Done. {USERS_DAT} now holds the default users.", rows
        # The card is still sound for the read-only tools after the write.
        c = run(vm, "check_card")
        assert (c["result"], c["writes"]) == ("ok", "0"), c
    assert read_card_file(sd, USERS_DAT) == DEFAULT
    assert read_card_file(sd, USERS_DEF) == DEFAULT
    # No work file of the atomic write is left.
    assert card_names(sd, "/UOS") == ["SYSUAF.DAT", "SYSUAF.DEF"]


def test_yes_with_no_old_file(board, maint_image, good_files, need_mtools, tmp_path):
    sd = users_card(tmp_path, good_files, old=None)
    with alone(maint_image, board, sd=sd) as vm:
        ask(vm)
        char(vm, "Y")
        assert wait_result(vm, "restore_users", 1)["result"] == "ok"
    assert read_card_file(sd, USERS_DAT) == DEFAULT
