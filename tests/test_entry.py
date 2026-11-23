"""Entry: the first markers, the trust rule, the screen and the self check."""

from harness.boot import BOARDS

from mfboot import IDLE, TIMEOUT, alone, events, one, order


def test_alone_without_card(board, mfboot_image):
    with alone(mfboot_image, board) as vm:
        vm.wait_for(IDLE, timeout=TIMEOUT)
        assert order(vm)[:5] == ["boot", "handoff", "screen", "input", "selfcheck"], vm.serial
        boot = one(vm, "boot")
        # Alone, the stage starts in the mode the firmware stub uses on this board.
        assert boot["entry_mode"] == BOARDS[board].auto_mode
        assert boot["version"] == "0.1.0"
        assert one(vm, "handoff")["trusted"] == "0"
        screen = one(vm, "screen")
        # Cold: no adopt call runs, the stage starts its own screen.
        assert (screen["fb"], screen["console"], screen["kept"], screen["path"],
                screen["adopt_fb"], screen["adopt_console"], screen["unknown"]) == \
            ("0", "0", "0", "init", "none", "none", "0"), screen
        assert screen["scheme"] == "0"
        sel = one(vm, "input")
        assert (sel["serial"], sel["mini"], sel["script"]) == ("0", "0", "0"), sel
        assert one(vm, "selfcheck")["rc"] == "ok"
        assert not events(vm, "jump")
