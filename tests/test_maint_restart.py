"""Restart: the tool marker, the reboot marker, then the image boots again."""

import re

from maintimg import (BOOT, READY, TIMEOUT, alone, char, command, events, good_card,  # noqa: F401
                      good_files, maint_image, state)


def two_boots(vm):
    vm.wait_for(re.compile(r"(?:@RC stage=maint ev=ready\r?\n[\s\S]*?){2}"), timeout=TIMEOUT)


def test_restart_from_the_port(board, maint_image, good_card):
    with alone(maint_image, board, sd=good_card) as vm:
        command(vm, "tool restart")
        two_boots(vm)
        evs = [m["ev"] for m in vm.markers() if m.get("stage") == "maint"]
        i = evs.index("tool")
        assert evs[i:i + 3] == ["tool", "reboot", "boot"], evs
        tool = events(vm, "tool")[0]
        assert (tool["name"], tool["result"]) == ("restart", "ok"), tool
        assert events(vm, "reboot")[0]["source"] == "tool"
        assert len(events(vm, "boot")) == 2
        assert state(vm)["phase"] == "menu"


def test_restart_from_the_menu(board, maint_image, good_card):
    with alone(maint_image, board, sd=good_card) as vm:
        char(vm, "6")
        two_boots(vm)
        assert [m["name"] for m in events(vm, "tool")] == ["restart"]
        assert len(events(vm, "boot")) == 2
