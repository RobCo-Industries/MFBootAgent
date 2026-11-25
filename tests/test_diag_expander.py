"""The expander test: both PCA9554, one, none, a live input line, and proof
that no register of either chip changed. The test reads the model's pins
from outside with QMP qom-get, which gives each pin's configuration bit OR
its output bit, and drives input lines through qtest (canary V17 in
RobCo-Core's tests/README.md)."""

import os
import re
import subprocess

from diag import (TIMEOUT, X0, X1, alone, command, diag_image, dump, events, key,  # noqa: F401
                  one, run, state)
from harness.env import minimal_env

# Outputs preset before the test, per chip: {pin: level}. The rest stay inputs.
PRESET = {0: {0: "low", 2: "low", 5: "high"}, 1: {1: "low", 7: "high"}}
# What the chip's registers then hold.
REGS = {0: {"in": "0xfa", "out": "0xfa", "pol": "0x00", "cfg": "0xda"},
        1: {"in": "0xfd", "out": "0xfd", "pol": "0x00", "cfg": "0x7d"}}


def path(chip):
    return f"/machine/peripheral/gx{chip}"


def pins(vm, chip):
    return sum((vm.qmp.execute("qom-get", path=path(chip), property=f"pin{p}") == "high") << p
               for p in range(8))


def preset(vm, chip):
    for pin, level in PRESET[chip].items():
        vm.qmp.execute("qom-set", path=path(chip), property=f"pin{pin}", value=level)


def chip_marker(vm, chip):
    return [m for m in events(vm, "expander") if m["chip"] == str(chip)][-1]


def view_done(vm):
    vm.wait_for(re.compile(r"ev=diag name=expander result=[^\n]*\n"), timeout=TIMEOUT)
    return [m for m in events(vm, "diag") if m["name"] == "expander"][-1]


def test_both_chips_read_only(board, diag_image):
    with alone(diag_image, board, devices=X0 + X1, qtest=True) as vm:
        for chip in (0, 1):
            preset(vm, chip)
        before = (pins(vm, 0), pins(vm, 1))
        assert before == (0xFA, 0xFD), [hex(x) for x in before]
        command(vm, "diag expander")
        vm.wait_for("ESC leaves.", timeout=TIMEOUT)
        for chip in (0, 1):
            m = chip_marker(vm, chip)
            got = {k: m[k] for k in ("in", "out", "pol", "cfg")}
            assert (m["present"], m["attached"], got) == ("1", "1", REGS[chip]), m
        # A sensor contact on X0.3 closes, then opens: the view follows.
        vm.qmp.set_irq_in(path(0), 3, 0)
        vm.wait_for("ev=expander_in chip=0 bits=0xf2\n", timeout=TIMEOUT)
        state(vm)
        _, rows = dump(vm)
        assert rows[21 + 3] == "X0.3  LOW           X1.3  HIGH", rows
        assert rows[21 + 1] == "X0.1  HIGH          X1.1  LOW", rows
        vm.qmp.set_irq_in(path(0), 3, 1)
        vm.wait_for("ev=expander_in chip=0 bits=0xfa\n", timeout=TIMEOUT)
        vm.qmp.set_irq_in(path(1), 6, 0)
        vm.wait_for("ev=expander_in chip=1 bits=0xbd\n", timeout=TIMEOUT)
        vm.qmp.set_irq_in(path(1), 6, 1)
        vm.wait_for("ev=expander_in chip=1 bits=0xfd\n", timeout=TIMEOUT)
        key(vm, "back")
        m = view_done(vm)
        assert (m["result"], m["view"]) == ("ok", "back"), m
        assert (m["x0"], m["cfg0"], m["out0"], m["x1"], m["cfg1"], m["out1"]) == \
            ("0x20", "0xda", "0xfa", "0x21", "0x7d", "0xfd"), m
        # Nothing the chips drive changed, from the start to the end.
        assert (pins(vm, 0), pins(vm, 1)) == before
        # Run again: the registers still read the same.
        command(vm, "diag expander")
        vm.wait_for("ESC leaves.", timeout=TIMEOUT)
        key(vm, "back")
        view_done(vm)
        for chip in (0, 1):
            m = chip_marker(vm, chip)
            assert {k: m[k] for k in ("in", "out", "pol", "cfg")} == REGS[chip], m
        assert (pins(vm, 0), pins(vm, 1)) == before


def test_one_chip(board, diag_image):
    with alone(diag_image, board, devices=X0) as vm:
        command(vm, "diag expander")
        vm.wait_for("ESC leaves.", timeout=TIMEOUT)
        state(vm)
        _, rows = dump(vm)
        assert rows[19] == "X0 at 0x20: IN 0xff  OUT 0xff  POL 0x00  CFG 0xff", rows
        assert rows[20] == "X1 at 0x21: absent", rows
        assert rows[21] == "X0.0  HIGH          X1.0  --", rows
        vm.send("\x1b")
        m = view_done(vm)
        assert (m["result"], m["x0"], m["x1"], m["view"]) == ("ok", "0x20", "absent", "back"), m
        assert one(vm, "i2c")["rc"] == "0"


def test_no_chip(board, diag_image):
    with alone(diag_image, board) as vm:
        m = run(vm, "expander")
        assert (m["result"], m["x0"], m["x1"]) == ("absent", "absent", "absent"), m
        assert "view" not in m, m


def test_image_has_no_expander_writes(diag_image):
    """The image does not even link a function that writes an expander or the clock."""
    elf = os.path.join(os.path.dirname(diag_image), "diag", "diag.elf")
    nm = subprocess.run(["arm-none-eabi-nm", elf], capture_output=True, text=True, check=True,
                        env=minimal_env())
    names = {line.split()[-1] for line in nm.stdout.splitlines() if line.strip()}
    assert "gpiox_attach" in names and "gpiox_read_port" in names
    for bad in ("gpiox_config", "gpiox_write", "gpiox_write_port", "rtc_set"):
        assert bad not in names, bad
