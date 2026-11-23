import os
import sys

# RobCo-Core's harness: the --board option, the board fixture, need_mtools
# and robco_build. MFBootAgent adds --build-dir for the image under test
# and --retros-img for the whole-chain tests.
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORE = os.path.join(ROOT, "external", "robco-core")
sys.path.insert(0, os.path.join(CORE, "tests"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
pytest_plugins = ["harness.plugin"]

import pytest  # noqa: E402


def pytest_addoption(parser):
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
def retros_image(request):
    path = request.config.getoption("--retros-img")
    if not path:
        pytest.skip("RETROS_IMG is not set: no whole-chain test")
    assert os.path.isfile(path), f"RETROS_IMG {path} is missing"
    return path


# Fault images: MFBootAgent built again under build/fault_<name>/ with one define.
@pytest.fixture(scope="session")
def fault_image(robco_build, build_dir):
    def get(name, define):
        out = os.path.join(build_dir, f"fault_{name}")
        flags = (f"BUILD={out}", f"EXTRA_CFLAGS=-D{define}=1")
        return robco_build(ROOT, target=f"{out}/mfboot.img", flags=flags,
                           image=os.path.join(out, "mfboot.img"))
    return get
