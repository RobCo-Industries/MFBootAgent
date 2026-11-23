# Contributing

## Before a pull request

```sh
make CI=1 all test-host
make test-target
make check-pins
```

## Rules

- Logic that needs no hardware goes in a pure file under `src/`, listed in `PURE_SRCS`, with a host test.
- A new marker follows the format in BOOT_PROTOCOL.md and goes in the marker table in `docs/BOOT.md`.
- A target test matches complete serial lines.
- A golden shows no size, CRC, time or toolchain text.
- A test must be able to fail. Break the code once and watch the test catch it.
- Tests that start a subprocess build its environment with `minimal_env()`. Never copy `os.environ`.
- To move RobCo-Core, update the submodule, the commit and image tag in `.github/workflows/ci.yml`, and the pinned links in the docs. `make check-pins` checks all three.
- Docs: short plain sentences, active voice, sentence-case headings. Say what is lore text and what is real.
