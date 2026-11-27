"""The library commit on the System screen: `make print-core-commit` reads the
checked-out library folder first and falls back to the CI workflow pin."""

import os
import re
import subprocess

from harness.env import minimal_env

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CORE = os.path.join(ROOT, "external", "robco-core")


def printed(*args):
    r = subprocess.run(["make", "-s", "--no-print-directory", "print-core-commit", *args],
                       cwd=ROOT, capture_output=True, text=True, env=minimal_env(), check=True)
    return r.stdout.strip()


def git(cwd, *args):
    """git in a test's own temp repository, which its user owns."""
    r = subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True,
                       env=minimal_env(GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull),
                       check=True)
    return r.stdout.strip()


def ci_pin():
    with open(os.path.join(ROOT, ".github", "workflows", "ci.yml")) as f:
        return re.search(r"robco-ci\.yml@([0-9a-f]{12})", f.read()).group(1)


def expected_core_commit():
    """What make takes, by the Makefile's rule: git asked in make's own
    environment, global settings included, or the pin when git fails or
    refuses the folder, as in a CI container on a checkout of another owner."""
    r = subprocess.run(["git", "rev-parse", "--verify", "-q", "HEAD"], cwd=CORE,
                       capture_output=True, text=True, env=minimal_env())
    head = r.stdout.strip()[:12]
    return head if r.returncode == 0 and re.fullmatch(r"[0-9a-f]{12}", head) else ci_pin()


def test_reads_the_library_folder():
    assert printed() == expected_core_commit()


def test_reads_another_commit_than_the_pin(tmp_path):
    git(tmp_path, "init", "-q")
    git(tmp_path, "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-q",
        "--allow-empty", "-m", "x")
    head = git(tmp_path, "rev-parse", "HEAD")[:12]
    assert head != ci_pin()
    assert printed(f"MFB_CORE_GIT_DIR={tmp_path}") == head


def test_falls_back_to_the_pin_without_git(tmp_path):
    assert printed(f"MFB_CORE_GIT_DIR={tmp_path}") == ci_pin()


def test_falls_back_to_the_pin_for_a_folder_inside_another_repository():
    # A copy without its own .git must not show the parent's commit.
    assert printed(f"MFB_CORE_GIT_DIR={os.path.join(ROOT, 'docs')}") == ci_pin()


def test_command_line_value_wins():
    assert printed("MFB_CORE_PIN=0123456789ab") == "0123456789ab"


def main_obj_ready(build, pin):
    """make -q on main.o: True when make would not compile main.c again."""
    obj = os.path.join(build, "obj", "src", "main.o")
    r = subprocess.run(["make", "-q", "--no-print-directory", f"BUILD={build}",
                        f"MFB_CORE_PIN={pin}", obj], cwd=ROOT, capture_output=True, text=True,
                       env=minimal_env())
    assert r.returncode in (0, 1), r.stderr
    return r.returncode == 0


def test_a_new_value_compiles_and_links_again_in_the_same_second(tmp_path):
    build = str(tmp_path / "build")
    obj = os.path.join(build, "obj", "src", "main.o")
    linked = [os.path.join(build, "mfboot.elf"), os.path.join(build, "mfboot.img")]
    stamp = os.path.join(build, "core_commit")
    assert not main_obj_ready(build, "aaaaaaaaaaaa")  # writes the stamp
    os.makedirs(os.path.dirname(obj))
    t = int(os.stat(stamp).st_mtime) + 1
    for f in [obj, *linked]:
        open(f, "wb").close()
    for f in [stamp, obj, *linked]:
        os.utime(f, (t, t))
    assert main_obj_ready(build, "aaaaaaaaaaaa")  # same value: no rebuild
    assert all(os.path.exists(f) for f in [obj, *linked])  # and no relink
    assert not main_obj_ready(build, "bbbbbbbbbbbb")
    # Gone, so make compiles and links them again whatever their times.
    assert not any(os.path.exists(f) for f in [obj, *linked])
    with open(stamp) as f:
        assert f.read().strip() == "bbbbbbbbbbbb"
