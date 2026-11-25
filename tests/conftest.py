import os
import sys

# RobCo-Core's harness: the --board option, the board fixture, need_mtools
# and robco_build. MFBootAgent adds --build-dir for the image under test
# and --retros-img for the whole-chain tests.
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORE = os.path.join(ROOT, "external", "robco-core")
sys.path.insert(0, os.path.join(CORE, "tests"))
# test_console: the console font and colour schemes, to render expected text.
sys.path.insert(0, os.path.join(CORE, "tests", "target"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import importlib.util  # noqa: E402

import pytest  # noqa: E402


pytest_plugins = ["harness.plugin"]

# The library's host timing skip limit, per board, from its target
# conftest. Its file name clashes with this one, so it loads by path and
# this conftest calls its two hooks.
_spec = importlib.util.spec_from_file_location(
    "robco_timing_skips", os.path.join(CORE, "tests", "target", "conftest.py"))
timing_skips = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(timing_skips)


def pytest_configure(config):
    timing_skips.pytest_configure(config)


def pytest_addoption(parser):
    timing_skips.pytest_addoption(parser)
    parser.addoption("--build-dir", default="build",
                     help="build directory, as BUILD in the Makefile")
    parser.addoption("--retros-img", default=None,
                     help="a RETROS.IMG for the whole-chain tests, as RETROS_IMG in the Makefile")


@pytest.fixture(scope="session")
def build_dir(request):
    return os.path.join(ROOT, request.config.getoption("--build-dir"))


@pytest.fixture(scope="session")
def mfboot_image(robco_build, build_dir):
    """build/mfboot.img, built with the test operating images by `make all`."""
    return robco_build(ROOT, flags=(f"BUILD={build_dir}",),
                       image=os.path.join(build_dir, "mfboot.img"))


@pytest.fixture(scope="session")
def mfboot(mfboot_image):
    with open(mfboot_image, "rb") as f:
        return f.read()


@pytest.fixture(scope="session")
def test_os(mfboot_image, build_dir):
    """The bytes of the test operating images: {"uos": ..., "maint": ..., "diag": ...}."""
    out = {}
    for kind in ("uos", "maint", "diag"):
        with open(os.path.join(build_dir, "os", kind, "testos.img"), "rb") as f:
            out[kind] = f.read()
    return out


@pytest.fixture(scope="session")
def chain1_image(robco_build, build_dir):
    """RobCo-Core's examples/chain1, built to load /ROBCO/MFBOOT.SYS from the card
    and to hand over a framebuffer and an amber console."""
    out = os.path.join(build_dir, "chain1")
    flags = (f"BUILD={out}", "EXTRA_CFLAGS=-DCHAIN1_SD=1 -DCHAIN1_FB=1")
    return robco_build(os.path.join(CORE, "examples", "chain1"), flags=flags,
                       image=os.path.join(out, "chain1.img"))


@pytest.fixture(scope="session")
def chain1_nofb_image(robco_build, build_dir):
    """examples/chain1 built with CHAIN1_SD only: a trusted handoff with no framebuffer."""
    out = os.path.join(build_dir, "chain1nofb")
    flags = (f"BUILD={out}", "EXTRA_CFLAGS=-DCHAIN1_SD=1")
    return robco_build(os.path.join(CORE, "examples", "chain1"), flags=flags,
                       image=os.path.join(out, "chain1.img"))


@pytest.fixture(scope="session")
def chain1_grid_image(robco_build, build_dir):
    """chain1_grid_image(cols, rows): tests/chain1grid, examples/chain1 with the SD
    and framebuffer variants that hands over a cols by rows console grid."""
    def get(cols, rows):
        out = os.path.join(build_dir, f"chain1grid_{cols}x{rows}")
        flags = (f"BUILD={out}", f"GRID_COLS={cols}", f"GRID_ROWS={rows}")
        return robco_build(os.path.join(ROOT, "tests", "chain1grid"), flags=flags,
                           image=os.path.join(out, "chain1.img"))
    return get


@pytest.fixture(scope="session")
def chain2_wait_image(robco_build, build_dir):
    """RobCo-Core's examples/chain2 built with CHAIN2_VIDEO_WAIT. Loaded as
    MFBOOT.SYS, it adopts the screen, writes nothing and waits: a
    screenshot then shows the screen exactly as the first stage left it."""
    out = os.path.join(build_dir, "chain2wait")
    flags = (f"BUILD={out}", "EXTRA_CFLAGS=-DCHAIN2_VIDEO_WAIT=1")
    return robco_build(os.path.join(CORE, "examples", "chain2"), flags=flags,
                       image=os.path.join(out, "chain2.img"))


@pytest.fixture(scope="session")
def retros_image(request):
    path = request.config.getoption("--retros-img")
    if not path:
        pytest.skip("RETROS_IMG is not set: no whole-chain test")
    assert os.path.isfile(path), f"RETROS_IMG {path} is missing"
    return path


@pytest.fixture(scope="session")
def fixed_os():
    """The bytes of the fixed operating image, checked against its pins."""
    import fixed_os as fo
    data = fo.build()
    assert (len(data), fo.sha256(data)) == (fo.SIZE, fo.SHA256)
    return data


@pytest.fixture(scope="session")
def chain1_maint_image(robco_build, build_dir):
    """tests/chain1maint: examples/chain1 with the SD and framebuffer variants that
    hands over boot_flags bit 1, the user's request for maintenance mode."""
    out = os.path.join(build_dir, "chain1maint")
    return robco_build(os.path.join(ROOT, "tests", "chain1maint"), flags=(f"BUILD={out}",),
                       image=os.path.join(out, "chain1.img"))
