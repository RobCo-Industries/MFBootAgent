"""The library's host timing skip limit is wired in, no QEMU.

RobCo-Core's tests/target/conftest.py counts the skips per board and
tests its own counting. These tests check that this project loads it and
that the diagnostics helpers skip with a reason it counts.
"""

import pytest

import diag
from harness import hosttiming


def test_the_library_counter_is_registered(request):
    counter = request.config.pluginmanager.get_plugin("robco-timing-skips")
    assert counter is not None
    assert request.config.getoption("--timing-skips-max") >= 0


def test_a_starved_timer_skip_is_counted():
    m = {"result": "fail", "ticks": "100", "hz": "100", "us": "1201762",
         "missed": "20", "window_missed": "20", "ppm": "0", "limit": "10000"}
    with pytest.raises(pytest.skip.Exception) as e:
        diag.skip_if_timer_alone_starved(None, m)
    assert hosttiming.is_timing_skip(str(e.value))
