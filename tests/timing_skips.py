"""Skips a busy host explains, counted per session.

A test calls skip(reason) when host timing alone explains a failure. The
reason gets the prefix PREFIX. The plugin counts those skips and fails
the session when there are more than one per board, so a starved host
never passes in silence. --timing-skips-max=<n> sets another limit for a
loaded local machine.
"""

import pytest

from harness.plugin import selected_boards

PREFIX = "host timing: "
OPTION = "--timing-skips-max"


def skip(reason):
    pytest.skip(PREFIX + reason)


def is_timing_skip(report):
    """1 when the report is a skip from skip()."""
    if not report.skipped or not isinstance(report.longrepr, tuple):
        return False
    message = report.longrepr[2]
    return message.removeprefix("Skipped: ").startswith(PREFIX)


def limit(config):
    """The most timing skips the session allows: the option, else one per board."""
    value = config.getoption(OPTION)
    if value is not None:
        return value
    return len(selected_boards(config))


class Counter:
    def __init__(self, config):
        self.config = config
        self.skips = 0

    def pytest_runtest_logreport(self, report):
        if is_timing_skip(report):
            self.skips += 1

    def pytest_sessionfinish(self, session):
        most = limit(self.config)
        if self.skips > most:
            if session.exitstatus == pytest.ExitCode.OK:
                session.exitstatus = pytest.ExitCode.TESTS_FAILED

    def pytest_terminal_summary(self, terminalreporter):
        most = limit(self.config)
        if self.skips > most:
            terminalreporter.write_line(
                f"{self.skips} skips for host timing, more than {most}: the host is too "
                f"busy for these results. Set {OPTION} to allow more.", red=True)
        elif self.skips:
            terminalreporter.write_line(f"{self.skips} skips for host timing, at most {most}.")


def pytest_addoption(parser):
    parser.addoption(OPTION, type=int, default=None,
                     help="most skips for host timing in a session (default: one per board)")


def pytest_configure(config):
    config.pluginmanager.register(Counter(config), "timing_skips_counter")
