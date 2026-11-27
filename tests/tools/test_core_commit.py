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
    r = subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True,
                       env=minimal_env(GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull),
                       check=True)
    return r.stdout.strip()


def ci_pin():
    with open(os.path.join(ROOT, ".github", "workflows", "ci.yml")) as f:
        return re.search(r"robco-ci\.yml@([0-9a-f]{12})", f.read()).group(1)


def test_reads_the_library_folder():
    assert printed() == git(CORE, "rev-parse", "HEAD")[:12]


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
