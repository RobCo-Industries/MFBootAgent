"""The port helpers against a fake port, no QEMU: a reply left unread by
an earlier raw write must not pass for the answer to a later command."""

import threading
import types

import pytest

from harness.boot import Vm
from mfboot import batch, state

STATE = "| phase=menu sel=1 counting=0 maint=0 pending=0\nok\n"


class FakePort:
    """Answers each written line with `answers[line]`, all at once."""

    def __init__(self, vm, answers, late=""):
        self.vm, self.answers, self.late = vm, answers, late

    def sendall(self, data):
        out = self.late
        self.late = ""
        for line in data.decode().splitlines():
            out += self.answers[line]
        with self.vm._cond:
            self.vm._bufs[1] += out.encode()
            self.vm._cond.notify_all()


def fake_vm(answers, late=""):
    vm = Vm.__new__(Vm)
    vm._cond = threading.Condition()
    vm._bufs = [bytearray(), bytearray()]
    vm.qemu = types.SimpleNamespace(proc=types.SimpleNamespace(poll=lambda: None))
    vm._socks = [None, FakePort(vm, answers, late)]
    return vm


def test_stale_ok_is_reported_by_state():
    # The `ok` of an earlier raw "maint" write lands just before the state reply.
    vm = fake_vm({"state": STATE}, late="ok\n")
    with pytest.raises(AssertionError, match="left a reply unread"):
        state(vm)


def test_batch_reads_every_reply():
    vm = fake_vm({"key select": "ok\n", "maint": "err already open\n", "state": STATE})
    replies = batch(vm, ["key select", "maint"])
    assert [x.status for x in replies] == ["ok", "err already open"]
    assert state(vm)["phase"] == "menu"


def test_batch_reports_extra_replies():
    vm = fake_vm({"maint": "ok\n"}, late="ok\n")
    with pytest.raises(AssertionError):
        batch(vm, ["maint"])
