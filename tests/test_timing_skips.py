"""The host timing skip counter against fake reports, no QEMU: more than one
skip per board fails the session, unless --timing-skips-max allows more."""

import types

import pytest

import timing_skips as ts


def report(outcome, message=""):
    longrepr = ("tests/test_x.py", 1, message) if outcome == "skipped" else None
    return types.SimpleNamespace(skipped=outcome == "skipped", longrepr=longrepr)


def fake_config(boards, most=None):
    opts = {"--board": boards, ts.OPTION: most}
    return types.SimpleNamespace(getoption=opts.get,
                                 getini=lambda name: [])


def finish(config, reports, status=pytest.ExitCode.OK):
    counter = ts.Counter(config)
    for r in reports:
        counter.pytest_runtest_logreport(r)
    session = types.SimpleNamespace(exitstatus=status)
    counter.pytest_sessionfinish(session)
    return counter, session.exitstatus


def timing(n):
    return [report("skipped", f"Skipped: {ts.PREFIX}timer missed {i} ticks") for i in range(n)]


def test_only_timing_skips_count():
    others = [report("passed"), report("skipped", "Skipped: RETROS_IMG is not set"),
              report("skipped", "Skipped: timer missed 3 ticks")]
    counter, status = finish(fake_config(["raspi2b"]), others + timing(1))
    assert counter.skips == 1
    assert status == pytest.ExitCode.OK


def test_one_per_board_passes():
    _, status = finish(fake_config(["raspi0", "raspi2b"]), timing(2))
    assert status == pytest.ExitCode.OK


def test_more_than_one_per_board_fails():
    _, status = finish(fake_config(["raspi2b"]), timing(2))
    assert status == pytest.ExitCode.TESTS_FAILED
    _, status = finish(fake_config(["all"]), timing(5))
    assert status == pytest.ExitCode.TESTS_FAILED


def test_all_counts_every_board():
    _, status = finish(fake_config(["all"]), timing(4))
    assert status == pytest.ExitCode.OK


def test_option_overrides_the_limit():
    _, status = finish(fake_config(["raspi2b"], most=3), timing(3))
    assert status == pytest.ExitCode.OK
    _, status = finish(fake_config(["raspi0", "raspi2b"], most=0), timing(1))
    assert status == pytest.ExitCode.TESTS_FAILED


def test_failed_session_stays_failed():
    _, status = finish(fake_config(["raspi2b"]), timing(0), pytest.ExitCode.TESTS_FAILED)
    assert status == pytest.ExitCode.TESTS_FAILED


def test_skip_has_the_prefix():
    with pytest.raises(pytest.skip.Exception, match=r"^host timing: timer missed"):
        ts.skip("timer missed 2 ticks")
